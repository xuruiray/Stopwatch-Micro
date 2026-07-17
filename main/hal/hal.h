/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "utils/button/Button_Class.hpp"
#include <system_config.h>
#include <memory>
#include <cstdint>
#include <string>
#include <lvgl.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <i2c_bus.h>
#include <string_view>
#include <array>
#include <vector>
/** Startup screen shown while the system UI is being created. */
class BootLogo {
public:
    BootLogo()
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setSize(466, 466);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setBorderWidth(0);
        _panel->setBgOpa(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_black());

        _label_logo = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _label_logo->setTextFont(&lv_font_montserrat_28);
        _label_logo->setTextColor(lv_color_hex(0xFFFFFF));
        _label_logo->align(LV_ALIGN_CENTER, 0, -14);
        _label_logo->setText(system_config::ProductName);

        _label_msg = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _label_msg->setTextFont(&lv_font_montserrat_16);
        _label_msg->setTextColor(lv_color_hex(0xBFBFBF));
        _label_msg->align(LV_ALIGN_CENTER, 0, 14);
        _label_msg->setText("Starting up ...");

        _label_version = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _label_version->setTextFont(&lv_font_montserrat_14);
        _label_version->setTextColor(lv_color_hex(0x8B8B8B));
        _label_version->align(LV_ALIGN_BOTTOM_MID, 0, -12);
        _label_version->setText(system_config::FirmwareVersion);
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_logo;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_msg;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_version;
};

/** Board services required by the Stopwatch Micro system firmware. */
class Hal {
public:
    void init();

    /* --------------------------------- System --------------------------------- */
    void delay(std::uint32_t ms);
    std::uint32_t millis();
    void feedTheDog();
    std::array<uint8_t, 6> getFactoryMac();
    std::string getFactoryMacString(std::string divider = "");
    void reboot();
    void factoryReset();

    /* ---------------------------------- Power --------------------------------- */
    uint8_t getBatteryLevel();
    bool isBatteryCharging(bool strict = false);

    /* --------------------------------- Display -------------------------------- */
    void setBackLightBrightness(int brightness, bool saveToSettings = false);
    int getBackLightBrightness(bool loadFromSettings = false);

    // Lvgl
    lv_indev_t* lvTouchpad = nullptr;
    std::unique_ptr<BootLogo> bootLogo;
    bool lvglLock();
    void lvglUnlock();
    void startLvglUpdate();
    void stopLvglUpdate();

    /* ---------------------------------- Touch --------------------------------- */
    struct TouchPoint {
        int num = 0;
        int x   = -1;
        int y   = -1;
    };
    TouchPoint getTouchPoint();

    /* ---------------------------------- Audio --------------------------------- */
    void setSpeakerVolume(int volume, bool saveToSettings = false);
    int getSpeakerVolume(bool loadFromSettings = false);
    int getAudioSampleRate();
    void audioPlay(std::vector<int16_t>& data, bool async = true);

    /* ----------------------------- Vibrator Motor ----------------------------- */
    void vibrate(uint16_t durationMs, uint8_t strength = 100);
    void stopVibrate();

    /* --------------------------------- Button --------------------------------- */
    m5::Button_Class btnA;
    m5::Button_Class btnB;
    m5::Button_Class btnPwr;

    struct ButtonConfig {
        bool sfxEnabled     = true;
        bool vibrateEnabled = true;
    };

    void updateButtonStates();
    void setButtonConfig(ButtonConfig config, bool saveToSettings = false);
    const ButtonConfig& getButtonConfig(bool loadFromSettings = false);

private:
    static constexpr std::string_view SettingsNs = "system";

    i2c_bus_handle_t _i2c_bus = nullptr;
    ButtonConfig _btn_config;
    int _bl_brightness = 80;
    int _spk_volume    = 80;

    void i2c_init();
    void i2c_detect();
    void pmic_init();
    bool pmic_get_pwr_btn_state();
    void ioe_init();
    void ioe_tp_reset();
    void ioe_speaker_enable(bool enable);
    void display_init();
    void touchpad_init();
    void lvgl_init();
    void audio_init();
    void button_init();
};

Hal& GetHAL();

/** Scoped serialization for LVGL access from the Mooncake task. */
class LvglLockGuard {
public:
    LvglLockGuard() : _locked(GetHAL().lvglLock())
    {
    }
    ~LvglLockGuard()
    {
        if (_locked) {
            GetHAL().lvglUnlock();
        }
    }

    LvglLockGuard(const LvglLockGuard&)            = delete;
    LvglLockGuard& operator=(const LvglLockGuard&) = delete;

private:
    bool _locked;
};
