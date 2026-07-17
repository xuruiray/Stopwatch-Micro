/*
 * SPDX-License-Identifier: MIT
 *
 * Codex Micro compatibility protocol adapted from imliubo/codex-micro-4-core2.
 * Copyright (c) 2026 imliubo.
 */
#include "codex_micro_ble.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include <cJSON.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gatts_api.h>
#include <esp_hid_common.h>
#include <esp_hidd_gatts.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/task.h>
#include <system_config.h>

extern "C" void codex_micro_hid_gatt_compat_link_anchor();

namespace {

constexpr const char* Tag             = "CodexMicro-BLE";
constexpr const char* DeviceName      = system_config::ProductName;
constexpr const char* Manufacturer    = "Work Louder";
constexpr const char* FirmwareVersion = system_config::FirmwareVersion;
constexpr uint16_t VendorId           = 0x303A;
constexpr uint16_t ProductId          = 0x8360;
constexpr uint16_t ProductVersion     = 0x0101;

const uint8_t ReportMap[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (1)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x06,        // Report ID (6)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x3F,        // Report Count (63)
    0x09, 0x01,        // Usage (1)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0x95, 0x3F,        // Report Count (63)
    0x09, 0x02,        // Usage (2)
    0x91, 0x02,        // Output (Data, Variable, Absolute)
    0xC0               // End Collection
};

esp_hid_raw_report_map_t ReportMaps[] = {
    {
        .data = ReportMap,
        .len  = sizeof(ReportMap),
    },
};

esp_hid_device_config_t HidConfig = {
    .vendor_id         = VendorId,
    .product_id        = ProductId,
    .version           = ProductVersion,
    .device_name       = DeviceName,
    .manufacturer_name = Manufacturer,
    .serial_number     = nullptr,
    .report_maps       = ReportMaps,
    .report_maps_len   = 1,
};

const uint8_t HidServiceUuid128[] = {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

esp_ble_adv_params_t AdvertisingParams = {
    .adv_int_min       = 0x20,
    .adv_int_max       = 0x30,
    .adv_type          = ADV_TYPE_IND,
    .own_addr_type     = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr         = {},
    .peer_addr_type    = BLE_ADDR_TYPE_PUBLIC,
    .channel_map       = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

bool logStepError(const char* step, esp_err_t error)
{
    if (error == ESP_OK) {
        return false;
    }
    ESP_LOGE(Tag, "%s failed: %s", step, esp_err_to_name(error));
    return true;
}

const cJSON* objectItem(const cJSON* object, const char* name)
{
    return object == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(object, name);
}

CodexMicroLightEffect parseLightEffect(const cJSON* value, CodexMicroLightEffect fallback)
{
    if (cJSON_IsNumber(value)) {
        const int effect = value->valueint;
        if (effect >= static_cast<int>(CodexMicroLightEffect::Off) &&
            effect <= static_cast<int>(CodexMicroLightEffect::ShallowBreath)) {
            return static_cast<CodexMicroLightEffect>(effect);
        }
        return fallback;
    }
    if (!cJSON_IsString(value) || value->valuestring == nullptr) {
        return fallback;
    }

    struct EffectName {
        const char* name;
        CodexMicroLightEffect effect;
    };
    constexpr EffectName names[] = {
        {"off", CodexMicroLightEffect::Off},
        {"solid", CodexMicroLightEffect::Solid},
        {"snake", CodexMicroLightEffect::Snake},
        {"rainbow", CodexMicroLightEffect::Rainbow},
        {"breath", CodexMicroLightEffect::Breath},
        {"gradient", CodexMicroLightEffect::Gradient},
        {"shallowBreath", CodexMicroLightEffect::ShallowBreath},
    };
    for (const EffectName& candidate : names) {
        if (std::strcmp(value->valuestring, candidate.name) == 0) {
            return candidate.effect;
        }
    }
    return fallback;
}

bool jsonFlag(const cJSON* value)
{
    return cJSON_IsTrue(value) || (cJSON_IsNumber(value) && value->valueint != 0);
}

void addResponseId(cJSON* response, const cJSON* id)
{
    if (id == nullptr) {
        cJSON_AddNullToObject(response, "id");
        return;
    }
    cJSON* copy = cJSON_Duplicate(id, true);
    if (copy != nullptr) {
        cJSON_AddItemToObject(response, "id", copy);
    } else {
        cJSON_AddNullToObject(response, "id");
    }
}

void restartAfterPairingReset(void*)
{
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

}  // namespace

CodexMicroBle& GetCodexMicroBle()
{
    static CodexMicroBle instance;
    return instance;
}

bool CodexMicroBle::begin()
{
    codex_micro_hid_gatt_compat_link_anchor();
    bool expected = false;
    if (!_initialized.compare_exchange_strong(expected, true)) {
        return _hid_device != nullptr;
    }

    _state_mutex = xSemaphoreCreateMutex();
    _tx_mutex    = xSemaphoreCreateMutex();
    if (_state_mutex == nullptr || _tx_mutex == nullptr) {
        ESP_LOGE(Tag, "failed to create protocol mutexes");
        return false;
    }

    if (!initializeController() || !configureAdvertising()) {
        return false;
    }

    if (logStepError("esp_ble_gatts_register_callback", esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler))) {
        return false;
    }

    if (logStepError("esp_hidd_dev_init",
                     esp_hidd_dev_init(&HidConfig, ESP_HID_TRANSPORT_BLE, hidEventCallback, &_hid_device))) {
        return false;
    }

    ESP_LOGI(Tag, "initializing vendor HID VID=%04X PID=%04X usage=FF00 report=%u", VendorId, ProductId, ReportId);
    return true;
}

bool CodexMicroBle::initializeController()
{
    esp_err_t error = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (logStepError("esp_bt_controller_mem_release", error)) {
        return false;
    }

    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (logStepError("esp_bt_controller_init", esp_bt_controller_init(&config))) {
        return false;
    }
    if (logStepError("esp_bt_controller_enable", esp_bt_controller_enable(ESP_BT_MODE_BLE))) {
        return false;
    }

    esp_bluedroid_config_t bluedroid_config = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_config.ssp_en                  = false;
    if (logStepError("esp_bluedroid_init", esp_bluedroid_init_with_cfg(&bluedroid_config))) {
        return false;
    }
    if (logStepError("esp_bluedroid_enable", esp_bluedroid_enable())) {
        return false;
    }
    if (logStepError("esp_ble_gap_register_callback", esp_ble_gap_register_callback(gapEventCallback))) {
        return false;
    }
    return true;
}

bool CodexMicroBle::configureAdvertising()
{
    esp_ble_auth_req_t auth_request = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t io_capability  = ESP_IO_CAP_NONE;
    uint8_t init_key                = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t response_key            = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t key_size                = 16;

    if (logStepError("set auth mode",
                     esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_request, sizeof(auth_request))) ||
        logStepError("set IO capability",
                     esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io_capability, sizeof(io_capability))) ||
        logStepError("set initiator keys",
                     esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key))) ||
        logStepError("set responder keys",
                     esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &response_key, sizeof(response_key))) ||
        logStepError("set key size",
                     esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size))) ||
        logStepError("set device name", esp_ble_gap_set_device_name(DeviceName))) {
        return false;
    }

    esp_ble_adv_data_t advertising_data = {
        .set_scan_rsp        = false,
        // The 128-bit HID service UUID leaves no room for the full name in
        // the 31-byte advertising packet, so the name is sent separately in
        // the scan response.
        .include_name        = false,
        .include_txpower     = true,
        .min_interval        = 0x0006,
        .max_interval        = 0x0012,
        .appearance          = ESP_HID_APPEARANCE_GENERIC,
        .manufacturer_len    = 0,
        .p_manufacturer_data = nullptr,
        .service_data_len    = 0,
        .p_service_data      = nullptr,
        .service_uuid_len    = sizeof(HidServiceUuid128),
        .p_service_uuid      = const_cast<uint8_t*>(HidServiceUuid128),
        .flag                = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    };

    esp_ble_adv_data_t scan_response_data = {
        .set_scan_rsp        = true,
        .include_name        = true,
        .include_txpower     = false,
        .min_interval        = 0,
        .max_interval        = 0,
        .appearance          = 0,
        .manufacturer_len    = 0,
        .p_manufacturer_data = nullptr,
        .service_data_len    = 0,
        .p_service_data      = nullptr,
        .service_uuid_len    = 0,
        .p_service_uuid      = nullptr,
        .flag                = 0,
    };

    return !logStepError("esp_ble_gap_config_adv_data", esp_ble_gap_config_adv_data(&advertising_data)) &&
           !logStepError("esp_ble_gap_config_scan_rsp_data", esp_ble_gap_config_adv_data(&scan_response_data));
}

