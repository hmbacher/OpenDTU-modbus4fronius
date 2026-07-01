// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <ArduinoJson.h>
#include <ModbusIP_ESP8266.h>
#include <TaskSchedulerDeclarations.h>
#include <cstdint>

// Aggregated meter values that feed the SunSpec meter model.
struct MeterValues {
    float current = 0; // total AC current (A)
    float currentL1 = 0;
    float currentL2 = 0;
    float currentL3 = 0;
    float voltageLN = 0; // L-N average (V)
    float voltageL1 = 0;
    float voltageL2 = 0;
    float voltageL3 = 0;
    float voltageLL = 0; // L-L average (V)
    float frequency = 0; // Hz
    float power = 0; // total real power (W)
    float powerL1 = 0;
    float powerL2 = 0;
    float powerL3 = 0;
    float apparentPower = 0; // VA
    float reactivePower = 0; // var
    float powerFactor = 1.0f;
    float exportedWh = 0; // total real energy exported (Wh)
    float importedWh = 0; // total real energy imported (Wh)
};

class ModbusDtuClass {
public:
    ModbusDtuClass();
    void init(Scheduler& scheduler);

    // (Re)build the register map and (lazily) start the TCP server from the
    // current configuration. Safe to call repeatedly (e.g. after a config POST).
    void applyConfig();

    // --- status accessors (used by the web API) ---
    bool isServerRunning() const { return _serverStarted; }
    uint16_t getActivePort() const { return _activePort; }
    bool hasValidData() const { return _haveData; }
    uint8_t getIncludedCount() const { return _includedCount; }
    const MeterValues& getValues() const { return _values; }

    float getCurrentPower() const { return _values.power; }
    float getCurrentVoltage() const { return _values.voltageLN; }
    float getCurrentFrequency() const { return _values.frequency; }
    float getCurrentCurrent() const { return _values.current; }
    float getTotalYield() const { return _values.exportedWh; }

    uint16_t getBaseAddress() const { return _mapBase; }
    uint16_t getMeterModelId() const { return meterModelId(); }
    uint16_t getMeterModelLength() const { return meterModelLength(); }

    // Append a {name, address, value} entry for every populated SunSpec field,
    // using the live/test values and the active representation's register offsets.
    void buildRegisterReference(JsonArray& array) const;

private:
    void loop();
    void rebuildRegisters();
    void updateValues();
    void collectData(); // fill _values from the selected inverters
    void fillTestValues(); // fill _values with constant test data
    void writeFloatModel(uint16_t dataAddr);
    void writeIntModel(uint16_t dataAddr);

    uint16_t meterModelId() const; // 213 (float) or 203 (int+SF) -- three phase (required by Fronius)
    uint16_t meterModelLength() const; // 124 (float) or 105 (int+SF)

    // SunSpec register helpers (all values written big-endian, MSW first)
    void setUInt16(uint16_t addr, uint16_t value);
    void setInt16(uint16_t addr, int16_t value);
    void setUInt32(uint16_t addr, uint32_t value);
    void setFloat32(uint16_t addr, float value);
    void setString(uint16_t addr, uint8_t numRegs, const char* str);

    Task _loopTask;
    ModbusIP _mb;

    bool _serverStarted = false;
    uint16_t _activePort = 0;

    bool _mapAllocated = false;
    uint16_t _mapBase = 0;
    uint16_t _mapLen = 0;

    uint32_t _lastUpdateMillis = 0;

    MeterValues _values;
    uint8_t _includedCount = 0;
    bool _haveData = false;
};

extern ModbusDtuClass ModbusDtu;
