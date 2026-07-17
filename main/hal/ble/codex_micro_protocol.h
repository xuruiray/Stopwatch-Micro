/*
 * SPDX-License-Identifier: MIT
 *
 * Codex Micro compatibility protocol adapted from imliubo/codex-micro-4-core2.
 * Copyright (c) 2026 imliubo.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

enum class CodexMicroControl : uint8_t {
    Agent1,
    Agent2,
    Agent3,
    Agent4,
    Agent5,
    Agent6,
    Fast,
    Approve,
    Decline,
    NewChat,
    Mic,
    Send,
    EncoderPress,
    EncoderClockwise,
    EncoderCounterClockwise,
};

enum class CodexMicroKeyAction : uint8_t {
    Release = 0,
    Press   = 1,
    Rotate  = 2,
};

enum class CodexMicroLightEffect : uint8_t {
    Off           = 0,
    Solid         = 1,
    Snake         = 2,
    Rainbow       = 3,
    Breath        = 4,
    Gradient      = 5,
    ShallowBreath = 6,
};

inline constexpr std::array<const char*, 15> CodexMicroControlCodes = {
    "AG00",  "AG01",  "AG02",  "AG03",  "AG04", "AG05",   "ACT06",  "ACT07",
    "ACT08", "ACT09", "ACT10", "ACT12", "ENC",  "ENC_CW", "ENC_CC",
};

static_assert(CodexMicroControlCodes.size() ==
              static_cast<std::size_t>(CodexMicroControl::EncoderCounterClockwise) + 1);

constexpr const char* codexMicroControlCode(CodexMicroControl control)
{
    return CodexMicroControlCodes[static_cast<std::size_t>(control)];
}

static_assert(std::string_view(codexMicroControlCode(CodexMicroControl::Agent1)) == "AG00");
static_assert(std::string_view(codexMicroControlCode(CodexMicroControl::Mic)) == "ACT10");
static_assert(std::string_view(codexMicroControlCode(CodexMicroControl::Send)) == "ACT12");
static_assert(std::string_view(codexMicroControlCode(CodexMicroControl::EncoderPress)) == "ENC");