void CodexMicroBle::gapEventCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
{
    auto& owner = GetCodexMicroBle();
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            owner._adv_data_ready.store(true);
            owner.maybeStartAdvertising();
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            owner._scan_rsp_ready.store(true);
            owner.maybeStartAdvertising();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(Tag, "advertising as %s", DeviceName);
            } else {
                owner._advertising.store(false);
                ESP_LOGE(Tag, "advertising start failed: %d", param->adv_start_cmpl.status);
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            owner._advertising.store(false);
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param->ble_security.auth_cmpl.success) {
                ESP_LOGI(Tag, "pairing complete");
            } else {
                ESP_LOGE(Tag, "pairing failed: 0x%02x", param->ble_security.auth_cmpl.fail_reason);
            }
            break;
        case ESP_GAP_BLE_REMOVE_BOND_DEV_COMPLETE_EVT:
            if (!owner._pairing_reset_requested.load()) {
                break;
            }
            if (param->remove_bond_dev_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(Tag, "BLE bond removal completion failed: %d", param->remove_bond_dev_cmpl.status);
                owner._pairing_bonds_pending.store(0);
                owner._pairing_reset_requested.store(false);
                break;
            }
            if (owner._pairing_bonds_pending.fetch_sub(1) == 1) {
                ESP_LOGW(Tag, "all BLE bonds removed; restarting in pairing mode");
                owner.schedulePairingRestart();
            }
            break;
        default:
            break;
    }
}

