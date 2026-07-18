/*
 * SPDX-License-Identifier: MIT
 */
#include "serial_debug.h"

#include <apps/app_codex_micro/app_codex_micro.h>
#include <apps/common/audio/audio.h>
#include <hal/ble/codex_micro_ble.h>
#include <hal/hal.h>
#include <system_config.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <sdkconfig.h>

namespace {

constexpr uint32_t DefaultMicTestMs         = 2000;
constexpr uint32_t DefaultInputTestMs       = 15000;
constexpr uint32_t DefaultPerformanceTestMs = 3000;
constexpr uint32_t PerformanceSendPeriodMs  = 20;
constexpr uint32_t UiCycleStepMs            = 300;
constexpr uint32_t TransportWaitMs          = 500;
constexpr float MicrophoneSignalFloor       = 0.02f;

bool elapsed(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

const char* onOff(bool value)
{
    return value ? "1" : "0";
}

}  // namespace

SerialDebug::SerialDebug(AppCodexMicro& app) : _app(app)
{
}

bool SerialDebug::begin()
{
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    result("init", "FAIL", "reason=usb_serial_jtag_not_primary");
    return false;
#else
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {
            .tx_buffer_size = 2048,
            .rx_buffer_size = 2048,
        };
        const esp_err_t error = usb_serial_jtag_driver_install(&config);
        if (error != ESP_OK) {
            result("init", "FAIL", "reason=usb_serial_jtag_driver_install");
            return false;
        }
    }
    // The default VFS path polls hardware registers and proved unreliable for
    // host-to-device traffic while BLE/LVGL were active. The official
    // interrupt-driven driver buffers RX independently of the UI loop.
    usb_serial_jtag_vfs_use_driver();
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        result("init", "FAIL", "reason=nonblocking_stdin");
        return false;
    }
    _active = true;
    std::printf("DBG READY version=%s transport=usb-serial-jtag mode=nonblocking\r\n", system_config::FirmwareVersion);
    std::fflush(stdout);
    return true;
#endif
}

void SerialDebug::end()
{
    if (!_active) {
        return;
    }
    cancelAsyncTest("shutdown", false);
    _active = false;
}

void SerialDebug::poll()
{
    if (!_active) {
        return;
    }

    std::array<char, 64> bytes = {};
    while (true) {
        const ssize_t count = read(STDIN_FILENO, bytes.data(), bytes.size());
        if (count > 0) {
            for (ssize_t index = 0; index < count; ++index) {
                consume(bytes[static_cast<std::size_t>(index)]);
            }
            continue;
        }
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            result("read", "FAIL", "reason=stdin_error");
        }
        break;
    }
    updateAsyncTest();
}

void SerialDebug::consume(char value)
{
    if (value == '\r' || value == '\n') {
        if (_line_length == 0 && !_line_overflow) {
            return;
        }
        if (_line_overflow) {
            result("parse", "FAIL", "reason=line_too_long");
        } else {
            _line[_line_length] = '\0';
            handleLine(_line.data());
        }
        _line_length   = 0;
        _line_overflow = false;
        return;
    }
    if (value == '\b' || value == 0x7F) {
        if (_line_length > 0) {
            --_line_length;
        }
        return;
    }
    if (value < 0x20 || value > 0x7E) {
        return;
    }
    if (_line_length + 1 >= _line.size()) {
        _line_overflow = true;
        return;
    }
    _line[_line_length++] = value;
}

