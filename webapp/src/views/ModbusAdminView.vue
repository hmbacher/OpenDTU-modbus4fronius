<template>
    <BasePage :title="$t('modbusadmin.ModbusSettings')" :isLoading="dataLoading">
        <BootstrapAlert
            v-model="alert.show"
            dismissible
            :variant="alert.type"
            :auto-dismiss="alert.type != 'success' ? 0 : 5000"
        >
            {{ alert.message }}
        </BootstrapAlert>

        <form @submit="saveModbusConfig">
            <CardElement :text="$t('modbusadmin.ServerConfiguration')" textVariant="text-bg-primary">
                <InputElement
                    :label="$t('modbusadmin.EnableModbus')"
                    v-model="modbusConfigList.enabled"
                    type="checkbox"
                    wide
                    :tooltip="$t('modbusadmin.EnableModbusHint')"
                />

                <template v-if="modbusConfigList.enabled">
                    <InputElement
                        :label="$t('modbusadmin.Port')"
                        v-model="modbusConfigList.port"
                        type="number"
                        min="1"
                        max="65535"
                        wide
                        :tooltip="$t('modbusadmin.PortHint')"
                    />

                    <InputElement
                        :label="$t('modbusadmin.TestMode')"
                        v-model="modbusConfigList.test_mode"
                        type="checkbox"
                        wide
                        :tooltip="$t('modbusadmin.TestModeHint')"
                    />

                    <div v-if="modbusConfigList.test_mode" class="alert alert-warning" role="alert">
                        {{ $t('modbusadmin.TestModeActiveHint') }}
                    </div>

                    <CardElement :text="$t('modbusadmin.MeterModel')" textVariant="text-bg-secondary" add-space>
                        <div class="row mb-3">
                            <label class="col-sm-4 col-form-label">
                                {{ $t('modbusadmin.Representation') }}
                                <BIconInfoCircle v-tooltip :title="$t('modbusadmin.RepresentationHint')" />
                            </label>
                            <div class="col-sm-8">
                                <select class="form-select" v-model.number="modbusConfigList.representation">
                                    <option :value="0">{{ $t('modbusadmin.RepresentationFloat') }}</option>
                                    <option :value="1">{{ $t('modbusadmin.RepresentationInt') }}</option>
                                </select>
                            </div>
                        </div>

                        <div class="row mb-3">
                            <label class="col-sm-4 col-form-label">
                                {{ $t('modbusadmin.Sign') }}
                                <BIconInfoCircle v-tooltip :title="$t('modbusadmin.SignHint')" />
                            </label>
                            <div class="col-sm-8">
                                <select class="form-select" v-model.number="modbusConfigList.sign">
                                    <option :value="-1">{{ $t('modbusadmin.SignNegative') }}</option>
                                    <option :value="1">{{ $t('modbusadmin.SignPositive') }}</option>
                                </select>
                            </div>
                        </div>

                        <InputElement
                            :label="$t('modbusadmin.UpdateInterval')"
                            v-model="modbusConfigList.update_interval"
                            type="number"
                            min="200"
                            max="60000"
                            wide
                            :postfix="$t('modbusadmin.Milliseconds')"
                            :tooltip="$t('modbusadmin.UpdateIntervalHint')"
                        />

                        <InputElement
                            :label="$t('modbusadmin.Manufacturer')"
                            v-model="modbusConfigList.manufacturer"
                            type="text"
                            maxlength="32"
                            wide
                            :tooltip="$t('modbusadmin.ManufacturerHint')"
                        />

                        <InputElement
                            :label="$t('modbusadmin.Model')"
                            v-model="modbusConfigList.model"
                            type="text"
                            maxlength="32"
                            wide
                            :tooltip="$t('modbusadmin.ModelHint')"
                        />

                        <InputElement
                            :label="$t('modbusadmin.Serial')"
                            v-model="modbusConfigList.serial"
                            type="text"
                            maxlength="32"
                            wide
                            :tooltip="$t('modbusadmin.SerialHint')"
                        />

                        <InputElement
                            :label="$t('modbusadmin.Version')"
                            v-model="modbusConfigList.version"
                            type="text"
                            maxlength="32"
                            wide
                            :tooltip="$t('modbusadmin.VersionHint')"
                        />
                    </CardElement>

                    <CardElement :text="$t('modbusadmin.DataSource')" textVariant="text-bg-secondary" add-space>
                        <div v-if="!modbusConfigList.inverters || modbusConfigList.inverters.length === 0">
                            <div class="alert alert-warning mb-0" role="alert">
                                {{ $t('modbusadmin.NoInverters') }}
                            </div>
                        </div>
                        <template v-else>
                            <InputElement
                                v-for="inverter in modbusConfigList.inverters"
                                :key="inverter.index"
                                :label="inverter.name || inverter.serial"
                                v-model="inverter.included"
                                type="checkbox"
                                wide
                                :tooltip="$t('modbusadmin.IncludeInverterHint')"
                            />

                            <div v-if="modbusConfigList.inverters.length > 1" class="row mb-3">
                                <label class="col-sm-4 col-form-label">
                                    {{ $t('modbusadmin.ReferenceInverter') }}
                                    <BIconInfoCircle v-tooltip :title="$t('modbusadmin.ReferenceInverterHint')" />
                                </label>
                                <div class="col-sm-8">
                                    <select class="form-select" v-model.number="modbusConfigList.reference_inverter">
                                        <option
                                            v-for="inverter in modbusConfigList.inverters"
                                            :key="inverter.index"
                                            :value="inverter.index"
                                        >
                                            {{ inverter.name || inverter.serial }}
                                        </option>
                                    </select>
                                </div>
                            </div>
                        </template>
                    </CardElement>
                </template>
            </CardElement>

            <FormFooter @reload="getModbusConfig" />
        </form>
    </BasePage>
</template>

<script lang="ts">
import BasePage from '@/components/BasePage.vue';
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import CardElement from '@/components/CardElement.vue';
import FormFooter from '@/components/FormFooter.vue';
import InputElement from '@/components/InputElement.vue';
import type { AlertResponse } from '@/types/AlertResponse';
import type { ModbusConfig } from '@/types/ModbusConfig';
import { authHeader, handleResponse } from '@/utils/authentication';
import { BIconInfoCircle } from 'bootstrap-icons-vue';
import { defineComponent } from 'vue';

export default defineComponent({
    components: {
        BasePage,
        BootstrapAlert,
        CardElement,
        FormFooter,
        InputElement,
        BIconInfoCircle,
    },
    data() {
        return {
            dataLoading: true,
            modbusConfigList: {} as ModbusConfig,
            alert: {} as AlertResponse,
        };
    },
    created() {
        this.getModbusConfig();
    },
    methods: {
        getModbusConfig() {
            this.dataLoading = true;
            fetch('/api/modbus/config', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.modbusConfigList = data;
                    this.dataLoading = false;
                });
        },
        saveModbusConfig(e: Event) {
            e.preventDefault();

            const formData = new FormData();
            formData.append('data', JSON.stringify(this.modbusConfigList));

            fetch('/api/modbus/config', {
                method: 'POST',
                headers: authHeader(),
                body: formData,
            })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((response) => {
                    this.alert.message = this.$t('apiresponse.' + response.code, response.param);
                    this.alert.type = response.type;
                    this.alert.show = true;
                });
        },
    },
});
</script>