void CodexMicroBle::hidEventCallback(void*, esp_event_base_t, int32_t id, void* eventData)
{
    auto& owner = GetCodexMicroBle();
    auto event  = static_cast<esp_hidd_event_t>(id);
    auto* data  = static_cast<esp_hidd_event_data_t*>(eventData);

    switch (event) {
        case ESP_HIDD_START_EVENT:
            owner._hid_ready.store(true);
            owner.setReady(true);
            ESP_LOGI(Tag, "CODEX_MICRO_BLE_READY");
            owner.maybeStartAdvertising();
            break;
        case ESP_HIDD_CONNECT_EVENT:
            owner._advertising.store(false);
            owner.onConnected(true);
            ESP_LOGI(Tag, "host connected");
            break;
        case ESP_HIDD_OUTPUT_EVENT:
            if (data != nullptr && data->output.report_id == ReportId) {
                owner.onOutput(data->output.data, data->output.length);
            }
            break;
        case ESP_HIDD_DISCONNECT_EVENT:
            owner.onConnected(false);
            owner._advertising.store(false);
            ESP_LOGI(Tag, "host disconnected reason=%d", data == nullptr ? -1 : data->disconnect.reason);
            owner.maybeStartAdvertising();
            break;
        default:
            break;
    }
}

void CodexMicroBle::maybeStartAdvertising()
{
    if (!_adv_data_ready.load() || !_scan_rsp_ready.load() || !_hid_ready.load() || connected()) {
        return;
    }
    if (_advertising.exchange(true)) {
        return;
    }
    esp_err_t error = esp_ble_gap_start_advertising(&AdvertisingParams);
    if (error != ESP_OK) {
        _advertising.store(false);
        ESP_LOGE(Tag, "esp_ble_gap_start_advertising failed: %s", esp_err_to_name(error));
    }
}