void SerialDebug::handleLine(char* line)
{
    char* save = nullptr;
    char* root = ::strtok_r(line, " \t", &save);
    if (root == nullptr) {
        return;
    }
    if (std::strcmp(root, "debug") != 0 && std::strcmp(root, "dbg") != 0) {
        result("parse", "FAIL", "reason=expected_debug_prefix");
        return;
    }

    char* command = ::strtok_r(nullptr, " \t", &save);
    if (command == nullptr || std::strcmp(command, "help") == 0) {
        printHelp();
        return;
    }
    if (std::strcmp(command, "ping") == 0) {
        result("ping", "PASS", "reply=pong");
        return;
    }
    if (std::strcmp(command, "status") == 0) {
        printStatus();
        return;
    }
    if (std::strcmp(command, "selftest") == 0) {
        runSelfTest();
        return;
    }
    if (std::strcmp(command, "controls") == 0) {
        printControls();
        return;
    }
    if (std::strcmp(command, "protocol") == 0) {
        const CodexMicroBleDiagnostics diagnostics = GetCodexMicroBle().diagnostics();
        const bool healthy = GetCodexMicroBle().protocolSelfTest() && diagnostics.inputDropped == 0 &&
                             diagnostics.txFailures == 0 && diagnostics.rpcErrors == 0;
        char details[144]  = {};
        std::snprintf(details, sizeof(details),
                      "controls=12 encoder=3 report_id=6 report_bytes=63 payload_bytes=61 dropped=%lu tx_failures=%lu "
                      "rpc_errors=%lu",
                      static_cast<unsigned long>(diagnostics.inputDropped),
                      static_cast<unsigned long>(diagnostics.txFailures),
                      static_cast<unsigned long>(diagnostics.rpcErrors));
        result("protocol", healthy ? "PASS" : "FAIL", details);
        return;
    }
    if (std::strcmp(command, "mic") == 0) {
        char* duration = ::strtok_r(nullptr, " \t", &save);
        startMicrophoneTest(parseUnsigned(duration, DefaultMicTestMs, 500, 30000));
        return;
    }
    if (std::strcmp(command, "inputs") == 0) {
        char* duration = ::strtok_r(nullptr, " \t", &save);
        startInputTest(parseUnsigned(duration, DefaultInputTestMs, 1000, 60000));
        return;
    }
    if (std::strcmp(command, "ui") == 0) {
        char* screen = ::strtok_r(nullptr, " \t", &save);
        if (screen == nullptr || std::strcmp(screen, "cycle") == 0) {
            startUiCycle();
            return;
        }
        AppCodexMicro::DebugScreen target;
        if (std::strcmp(screen, "command") == 0) {
            target = AppCodexMicro::DebugScreen::Command;
        } else if (std::strcmp(screen, "agent") == 0) {
            target = AppCodexMicro::DebugScreen::Agent;
        } else if (std::strcmp(screen, "mic") == 0) {
            target = AppCodexMicro::DebugScreen::Mic;
        } else {
            result("ui", "FAIL", "reason=invalid_screen");
            return;
        }
        const bool changed = _app.debugSetScreen(target);
        char details[64]   = {};
        std::snprintf(details, sizeof(details), "screen=%s actual=%s", screen, _app.debugScreenName());
        result("ui", changed && std::strcmp(screen, _app.debugScreenName()) == 0 ? "PASS" : "FAIL", details);
        return;
    }
    if (std::strcmp(command, "transport") == 0) {
        startTransportTest();
        return;
    }
    if (std::strcmp(command, "perf") == 0) {
        char* duration = ::strtok_r(nullptr, " \t", &save);
        startPerformanceTest(parseUnsigned(duration, DefaultPerformanceTestMs, 1000, 15000), true);
        return;
    }
    if (std::strcmp(command, "trace") == 0) {
        char* duration = ::strtok_r(nullptr, " \t", &save);
        startPerformanceTest(parseUnsigned(duration, DefaultPerformanceTestMs, 1000, 60000), false);
        return;
    }
    if (std::strcmp(command, "tone") == 0) {
        char* frequency   = ::strtok_r(nullptr, " \t", &save);
        char* duration    = ::strtok_r(nullptr, " \t", &save);
        const uint32_t hz = parseUnsigned(frequency, 880, 100, 4000);
        const uint32_t ms = parseUnsigned(duration, 250, 20, 1000);
        audio::play_tone(static_cast<int>(hz), static_cast<float>(ms) / 1000.0f, 0.6f);
        char details[64] = {};
        std::snprintf(details, sizeof(details), "hz=%lu duration_ms=%lu verify=audible", static_cast<unsigned long>(hz),
                      static_cast<unsigned long>(ms));
        result("tone", "OBSERVE", details);
        return;
    }
    if (std::strcmp(command, "vibrate") == 0) {
        char* duration        = ::strtok_r(nullptr, " \t", &save);
        char* strength        = ::strtok_r(nullptr, " \t", &save);
        const uint32_t ms     = parseUnsigned(duration, 300, 20, 3000);
        const uint32_t amount = parseUnsigned(strength, 80, 1, 100);
        GetHAL().vibrate(static_cast<uint16_t>(ms), static_cast<uint8_t>(amount));
        char details[80] = {};
        std::snprintf(details, sizeof(details), "duration_ms=%lu strength=%lu verify=physical",
                      static_cast<unsigned long>(ms), static_cast<unsigned long>(amount));
        result("vibrate", "OBSERVE", details);
        return;
    }
    if (std::strcmp(command, "backlight") == 0) {
        char* level               = ::strtok_r(nullptr, " \t", &save);
        const uint32_t brightness = parseUnsigned(level, 80, 10, 100);
        GetHAL().setBackLightBrightness(static_cast<int>(brightness), false);
        char details[64] = {};
        std::snprintf(details, sizeof(details), "brightness=%lu verify=visible",
                      static_cast<unsigned long>(brightness));
        result("backlight", "OBSERVE", details);
        return;
    }
    if (std::strcmp(command, "cancel") == 0) {
        cancelAsyncTest("requested", true);
        return;
    }
    if (std::strcmp(command, "pairing-reset") == 0) {
        char* confirmation = ::strtok_r(nullptr, " \t", &save);
        if (confirmation == nullptr || std::strcmp(confirmation, "CONFIRM") != 0) {
            result("pairing-reset", "SKIP", "reason=requires_CONFIRM warning=erases_bonds_and_restarts");
            return;
        }
        result("pairing-reset", GetCodexMicroBle().resetPairing() ? "PASS" : "FAIL", "action=erase_bonds_and_restart");
        return;
    }
    result(command, "FAIL", "reason=unknown_command");
}

