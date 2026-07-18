/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mooncake_log.h>
#include <mutex>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view Tag     = "HAL-Audio";
constexpr i2s_port_t I2sPort       = I2S_NUM_0;
constexpr gpio_num_t I2sMclk       = GPIO_NUM_18;
constexpr gpio_num_t I2sBclk       = GPIO_NUM_17;
constexpr gpio_num_t I2sLrck       = GPIO_NUM_15;
constexpr gpio_num_t I2sDin        = GPIO_NUM_16;
constexpr gpio_num_t I2sDout       = GPIO_NUM_21;
constexpr BaseType_t AudioTaskCore = 0;

class AudioCodec {
public:
    static constexpr int SampleRate = 44100;

    void init(i2c_master_bus_handle_t i2c_bus)
    {
        _silence_buffer.assign(SampleRate / 10, 0);
        initI2s();

        audio_codec_i2s_cfg_t i2s_config = {};
        i2s_config.rx_handle             = _rx_handle;
        i2s_config.tx_handle             = _tx_handle;
        _data_interface                  = audio_codec_new_i2s_data(&i2s_config);

        audio_codec_i2c_cfg_t i2c_config = {};
        i2c_config.addr                  = ES8311_CODEC_DEFAULT_ADDR;
        i2c_config.bus_handle            = i2c_bus;
        _control_interface               = audio_codec_new_i2c_ctrl(&i2c_config);
        _gpio_interface                  = audio_codec_new_gpio();

        es8311_codec_cfg_t codec_config = {};
        codec_config.ctrl_if            = _control_interface;
        codec_config.gpio_if            = _gpio_interface;
        codec_config.codec_mode         = ESP_CODEC_DEV_WORK_MODE_BOTH;
        codec_config.pa_pin             = GPIO_NUM_NC;
        codec_config.use_mclk           = true;
        _codec_interface                = es8311_codec_new(&codec_config);

        esp_codec_dev_cfg_t device_config = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
            .codec_if = _codec_interface,
            .data_if  = _data_interface,
        };
        _device = esp_codec_dev_new(&device_config);

        esp_codec_dev_sample_info_t sample_info = {};
        sample_info.bits_per_sample             = 16;
        sample_info.channel                     = 1;
        sample_info.sample_rate                 = SampleRate;
        ESP_ERROR_CHECK(esp_codec_dev_open(_device, &sample_info));
        ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(_device, MicrophoneGainDb));

        if (xTaskCreatePinnedToCore(taskEntry, "audio_task", 4 * 1024, this, 5, &_task_handle, AudioTaskCore) !=
            pdPASS) {
            ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
        }
        if (xTaskCreatePinnedToCore(microphoneTaskEntry, "mic_meter_task", 4 * 1024, this, 4, &_microphone_task_handle,
                                    AudioTaskCore) != pdPASS) {
            ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
        }
    }

    void setVolume(int volume)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(_device, volume));
    }

    int getVolume()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        int volume = 0;
        ESP_ERROR_CHECK(esp_codec_dev_get_out_vol(_device, &volume));
        return volume;
    }

    void play(const std::vector<int16_t>& data, bool async)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!async) {
            if (_is_playing) {
                mclog::tagWarn(Tag, "audio is already playing");
                return;
            }
            write(data);
            return;
        }

        _audio_data = data;
        _is_playing = true;
        xTaskNotifyGive(_task_handle);
    }

    void setMicrophoneMeterEnabled(bool enabled)
    {
        if (_microphone_meter_enabled.exchange(enabled) == enabled) {
            return;
        }

        _microphone_level.store(0);
        if (enabled) {
            _microphone_peak.store(0);
            mclog::tagInfo(Tag, "microphone meter enabled");
            xTaskNotifyGive(_microphone_task_handle);
        } else {
            mclog::tagInfo(Tag, "microphone meter disabled, peak={}%%", _microphone_peak.load() / 10);
        }
    }

    float getMicrophoneLevel() const
    {
        return static_cast<float>(_microphone_level.load()) / 1000.0f;
    }

    bool ready() const
    {
        return _device != nullptr && _rx_handle != nullptr && _tx_handle != nullptr && _task_handle != nullptr &&
               _microphone_task_handle != nullptr;
    }

    bool microphoneMeterEnabled() const
    {
        return _microphone_meter_enabled.load();
    }

