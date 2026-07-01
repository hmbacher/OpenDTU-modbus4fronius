// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 hmbacher and others
 *
 * Presents the aggregated Hoymiles production as a SunSpec-compliant meter over
 * Modbus TCP so a Fronius GEN24 (or any SunSpec meter consumer) can poll it.
 *
 * Register layout (offsets relative to the configurable base address, default 40000):
 *   base+0 .. base+1    "SunS" marker
 *   base+2 .. base+3    Common model (1) header (id, length=65)
 *   base+4 .. base+68   Common model data (Mn/Md/Opt/Vr/SN/DA)
 *   base+69 .. base+70  Meter model header (id, length)
 *   base+71 .. ...      Meter model data (float 124 regs or int+SF 105 regs)
 *   ...                 End model (0xFFFF, length=0)
 *
 * Common model length and field layout verified against Fronius's official
 * Smart_Meter_Register_Map_Float.xlsx / _Int&SF.xlsx (common model = 65 registers,
 * no padding register after DA).
 */
#include "ModbusDtu.h"
#include "Configuration.h"
#include <Hoymiles.h>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <esp_log.h>

#undef TAG
static const char* TAG = "modbus";

// SunSpec well-known base address: clients (incl. the Fronius GEN24) probe this
// fixed address for the "SunS" marker during discovery, so it must not be configurable.
static constexpr uint16_t MODBUS_BASE_ADDRESS = 40000;
// "DA" (Modbus device address) reported in the Common model. The TCP server
// itself never filters requests by unit ID (verified against the modbus-esp8266
// source), so this should be cosmetic only -- kept at the previously-working
// default (200) rather than an arbitrary value while a reported connection
// regression is still being isolated from a possible stale Fronius discovery cache.
static constexpr uint16_t MODBUS_DEVICE_ADDRESS = 200;

// fixed sizes of the static SunSpec blocks
static constexpr uint16_t SUNS_MARKER_LEN = 2;
static constexpr uint16_t MODEL_HEADER_LEN = 2;
static constexpr uint16_t COMMON_MODEL_LEN = 65; // Mn(16)+Md(16)+Opt(8)+Vr(8)+SN(16)+DA(1), no padding
static constexpr uint16_t END_MODEL_LEN = 2;
static constexpr uint16_t METER_DATA_OFFSET = SUNS_MARKER_LEN + MODEL_HEADER_LEN + COMMON_MODEL_LEN + MODEL_HEADER_LEN; // = 71

ModbusDtuClass ModbusDtu;

ModbusDtuClass::ModbusDtuClass()
    : _loopTask(TASK_IMMEDIATE, TASK_FOREVER, std::bind(&ModbusDtuClass::loop, this))
{
}

void ModbusDtuClass::init(Scheduler& scheduler)
{
    scheduler.addTask(_loopTask);
    _loopTask.enable();

    applyConfig();
}

uint16_t ModbusDtuClass::meterModelId() const
{
    // The Fronius GEN24's meter discovery only binds the THREE-PHASE meter model,
    // so we always emit 213 (float) / 203 (int+SF) even though a Hoymiles aggregate
    // is single phase. Production is reported on L1; L2/L3 stay at zero because a
    // single-phase source has no per-phase measurement for them.
    return Configuration.get().Modbus.Representation == 1 ? 203 : 213;
}

uint16_t ModbusDtuClass::meterModelLength() const
{
    return Configuration.get().Modbus.Representation == 1 ? 105 : 124;
}

void ModbusDtuClass::applyConfig()
{
    const auto& cfg = Configuration.get().Modbus;

    if (cfg.Enabled && !_serverStarted) {
        _mb.server(cfg.Port);
        _serverStarted = true;
        _activePort = cfg.Port;
        ESP_LOGI(TAG, "Modbus TCP server started on port %" PRIu16, cfg.Port);
    } else if (_serverStarted && cfg.Enabled && cfg.Port != _activePort) {
        ESP_LOGW(TAG, "Modbus port change to %" PRIu16 " requires a reboot to take effect", cfg.Port);
    }

    rebuildRegisters();
}