void SerialDebug::printHelp()
{
    std::printf("DBG HELP commands=ping,status,selftest,controls,protocol\r\n");
    std::printf(
        "DBG HELP commands=ui_[command|agent|mic|cycle],transport,perf_[ms],trace_[ms],mic_[ms],inputs_[ms]\r\n");
    std::printf("DBG HELP commands=tone_[hz]_[ms],vibrate_[ms]_[strength],backlight_[10-100],cancel\r\n");
    std::printf("DBG HELP destructive=pairing-reset_CONFIRM\r\n");
    std::fflush(stdout);
    result("help", "PASS");
}

void SerialDebug::printStatus()
{
    const Hal::Diagnostics hal         = GetHAL().diagnostics();
    const CodexMicroState state        = GetCodexMicroBle().snapshot();
    const CodexMicroBleDiagnostics ble = GetCodexMicroBle().diagnostics();
    std::printf("DBG STATUS firmware=%s battery=%u charging=%s ui=%s heap_free=%lu heap_min=%lu\r\n",
                system_config::FirmwareVersion, static_cast<unsigned>(GetHAL().getBatteryLevel()),
                onOff(GetHAL().isBatteryCharging()), _app.debugScreenName(),
                static_cast<unsigned long>(esp_get_free_heap_size()),
                static_cast<unsigned long>(esp_get_minimum_free_heap_size()));
    std::printf(
        "DBG STATUS hal_i2c=%s hal_pmic=%s hal_ioe=%s hal_display=%s hal_touch=%s hal_audio=%s hal_vibrator=%s "
        "hal_buttons=%s\r\n",
        onOff(hal.i2c), onOff(hal.pmic), onOff(hal.ioExpander), onOff(hal.display), onOff(hal.touch), onOff(hal.audio),
        onOff(hal.vibrator), onOff(hal.buttons));
    std::printf(
        "DBG STATUS ble_ready=%s ble_connected=%s advertising=%s revision=%lu queued=%lu dropped=%lu processed=%lu "
        "tx_messages=%lu tx_reports=%lu tx_failures=%lu rx_reports=%lu rpc=%lu rpc_errors=%lu pending=%lu\r\n",
        onOff(state.ready && ble.hidReady), onOff(state.connected), onOff(ble.advertising),
        static_cast<unsigned long>(state.revision), static_cast<unsigned long>(ble.inputQueued),
        static_cast<unsigned long>(ble.inputDropped), static_cast<unsigned long>(ble.inputProcessed),
        static_cast<unsigned long>(ble.txMessages), static_cast<unsigned long>(ble.txReports),
        static_cast<unsigned long>(ble.txFailures), static_cast<unsigned long>(ble.rxReports),
        static_cast<unsigned long>(ble.rpcMessages), static_cast<unsigned long>(ble.rpcErrors),
        static_cast<unsigned long>(ble.queuePending));
    const Hal::PerformanceDiagnostics performance = GetHAL().performanceDiagnostics();
    std::printf(
        "DBG STATUS perf_lvgl_core=%d perf_tx_core=%d lvgl_calls=%lu lvgl_max_us=%lu touch_reads=%lu "
        "touch_gap_max_us=%lu queue_high=%lu tx_max_us=%lu tx_total_us=%lu\r\n",
        static_cast<int>(performance.lvglTaskCore), static_cast<int>(ble.inputTaskCore),
        static_cast<unsigned long>(performance.lvglHandlerCalls),
        static_cast<unsigned long>(performance.lvglHandlerMaxUs), static_cast<unsigned long>(performance.touchReads),
        static_cast<unsigned long>(performance.touchMaxGapUs), static_cast<unsigned long>(ble.queueHighWater),
        static_cast<unsigned long>(ble.txMaxUs), static_cast<unsigned long>(ble.txTotalUs));
    std::fflush(stdout);
    result("status", "PASS");
}

