/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <cstdint>
#include <memory>
#include <mooncake.h>

class SerialDebug;

class AppCodexMicro : public mooncake::AppAbility {
public:
    enum class DebugScreen : uint8_t {
        Command,
        Agent,
        Mic,
    };

    struct DebugInputState {
        bool buttonA = false;
        bool buttonB = false;
        bool touch   = false;
        int16_t x    = -1;
        int16_t y    = -1;
    };

    AppCodexMicro();
    ~AppCodexMicro() override;

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

    bool debugUiReady();
    bool debugSetScreen(DebugScreen screen);
    const char* debugScreenName();
    void debugSetInputCapture(bool enabled);
    DebugInputState debugInputState();

private:
    static constexpr uint32_t UiRefreshPeriodMs = 33;

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::CodexMicroView> _view;
    std::unique_ptr<SerialDebug> _serial_debug;
    uint32_t _last_ui_update_ms = 0;
    bool _mic_host_active       = false;
    bool _send_host_active      = false;
    bool _debug_input_capture   = false;
};
