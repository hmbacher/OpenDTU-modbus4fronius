export interface ModbusInverter {
    index: number;
    serial: string;
    name: string;
    included: boolean;
}

export interface ModbusConfig {
    enabled: boolean;
    test_mode: boolean;
    port: number;
    representation: number;
    sign: number;
    update_interval: number;
    manufacturer: string;
    model: string;
    serial: string;
    version: string;
    reference_inverter: number;
    inverters: ModbusInverter[];
}

export interface ModbusStatusInverter {
    index: number;
    serial: string;
    name: string;
    included: boolean;
    reachable: boolean;
}

export interface ModbusRegister {
    name: string;
    address: number;
    unit: string;
    value: number;
}

export interface ModbusStatus {
    enabled: boolean;
    test_mode: boolean;
    running: boolean;
    port: number;
    valid: boolean;
    included_count: number;
    base_address: number;
    model_id: number;
    model_length: number;
    power: number;
    voltage: number;
    frequency: number;
    current: number;
    yield: number;
    registers: ModbusRegister[];
    inverters: ModbusStatusInverter[];
}