void ModbusDtuClass::rebuildRegisters()
{
    const auto& cfg = Configuration.get().Modbus;

    // Tear down a previously allocated map (e.g. model/representation changed)
    if (_mapAllocated) {
        _mb.removeHreg(_mapBase, _mapLen);
        _mapAllocated = false;
    }

    if (!cfg.Enabled) {
        return;
    }

    const uint16_t base = MODBUS_BASE_ADDRESS;
    const uint16_t meterLen = meterModelLength();
    const uint16_t totalLen = METER_DATA_OFFSET + meterLen + END_MODEL_LEN;

    _mb.addHreg(base, 0, totalLen);
    _mapBase = base;
    _mapLen = totalLen;
    _mapAllocated = true;

    // "SunS" marker
    setUInt16(base + 0, 0x5375); // "Su"
    setUInt16(base + 1, 0x6E53); // "nS"

    // Common model (1) header + data
    setUInt16(base + 2, 1);
    setUInt16(base + 3, COMMON_MODEL_LEN);
    const uint16_t c = base + 4;
    setString(c + 0, 16, cfg.ManufacturerName); // Mn

    // Md: use the configured model, or derive it from the reference inverter type
    char model[MODBUS_MAX_STR_STRLEN + 1];
    if (strlen(cfg.ModelName) > 0) {
        strlcpy(model, cfg.ModelName, sizeof(model));
    } else {
        strlcpy(model, "OpenDTU", sizeof(model));
        if (cfg.ReferenceInverter < INV_MAX_COUNT) {
            const uint64_t refSerial = Configuration.get().Inverter[cfg.ReferenceInverter].Serial;
            if (refSerial != 0) {
                auto inv = Hoymiles.getInverterBySerial(refSerial);
                if (inv != nullptr) {
                    strlcpy(model, inv->typeName().c_str(), sizeof(model));
                }
            }
        }
    }
    setString(c + 16, 16, model); // Md
    setString(c + 32, 8, ""); // Opt
    setString(c + 40, 8, cfg.VersionStr); // Vr

    char serial[MODBUS_MAX_STR_STRLEN + 1];
    if (strlen(cfg.SerialStr) > 0) {
        strlcpy(serial, cfg.SerialStr, sizeof(serial));
    } else {
        const uint64_t dtuSerial = Configuration.get().Dtu.Serial;
        snprintf(serial, sizeof(serial), "%08" PRIX32 "%08" PRIX32,
            static_cast<uint32_t>((dtuSerial >> 32) & 0xFFFFFFFF),
            static_cast<uint32_t>(dtuSerial & 0xFFFFFFFF));
    }
    setString(c + 48, 16, serial); // SN
    setUInt16(c + 64, MODBUS_DEVICE_ADDRESS); // DA (Modbus device address) -- last of the 65 common-model registers

    // Meter model header (data block is zero-initialised by addHreg)
    const uint16_t m = base + SUNS_MARKER_LEN + MODEL_HEADER_LEN + COMMON_MODEL_LEN;
    setUInt16(m + 0, meterModelId());
    setUInt16(m + 1, meterLen);

    // End model
    const uint16_t e = base + METER_DATA_OFFSET + meterLen;
    setUInt16(e + 0, 0xFFFF);
    setUInt16(e + 1, 0);

    ESP_LOGI(TAG, "SunSpec map built at %" PRIu16 ": common(1) + meter(%" PRIu16 ") + end, %" PRIu16 " regs",
        base, meterModelId(), totalLen);

    updateValues();
}

void ModbusDtuClass::loop()
{
    if (_serverStarted) {
        _mb.task();
    }

    if (!_mapAllocated) {
        return;
    }

    const auto& cfg = Configuration.get().Modbus;
    if (millis() - _lastUpdateMillis >= cfg.UpdateInterval) {
        _lastUpdateMillis = millis();
        updateValues();
    }
}

