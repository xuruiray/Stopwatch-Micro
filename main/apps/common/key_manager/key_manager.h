/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <hal/hal.h>

namespace input {

enum class KeyEvent : uint8_t {
    None        = 0,
    MicPress    = 1U << 0,
    MicRelease  = 1U << 1,
    SendPress   = 1U << 2,
    SendRelease = 1U << 3,
    TogglePage  = 1U << 4,
};

constexpr KeyEvent operator|(KeyEvent lhs, KeyEvent rhs)
{
    return static_cast<KeyEvent>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

inline KeyEvent& operator|=(KeyEvent& lhs, KeyEvent rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasKeyEvent(KeyEvent value, KeyEvent flag)
{
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
}

class KeyManager {
public:
    KeyEvent update(bool updateButtonStates = true);
    KeyEvent getEvent() const
    {
        return _event;
    }

private:
    enum class PendingKey : uint8_t {
        None,
        Mic,
        Send,
    };

    static constexpr uint32_t ChordWindowMs = 50;

    KeyEvent _event         = KeyEvent::None;
    PendingKey _pending_key = PendingKey::None;
    uint32_t _pending_since = 0;
    bool _mic_active        = false;
    bool _send_active       = false;
    bool _combo_latched     = false;
};

}  // namespace input
