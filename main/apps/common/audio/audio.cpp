/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <hal/hal.h>

namespace audio {

static float get_effective_volume_scale(float volumeScale)
{
    return std::max(0.0f, volumeScale);
}

void play_tone(int frequency, float durationSec, float volumeScale)
{
    if (GetHAL().getSpeakerVolume() <= 0) {
        return;
    }

    const int sample_rate = GetHAL().getAudioSampleRate();
    const int samples     = static_cast<int>(sample_rate * durationSec);
    std::vector<int16_t> buffer(samples);

    const int fade_len        = 200;
    const float amplitude     = 32767.0f / 5;
    const float scaled_volume = get_effective_volume_scale(volumeScale);

    // Optimization: Use float (sinf) and incremental phase to avoid double precision math and division inside loop
    const float angle_step = 2.0f * (float)M_PI * frequency / sample_rate;
    float current_angle    = 0.0f;

    for (int i = 0; i < samples; ++i) {
        float amp = amplitude * scaled_volume;

        if (i >= samples - fade_len) {
            float fade_factor = static_cast<float>(samples - i) / fade_len;
            amp *= fade_factor;
        }

        // Use sinf instead of sin
        int16_t value = static_cast<int16_t>(amp * sinf(current_angle));
        buffer[i]     = value;

        current_angle += angle_step;
        // Keep angle within reasonable bounds to preserve precision (though for short clips it matters less)
        if (current_angle > 2.0f * (float)M_PI) {
            current_angle -= 2.0f * (float)M_PI;
        }
    }

    GetHAL().audioPlay(buffer);
}

void play_tone_from_midi(int midi, float durationSec, float volumeScale)
{
    // Optimization: Use float math
    float freq = 440.0f * powf(2.0f, (midi - 69) / 12.0f);
    play_tone(static_cast<int>(freq), durationSec, volumeScale);
}

}  // namespace audio
