/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <cstdint>
#include <memory>
#include <mooncake.h>

class AppCodexMicro : public mooncake::AppAbility {
public:
    AppCodexMicro();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr uint32_t UiRefreshPeriodMs = 33;

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::CodexMicroView> _view;
    uint32_t _last_ui_update_ms = 0;
    bool _mic_host_active = false;
    bool _send_host_active = false;
};