private:
    static constexpr float MicrophoneGainDb              = 30.0f;
    static constexpr float MicrophoneNoiseFloorDb        = -56.0f;
    static constexpr float MicrophoneLoudDb              = -18.0f;
    static constexpr std::size_t MicrophoneWindowSamples = 512;

    static void taskEntry(void* context)
    {
        static_cast<AudioCodec*>(context)->run();
    }

    static void microphoneTaskEntry(void* context)
    {
        static_cast<AudioCodec*>(context)->runMicrophoneMeter();
    }

    void run()
    {
        mclog::tagInfo(Tag, "start audio play task");
        std::vector<int16_t> current_data;

        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            while (true) {
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (_audio_data.empty()) {
                        _is_playing = false;
                        break;
                    }
                    current_data = std::move(_audio_data);
                    _audio_data.clear();
                }

                bool interrupted                   = false;
                std::size_t offset                 = 0;
                constexpr std::size_t ChunkSamples = 512;
                while (offset < current_data.size()) {
                    if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
                        interrupted = true;
                        break;
                    }

                    const std::size_t remaining = current_data.size() - offset;
                    const std::size_t count     = std::min(remaining, ChunkSamples);
                    ESP_ERROR_CHECK(
                        esp_codec_dev_write(_device, current_data.data() + offset, count * sizeof(int16_t)));
                    offset += count;
                }

                if (interrupted) {
                    ESP_ERROR_CHECK(i2s_channel_disable(_tx_handle));
                    ESP_ERROR_CHECK(i2s_channel_enable(_tx_handle));
                    continue;
                }
                writeSilence();
            }
        }
    }

    void runMicrophoneMeter()
    {
        std::array<int16_t, MicrophoneWindowSamples> samples = {};

        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            while (_microphone_meter_enabled.load()) {
                const int result = esp_codec_dev_read(_device, samples.data(), samples.size() * sizeof(int16_t));
                if (result != ESP_CODEC_DEV_OK) {
                    mclog::tagWarn(Tag, "microphone read failed: {}", result);
                    _microphone_level.store(0);
                    vTaskDelay(pdMS_TO_TICKS(20));
                    continue;
                }
                if (!_microphone_meter_enabled.load()) {
                    break;
                }

                int64_t sum = 0;
                for (const int16_t sample : samples) {
                    sum += sample;
                }
                const double mean  = static_cast<double>(sum) / static_cast<double>(samples.size());
                double squared_sum = 0.0;
                for (const int16_t sample : samples) {
                    const double centered = static_cast<double>(sample) - mean;
                    squared_sum += centered * centered;
                }

                const double rms      = std::sqrt(squared_sum / static_cast<double>(samples.size()));
                const float amplitude = static_cast<float>(rms / 32768.0);
                const float dbfs      = 20.0f * std::log10(std::max(amplitude, 0.000001f));
                const float level     = std::clamp(
                    (dbfs - MicrophoneNoiseFloorDb) / (MicrophoneLoudDb - MicrophoneNoiseFloorDb), 0.0f, 1.0f);
                const uint16_t scaled_level = static_cast<uint16_t>(std::lround(level * 1000.0f));
                _microphone_level.store(scaled_level);

                uint16_t peak = _microphone_peak.load();
                while (scaled_level > peak && !_microphone_peak.compare_exchange_weak(peak, scaled_level)) {
                }
            }

            _microphone_level.store(0);
        }
    }

    void write(const std::vector<int16_t>& data)
    {
        ESP_ERROR_CHECK(esp_codec_dev_write(_device, const_cast<int16_t*>(data.data()), data.size() * sizeof(int16_t)));
        writeSilence();
    }

    void writeSilence()
    {
        ESP_ERROR_CHECK(esp_codec_dev_write(_device, _silence_buffer.data(), _silence_buffer.size() * sizeof(int16_t)));
    }

    void initI2s()
    {
        mclog::tagInfo(Tag, "i2s init");
        i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2sPort, I2S_ROLE_MASTER);
        i2s_std_config_t standard_config = {};
        standard_config.clk_cfg          = I2S_STD_CLK_DEFAULT_CONFIG(SampleRate);
        standard_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        standard_config.gpio_cfg.mclk = I2sMclk;
        standard_config.gpio_cfg.bclk = I2sBclk;
        standard_config.gpio_cfg.ws   = I2sLrck;
        standard_config.gpio_cfg.dout = I2sDout;
        standard_config.gpio_cfg.din  = I2sDin;

        ESP_ERROR_CHECK(i2s_new_channel(&channel_config, &_tx_handle, &_rx_handle));
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(_tx_handle, &standard_config));
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(_rx_handle, &standard_config));
        ESP_ERROR_CHECK(i2s_channel_enable(_tx_handle));
        ESP_ERROR_CHECK(i2s_channel_enable(_rx_handle));
    }

    i2s_chan_handle_t _tx_handle                    = nullptr;
    i2s_chan_handle_t _rx_handle                    = nullptr;
    esp_codec_dev_handle_t _device                  = nullptr;
    const audio_codec_data_if_t* _data_interface    = nullptr;
    const audio_codec_ctrl_if_t* _control_interface = nullptr;
    const audio_codec_gpio_if_t* _gpio_interface    = nullptr;
    const audio_codec_if_t* _codec_interface        = nullptr;
    TaskHandle_t _task_handle                       = nullptr;
    TaskHandle_t _microphone_task_handle            = nullptr;
    std::mutex _mutex;
    std::vector<int16_t> _audio_data;
    std::vector<int16_t> _silence_buffer;
    std::atomic<bool> _microphone_meter_enabled = false;
    std::atomic<uint16_t> _microphone_level     = 0;
    std::atomic<uint16_t> _microphone_peak      = 0;
    bool _is_playing                            = false;
};