void ModbusDtuClass::collectData()
{
    const auto& cfg = Configuration.get().Modbus;
    const auto& full = Configuration.get();

    float sumW = 0;
    float sumVAR = 0;
    float sumWh = 0;
    // Start at zero: if no included inverter provides data, the meter reports zeros
    // (an honest "no measurement") instead of fabricated grid values.
    float refV = 0.0f;
    float refHz = 0.0f;
    float refPF = 0.0f;
    bool refCaptured = false;
    bool anyReachable = false;
    uint8_t count = 0;

    for (uint8_t pos = 0; pos < Hoymiles.getNumInverters(); pos++) {
        auto inv = Hoymiles.getInverterByPos(pos);
        if (inv == nullptr) {
            continue;
        }

        // Map this inverter back to its slot in Inverter[] to honour the selection
        int8_t cfgIdx = -1;
        for (uint8_t i = 0; i < INV_MAX_COUNT; i++) {
            if (full.Inverter[i].Serial == inv->serial()) {
                cfgIdx = static_cast<int8_t>(i);
                break;
            }
        }
        if (cfgIdx < 0 || !cfg.IncludeInverter[cfgIdx]) {
            continue;
        }

        count++;
        auto stats = inv->Statistics();

        // Yield is monotonic; keep the last known value even when unreachable
        sumWh += stats->getChannelFieldValue(TYPE_INV, CH0, FLD_YT) * 1000.0f; // kWh -> Wh

        if (inv->isReachable()) {
            anyReachable = true;
            sumW += stats->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC);
            sumVAR += stats->getChannelFieldValue(TYPE_AC, CH0, FLD_Q);

            const bool isReference = (cfgIdx == static_cast<int8_t>(cfg.ReferenceInverter));
            if (isReference || !refCaptured) {
                const float v = stats->getChannelFieldValue(TYPE_AC, CH0, FLD_UAC);
                const float hz = stats->getChannelFieldValue(TYPE_AC, CH0, FLD_F);
                const float pf = stats->getChannelFieldValue(TYPE_AC, CH0, FLD_PF);
                if (v > 0) {
                    refV = v;
                }
                if (hz > 0) {
                    refHz = hz;
                }
                if (pf != 0) {
                    refPF = pf;
                }
                refCaptured = isReference || refCaptured;
            }
        }
    }

    // Power is held at 0 when nothing is reachable; energy keeps its last value
    const float power = anyReachable ? sumW * cfg.Sign : 0.0f;
    const float current = (refV > 1.0f) ? std::fabs(power) / refV : 0.0f;

    // Three-phase model (Fronius requirement) fed from a single-phase source:
    // only L1 carries data; L2/L3 stay at zero (no per-phase measurement exists).
    MeterValues v;
    v.frequency = refHz;
    v.powerFactor = refPF;
    v.voltageLN = refV;
    v.voltageL1 = refV;
    v.power = power;
    v.powerL1 = power;
    v.apparentPower = std::fabs(power);
    v.reactivePower = anyReachable ? sumVAR * cfg.Sign : 0.0f;
    v.current = current;
    v.currentL1 = current;
    v.exportedWh = sumWh;
    _values = v;

    _includedCount = count;
    _haveData = anyReachable;
}

void ModbusDtuClass::fillTestValues()
{
    const auto& cfg = Configuration.get().Modbus;

    // A clean, physically consistent single-phase load -- the exact shape live data
    // takes: 230 V x 10 A = 2300 VA at unity power factor, entirely on L1, with
    // L2/L3 at zero. This keeps the total equal to the L1 value (no "total != phase"
    // confusion) and lets every register be verified against a plausible reading.
    const float voltage = 230.0f;
    const float current = 10.0f;

    // Single-phase load on L1; L2/L3 stay at zero (no per-phase measurement exists).
    MeterValues v;
    v.frequency = 50.0f;
    v.voltageLN = voltage;
    v.voltageL1 = voltage;
    v.current = current;
    v.currentL1 = current;
    // Honour the sign convention here too, so it can be dialled in from test mode.
    v.power = 2300.0f * cfg.Sign;
    v.powerL1 = v.power;
    v.apparentPower = 2300.0f; // |S| = V * I
    v.reactivePower = 0.0f; // unity power factor
    v.powerFactor = 1.0f;

    // Kept at 0 deliberately: Fronius/Solar.web may persist TotWhExp/TotWhImp into
    // long-term yield statistics, and test mode must never inject fake energy totals.
    v.exportedWh = 0.0f;
    v.importedWh = 0.0f;

    _values = v;
    _includedCount = 0;
    _haveData = true;
}