void SerialDebug::runSelfTest()
{
    if (_async_test != AsyncTest::None) {
        result("selftest", "FAIL", "reason=async_test_active");
        return;
    }
    unsigned passed  = 0;
    unsigned failed  = 0;
    const auto check = [&passed, &failed](const char* name, bool ok, const char* details = nullptr) {
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
        SerialDebug::result(name, ok ? "PASS" : "FAIL", details);
    };

    const Hal::Diagnostics hal = GetHAL().diagnostics();
    check("hal.i2c", hal.i2c);
    check("hal.pmic", hal.pmic);
    check("hal.io_expander", hal.ioExpander);
    check("hal.display", hal.display);
    check("hal.touch", hal.touch);
    check("hal.audio", hal.audio);
    check("hal.vibrator", hal.vibrator);
    check("hal.buttons", hal.buttons);

    const bool heap_ok              = heap_caps_check_integrity_all(false);
    const std::size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const std::size_t psram_total   = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    char heap_details[96]           = {};
    std::snprintf(heap_details, sizeof(heap_details), "internal_free=%lu psram_total=%lu",
                  static_cast<unsigned long>(internal_free), static_cast<unsigned long>(psram_total));
    check("memory.integrity", heap_ok && internal_free > 32768 && psram_total > 0, heap_details);

    lv_display_t* display    = lv_display_get_default();
    const int32_t width      = display == nullptr ? 0 : lv_display_get_horizontal_resolution(display);
    const int32_t height     = display == nullptr ? 0 : lv_display_get_vertical_resolution(display);
    char display_details[64] = {};
    std::snprintf(display_details, sizeof(display_details), "width=%ld height=%ld", static_cast<long>(width),
                  static_cast<long>(height));
    check("display.geometry", display != nullptr && width > 0 && height > 0, display_details);

    const uint8_t battery    = GetHAL().getBatteryLevel();
    char battery_details[64] = {};
    std::snprintf(battery_details, sizeof(battery_details), "level=%u charging=%s", static_cast<unsigned>(battery),
                  onOff(GetHAL().isBatteryCharging()));
    check("power.telemetry", battery <= 100, battery_details);

    char audio_details[64] = {};
    std::snprintf(audio_details, sizeof(audio_details), "sample_rate=%d volume=%d", GetHAL().getAudioSampleRate(),
                  GetHAL().getSpeakerVolume());
    check("audio.configuration",
          GetHAL().getAudioSampleRate() == 44100 && GetHAL().getSpeakerVolume() >= 0 &&
              GetHAL().getSpeakerVolume() <= 100,
          audio_details);

    const CodexMicroState state        = GetCodexMicroBle().snapshot();
    const CodexMicroBleDiagnostics ble = GetCodexMicroBle().diagnostics();
    char ble_details[80]               = {};
    std::snprintf(ble_details, sizeof(ble_details), "ready=%s connected=%s advertising=%s", onOff(state.ready),
                  onOff(state.connected), onOff(ble.advertising));
    check("ble.service", ble.initialized && ble.hidReady && state.ready, ble_details);
    char protocol_details[112] = {};
    std::snprintf(protocol_details, sizeof(protocol_details),
                  "controls=12 encoder=3 rpc_buffer=4096 dropped=%lu tx_failures=%lu rpc_errors=%lu",
                  static_cast<unsigned long>(ble.inputDropped), static_cast<unsigned long>(ble.txFailures),
                  static_cast<unsigned long>(ble.rpcErrors));
    check("ble.protocol",
          GetCodexMicroBle().protocolSelfTest() && ble.inputDropped == 0 && ble.txFailures == 0 && ble.rpcErrors == 0,
          protocol_details);
    check("ui.objects", _app.debugUiReady(), _app.debugScreenName());
    check("serial.transport", true, "primary=usb-serial-jtag nonblocking=1");

    char summary[64] = {};
    std::snprintf(summary, sizeof(summary), "passed=%u failed=%u", passed, failed);
    result("selftest", failed == 0 ? "PASS" : "FAIL", summary);
}