AudioCodec Audio;

}  // namespace

void Hal::audio_init()
{
    mclog::tagInfo(Tag, "init");
    Audio.init(i2c_bus_get_internal_bus_handle(_i2c_bus));
    ioe_speaker_enable(true);
    setSpeakerVolume(getSpeakerVolume(true), false);
}

void Hal::setSpeakerVolume(int volume, bool saveToSettings)
{
    _spk_volume = uitk::clamp(volume, 0, 100);
    mclog::tagInfo(Tag, "set speaker volume to {}", _spk_volume);
    Audio.setVolume(_spk_volume);

    if (saveToSettings) {
        Settings settings(std::string(SettingsNs), true);
        settings.SetInt("spk_vol", _spk_volume);
        mclog::tagInfo(Tag, "volume saved to settings: {}", _spk_volume);
    }
}

int Hal::getSpeakerVolume(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(SettingsNs), false);
        _spk_volume = uitk::clamp(settings.GetInt("spk_vol", 80), 0, 100);
        mclog::tagInfo(Tag, "volume loaded from settings: {}", _spk_volume);
    }
    return _spk_volume;
}

void Hal::audioPlay(std::vector<int16_t>& data, bool async)
{
    Audio.play(data, async);
}

void Hal::setMicrophoneMeterEnabled(bool enabled)
{
    Audio.setMicrophoneMeterEnabled(enabled);
}

bool Hal::isMicrophoneMeterEnabled()
{
    return Audio.microphoneMeterEnabled();
}

float Hal::getMicrophoneLevel()
{
    return Audio.getMicrophoneLevel();
}

int Hal::getAudioSampleRate()
{
    return AudioCodec::SampleRate;
}

bool Hal::audio_ready() const
{
    return Audio.ready();
}