void ModbusDtuClass::updateValues()
{
    const auto& cfg = Configuration.get().Modbus;

    if (cfg.TestMode) {
        fillTestValues();
    } else {
        collectData();
    }

    const uint16_t d = MODBUS_BASE_ADDRESS + METER_DATA_OFFSET;

    if (cfg.Representation == 1) {
        writeIntModel(d);
    } else {
        writeFloatModel(d);
    }
}

void ModbusDtuClass::writeFloatModel(const uint16_t d)
{
    // Three-phase meter (model 213) fed from a single-phase source: only L1 carries
    // data; L2/L3 read zero (no per-phase measurement from a single-phase inverter).
    const MeterValues& v = _values;

    setFloat32(d + 0, v.current); // A (total)
    setFloat32(d + 2, v.currentL1); // AphA
    setFloat32(d + 4, 0); // AphB
    setFloat32(d + 6, 0); // AphC

    setFloat32(d + 8, v.voltageLN); // PhV (L-N average)
    setFloat32(d + 10, v.voltageL1); // PhVphA
    setFloat32(d + 12, v.voltageL2); // PhVphB
    setFloat32(d + 14, v.voltageL3); // PhVphC
    setFloat32(d + 16, 0); // PPV (L-L average; not measured from a single-phase source)
    setFloat32(d + 18, 0); // PPVphAB
    setFloat32(d + 20, 0); // PPVphBC
    setFloat32(d + 22, 0); // PPVphCA

    setFloat32(d + 24, v.frequency); // Hz

    setFloat32(d + 26, v.power); // W (total)
    setFloat32(d + 28, v.powerL1); // WphA
    setFloat32(d + 30, 0); // WphB
    setFloat32(d + 32, 0); // WphC

    setFloat32(d + 34, v.apparentPower); // VA (total)
    setFloat32(d + 36, v.apparentPower); // VAphA
    setFloat32(d + 38, 0); // VAphB
    setFloat32(d + 40, 0); // VAphC

    setFloat32(d + 42, v.reactivePower); // VAR (total)
    setFloat32(d + 44, v.reactivePower); // VARphA
    setFloat32(d + 46, 0); // VARphB
    setFloat32(d + 48, 0); // VARphC

    setFloat32(d + 50, v.powerFactor); // PF
    setFloat32(d + 52, v.powerFactor); // PFphA
    setFloat32(d + 54, 0); // PFphB
    setFloat32(d + 56, 0); // PFphC

    setFloat32(d + 58, v.exportedWh); // TotWhExp
    setFloat32(d + 60, v.exportedWh); // TotWhExpPhA
    setFloat32(d + 62, 0); // TotWhExpPhB
    setFloat32(d + 64, 0); // TotWhExpPhC
    setFloat32(d + 66, v.importedWh); // TotWhImp
    setFloat32(d + 68, v.importedWh); // TotWhImpPhA
    setFloat32(d + 70, 0); // TotWhImpPhB
    setFloat32(d + 72, 0); // TotWhImpPhC

    // offsets 74..121 (VAh, VArh accumulators) stay zero-initialised
    setUInt32(d + 122, 0); // Evt (events bitfield)
}

