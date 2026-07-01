<template>
    <BasePage
        :title="$t('modbusinfo.ModbusInformation')"
        :isLoading="dataLoading"
        :show-reload="true"
        @reload="getModbusStatus"
    >
        <CardElement :text="$t('modbusinfo.ServerStatus')" textVariant="text-bg-primary" table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody>
                        <tr>
                            <th>{{ $t('modbusinfo.ServerState') }}</th>
                            <td>
                                <StatusBadge
                                    :status="status.running"
                                    true_text="modbusinfo.Running"
                                    false_text="modbusinfo.Stopped"
                                />
                                <span v-if="status.running"> ({{ status.port }})</span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.Mode') }}</th>
                            <td>
                                <span v-if="status.test_mode" class="badge text-bg-warning">
                                    {{ $t('modbusinfo.TestMode') }}
                                </span>
                                <span v-else class="badge text-bg-success">
                                    {{ $t('modbusinfo.LiveMode') }}
                                </span>
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.BaseAddress') }}</th>
                            <td>{{ status.base_address }}</td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.MeterModelInfo') }}</th>
                            <td>{{ status.model_id }} ({{ status.model_length }} {{ $t('modbusinfo.Registers') }})</td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.DataValid') }}</th>
                            <td>
                                <StatusBadge
                                    :status="status.valid"
                                    true_text="modbusinfo.Valid"
                                    false_text="modbusinfo.Invalid"
                                    :true_class="'text-bg-success'"
                                    :false_class="'text-bg-warning'"
                                />
                            </td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.IncludedInverters') }}</th>
                            <td>{{ status.included_count }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>

        <CardElement :text="$t('modbusinfo.MeterOutput')" textVariant="text-bg-primary" add-space table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <tbody>
                        <tr>
                            <th>{{ $t('modbusinfo.CurrentPower') }}</th>
                            <td>{{ (status.power ?? 0).toFixed(1) }} W</td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.TotalYield') }}</th>
                            <td>{{ ((status.yield ?? 0) / 1000).toFixed(3) }} kWh</td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.Voltage') }}</th>
                            <td>{{ (status.voltage ?? 0).toFixed(1) }} V</td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.Frequency') }}</th>
                            <td>{{ (status.frequency ?? 0).toFixed(2) }} Hz</td>
                        </tr>
                        <tr>
                            <th>{{ $t('modbusinfo.Current') }}</th>
                            <td>{{ (status.current ?? 0).toFixed(2) }} A</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>

        <CardElement :text="$t('modbusinfo.RegisterReference')" textVariant="text-bg-primary" add-space table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <thead>
                        <tr>
                            <th>{{ $t('modbusinfo.ColField') }}</th>
                            <th class="text-end">{{ $t('modbusinfo.ColAddress') }}</th>
                            <th class="text-end">{{ $t('modbusinfo.ColValue') }}</th>
                            <th>{{ $t('modbusinfo.ColUnit') }}</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="reg in status.registers" :key="reg.address">
                            <td>{{ reg.name }}</td>
                            <td class="text-end font-monospace">{{ reg.address }}</td>
                            <td class="text-end font-monospace">{{ formatValue(reg.value, reg.unit) }}</td>
                            <td>{{ reg.unit }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <div class="alert alert-secondary mb-0" role="alert">
                {{ $t('modbusinfo.AddressHint') }}
            </div>
        </CardElement>

        <CardElement :text="$t('modbusinfo.Inverters')" textVariant="text-bg-primary" add-space table>
            <div class="table-responsive">
                <table class="table table-hover table-condensed">
                    <thead>
                        <tr>
                            <th>{{ $t('modbusinfo.ColName') }}</th>
                            <th>{{ $t('modbusinfo.ColSerial') }}</th>
                            <th class="text-center">{{ $t('modbusinfo.ColIncluded') }}</th>
                            <th class="text-center">{{ $t('modbusinfo.ColReachable') }}</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="inverter in status.inverters" :key="inverter.serial">
                            <td>{{ inverter.name }}</td>
                            <td>{{ inverter.serial }}</td>
                            <td class="text-center">
                                <StatusBadge
                                    :status="inverter.included"
                                    true_text="base.Yes"
                                    false_text="base.No"
                                />
                            </td>
                            <td class="text-center">
                                <StatusBadge
                                    :status="inverter.reachable"
                                    true_text="base.Yes"
                                    false_text="base.No"
                                    :true_class="'text-bg-success'"
                                    :false_class="'text-bg-secondary'"
                                />
                            </td>
                        </tr>
                        <tr v-if="!status.inverters || status.inverters.length === 0">
                            <td colspan="4" class="text-center">{{ $t('modbusinfo.NoInverters') }}</td>
                        </tr>
                    </tbody>
                </table>
            </div>
        </CardElement>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import CardElement from '@/components/CardElement.vue';
import StatusBadge from '@/components/StatusBadge.vue';
import type { ModbusStatus } from '@/types/ModbusConfig';
import { authHeader, handleResponse } from '@/utils/authentication';
import { defineComponent } from 'vue';

export default defineComponent({
    components: {
        BasePage,
        CardElement,
        StatusBadge,
    },
    data() {
        return {
            dataLoading: true,
            status: {} as ModbusStatus,
            statusInterval: 0,
        };
    },
    created() {
        this.getModbusStatus();
        this.statusInterval = window.setInterval(() => {
            this.getModbusStatus();
        }, 5000);
    },
    unmounted() {
        clearInterval(this.statusInterval);
    },
    methods: {
        formatValue(value: number, unit: string): string {
            if (value === undefined || value === null) {
                return '';
            }
            // Energy accumulators are whole Wh; everything else gets two decimals
            return unit === 'Wh' ? value.toFixed(0) : value.toFixed(2);
        },
        getModbusStatus() {
            fetch('/api/modbus/status', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.status = data;
                    this.dataLoading = false;
                });
        },
    },
});
</script>
