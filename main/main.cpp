/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/app_codex_micro/app_codex_micro.h>
#include <hal/hal.h>
#include <hal/ble/codex_micro_ble.h>
#include <memory>
#include <new>
#include <utility>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // BLE is a system service and remains available for the device lifetime.
    if (!GetCodexMicroBle().begin()) {
        mclog::tagError("Codex Micro", "BLE initialization failed; restarting");
        GetHAL().delay(1000);
        GetHAL().reboot();
        return;
    }
    GetCodexMicroBle().setBattery(GetHAL().getBatteryLevel(), GetHAL().isBatteryCharging());
    uint32_t last_codex_battery_update = GetHAL().millis();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // This dedicated firmware has one system UI and no launcher.
    std::unique_ptr<AppCodexMicro> system_app(new (std::nothrow) AppCodexMicro());
    if (system_app == nullptr) {
        mclog::tagError("Codex Micro", "failed to allocate system app");
        return;
    }
    const int app_id = GetMooncake().installApp(std::move(system_app));
    if (app_id < 0 || !GetMooncake().openApp(app_id)) {
        mclog::tagError("Codex Micro", "failed to start system app");
        return;
    }

    // Main loop
    while (true) {
        GetHAL().feedTheDog();
        GetMooncake().update();
        const uint32_t now = GetHAL().millis();
        if (now - last_codex_battery_update >= 30000) {
            last_codex_battery_update = now;
            GetCodexMicroBle().setBattery(GetHAL().getBatteryLevel(), GetHAL().isBatteryCharging());
        }
    }
}
