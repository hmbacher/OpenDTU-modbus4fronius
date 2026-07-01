// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 hmbacher and others
 */
#include "WebApi_modbus.h"
#include "Configuration.h"
#include "ModbusDtu.h"
#include "WebApi.h"
#include "WebApi_errors.h"
#include "helper.h"
#include <AsyncJson.h>
#include <Hoymiles.h>
#include <cinttypes>

WebApiModbusClass::WebApiModbusClass()
    : _applyDataTask(500 * TASK_MILLISECOND, TASK_ONCE, std::bind(&WebApiModbusClass::applyDataTaskCb, this))
{
}

void WebApiModbusClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;

    server.on("/api/modbus/status", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiModbusClass::onModbusStatus, this, _1)));
    server.on("/api/modbus/config", HTTP_GET, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiModbusClass::onModbusAdminGet, this, _1)));
    server.on("/api/modbus/config", HTTP_POST, static_cast<ArRequestHandlerFunction>(std::bind(&WebApiModbusClass::onModbusAdminPost, this, _1)));

    scheduler.addTask(_applyDataTask);
}

void WebApiModbusClass::onModbusStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    const CONFIG_T& config = Configuration.get();

    root["enabled"] = config.Modbus.Enabled;
    root["test_mode"] = config.Modbus.TestMode;
    root["running"] = ModbusDtu.isServerRunning();
    root["port"] = ModbusDtu.getActivePort();
    root["valid"] = ModbusDtu.hasValidData();
    root["included_count"] = ModbusDtu.getIncludedCount();
    root["base_address"] = ModbusDtu.getBaseAddress();
    root["model_id"] = ModbusDtu.getMeterModelId();
    root["model_length"] = ModbusDtu.getMeterModelLength();
    root["power"] = ModbusDtu.getCurrentPower();
    root["voltage"] = ModbusDtu.getCurrentVoltage();
    root["frequency"] = ModbusDtu.getCurrentFrequency();
    root["current"] = ModbusDtu.getCurrentCurrent();
    root["yield"] = ModbusDtu.getTotalYield();

    JsonArray registers = root["registers"].to<JsonArray>();
    ModbusDtu.buildRegisterReference(registers);

    JsonArray inverters = root["inverters"].to<JsonArray>();
    for (uint8_t i = 0; i < INV_MAX_COUNT; i++) {
        if (config.Inverter[i].Serial == 0) {
            continue;
        }
        JsonObject obj = inverters.add<JsonObject>();
        obj["index"] = i;
        char serial[sizeof(uint64_t) * 8 + 1];
        snprintf(serial, sizeof(serial), "%0" PRIx32 "%08" PRIx32,
            static_cast<uint32_t>((config.Inverter[i].Serial >> 32) & 0xFFFFFFFF),
            static_cast<uint32_t>(config.Inverter[i].Serial & 0xFFFFFFFF));
        obj["serial"] = serial;
        obj["name"] = config.Inverter[i].Name;
        obj["included"] = config.Modbus.IncludeInverter[i];
        auto inv = Hoymiles.getInverterBySerial(config.Inverter[i].Serial);
        obj["reachable"] = (inv != nullptr) && inv->isReachable();
    }

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiModbusClass::onModbusAdminGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    const CONFIG_T& config = Configuration.get();

    root["enabled"] = config.Modbus.Enabled;
    root["test_mode"] = config.Modbus.TestMode;
    root["port"] = config.Modbus.Port;
    root["representation"] = config.Modbus.Representation;
    root["sign"] = config.Modbus.Sign;
    root["update_interval"] = config.Modbus.UpdateInterval;
    root["manufacturer"] = config.Modbus.ManufacturerName;
    root["model"] = config.Modbus.ModelName;
    root["serial"] = config.Modbus.SerialStr;
    root["version"] = config.Modbus.VersionStr;
    root["reference_inverter"] = config.Modbus.ReferenceInverter;

    JsonArray inverters = root["inverters"].to<JsonArray>();
    for (uint8_t i = 0; i < INV_MAX_COUNT; i++) {
        if (config.Inverter[i].Serial == 0) {
            continue;
        }
        JsonObject obj = inverters.add<JsonObject>();
        obj["index"] = i;
        char serial[sizeof(uint64_t) * 8 + 1];
        snprintf(serial, sizeof(serial), "%0" PRIx32 "%08" PRIx32,
            static_cast<uint32_t>((config.Inverter[i].Serial >> 32) & 0xFFFFFFFF),
            static_cast<uint32_t>(config.Inverter[i].Serial & 0xFFFFFFFF));
        obj["serial"] = serial;
        obj["name"] = config.Inverter[i].Name;
        obj["included"] = config.Modbus.IncludeInverter[i];
    }

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiModbusClass::onModbusAdminPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) {
        return;
    }

    auto& retMsg = response->getRoot();

    if (root["enabled"].isNull()
        || root["port"].isNull()
        || root["representation"].isNull()
        || root["update_interval"].isNull()) {
        retMsg["message"] = "Values are missing!";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (root["port"].as<uint32_t>() == 0 || root["port"].as<uint32_t>() > 65535) {
        retMsg["message"] = "Port must be a number between 1 and 65535!";
        retMsg["code"] = WebApiError::ModbusPortInvalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (root["representation"].as<uint8_t>() > 1) {
        retMsg["message"] = "Invalid representation!";
        retMsg["code"] = WebApiError::ModbusRepresentationInvalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (root["update_interval"].as<uint32_t>() < 200 || root["update_interval"].as<uint32_t>() > 60000) {
        retMsg["message"] = "Update interval must be between 200 and 60000 ms!";
        retMsg["code"] = WebApiError::ModbusUpdateIntervalInvalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (root["manufacturer"].as<String>().length() > MODBUS_MAX_STR_STRLEN
        || root["model"].as<String>().length() > MODBUS_MAX_STR_STRLEN
        || root["serial"].as<String>().length() > MODBUS_MAX_STR_STRLEN
        || root["version"].as<String>().length() > MODBUS_MAX_STR_STRLEN) {
        retMsg["message"] = "Identity strings must not exceed " STR_EXTRACT(MODBUS_MAX_STR_STRLEN) " characters!";
        retMsg["code"] = WebApiError::ModbusStringLength;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    {
        auto guard = Configuration.getWriteGuard();
        auto& config = guard.getConfig();

        config.Modbus.Enabled = root["enabled"].as<bool>();
        config.Modbus.TestMode = root["test_mode"].as<bool>();
        config.Modbus.Port = root["port"].as<uint16_t>();
        config.Modbus.Representation = root["representation"].as<uint8_t>();
        config.Modbus.Sign = root["sign"].as<int8_t>() < 0 ? -1 : 1;
        config.Modbus.UpdateInterval = root["update_interval"].as<uint32_t>();
        strlcpy(config.Modbus.ManufacturerName, root["manufacturer"].as<String>().c_str(), sizeof(config.Modbus.ManufacturerName));
        strlcpy(config.Modbus.ModelName, root["model"].as<String>().c_str(), sizeof(config.Modbus.ModelName));
        strlcpy(config.Modbus.SerialStr, root["serial"].as<String>().c_str(), sizeof(config.Modbus.SerialStr));
        strlcpy(config.Modbus.VersionStr, root["version"].as<String>().c_str(), sizeof(config.Modbus.VersionStr));

        uint8_t refInverter = root["reference_inverter"].as<uint8_t>();
        config.Modbus.ReferenceInverter = refInverter < INV_MAX_COUNT ? refInverter : 0;

        for (uint8_t i = 0; i < INV_MAX_COUNT; i++) {
            config.Modbus.IncludeInverter[i] = false;
        }
        for (JsonVariant obj : root["inverters"].as<JsonArray>()) {
            const uint8_t idx = obj["index"].as<uint8_t>();
            if (idx < INV_MAX_COUNT) {
                config.Modbus.IncludeInverter[idx] = obj["included"].as<bool>();
            }
        }
    }

    WebApi.writeConfig(retMsg);

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);

    _applyDataTask.enable();
    _applyDataTask.restart();
}

void WebApiModbusClass::applyDataTaskCb()
{
    ModbusDtu.applyConfig();
}