void ModbusDtuClass::writeIntModel(const uint16_t d)
{
    // Three-phase meter (model 203) fed from a single-phase source: only L1 carries
    // data; L2/L3 read zero (no per-phase measurement from a single-phase inverter).
    // Scale factors (powers of ten) chosen to keep values within int16 range
    constexpr int A_SF = -2; // 0.01 A
    constexpr int V_SF = -1; // 0.1 V
    constexpr int HZ_SF = -2; // 0.01 Hz
    constexpr int W_SF = 0; // 1 W
    constexpr int VA_SF = 0; // 1 VA
    constexpr int VAR_SF = 0; // 1 var
    constexpr int PF_SF = -3; // 0.001
    constexpr int WH_SF = 0; // 1 Wh

    auto scaled16 = [](float value, int sf) -> int16_t {
        float v = value * powf(10.0f, static_cast<float>(-sf));
        if (v > 32767.0f) {
            v = 32767.0f;
        }
        if (v < -32768.0f) {
            v = -32768.0f;
        }
        return static_cast<int16_t>(lroundf(v));
    };

    const MeterValues& v = _values;

    // AC current (d+0..4)
    setInt16(d + 0, scaled16(v.current, A_SF)); // A
    setInt16(d + 1, scaled16(v.currentL1, A_SF)); // AphA
    setInt16(d + 2, 0); // AphB
    setInt16(d + 3, 0); // AphC
    setInt16(d + 4, A_SF); // A_SF

    // Voltage (d+5..13)
    setInt16(d + 5, scaled16(v.voltageLN, V_SF)); // PhV
    setInt16(d + 6, scaled16(v.voltageL1, V_SF)); // PhVphA
    setInt16(d + 7, scaled16(v.voltageL2, V_SF)); // PhVphB
    setInt16(d + 8, scaled16(v.voltageL3, V_SF)); // PhVphC
    setInt16(d + 9, 0); // PPV (L-L average; not measured from a single-phase source)
    setInt16(d + 10, 0); // PPVphAB
    setInt16(d + 11, 0); // PPVphBC
    setInt16(d + 12, 0); // PPVphCA
    setInt16(d + 13, V_SF); // V_SF

    // Frequency (d+14..15)
    setInt16(d + 14, scaled16(v.frequency, HZ_SF)); // Hz
    setInt16(d + 15, HZ_SF); // Hz_SF

    // Real power (d+16..20)
    setInt16(d + 16, scaled16(v.power, W_SF)); // W
    setInt16(d + 17, scaled16(v.powerL1, W_SF)); // WphA
    setInt16(d + 18, 0); // WphB
    setInt16(d + 19, 0); // WphC
    setInt16(d + 20, W_SF); // W_SF

    // Apparent power (d+21..25)
    setInt16(d + 21, scaled16(v.apparentPower, VA_SF)); // VA
    setInt16(d + 22, scaled16(v.apparentPower, VA_SF)); // VAphA
    setInt16(d + 23, 0); // VAphB
    setInt16(d + 24, 0); // VAphC
    setInt16(d + 25, VA_SF); // VA_SF

    // Reactive power (d+26..30)
    setInt16(d + 26, scaled16(v.reactivePower, VAR_SF)); // VAR
    setInt16(d + 27, scaled16(v.reactivePower, VAR_SF)); // VARphA
    setInt16(d + 28, 0); // VARphB
    setInt16(d + 29, 0); // VARphC
    setInt16(d + 30, VAR_SF); // VAR_SF

    // Power factor (d+31..35)
    setInt16(d + 31, scaled16(v.powerFactor, PF_SF)); // PF
    setInt16(d + 32, scaled16(v.powerFactor, PF_SF)); // PFphA
    setInt16(d + 33, 0); // PFphB
    setInt16(d + 34, 0); // PFphC
    setInt16(d + 35, PF_SF); // PF_SF

    // Total real energy (acc32, d+36..51) + scale factor (d+52)
    const uint32_t whExp = static_cast<uint32_t>(v.exportedWh < 0 ? 0 : v.exportedWh);
    const uint32_t whImp = static_cast<uint32_t>(v.importedWh < 0 ? 0 : v.importedWh);
    setUInt32(d + 36, whExp); // TotWhExp
    setUInt32(d + 38, whExp); // TotWhExpPhA
    setUInt32(d + 40, 0); // TotWhExpPhB
    setUInt32(d + 42, 0); // TotWhExpPhC
    setUInt32(d + 44, whImp); // TotWhImp
    setUInt32(d + 46, whImp); // TotWhImpPhA
    setUInt32(d + 48, 0); // TotWhImpPhB
    setUInt32(d + 50, 0); // TotWhImpPhC
    setInt16(d + 52, WH_SF); // TotWh_SF

    // Apparent / reactive energy accumulators (d+53..101) stay zero
    setInt16(d + 69, 0); // TotVAh_SF
    setInt16(d + 102, 0); // TotVArh_SF
    setUInt32(d + 103, 0); // Evt (events bitfield)
}