void SerialDebug::printControls()
{
    constexpr std::size_t PhysicalControlCount = 12;
    bool all_valid                             = CodexMicroControlCodes.size() >= PhysicalControlCount;
    for (std::size_t index = 0; index < PhysicalControlCount; ++index) {
        const char* code = CodexMicroControlCodes[index];
        const bool valid = code != nullptr && code[0] != '\0';
        all_valid        = all_valid && valid;
        std::printf("DBG CONTROL index=%u code=%s status=%s\r\n", static_cast<unsigned>(index),
                    valid ? code : "invalid", valid ? "PASS" : "FAIL");
    }
    std::fflush(stdout);
    result("controls", all_valid ? "PASS" : "FAIL", "physical=12");
}

void SerialDebug::startMicrophoneTest(uint32_t durationMs)
{
    cancelAsyncTest("replaced", false);
    _async_test         = AsyncTest::Microphone;
    _test_started_ms    = GetHAL().millis();
    _test_deadline_ms   = _test_started_ms + durationMs;
    _meter_was_enabled  = GetHAL().isMicrophoneMeterEnabled();
    _microphone_peak    = 0.0f;
    _microphone_samples = 0;
    GetHAL().setMicrophoneMeterEnabled(true);
    char details[48] = {};
    std::snprintf(details, sizeof(details), "duration_ms=%lu", static_cast<unsigned long>(durationMs));
    result("mic", "RUNNING", details);
}

void SerialDebug::updateMicrophoneTest(uint32_t now)
{
    const float level = GetHAL().getMicrophoneLevel();
    if (std::isfinite(level) && level >= 0.0f && level <= 1.0f) {
        _microphone_peak = std::max(_microphone_peak, level);
        ++_microphone_samples;
    }
    if (!elapsed(now, _test_deadline_ms)) {
        return;
    }
    if (!_meter_was_enabled) {
        GetHAL().setMicrophoneMeterEnabled(false);
    }
    char details[96] = {};
    std::snprintf(details, sizeof(details), "peak=%.3f samples=%lu signal=%s", static_cast<double>(_microphone_peak),
                  static_cast<unsigned long>(_microphone_samples),
                  _microphone_peak >= MicrophoneSignalFloor ? "detected" : "quiet");
    const char* status = _microphone_samples == 0                    ? "FAIL"
                         : _microphone_peak >= MicrophoneSignalFloor ? "PASS"
                                                                     : "OBSERVE";
    _async_test        = AsyncTest::None;
    result("mic", status, details);
}

void SerialDebug::startInputTest(uint32_t durationMs)
{
    cancelAsyncTest("replaced", false);
    const AppCodexMicro::DebugInputState current = _app.debugInputState();
    _input_seen_a = _input_seen_b = _input_seen_touch = false;
    _input_previous_a                                 = current.buttonA;
    _input_previous_b                                 = current.buttonB;
    _input_previous_touch                             = current.touch;
    _input_min_x = _input_min_y = 32767;
    _input_max_x = _input_max_y = -1;
    _app.debugSetInputCapture(true);
    _async_test       = AsyncTest::Inputs;
    _test_started_ms  = GetHAL().millis();
    _test_deadline_ms = _test_started_ms + durationMs;
    char details[96]  = {};
    std::snprintf(details, sizeof(details), "duration_ms=%lu capture=1 expected=button_a,button_b,touch",
                  static_cast<unsigned long>(durationMs));
    result("inputs", "RUNNING", details);
}

