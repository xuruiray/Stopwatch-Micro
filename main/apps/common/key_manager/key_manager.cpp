/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "key_manager.h"

using namespace input;

KeyEvent KeyManager::update(bool updateButtonStates)
{
    if (updateButtonStates) {
        GetHAL().updateButtonStates();
    }

    _event = KeyEvent::None;

    Hal& hal           = GetHAL();
    const uint32_t now = hal.millis();

    // The chord owns both keys. If it arrives after a single-key action has
    // started, release that action before changing pages.
    if (hal.btnA.isPressed() && hal.btnB.isPressed()) {
        _pending_key = PendingKey::None;
        if (_mic_active) {
            _event |= KeyEvent::MicRelease;
            _mic_active = false;
        }
        if (_send_active) {
            _event |= KeyEvent::SendRelease;
            _send_active = false;
        }
        if (!_combo_latched) {
            _event |= KeyEvent::TogglePage;
            _combo_latched = true;
        }
        return _event;
    }

    // Once a chord has fired, ignore the remaining held key until both are up.
    if (_combo_latched) {
        if (hal.btnA.isReleased() && hal.btnB.isReleased()) {
            _combo_latched = false;
        }
        return _event;
    }

    if (_pending_key == PendingKey::None) {
        if (hal.btnA.wasPressed()) {
            _pending_key   = PendingKey::Mic;
            _pending_since = now;
        } else if (hal.btnB.wasPressed()) {
            _pending_key   = PendingKey::Send;
            _pending_since = now;
        }
    }

    if (_pending_key != PendingKey::None) {
        const bool is_mic  = _pending_key == PendingKey::Mic;
        const bool pressed = is_mic ? hal.btnA.isPressed() : hal.btnB.isPressed();
        if (!pressed) {
            // Preserve a very short tap that ended inside the chord window.
            _event |=
                is_mic ? (KeyEvent::MicPress | KeyEvent::MicRelease) : (KeyEvent::SendPress | KeyEvent::SendRelease);
            _pending_key = PendingKey::None;
        } else if (now - _pending_since >= ChordWindowMs) {
            if (is_mic) {
                _event |= KeyEvent::MicPress;
                _mic_active = true;
            } else {
                _event |= KeyEvent::SendPress;
                _send_active = true;
            }
            _pending_key = PendingKey::None;
        }
    }

    if (_mic_active && hal.btnA.wasReleased()) {
        _event |= KeyEvent::MicRelease;
        _mic_active = false;
    }
    if (_send_active && hal.btnB.wasReleased()) {
        _event |= KeyEvent::SendRelease;
        _send_active = false;
    }

    return _event;
}