void ModbusDtuClass::setUInt16(const uint16_t addr, const uint16_t value)
{
    _mb.Hreg(addr, value);
}

void ModbusDtuClass::setInt16(const uint16_t addr, const int16_t value)
{
    _mb.Hreg(addr, static_cast<uint16_t>(value));
}

void ModbusDtuClass::setUInt32(const uint16_t addr, const uint32_t value)
{
    _mb.Hreg(addr + 0, static_cast<uint16_t>(value >> 16)); // MSW first
    _mb.Hreg(addr + 1, static_cast<uint16_t>(value & 0xFFFF));
}

void ModbusDtuClass::setFloat32(const uint16_t addr, const float value)
{
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32 bit");
    memcpy(&bits, &value, sizeof(bits));
    setUInt32(addr, bits);
}

void ModbusDtuClass::setString(const uint16_t addr, const uint8_t numRegs, const char* str)
{
    const size_t len = strlen(str);
    for (uint16_t i = 0; i < numRegs; i++) {
        const uint8_t hi = (2u * i < len) ? static_cast<uint8_t>(str[2u * i]) : 0u;
        const uint8_t lo = (2u * i + 1u < len) ? static_cast<uint8_t>(str[2u * i + 1u]) : 0u;
        setUInt16(addr + i, static_cast<uint16_t>(hi << 8 | lo));
    }
}

void ModbusDtuClass::buildRegisterReference(JsonArray& array) const
{
    const auto& cfg = Configuration.get().Modbus;
    const uint16_t d = MODBUS_BASE_ADDRESS + METER_DATA_OFFSET;
    const bool isInt = (cfg.Representation == 1);
    const MeterValues& v = _values;

    // floatOff / intOff are the field's offset within the meter data block for
    // each representation; the reported address is where a client reads the value.
    auto add = [&](const char* name, uint16_t floatOff, uint16_t intOff, const char* unit, float value) {
        JsonObject o = array.add<JsonObject>();
        o["name"] = name;
        o["address"] = d + (isInt ? intOff : floatOff);
        o["unit"] = unit;
        o["value"] = value;
    };

    add("Current total", 0, 0, "A", v.current);
    add("Current L1", 2, 1, "A", v.currentL1);
    add("Current L2", 4, 2, "A", v.currentL2);
    add("Current L3", 6, 3, "A", v.currentL3);
    add("Voltage L-N avg", 8, 5, "V", v.voltageLN);
    add("Voltage L1-N", 10, 6, "V", v.voltageL1);
    add("Voltage L2-N", 12, 7, "V", v.voltageL2);
    add("Voltage L3-N", 14, 8, "V", v.voltageL3);
    add("Voltage L-L avg", 16, 9, "V", v.voltageLL);
    add("Frequency", 24, 14, "Hz", v.frequency);
    add("Power total", 26, 16, "W", v.power);
    add("Power L1", 28, 17, "W", v.powerL1);
    add("Power L2", 30, 18, "W", v.powerL2);
    add("Power L3", 32, 19, "W", v.powerL3);
    add("Apparent power", 34, 21, "VA", v.apparentPower);
    add("Reactive power", 42, 26, "var", v.reactivePower);
    add("Power factor", 50, 31, "", v.powerFactor);
    add("Energy exported", 58, 36, "Wh", v.exportedWh);
    add("Energy imported", 66, 44, "Wh", v.importedWh);
}