void SerialDebug::updateInputTest(uint32_t now)
{
    const AppCodexMicro::DebugInputState state = _app.debugInputState();
    if (state.buttonA && !_input_previous_a) {
        _input_seen_a = true;
        std::printf("DBG INPUT event=button_a_pressed\r\n");
    }
    if (state.buttonB && !_input_previous_b) {
        _input_seen_b = true;
        std::printf("DBG INPUT event=button_b_pressed\r\n");
    }
    if (state.touch) {
        _input_seen_touch = true;
        _input_min_x      = std::min(_input_min_x, state.x);
        _input_min_y      = std::min(_input_min_y, state.y);
        _input_max_x      = std::max(_input_max_x, state.x);
        _input_max_y      = std::max(_input_max_y, state.y);
        if (!_input_previous_touch) {
            std::printf("DBG INPUT event=touch_pressed x=%d y=%d\r\n", state.x, state.y);
        }
    } else if (_input_previous_touch) {
        std::printf("DBG INPUT event=touch_released\r\n");
    }
    _input_previous_a     = state.buttonA;
    _input_previous_b     = state.buttonB;
    _input_previous_touch = state.touch;
    std::fflush(stdout);

    if (!elapsed(now, _test_deadline_ms) && !(_input_seen_a && _input_seen_b && _input_seen_touch)) {
        return;
    }
    _app.debugSetInputCapture(false);
    char details[128] = {};
    std::snprintf(details, sizeof(details), "button_a=%s button_b=%s touch=%s min_x=%d min_y=%d max_x=%d max_y=%d",
                  onOff(_input_seen_a), onOff(_input_seen_b), onOff(_input_seen_touch), _input_min_x, _input_min_y,
                  _input_max_x, _input_max_y);
    const bool complete = _input_seen_a && _input_seen_b && _input_seen_touch;
    _async_test         = AsyncTest::None;
    result("inputs", complete ? "PASS" : "FAIL", details);
}

void SerialDebug::startUiCycle()
{
    cancelAsyncTest("replaced", false);
    if (!_app.debugSetScreen(AppCodexMicro::DebugScreen::Command)) {
        result("ui-cycle", "SKIP", "reason=requires_ble_connection");
        return;
    }
    _ui_cycle_stage   = 0;
    _async_test       = AsyncTest::UiCycle;
    _test_started_ms  = GetHAL().millis();
    _test_deadline_ms = _test_started_ms + UiCycleStepMs;
    result("ui-cycle", "RUNNING", "screen=command");
}