void CodexMicroBle::setReady(bool ready)
{
    if (_state_mutex == nullptr) {
        return;
    }
    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    _state.ready = ready;
    ++_state.revision;
    xSemaphoreGive(_state_mutex);
}

void CodexMicroBle::onConnected(bool connectedValue)
{
    if (_state_mutex == nullptr) {
        return;
    }
    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    _state.connected = connectedValue;
    if (!connectedValue) {
        _state.threads = {};
        _state.ambient = {};
        _state.keys = {};
        _configured_ambient = {};
        _configured_keys = {};
        _ambient_sync_thread = -1;
        _keys_sync_thread = -1;
    }
    ++_state.revision;
    xSemaphoreGive(_state_mutex);
    _rpc_buffer.clear();
}

bool CodexMicroBle::connected()
{
    if (_state_mutex == nullptr) {
        return false;
    }
    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    bool value = _state.connected;
    xSemaphoreGive(_state_mutex);
    return value;
}

CodexMicroState CodexMicroBle::snapshot()
{
    CodexMicroState copy;
    if (_state_mutex == nullptr) {
        return copy;
    }
    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    copy = _state;
    xSemaphoreGive(_state_mutex);
    return copy;
}

void CodexMicroBle::setBattery(uint8_t percentage, bool charging)
{
    percentage = std::min<uint8_t>(percentage, 100);
    if (_state_mutex != nullptr) {
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        if (_state.battery != percentage || _state.charging != charging) {
            _state.battery  = percentage;
            _state.charging = charging;
            ++_state.revision;
        }
        xSemaphoreGive(_state_mutex);
    }
    if (_hid_device != nullptr) {
        esp_hidd_dev_battery_set(_hid_device, percentage);
    }
}

bool CodexMicroBle::resetPairing()
{
    if (!_initialized.load()) {
        return false;
    }
    if (_pairing_reset_requested.exchange(true)) {
        return true;
    }

    int bond_count = esp_ble_get_bond_device_num();
    if (bond_count < 0) {
        ESP_LOGE(Tag, "failed to read BLE bond database");
        _pairing_reset_requested.store(false);
        return false;
    }

    if (bond_count > 0) {
        std::unique_ptr<esp_ble_bond_dev_t[]> bonds(new (std::nothrow) esp_ble_bond_dev_t[bond_count]);
        if (bonds == nullptr) {
            ESP_LOGE(Tag, "failed to allocate BLE bond list");
            _pairing_reset_requested.store(false);
            return false;
        }
        int listed = bond_count;
        esp_err_t error = esp_ble_get_bond_device_list(&listed, bonds.get());
        if (error != ESP_OK) {
            ESP_LOGE(Tag, "failed to list BLE bonds: %s", esp_err_to_name(error));
            _pairing_reset_requested.store(false);
            return false;
        }
        if (listed == 0) {
            ESP_LOGW(Tag, "pairing reset requested; bond list became empty");
            return schedulePairingRestart();
        }
        _pairing_bonds_pending.store(listed);
        for (int i = 0; i < listed; ++i) {
            error = esp_ble_remove_bond_device(bonds[i].bd_addr);
            if (error != ESP_OK) {
                ESP_LOGE(Tag, "failed to remove BLE bond %d: %s", i, esp_err_to_name(error));
                _pairing_bonds_pending.store(0);
                _pairing_reset_requested.store(false);
                return false;
            }
        }
        ESP_LOGW(Tag, "pairing reset requested; removing %d bond(s)", listed);
        return true;
    }

    ESP_LOGW(Tag, "pairing reset requested; no stored bonds");
    return schedulePairingRestart();
}

