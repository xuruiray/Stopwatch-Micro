/*
 * SPDX-License-Identifier: MIT
 */
#include "app_codex_micro.h"

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
    _key_manager = std::move(key_manager);
    _last_ui_update_ms = 0;
    _mic_host_active = false;
    _send_host_active = false;

    LvglLockGuard lock;
    GetHAL().bootLogo.reset();
    _view = std::move(app_view);
    _view->init(lv_screen_active());
}

void AppCodexMicro::onRunning()
{
    GetHAL().updateButtonStates();
    const input::KeyEvent event = _key_manager == nullptr ? input::KeyEvent::None : _key_manager->update(false);
    const uint32_t now = GetHAL().millis();
    const bool refresh_due = now - _last_ui_update_ms >= UiRefreshPeriodMs;
    if (event == input::KeyEvent::None && !refresh_due) {
        return;
    }
    const CodexMicroState state = GetCodexMicroBle().snapshot();

    LvglLockGuard lock;
    if (_view == nullptr) {
        return;
    }
    _view->update(state);

    if (!state.connected) {
        _mic_host_active = false;
        _send_host_active = false;
    }

    if (input::hasKeyEvent(event, input::KeyEvent::MicPress) && state.connected) {
        _mic_host_active = GetCodexMicroBle().sendKey(
            CodexMicroControl::Mic, CodexMicroKeyAction::Press);
        _view->setMicActive(_mic_host_active);
    }
    if (input::hasKeyEvent(event, input::KeyEvent::SendPress) && state.connected) {
        _send_host_active = GetCodexMicroBle().sendKey(
            CodexMicroControl::Send, CodexMicroKeyAction::Press);
    }
    if (input::hasKeyEvent(event, input::KeyEvent::MicRelease)) {
        if (_mic_host_active && state.connected) {
            GetCodexMicroBle().sendKey(CodexMicroControl::Mic, CodexMicroKeyAction::Release);
        }
        _mic_host_active = false;
        _view->setMicActive(false);
    }
    if (input::hasKeyEvent(event, input::KeyEvent::SendRelease)) {
        if (_send_host_active && state.connected) {
            GetCodexMicroBle().sendKey(CodexMicroControl::Send, CodexMicroKeyAction::Release);
        }
        _send_host_active = false;
    }
    if (input::hasKeyEvent(event, input::KeyEvent::TogglePage) && state.connected) {
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
    GetHAL().stopVibrate();
    std::vector<int16_t> empty_audio;
    GetHAL().audioPlay(empty_audio, true);

    LvglLockGuard lock;
    _view.reset();
}