void SerialDebug::updateUiCycle(uint32_t now)
{
    if (!elapsed(now, _test_deadline_ms)) {
        return;
    }
    bool ok              = false;
    const char* expected = nullptr;
    if (_ui_cycle_stage == 0) {
        expected = "agent";
        ok       = _app.debugSetScreen(AppCodexMicro::DebugScreen::Agent);
    } else if (_ui_cycle_stage == 1) {
        expected = "mic";
        ok       = _app.debugSetScreen(AppCodexMicro::DebugScreen::Mic);
    } else {
        expected = "command";
        ok       = _app.debugSetScreen(AppCodexMicro::DebugScreen::Command);
    }
    ok = ok && std::strcmp(_app.debugScreenName(), expected) == 0;
    std::printf("DBG UI stage=%u expected=%s actual=%s status=%s\r\n", static_cast<unsigned>(_ui_cycle_stage + 1),
                expected, _app.debugScreenName(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok || _ui_cycle_stage >= 2) {
        _async_test = AsyncTest::None;
        result("ui-cycle", ok ? "PASS" : "FAIL", "final=command verify=visible");
        return;
    }
    ++_ui_cycle_stage;
    _test_deadline_ms = now + UiCycleStepMs;
}

void SerialDebug::startTransportTest()
{
    cancelAsyncTest("replaced", false);
    if (!GetCodexMicroBle().connected()) {
        result("transport", "SKIP", "reason=requires_ble_connection");
        return;
    }
    const CodexMicroBleDiagnostics before = GetCodexMicroBle().diagnostics();
    _transport_tx_messages                = before.txMessages;
    _transport_tx_reports                 = before.txReports;
    _transport_tx_failures                = before.txFailures;
    if (!GetCodexMicroBle().sendJoystick(0.0f, 0.0f)) {
        result("transport", "FAIL", "reason=neutral_report_not_queued");
        return;
    }
    _async_test       = AsyncTest::Transport;
    _test_started_ms  = GetHAL().millis();
    _test_deadline_ms = _test_started_ms + TransportWaitMs;
    result("transport", "RUNNING", "payload=neutral_joystick safe=1");
}

void SerialDebug::updateTransportTest(uint32_t now)
{
    if (!elapsed(now, _test_deadline_ms)) {
        return;
    }
    const CodexMicroBleDiagnostics after = GetCodexMicroBle().diagnostics();
    const bool sent   = after.txMessages > _transport_tx_messages && after.txReports > _transport_tx_reports &&
                        after.txFailures == _transport_tx_failures;
    char details[112] = {};
    std::snprintf(details, sizeof(details), "messages_delta=%lu reports_delta=%lu failures_delta=%lu",
                  static_cast<unsigned long>(after.txMessages - _transport_tx_messages),
                  static_cast<unsigned long>(after.txReports - _transport_tx_reports),
                  static_cast<unsigned long>(after.txFailures - _transport_tx_failures));
    _async_test = AsyncTest::None;
    result("transport", sent ? "PASS" : "FAIL", details);
}

void SerialDebug::startPerformanceTest(uint32_t durationMs, bool generateTraffic)
{
    cancelAsyncTest("replaced", false);
    if (!GetCodexMicroBle().connected()) {
        result("perf", "SKIP", "reason=requires_ble_connection");
        return;
    }
    GetHAL().resetPerformanceDiagnostics();
    GetCodexMicroBle().resetPerformanceDiagnostics();
    const CodexMicroBleDiagnostics before = GetCodexMicroBle().diagnostics();
    _performance_input_queued             = before.inputQueued;
    _performance_input_dropped            = before.inputDropped;
    _performance_input_processed          = before.inputProcessed;
    _performance_tx_messages              = before.txMessages;
    _performance_tx_reports               = before.txReports;
    _performance_tx_failures              = before.txFailures;
    _performance_generated                = 0;
    _performance_accepted                 = 0;
    _performance_draining                 = false;
    _performance_generate_traffic         = generateTraffic;
    _performance_loop_max_gap_us          = 0;
    _performance_last_poll_us             = esp_timer_get_time();
    _test_started_ms                      = GetHAL().millis();
    _performance_next_send_ms             = _test_started_ms;
    _test_deadline_ms                     = _test_started_ms + durationMs;
    _async_test                           = AsyncTest::Performance;
    char details[80]                      = {};
    std::snprintf(details, sizeof(details), "duration_ms=%lu mode=%s rate_hz=%u",
                  static_cast<unsigned long>(durationMs), generateTraffic ? "synthetic" : "observe",
                  generateTraffic ? 50U : 0U);
    result(generateTraffic ? "perf" : "trace", "RUNNING", details);
}

void SerialDebug::updatePerformanceTest(uint32_t now)
{
    const int64_t now_us = esp_timer_get_time();
    if (_performance_last_poll_us > 0 && now_us > _performance_last_poll_us) {
        _performance_loop_max_gap_us =
            std::max(_performance_loop_max_gap_us, static_cast<uint32_t>(now_us - _performance_last_poll_us));
    }
    _performance_last_poll_us = now_us;

    if (_performance_generate_traffic && !_performance_draining && elapsed(now, _performance_next_send_ms)) {
        ++_performance_generated;
        if (GetCodexMicroBle().sendJoystick(0.0f, 0.0f)) {
            ++_performance_accepted;
        }
        _performance_next_send_ms += PerformanceSendPeriodMs;
        if (elapsed(now, _performance_next_send_ms + PerformanceSendPeriodMs)) {
            _performance_next_send_ms = now + PerformanceSendPeriodMs;
        }
    }

    if (!_performance_draining && elapsed(now, _test_deadline_ms)) {
        _performance_draining = true;
        _test_deadline_ms     = now + 250;
        return;
    }
    if (!_performance_draining || !elapsed(now, _test_deadline_ms)) {
        return;
    }

    const CodexMicroBleDiagnostics after      = GetCodexMicroBle().diagnostics();
    const Hal::PerformanceDiagnostics display = GetHAL().performanceDiagnostics();
    const uint32_t queued                     = after.inputQueued - _performance_input_queued;
    const uint32_t dropped                    = after.inputDropped - _performance_input_dropped;
    const uint32_t processed                  = after.inputProcessed - _performance_input_processed;
    const uint32_t messages                   = after.txMessages - _performance_tx_messages;
    const uint32_t reports                    = after.txReports - _performance_tx_reports;
    const uint32_t failures                   = after.txFailures - _performance_tx_failures;
    const bool activity_ok = _performance_generate_traffic
                                 ? _performance_generated >= 40 && _performance_accepted == _performance_generated &&
                                       queued == _performance_accepted
                                 : queued > 0 && processed > 0;
    const bool responsive  = activity_ok && dropped == 0 && processed > 0 && messages > 0 && reports > 0 &&
                             failures == 0 && display.touchReads > 0 && display.touchMaxGapUs <= 50000 &&
                             _performance_loop_max_gap_us <= 50000;
    char details[320]      = {};
    std::snprintf(details, sizeof(details),
                  "generated=%lu accepted=%lu queued=%lu processed=%lu dropped=%lu messages=%lu reports=%lu "
                  "failures=%lu queue_high=%lu tx_max_us=%lu loop_gap_max_us=%lu lvgl_core=%d tx_core=%d "
                  "lvgl_max_us=%lu touch_reads=%lu touch_gap_max_us=%lu",
                  static_cast<unsigned long>(_performance_generated), static_cast<unsigned long>(_performance_accepted),
                  static_cast<unsigned long>(queued), static_cast<unsigned long>(processed),
                  static_cast<unsigned long>(dropped), static_cast<unsigned long>(messages),
                  static_cast<unsigned long>(reports), static_cast<unsigned long>(failures),
                  static_cast<unsigned long>(after.queueHighWater), static_cast<unsigned long>(after.txMaxUs),
                  static_cast<unsigned long>(_performance_loop_max_gap_us), static_cast<int>(display.lvglTaskCore),
                  static_cast<int>(after.inputTaskCore), static_cast<unsigned long>(display.lvglHandlerMaxUs),
                  static_cast<unsigned long>(display.touchReads), static_cast<unsigned long>(display.touchMaxGapUs));
    _async_test = AsyncTest::None;
    result(_performance_generate_traffic ? "perf" : "trace", responsive ? "PASS" : "FAIL", details);
}

void SerialDebug::updateAsyncTest()
{
    const uint32_t now = GetHAL().millis();
    switch (_async_test) {
        case AsyncTest::None:
            return;
        case AsyncTest::Microphone:
            updateMicrophoneTest(now);
            return;
        case AsyncTest::Inputs:
            updateInputTest(now);
            return;
        case AsyncTest::UiCycle:
            updateUiCycle(now);
            return;
        case AsyncTest::Transport:
            updateTransportTest(now);
            return;
        case AsyncTest::Performance:
            updatePerformanceTest(now);
            return;
    }
}

void SerialDebug::cancelAsyncTest(const char* reason, bool reportCancellation)
{
    if (_async_test == AsyncTest::None) {
        if (reportCancellation) {
            result("cancel", "SKIP", "reason=no_async_test");
        }
        return;
    }
    if (_async_test == AsyncTest::Microphone && !_meter_was_enabled) {
        GetHAL().setMicrophoneMeterEnabled(false);
    }
    if (_async_test == AsyncTest::Inputs) {
        _app.debugSetInputCapture(false);
    }
    _async_test = AsyncTest::None;
    if (reportCancellation) {
        char details[64] = {};
        std::snprintf(details, sizeof(details), "reason=%s", reason == nullptr ? "unknown" : reason);
        result("cancel", "PASS", details);
    }
}

uint32_t SerialDebug::parseUnsigned(const char* value, uint32_t fallback, uint32_t minimum, uint32_t maximum)
{
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end                  = nullptr;
    errno                      = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return fallback;
    }
    return std::clamp<uint32_t>(static_cast<uint32_t>(parsed), minimum, maximum);
}

void SerialDebug::result(const char* command, const char* status, const char* details)
{
    std::printf("DBG RESULT command=%s status=%s", command == nullptr ? "unknown" : command,
                status == nullptr ? "FAIL" : status);
    if (details != nullptr && details[0] != '\0') {
        std::printf(" %s", details);
    }
    std::printf("\r\n");
    std::fflush(stdout);
}