bool CodexMicroBle::schedulePairingRestart()
{
    if (xTaskCreate(restartAfterPairingReset, "codex_pair_reset", 2048, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(Tag, "failed to schedule pairing reset restart");
        _pairing_bonds_pending.store(0);
        _pairing_reset_requested.store(false);
        return false;
    }
    return true;
}

bool CodexMicroBle::sendKey(CodexMicroControl control, CodexMicroKeyAction action, int8_t agent)
{
    cJSON* message = cJSON_CreateObject();
    cJSON* params  = cJSON_CreateObject();
    if (message == nullptr || params == nullptr) {
        cJSON_Delete(message);
        cJSON_Delete(params);
        return false;
    }
    cJSON_AddStringToObject(message, "method", "v.oai.hid");
    const char* key = codexMicroControlCode(control);
    cJSON_AddStringToObject(params, "k", key);
    cJSON_AddNumberToObject(params, "act", static_cast<uint8_t>(action));
    if (agent >= 0) {
        cJSON_AddNumberToObject(params, "ag", agent);
    }
    cJSON_AddItemToObject(message, "params", params);
    const bool sent = sendJsonObject(message);
    ESP_LOGI(Tag, "TX key=%s act=%u agent=%d sent=%d", key == nullptr ? "" : key,
             static_cast<unsigned>(action), static_cast<int>(agent), sent ? 1 : 0);
    return sent;
}

bool CodexMicroBle::sendJoystick(float angle, float distance)
{
    cJSON* message = cJSON_CreateObject();
    cJSON* params  = cJSON_CreateObject();
    if (message == nullptr || params == nullptr) {
        cJSON_Delete(message);
        cJSON_Delete(params);
        return false;
    }
    cJSON_AddStringToObject(message, "method", "v.oai.rad");
    cJSON_AddNumberToObject(params, "a", angle);
    cJSON_AddNumberToObject(params, "d", distance);
    cJSON_AddItemToObject(message, "params", params);
    const bool sent = sendJsonObject(message);
    ESP_LOGI(Tag, "TX stick angle=%.3f distance=%.3f sent=%d", static_cast<double>(angle),
             static_cast<double>(distance), sent ? 1 : 0);
    return sent;
}

bool CodexMicroBle::sendJsonObject(cJSON* object)
{
    if (object == nullptr) {
        return false;
    }
    char* encoded = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    if (encoded == nullptr) {
        return false;
    }
    bool sent = sendJson(encoded);
    std::free(encoded);
    return sent;
}

bool CodexMicroBle::sendJson(const char* json)
{
    if (json == nullptr || _hid_device == nullptr || _tx_mutex == nullptr || !connected()) {
        return false;
    }

    xSemaphoreTake(_tx_mutex, portMAX_DELAY);
    if (!connected()) {
        xSemaphoreGive(_tx_mutex);
        return false;
    }

    std::string framed(json);
    framed.push_back('\n');
    bool success      = true;
    std::size_t offset = 0;
    while (offset < framed.size()) {
        std::size_t chunk = std::min(PayloadSize, framed.size() - offset);
        uint8_t report[ReportSize] = {};
        report[0]                  = 2;
        report[1]                  = static_cast<uint8_t>(chunk);
        std::memcpy(report + 2, framed.data() + offset, chunk);
        esp_err_t error = esp_hidd_dev_input_set(_hid_device, 0, ReportId, report, sizeof(report));
        if (error != ESP_OK) {
            ESP_LOGW(Tag, "input report failed: %s", esp_err_to_name(error));
            success = false;
            break;
        }
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    xSemaphoreGive(_tx_mutex);
    return success;
}

void CodexMicroBle::onOutput(const uint8_t* data, std::size_t length)
{
    if (data == nullptr || length < 2) {
        return;
    }

    std::size_t offset = length >= 3 && data[0] == ReportId ? 1 : 0;
    if (length < offset + 2 || data[offset] != 2) {
        return;
    }

    if (data[offset + 1] > PayloadSize) {
        ESP_LOGW(Tag, "invalid output payload length=%u", static_cast<unsigned>(data[offset + 1]));
        _rpc_buffer.clear();
        return;
    }
    const std::size_t payloadLength = data[offset + 1];
    if (length < offset + 2 + payloadLength) {
        ESP_LOGW(Tag, "truncated output report len=%u payload=%u", static_cast<unsigned>(length),
                 static_cast<unsigned>(payloadLength));
        return;
    }

    const char* payload = reinterpret_cast<const char*>(data + offset + 2);
    constexpr char TopLevelPrefix[] = "{\"method\"";
    bool startsTopLevel = payloadLength >= sizeof(TopLevelPrefix) - 1 &&
                          std::memcmp(payload, TopLevelPrefix, sizeof(TopLevelPrefix) - 1) == 0;
    if (startsTopLevel && !_rpc_buffer.empty()) {
        _rpc_buffer.clear();
    }

    if (_rpc_buffer.size() + payloadLength > MaxRpcBufferSize) {
        ESP_LOGW(Tag, "RPC buffer limit exceeded");
        _rpc_buffer.clear();
        return;
    }

    if (_rpc_buffer.empty()) {
        std::size_t jsonStart = 0;
        while (jsonStart < payloadLength && payload[jsonStart] != '{') {
            ++jsonStart;
        }
        if (jsonStart == payloadLength) {
            return;
        }
        _rpc_buffer.append(payload + jsonStart, payloadLength - jsonStart);
    } else {
        _rpc_buffer.append(payload, payloadLength);
    }

    cJSON* request = cJSON_ParseWithLength(_rpc_buffer.data(), _rpc_buffer.size());
    if (request == nullptr) {
        if (!_rpc_buffer.empty() && _rpc_buffer.back() == '\n') {
            ESP_LOGW(Tag, "invalid complete RPC payload");
            _rpc_buffer.clear();
        }
        return;
    }

    handleRpc(request);
    cJSON_Delete(request);
    _rpc_buffer.clear();
}

void CodexMicroBle::handleRpc(const cJSON* request)
{
    const cJSON* methodValue = objectItem(request, "method");
    const char* method       = cJSON_IsString(methodValue) ? methodValue->valuestring : "";
    const cJSON* id          = objectItem(request, "id");
    const cJSON* params      = objectItem(request, "params");
    ESP_LOGI(Tag, "RPC method=%s", method);

    if (std::strcmp(method, "sys.version") == 0) {
        cJSON* result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "version", FirmwareVersion);
        sendResult(id, result);
        return;
    }

    if (std::strcmp(method, "device.status") == 0) {
        CodexMicroState state = snapshot();
        cJSON* result         = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "version", FirmwareVersion);
        cJSON_AddNumberToObject(result, "profile_index", 0);
        cJSON_AddNumberToObject(result, "layer_index", 1);
        cJSON_AddNumberToObject(result, "battery", state.battery);
        cJSON_AddBoolToObject(result, "is_charging", state.charging);
        sendResult(id, result);
        return;
    }

    if (std::strcmp(method, "v.oai.thstatus") == 0 && cJSON_IsArray(params)) {
        updateThreadLighting(params);
        sendSuccess(id);
        return;
    }

    if (std::strcmp(method, "v.oai.rgbcfg") == 0 && cJSON_IsObject(params)) {
        xSemaphoreTake(_state_mutex, portMAX_DELAY);
        updateLightingSide(_configured_ambient, objectItem(params, "ambient"));
        updateLightingSide(_configured_keys, objectItem(params, "keys"));
        if (_ambient_sync_thread < 0) {
            _state.ambient = _configured_ambient;
        }
        if (_keys_sync_thread < 0) {
            _state.keys = _configured_keys;
        }
        ++_state.revision;
        xSemaphoreGive(_state_mutex);
        sendSuccess(id);
        return;
    }

    if (std::strcmp(method, "lights.preview") == 0 || std::strcmp(method, "host.focused_app") == 0) {
        sendSuccess(id);
        return;
    }

    cJSON* response = cJSON_CreateObject();
    addResponseId(response, id);
    cJSON* error = cJSON_AddObjectToObject(response, "error");
    cJSON_AddNumberToObject(error, "code", -32601);
    cJSON_AddStringToObject(error, "message", "Method not found");
    sendJsonObject(response);
}

