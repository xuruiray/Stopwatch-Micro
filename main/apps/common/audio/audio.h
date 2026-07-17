/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
namespace audio {

void play_tone(int frequency, float durationSec = 0.02f, float volumeScale = 0.5f);
void play_tone_from_midi(int midi, float durationSec = 0.02f, float volumeScale = 0.5f);

}  // namespace audio
