/*
 * SPDX-License-Identifier: MIT
 */
#include "app_codex_micro.h"

#include <debug/serial_debug.h>
#include <hal/ble/codex_micro_ble.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <system_config.h>
#include <new>
#include <utility>
#include <vector>

using namespace mooncake;

AppCodexMicro::AppCodexMicro()
{
    setAppInfo().name = system_config::ProductName;
}

AppCodexMicro::~AppCodexMicro() = default;

void AppCodexMicro::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppCodexMicro::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    std::unique_ptr<input::KeyManager> key_manager(new (std::nothrow) input::KeyManager());
    std::unique_ptr<view::CodexMicroView> app_view(new (std::nothrow) view::CodexMicroView());
    if (key_manager == nullptr || app_view == nullptr) {
        mclog::tagError(getAppInfo().name, "failed to allocate app state");
        return;
    }
    _key_manager       = std::move(key_manager);
    _last_ui_update_ms = 0;
    _mic_host_active   = false;
    _send_host_active  = false;

    LvglLockGuard lock;
    GetHAL().bootLogo.reset();
    _view = std::move(app_view);
    _view->init(lv_screen_active());

    _serial_debug.reset(new (std::nothrow) SerialDebug(*this));
    if (_serial_debug == nullptr || !_serial_debug->begin()) {
        mclog::tagError(getAppInfo().name, "serial debug initialization failed");
        _serial_debug.reset();
    }
}

void AppCodexMicro::onRunning()
{
    GetHAL().updateButtonStates();
    if (_serial_debug != nullptr) {
        _serial_debug->poll();
    }
    const input::KeyEvent event =
        _key_manager == nullptr || _debug_input_capture ? input::KeyEvent::None : _key_manager->update(false);
    const uint32_t now     = GetHAL().millis();
    const bool refresh_due = now - _last_ui_update_ms >= UiRefreshPeriodMs;
    if (event == input::KeyEvent::None && !refresh_due) {
        return;
    }
    const CodexMicroState state = GetCodexMicroBle().snapshot();

    if (!state.connected) {
        _mic_host_active  = false;
        _send_host_active = false;
    }

    bool mic_view_changed = false;
    if (input::hasKeyEvent(event, input::KeyEvent::MicPress) && state.connected) {
        _mic_host_active = GetCodexMicroBle().sendKey(CodexMicroControl::Mic, CodexMicroKeyAction::Press);
        mic_view_changed = true;
    }
    if (input::hasKeyEvent(event, input::KeyEvent::SendPress) && state.connected) {
        _send_host_active = GetCodexMicroBle().sendKey(CodexMicroControl::Send, CodexMicroKeyAction::Press);
    }
    if (input::hasKeyEvent(event, input::KeyEvent::MicRelease)) {
        if (_mic_host_active && state.connected) {
            GetCodexMicroBle().sendKey(CodexMicroControl::Mic, CodexMicroKeyAction::Release);
        }
        _mic_host_active = false;
        mic_view_changed = true;
    }
    if (input::hasKeyEvent(event, input::KeyEvent::SendRelease)) {
        if (_send_host_active && state.connected) {
            GetCodexMicroBle().sendKey(CodexMicroControl::Send, CodexMicroKeyAction::Release);
        }
        _send_host_active = false;
    }
    const bool toggle_page = input::hasKeyEvent(event, input::KeyEvent::TogglePage) && state.connected;

    // BLE report creation/transmission is intentionally outside the LVGL
    // mutex. Holding that mutex here prevented the touch task from sampling
    // while a physical-key report was being sent.
    LvglLockGuard lock;
    if (_view == nullptr) {
        return;
    }
    _view->update(state);
    if (mic_view_changed) {
        _view->setMicActive(_mic_host_active);
    }
    if (toggle_page) {
        _view->setMicActive(false);
        _view->togglePage();
    }
    _last_ui_update_ms = now;
}

void AppCodexMicro::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_mic_host_active) {
        GetCodexMicroBle().sendKey(CodexMicroControl::Mic, CodexMicroKeyAction::Release);
        _mic_host_active = false;
    }
    if (_send_host_active) {
        GetCodexMicroBle().sendKey(CodexMicroControl::Send, CodexMicroKeyAction::Release);
        _send_host_active = false;
    }
    _key_manager.reset();
    if (_serial_debug != nullptr) {
        _serial_debug->end();
        _serial_debug.reset();
    }
    GetHAL().stopVibrate();
    std::vector<int16_t> empty_audio;
    GetHAL().audioPlay(empty_audio, true);

    LvglLockGuard lock;
    _view.reset();
}

bool AppCodexMicro::debugUiReady()
{
    LvglLockGuard lock;
    return _view != nullptr && _view->ready();
}

bool AppCodexMicro::debugSetScreen(DebugScreen screen)
{
    LvglLockGuard lock;
    if (_view == nullptr || !_view->ready() || !_view->functionalEnabled()) {
        return false;
    }
    switch (screen) {
        case DebugScreen::Command:
            return _view->setPageForDebug(view::CodexMicroView::Page::Command);
        case DebugScreen::Agent:
            return _view->setPageForDebug(view::CodexMicroView::Page::Agent);
        case DebugScreen::Mic:
            _view->setMicActive(true);
            return _view->micActive();
    }
    return false;
}

const char* AppCodexMicro::debugScreenName()
{
    LvglLockGuard lock;
    if (_view == nullptr || !_view->ready()) {
        return "unavailable";
    }
    if (_view->micActive()) {
        return "mic";
    }
    return _view->currentPage() == view::CodexMicroView::Page::Command ? "command" : "agent";
}

void AppCodexMicro::debugSetInputCapture(bool enabled)
{
    _debug_input_capture = enabled;
    LvglLockGuard lock;
    if (_view != nullptr) {
        _view->setInputSuppressed(enabled);
    }
}

AppCodexMicro::DebugInputState AppCodexMicro::debugInputState()
{
    DebugInputState state = {
        .buttonA = GetHAL().btnA.isPressed(),
        .buttonB = GetHAL().btnB.isPressed(),
    };
    LvglLockGuard lock;
    if (GetHAL().lvTouchpad != nullptr) {
        state.touch = lv_indev_get_state(GetHAL().lvTouchpad) == LV_INDEV_STATE_PRESSED;
        if (state.touch) {
            lv_point_t point;
            lv_indev_get_point(GetHAL().lvTouchpad, &point);
            state.x = static_cast<int16_t>(point.x);
            state.y = static_cast<int16_t>(point.y);
        }
    }
    return state;
}