void CodexMicroBle::sendResult(const cJSON* id, cJSON* result)
{
    cJSON* response = cJSON_CreateObject();
    if (response == nullptr || result == nullptr) {
        cJSON_Delete(response);
        cJSON_Delete(result);
        return;
    }
    addResponseId(response, id);
    cJSON_AddItemToObject(response, "result", result);
    sendJsonObject(response);
}

void CodexMicroBle::sendSuccess(const cJSON* id)
{
    cJSON* result = cJSON_CreateObject();
    if (result == nullptr) {
        return;
    }
    cJSON_AddBoolToObject(result, "ok", true);
    sendResult(id, result);
}

void CodexMicroBle::updateThreadLighting(const cJSON* values)
{
    xSemaphoreTake(_state_mutex, portMAX_DELAY);
    const cJSON* value = nullptr;
    cJSON_ArrayForEach(value, values)
    {
        const cJSON* idValue = objectItem(value, "id");
        if (!cJSON_IsNumber(idValue) || idValue->valueint < 0 || idValue->valueint >= static_cast<int>(_state.threads.size())) {
            continue;
        }
        const int8_t thread = static_cast<int8_t>(idValue->valueint);
        CodexMicroLight& light = _state.threads[thread];
        updateLightingSide(light, value);
        const cJSON* sync_keys = objectItem(value, "sk");
        if (sync_keys != nullptr) {
            if (jsonFlag(sync_keys)) {
                _keys_sync_thread = thread;
            } else if (_keys_sync_thread == thread) {
                _keys_sync_thread = -1;
            }
        }
        const cJSON* sync_ambient = objectItem(value, "sa");
        if (sync_ambient != nullptr) {
            if (jsonFlag(sync_ambient)) {
                _ambient_sync_thread = thread;
            } else if (_ambient_sync_thread == thread) {
                _ambient_sync_thread = -1;
            }
        }
    }
    if (_keys_sync_thread >= 0) {
        _state.keys       = _state.threads[_keys_sync_thread];
        _state.keys.magic = 0;
    } else {
        _state.keys = _configured_keys;
    }
    if (_ambient_sync_thread >= 0) {
        _state.ambient       = _state.threads[_ambient_sync_thread];
        _state.ambient.magic = 0;
    } else {
        _state.ambient = _configured_ambient;
    }
    ++_state.revision;
    xSemaphoreGive(_state_mutex);
}

void CodexMicroBle::updateLightingSide(CodexMicroLight& side, const cJSON* value)
{
    if (!cJSON_IsObject(value)) {
        return;
    }
    const cJSON* color = objectItem(value, "c");
    const cJSON* brightness = objectItem(value, "b");
    const cJSON* effect = objectItem(value, "e");
    const cJSON* speed = objectItem(value, "s");
    const cJSON* magic = objectItem(value, "m");
    if (cJSON_IsNumber(color)) {
        side.color = static_cast<uint32_t>(color->valuedouble);
    }
    if (cJSON_IsNumber(brightness)) {
        side.brightness = static_cast<float>(brightness->valuedouble);
    }
    side.effect = parseLightEffect(effect, side.effect);
    if (cJSON_IsNumber(speed)) {
        side.speed = static_cast<float>(speed->valuedouble);
    }
    if (cJSON_IsNumber(magic)) {
        side.magic = static_cast<uint32_t>(magic->valuedouble);
    }
}
