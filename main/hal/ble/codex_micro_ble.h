/*
 * SPDX-License-Identifier: MIT
 *
 * Codex Micro compatibility protocol adapted from imliubo/codex-micro-4-core2.
 * Copyright (c) 2026 imliubo.
 */
#pragma once

#include "codex_micro_protocol.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_event.h>
#include <esp_gap_ble_api.h>
#include <esp_hidd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct cJSON;

struct CodexMicroLight {
    uint32_t color               = 0;
    float brightness             = 0.0f;
    CodexMicroLightEffect effect = CodexMicroLightEffect::Off;
    float speed                  = 0.0f;
    uint32_t magic               = 0;
};

struct CodexMicroState {
    std::array<CodexMicroLight, 6> threads = {};
    CodexMicroLight ambient;
    CodexMicroLight keys;
    bool ready        = false;
    bool connected    = false;
    uint8_t battery   = 100;
    bool charging     = false;
    uint32_t revision = 0;
};

class CodexMicroBle {
public:
    bool begin();
    bool connected();
    CodexMicroState snapshot();
    void setBattery(uint8_t percentage, bool charging);
    bool resetPairing();

    bool sendKey(CodexMicroControl control, CodexMicroKeyAction action, int8_t agent = -1);
    bool sendJoystick(float angle, float distance);

private:
    static constexpr uint8_t ReportId             = 6;
    static constexpr std::size_t ReportSize       = 63;
    static constexpr std::size_t PayloadSize      = 61;
    static constexpr std::size_t MaxRpcBufferSize = 4096;

    static void gapEventCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
    static void hidEventCallback(void* handlerArgs, esp_event_base_t base, int32_t id, void* eventData);

    bool initializeController();
    bool configureAdvertising();
    bool schedulePairingRestart();
    void maybeStartAdvertising();
    void onConnected(bool connected);
    void onOutput(const uint8_t* data, std::size_t length);
    void handleRpc(const cJSON* request);
    void updateThreadLighting(const cJSON* values);
    void updateLightingSide(CodexMicroLight& side, const cJSON* value);

    bool sendJson(const char* json);
    bool sendJsonObject(cJSON* object);
    void sendResult(const cJSON* id, cJSON* result);
    void sendSuccess(const cJSON* id);
    void setReady(bool ready);

    esp_hidd_dev_t* _hid_device    = nullptr;
    SemaphoreHandle_t _state_mutex = nullptr;
    SemaphoreHandle_t _tx_mutex    = nullptr;
    CodexMicroState _state;
    CodexMicroLight _configured_ambient;
    CodexMicroLight _configured_keys;
    std::string _rpc_buffer;
    int8_t _ambient_sync_thread               = -1;
    int8_t _keys_sync_thread                  = -1;
    std::atomic_bool _initialized             = false;
    std::atomic_bool _adv_data_ready          = false;
    std::atomic_bool _scan_rsp_ready          = false;
    std::atomic_bool _hid_ready               = false;
    std::atomic_bool _advertising             = false;
    std::atomic_bool _pairing_reset_requested = false;
    std::atomic_int _pairing_bonds_pending    = 0;
};

CodexMicroBle& GetCodexMicroBle();
