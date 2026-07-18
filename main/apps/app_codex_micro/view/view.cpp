/*
 * SPDX-License-Identifier: MIT
 */
#include "view.h"

#include <apps/common/audio/audio.h>
#include <hal/hal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <esp_log.h>
#include <esp_timer.h>
#include <core/lv_obj_event_private.h>

namespace {

constexpr const char* Tag = "CodexMicro-UI";

constexpr uint32_t Background              = 0x0A0D0B;
constexpr uint32_t Text                    = 0xF7F7F0;
constexpr uint32_t Green                   = 0x59E3A5;
constexpr uint32_t Key                     = 0xF1F0EB;
constexpr uint32_t KeyPressed              = 0xE8ECE8;
constexpr uint32_t KeyHigh                 = 0xFFFFFA;
constexpr uint32_t KeyPressedHigh          = 0xFFFFFF;
constexpr uint32_t KeyPressedLow           = 0xEEF4F0;
constexpr uint32_t KeyBorder               = 0xFFFFFA;
constexpr uint32_t KeyInk                  = 0x171A18;
constexpr uint32_t KeyMuted                = 0x69716D;
constexpr uint32_t CodexBlue               = 0x566BF7;
constexpr uint32_t CodexBlueHigh           = 0x9DA1FC;
constexpr uint32_t CodexLavender           = 0xC2B8FF;
constexpr uint32_t ArcTrack                = 0x686D6A;
constexpr uint32_t ArcThumb                = 0x626763;
constexpr uint32_t ArcThumbBorder          = 0xA9AEAB;
constexpr uint32_t ArcThumbActive          = 0x747A76;
constexpr uint32_t ArcThumbActiveBorder    = 0xD1D5D3;
constexpr uint32_t ArcThumbReturning       = 0x656A67;
constexpr uint32_t ArcThumbReturningBorder = 0xA6ACA8;
constexpr uint32_t Touch                   = 0x1D211F;
constexpr uint32_t TouchBorder             = 0x686F6B;
constexpr uint32_t Fingerprint             = 0xC9CECB;
constexpr uint32_t PairingLine             = 0x777E7A;
constexpr uint32_t PairingCore             = 0xF7F7EF;
constexpr uint32_t AgentOff                = 0xAEB4B0;
constexpr lv_style_selector_t PressedStyle =
    static_cast<lv_style_selector_t>(LV_PART_MAIN) | static_cast<lv_style_selector_t>(LV_STATE_PRESSED);

constexpr std::array<CodexMicroControl, 6> AgentControls = {
    CodexMicroControl::Agent1, CodexMicroControl::Agent2, CodexMicroControl::Agent3,
    CodexMicroControl::Agent4, CodexMicroControl::Agent5, CodexMicroControl::Agent6,
};
constexpr std::array<CodexMicroControl, 4> CommandControls = {
    CodexMicroControl::Fast,
    CodexMicroControl::Approve,
    CodexMicroControl::Decline,
    CodexMicroControl::NewChat,
};
constexpr float FeedbackToneDurationSeconds            = 0.016f;
constexpr float FeedbackToneVolume                     = 0.38f;
constexpr uint16_t FeedbackVibrationDurationMs         = 18;
constexpr uint8_t FeedbackVibrationStrength            = 58;
constexpr float DialRatchetToneDurationSeconds         = 0.010f;
constexpr float DialRatchetToneVolume                  = 0.30f;
constexpr uint16_t DialRatchetVibrationDurationMs      = 10;
constexpr uint8_t DialRatchetVibrationStrength         = 44;
constexpr uint32_t AnimatedLightingRefreshPeriodMs     = 100;
constexpr float AmbientBrightnessScale                 = 0.42f;
constexpr float AmbientSaturationScale                 = 0.72f;
constexpr int AmbientOuterSize                         = 464;
constexpr int AmbientLayerWidth                        = 2;
constexpr int AmbientLayerSizeStep                     = AmbientLayerWidth * 2;
constexpr std::array<lv_opa_t, 14> AmbientLayerOpacity = {
    LV_OPA_COVER,
    static_cast<lv_opa_t>(235),
    static_cast<lv_opa_t>(214),
    static_cast<lv_opa_t>(194),
    static_cast<lv_opa_t>(173),
    static_cast<lv_opa_t>(153),
    static_cast<lv_opa_t>(133),
    static_cast<lv_opa_t>(112),
    static_cast<lv_opa_t>(92),
    static_cast<lv_opa_t>(71),
    static_cast<lv_opa_t>(51),
    static_cast<lv_opa_t>(36),
    static_cast<lv_opa_t>(20),
    static_cast<lv_opa_t>(8),
};

constexpr int DisplayCenter                   = 233;
constexpr int JoystickSize                    = 206;
constexpr int JoystickKnobSize                = 40;
constexpr int JoystickTravelLimit             = 73;
constexpr int JoystickPressRadius             = 78;
constexpr int JoystickNeutralPosition         = (JoystickSize - JoystickKnobSize) / 2;
constexpr float JoystickHostDeadZone          = 0.06f;
constexpr float JoystickHostFullScale         = 0.38f;
constexpr float JoystickDistanceSendThreshold = 0.025f;
constexpr float JoystickAngleSendThreshold    = 0.01f;
constexpr uint32_t JoystickReportPeriodMs     = 20;
constexpr int DialRadius                      = 208;
constexpr int DialStartDegrees                = 132;
constexpr int DialEndDegrees                  = 408;
constexpr int DialStepCount                   = 60;
constexpr int DialCenterStep                  = DialStepCount / 2;
constexpr int DialPressRadius                 = 44;
constexpr int DialThumbWidth                  = 46;
constexpr int DialThumbHeight                 = 30;
constexpr uint32_t DialReturnDurationMs       = 240;
constexpr uint32_t DialFeedbackPeriodMs       = 40;
constexpr float CommandPathCenter             = 174.0f;
constexpr float CommandCanvasSize             = 348.0f;
constexpr float CommandShadowOffsetY          = 4.0f;
constexpr float CommandShadowStrokeWidth      = 5.0f;
constexpr float CommandBorderStrokeWidth      = 2.0f;
constexpr lv_opa_t CommandShadowFillOpacity   = LV_OPA_30;
constexpr lv_opa_t CommandShadowStrokeOpacity = LV_OPA_20;

lv_area_t commandSegmentArea(int center_x, int center_y, std::size_t slot)
{
    // The outer SVG arc is radius 168 around the 174 px path center, so its
    // apex reaches 168 px away from the display center. Include stroke and
    // shadow margins; otherwise a moving overlay can clear the outer 28 px of
    // a segment without scheduling that segment to be redrawn.
    switch (slot) {
        case 0:
            return {center_x - 116, center_y - 175, center_x + 116, center_y - 59};
        case 1:
            return {center_x - 175, center_y - 116, center_x - 59, center_y + 116};
        case 2:
            return {center_x + 59, center_y - 116, center_x + 175, center_y + 116};
        default:
            return {center_x - 116, center_y + 59, center_x + 116, center_y + 175};
    }
}

static_assert(AgentControls.size() == 6, "Codex Micro requires six Agent Keys");
static_assert(CommandControls.size() == 4, "Command page exposes four touch keys");

int feedbackMidi(CodexMicroControl control, int8_t agent)
{
    if (agent >= 0) {
        return 78 + agent * 2;
    }

    switch (control) {
        case CodexMicroControl::Fast:
            return 90;
        case CodexMicroControl::Approve:
            return 92;
        case CodexMicroControl::Decline:
            return 84;
        case CodexMicroControl::NewChat:
            return 88;
        case CodexMicroControl::Mic:
            return 82;
        case CodexMicroControl::Send:
            return 94;
        case CodexMicroControl::EncoderPress:
            return 86;
        default:
            return 80;
    }
}

void playFeedback(CodexMicroControl control, int8_t agent = -1)
{
    const Hal::ButtonConfig& config = GetHAL().getButtonConfig();
    if (config.sfxEnabled) {
        audio::play_tone_from_midi(feedbackMidi(control, agent), FeedbackToneDurationSeconds, FeedbackToneVolume);
    }
    if (config.vibrateEnabled) {
        GetHAL().vibrate(FeedbackVibrationDurationMs, FeedbackVibrationStrength);
    }
}

void playDialRatchetFeedback(int direction)
{
    const Hal::ButtonConfig& config = GetHAL().getButtonConfig();
    if (config.sfxEnabled) {
        audio::play_tone_from_midi(direction > 0 ? 84 : 80, DialRatchetToneDurationSeconds, DialRatchetToneVolume);
    }
    if (config.vibrateEnabled) {
        GetHAL().vibrate(DialRatchetVibrationDurationMs, DialRatchetVibrationStrength);
    }
}

uint32_t scaledColor(uint32_t color, float brightness)
{
    brightness          = std::clamp(brightness, 0.0f, 1.0f);
    const uint8_t red   = static_cast<uint8_t>(((color >> 16) & 0xFF) * brightness);
    const uint8_t green = static_cast<uint8_t>(((color >> 8) & 0xFF) * brightness);
    const uint8_t blue  = static_cast<uint8_t>((color & 0xFF) * brightness);
    return (static_cast<uint32_t>(red) << 16) | (static_cast<uint32_t>(green) << 8) | blue;
}

uint32_t softenedAmbientColor(uint32_t color, float brightness)
{
    const float red     = static_cast<float>((color >> 16) & 0xFF);
    const float green   = static_cast<float>((color >> 8) & 0xFF);
    const float blue    = static_cast<float>(color & 0xFF);
    const float neutral = (red + green + blue) / 3.0f;
    const auto soften   = [neutral](float channel) { return neutral + (channel - neutral) * AmbientSaturationScale; };
    const uint32_t softened = (static_cast<uint32_t>(std::lround(soften(red))) << 16) |
                              (static_cast<uint32_t>(std::lround(soften(green))) << 8) |
                              static_cast<uint32_t>(std::lround(soften(blue)));
    return scaledColor(softened, brightness * AmbientBrightnessScale);
}

bool lightAssigned(const CodexMicroLight& light)
{
    // Brightness is a user setting and may legitimately be zero. The host uses
    // an off effect with color 0 for an unassigned Agent slot.
    return light.effect != CodexMicroLightEffect::Off && light.color != 0;
}

bool sameLight(const CodexMicroLight& lhs, const CodexMicroLight& rhs)
{
    return lhs.color == rhs.color && lhs.brightness == rhs.brightness && lhs.effect == rhs.effect &&
           lhs.speed == rhs.speed && lhs.magic == rhs.magic;
}

float animatedBrightness(const CodexMicroLight& light, float seconds)
{
    float brightness = light.brightness;
    if (light.effect == CodexMicroLightEffect::Breath || light.effect == CodexMicroLightEffect::ShallowBreath) {
        const float speed = light.speed > 0.05f ? light.speed : 1.0f;
        const float depth = light.effect == CodexMicroLightEffect::ShallowBreath ? 0.22f : 0.45f;
        brightness *= (1.0f - depth) + depth * (std::sin(seconds * 3.8f * speed) * 0.5f + 0.5f);
    }
    return std::clamp(brightness, 0.0f, 1.0f);
}

const char* lightStatus(const CodexMicroLight& light)
{
    if (!lightAssigned(light)) {
        return "Unassigned";
    }

    const int red       = (light.color >> 16) & 0xFF;
    const int green     = (light.color >> 8) & 0xFF;
    const int blue      = light.color & 0xFF;
    const int max_value = std::max({red, green, blue});
    const int min_value = std::min({red, green, blue});
    if (max_value - min_value < 48 && max_value > 150) {
        return "Idle";
    }
    if (blue >= red && blue > green) {
        return "Thinking";
    }
    if (red > 170 && green > 105 && blue < 120) {
        return "Needs input";
    }
    if (red > green && red >= blue) {
        return "Error";
    }
    if (green >= red && green >= blue) {
        return "Complete";
    }
    return "Active";
}

void drawLine(lv_layer_t* layer, int x1, int y1, int x2, int y2, int width, uint32_t color)
{
    lv_draw_line_dsc_t draw;
    lv_draw_line_dsc_init(&draw);
    draw.color       = lv_color_hex(color);
    draw.width       = width;
    draw.round_start = 1;
    draw.round_end   = 1;
    draw.p1          = {static_cast<lv_value_precise_t>(x1), static_cast<lv_value_precise_t>(y1)};
    draw.p2          = {static_cast<lv_value_precise_t>(x2), static_cast<lv_value_precise_t>(y2)};
    lv_draw_line(layer, &draw);
}

void drawArc(lv_layer_t* layer, int x, int y, int radius, int start_angle, int end_angle, int width, uint32_t color,
             bool rounded = true, lv_opa_t opacity = LV_OPA_COVER)
{
    lv_draw_arc_dsc_t draw;
    lv_draw_arc_dsc_init(&draw);
    draw.color       = lv_color_hex(color);
    draw.width       = width;
    draw.rounded     = rounded ? 1 : 0;
    draw.center      = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    draw.radius      = radius;
    draw.start_angle = start_angle;
    draw.end_angle   = end_angle;
    draw.opa         = opacity;
    lv_draw_arc(layer, &draw);
}

lv_fpoint_t commandPathPoint(int center_x, int center_y, float x, float y, uint8_t quarter_turns)
{
    const float dx = x - CommandPathCenter;
    const float dy = y - CommandPathCenter;
    switch (quarter_turns % 4) {
        case 1:
            return {center_x - dy, center_y + dx};
        case 2:
            return {center_x - dx, center_y - dy};
        case 3:
            return {center_x + dy, center_y - dx};
        default:
            return {center_x + dx, center_y + dy};
    }
}

void buildCommandSegmentPath(lv_vector_path_t* path, int center_x, int center_y, uint8_t quarter_turns)
{
    const auto point  = [=](float x, float y) { return commandPathPoint(center_x, center_y, x, y, quarter_turns); };
    const auto moveTo = [&](float x, float y) {
        const lv_fpoint_t target = point(x, y);
        lv_vector_path_move_to(path, &target);
    };
    const auto lineTo = [&](float x, float y) {
        const lv_fpoint_t target = point(x, y);
        lv_vector_path_line_to(path, &target);
    };
    const auto quadTo = [&](float control_x, float control_y, float x, float y) {
        const lv_fpoint_t control = point(control_x, control_y);
        const lv_fpoint_t target  = point(x, y);
        lv_vector_path_quad_to(path, &control, &target);
    };
    const auto arcTo = [&](float radius, bool clockwise, float x, float y) {
        const lv_fpoint_t target = point(x, y);
        lv_vector_path_arc_to(path, radius, radius, 0.0f, false, clockwise, &target);
    };

    // Exact path from the reviewed 348 x 348 HTML command button. The other
    // three buttons are quarter-turn rotations around its 174 x 174 center.
    moveTo(68.3f, 56.6f);
    quadTo(61.6f, 49.2f, 68.3f, 43.4f);
    arcTo(168.0f, true, 279.7f, 43.4f);
    quadTo(286.4f, 49.2f, 279.7f, 56.6f);
    lineTo(240.9f, 99.7f);
    quadTo(234.2f, 107.1f, 228.2f, 102.1f);
    arcTo(90.0f, false, 119.8f, 102.1f);
    quadTo(113.8f, 107.1f, 107.1f, 99.7f);
    lv_vector_path_close(path);
}

void drawOutline(lv_layer_t* layer, int x1, int y1, int x2, int y2, int radius, int width, uint32_t color)
{
    lv_draw_rect_dsc_t draw;
    lv_draw_rect_dsc_init(&draw);
    draw.bg_opa       = LV_OPA_TRANSP;
    draw.border_color = lv_color_hex(color);
    draw.border_width = width;
    draw.radius       = radius;
    lv_area_t area    = {x1, y1, x2, y2};
    lv_draw_rect(layer, &draw, &area);
}

}  // namespace

namespace view {

CodexMicroView::~CodexMicroView()
{
    GetHAL().setMicrophoneMeterEnabled(false);
    releaseActiveInputs();
    if (_root != nullptr) {
        lv_obj_delete(_root);
    }
    if (_command_path != nullptr) {
        lv_vector_path_delete(_command_path);
    }
}

void CodexMicroView::stylePanel(lv_obj_t* object, uint32_t background, uint32_t border, int radius, int border_width)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(object, lv_color_hex(border), LV_PART_MAIN);
    lv_obj_set_style_border_width(object, border_width, LV_PART_MAIN);
    lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
}

void CodexMicroView::init(lv_obj_t* parent)
{
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(Background), LV_PART_MAIN);

    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, 466, 466);
    lv_obj_align(_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    stylePanel(_root, Background, Background, 0, 0);

    for (std::size_t index = 0; index < _ambient_layers.size(); ++index) {
        lv_obj_t* layer        = lv_arc_create(_root);
        _ambient_layers[index] = layer;
        const int size         = AmbientOuterSize - static_cast<int>(index) * AmbientLayerSizeStep;
        lv_obj_set_size(layer, size, size);
        lv_obj_align(layer, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(layer, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_width(layer, AmbientLayerWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(layer, false, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(layer, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_arc_set_angles(layer, 0, 360);
    }

    for (lv_obj_t*& page_root : _page_roots) {
        page_root = createPageRoot();
        lv_obj_add_flag(page_root, LV_OBJ_FLAG_HIDDEN);
    }
    renderCommand(_page_roots[static_cast<std::size_t>(Page::Command)]);
    renderAgent(_page_roots[static_cast<std::size_t>(Page::Agent)]);
    lv_obj_remove_flag(_page_roots[static_cast<std::size_t>(_page)], LV_OBJ_FLAG_HIDDEN);

    _touch_control = lv_button_create(_root);
    lv_obj_set_pos(_touch_control, 156, 414);
    lv_obj_set_size(_touch_control, 154, 68);
    stylePanel(_touch_control, Touch, TouchBorder, 77, 1);
    lv_obj_set_style_bg_color(_touch_control, lv_color_hex(0x2A2F2C), PressedStyle);
    lv_obj_set_style_border_color(_touch_control, lv_color_hex(Green), PressedStyle);
    lv_obj_set_style_shadow_width(_touch_control, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(_touch_control, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_touch_control, lv_color_hex(0x000000), LV_PART_MAIN);
    _icon_contexts[5] = {.owner = this, .icon = Icon::Fingerprint};
    lv_obj_add_event_cb(_touch_control, iconEvent, LV_EVENT_DRAW_MAIN, &_icon_contexts[5]);
    lv_obj_add_event_cb(_touch_control, touchEvent, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(_touch_control, touchEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(_touch_control, touchEvent, LV_EVENT_PRESS_LOST, this);

    lv_obj_move_foreground(_touch_control);

    _mic_screen = lv_obj_create(_root);
    lv_obj_set_pos(_mic_screen, 0, 0);
    lv_obj_set_size(_mic_screen, 466, 466);
    lv_obj_add_flag(_mic_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_mic_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_mic_screen, LV_OBJ_FLAG_SCROLLABLE);
    stylePanel(_mic_screen, Background, Background, 0, 0);

    lv_obj_t* mic_icon = lv_obj_create(_mic_screen);
    lv_obj_remove_flag(mic_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(mic_icon, 169, 92);
    lv_obj_set_size(mic_icon, 128, 128);
    stylePanel(mic_icon, Key, KeyBorder, 44, 1);
    lv_obj_set_style_shadow_width(mic_icon, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(mic_icon, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(mic_icon, lv_color_hex(0x000000), LV_PART_MAIN);
    _icon_contexts[4] = {.owner = this, .icon = Icon::Mic};
    lv_obj_add_event_cb(mic_icon, iconEvent, LV_EVENT_DRAW_MAIN, &_icon_contexts[4]);

    constexpr int MicMeterLeft    = 165;
    constexpr int MicMeterCenterY = 307;
    constexpr int MicBarPitch     = 17;
    for (std::size_t index = 0; index < _mic_bars.size(); ++index) {
        _mic_bars[index] = lv_obj_create(_mic_screen);
        lv_obj_remove_flag(_mic_bars[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(_mic_bars[index], MicMeterLeft + static_cast<int>(index) * MicBarPitch, MicMeterCenterY - 3);
        lv_obj_set_size(_mic_bars[index], 8, 6);
        stylePanel(_mic_bars[index], Text, Text, 4, 0);
        lv_obj_set_style_opa(_mic_bars[index], LV_OPA_30, LV_PART_MAIN);
    }

    lv_obj_t* mic_source = lv_label_create(_mic_screen);
    lv_label_set_text(mic_source, "HOST PTT / LOCAL LEVEL");
    lv_obj_set_style_text_font(mic_source, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(mic_source, lv_color_hex(KeyMuted), LV_PART_MAIN);
    lv_obj_align(mic_source, LV_ALIGN_CENTER, 0, 116);

    _pairing_screen = lv_obj_create(_root);
    lv_obj_set_pos(_pairing_screen, 0, 0);
    lv_obj_set_size(_pairing_screen, 466, 466);
    lv_obj_add_flag(_pairing_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_pairing_screen, LV_OBJ_FLAG_SCROLLABLE);
    stylePanel(_pairing_screen, Background, Background, 0, 0);

    _pairing_pulse = lv_obj_create(_pairing_screen);
    lv_obj_remove_flag(_pairing_pulse, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(_pairing_pulse, 78, 78);
    lv_obj_align(_pairing_pulse, LV_ALIGN_CENTER, 0, -55);
    lv_obj_set_style_bg_opa(_pairing_pulse, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(_pairing_pulse, lv_color_hex(PairingLine), LV_PART_MAIN);
    lv_obj_set_style_border_width(_pairing_pulse, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(_pairing_pulse, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_pairing_pulse, 14, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(_pairing_pulse, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_pairing_pulse, lv_color_hex(PairingCore), LV_PART_MAIN);

    _pairing_core = lv_obj_create(_pairing_pulse);
    lv_obj_remove_flag(_pairing_core, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(_pairing_core, 14, 14);
    lv_obj_align(_pairing_core, LV_ALIGN_CENTER, 0, 0);
    stylePanel(_pairing_core, PairingCore, PairingCore, LV_RADIUS_CIRCLE, 0);

    lv_obj_t* pairing_title = lv_label_create(_pairing_screen);
    lv_label_set_text(pairing_title, "Pairing");
    lv_obj_set_style_text_font(pairing_title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(pairing_title, lv_color_hex(Text), LV_PART_MAIN);
    lv_obj_align(pairing_title, LV_ALIGN_CENTER, 0, 20);

    for (std::size_t index = 0; index < _pairing_dots.size(); ++index) {
        _pairing_dots[index] = lv_obj_create(_pairing_screen);
        lv_obj_remove_flag(_pairing_dots[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(_pairing_dots[index], 6, 6);
        lv_obj_set_pos(_pairing_dots[index], 216 + static_cast<int>(index) * 14, 286);
        stylePanel(_pairing_dots[index], PairingCore, PairingCore, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_move_foreground(_pairing_screen);

    renderPage();
    ESP_LOGI(Tag, "Stopwatch Micro UI ready: 4 touch command + A mic + B send + 6 agent keys");
}

lv_obj_t* CodexMicroView::createPageRoot()
{
    lv_obj_t* page = lv_obj_create(_root);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, 466, 466);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    return page;
}

lv_obj_t* CodexMicroView::createKeyButton(lv_obj_t* parent, std::array<lv_obj_t*, 6>& buttons,
                                          std::array<KeyContext, 6>& contexts, std::size_t slot, int x, int y,
                                          int width, int height, CodexMicroControl control, int8_t agent,
                                          uint32_t background, uint32_t border, int radius)
{
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    stylePanel(button, background, border, radius, 1);
    lv_obj_set_style_bg_color(button, lv_color_hex(KeyPressed), PressedStyle);
    lv_obj_set_style_border_color(button, lv_color_hex(CodexBlue), PressedStyle);
    lv_obj_set_style_transform_width(button, -4, PressedStyle);
    lv_obj_set_style_transform_height(button, -4, PressedStyle);
    lv_obj_set_style_shadow_width(button, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x000000), LV_PART_MAIN);

    buttons[slot]  = button;
    contexts[slot] = {.owner = this, .control = control, .agent = agent, .active = false};
    lv_obj_add_event_cb(button, keyEvent, LV_EVENT_PRESSED, &contexts[slot]);
    lv_obj_add_event_cb(button, keyEvent, LV_EVENT_RELEASED, &contexts[slot]);
    lv_obj_add_event_cb(button, keyEvent, LV_EVENT_PRESS_LOST, &contexts[slot]);
    return button;
}

void CodexMicroView::createCommandButton(lv_obj_t* parent, std::size_t slot, int x, int y, int width, int height,
                                         int radius, CodexMicroControl control, Icon icon)
{
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    stylePanel(button, Background, Background, radius, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, PressedStyle);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);

    _command_contexts[slot] = {.owner = this, .control = control, .agent = -1, .active = false};
    lv_obj_add_event_cb(button, keyEvent, LV_EVENT_PRESSED, &_command_contexts[slot]);
    lv_obj_add_event_cb(button, keyEvent, LV_EVENT_RELEASED, &_command_contexts[slot]);
    lv_obj_add_event_cb(button, keyEvent, LV_EVENT_PRESS_LOST, &_command_contexts[slot]);

    _icon_contexts[slot] = {.owner = this, .icon = icon};
    lv_obj_add_event_cb(button, iconEvent, LV_EVENT_DRAW_MAIN, &_icon_contexts[slot]);
}

void CodexMicroView::renderCommand(lv_obj_t* parent)
{
    _command_path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_HIGH);
    if (_command_path == nullptr) {
        ESP_LOGE(Tag, "unable to allocate persistent command vector path");
    }
    lv_obj_add_event_cb(parent, commandDeckEvent, LV_EVENT_DRAW_MAIN, this);

    createCommandButton(parent, 0, 165, 47, 136, 116, 38, CommandControls[0], Icon::Fast);
    createCommandButton(parent, 1, 47, 165, 116, 136, 38, CommandControls[1], Icon::Approve);
    createCommandButton(parent, 2, 303, 165, 116, 136, 38, CommandControls[2], Icon::Decline);
    createCommandButton(parent, 3, 165, 303, 136, 116, 38, CommandControls[3], Icon::NewChat);

    renderNavigation(parent);
}

void CodexMicroView::renderAgent(lv_obj_t* parent)
{
    for (std::size_t index = 0; index < AgentControls.size(); ++index) {
        const int row    = static_cast<int>(index / 2);
        const int col    = static_cast<int>(index % 2);
        const int x      = 48 + col * 191;
        const int y      = 104 + row * 88;
        lv_obj_t* button = createKeyButton(parent, _agent_buttons, _agent_contexts, index, x, y, 179, 78,
                                           AgentControls[index], static_cast<int8_t>(index), Key, KeyBorder, 28);

        lv_obj_t* title = lv_label_create(button);
        lv_label_set_text_fmt(title, "Agent %d", static_cast<int>(index + 1));
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(KeyInk), LV_PART_MAIN);
        lv_obj_set_pos(title, 14, 16);

        _agent_labels[index] = lv_label_create(button);
        lv_label_set_text(_agent_labels[index], "Unassigned");
        lv_obj_set_style_text_font(_agent_labels[index], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(_agent_labels[index], lv_color_hex(KeyMuted), LV_PART_MAIN);
        lv_obj_set_pos(_agent_labels[index], 14, 43);

        _agent_dots[index] = lv_obj_create(button);
        lv_obj_remove_flag(_agent_dots[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(_agent_dots[index], 14, 14);
        stylePanel(_agent_dots[index], AgentOff, 0x979E9A, LV_RADIUS_CIRCLE, 1);
        lv_obj_set_pos(_agent_dots[index], 149, 32);
    }
}

void CodexMicroView::renderNavigation(lv_obj_t* parent)
{
    lv_obj_add_event_cb(parent, dialTrackEvent, LV_EVENT_DRAW_MAIN, this);

    _joystick = lv_obj_create(parent);
    lv_obj_set_pos(_joystick, DisplayCenter - JoystickSize / 2, DisplayCenter - JoystickSize / 2);
    lv_obj_set_size(_joystick, JoystickSize, JoystickSize);
    lv_obj_add_flag(_joystick, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_joystick, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_flag(_joystick, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_remove_flag(_joystick, LV_OBJ_FLAG_SCROLLABLE);
    stylePanel(_joystick, Background, Background, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_joystick, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_joystick, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(_joystick, joystickEvent, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(_joystick, joystickEvent, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_joystick, joystickEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(_joystick, joystickEvent, LV_EVENT_PRESS_LOST, this);
    lv_obj_add_event_cb(_joystick, joystickHitTestEvent, LV_EVENT_HIT_TEST, this);

    _joystick_knob = lv_obj_create(_joystick);
    lv_obj_remove_flag(_joystick_knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(_joystick_knob, JoystickNeutralPosition, JoystickNeutralPosition);
    lv_obj_set_size(_joystick_knob, JoystickKnobSize, JoystickKnobSize);
    stylePanel(_joystick_knob, CodexBlue, CodexLavender, LV_RADIUS_CIRCLE, 1);
    lv_obj_set_style_bg_grad_color(_joystick_knob, lv_color_hex(CodexBlueHigh), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(_joystick_knob, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_joystick_knob, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(_joystick_knob, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_joystick_knob, lv_color_hex(CodexBlue), LV_PART_MAIN);
    lv_obj_move_foreground(_joystick_knob);

    _dial = lv_button_create(parent);
    lv_obj_set_pos(_dial, 0, 0);
    lv_obj_set_size(_dial, 466, 466);
    lv_obj_add_flag(_dial, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_flag(_dial, LV_OBJ_FLAG_ADV_HITTEST);
    stylePanel(_dial, Background, Background, 14, 0);
    lv_obj_set_style_bg_opa(_dial, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_dial, LV_OPA_TRANSP, PressedStyle);
    lv_obj_set_style_shadow_width(_dial, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(_dial, dialEvent, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(_dial, dialEvent, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_dial, dialEvent, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(_dial, dialEvent, LV_EVENT_PRESS_LOST, this);
    lv_obj_add_event_cb(_dial, dialHitTestEvent, LV_EVENT_HIT_TEST, this);

    _dial_thumb = lv_obj_create(_dial);
    lv_obj_remove_flag(_dial_thumb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(_dial_thumb, DialThumbWidth, DialThumbHeight);
    stylePanel(_dial_thumb, ArcThumb, ArcThumbBorder, 9, 2);
    lv_obj_set_style_shadow_width(_dial_thumb, 7, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(_dial_thumb, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(_dial_thumb, lv_color_hex(ArcThumbBorder), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(_dial_thumb, DialThumbWidth / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(_dial_thumb, DialThumbHeight / 2, LV_PART_MAIN);

    for (int x : {15, 22, 29}) {
        lv_obj_t* grip = lv_obj_create(_dial_thumb);
        lv_obj_remove_flag(grip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(grip, x, 8);
        lv_obj_set_size(grip, 1, 14);
        stylePanel(grip, 0xEEF1EF, 0xEEF1EF, 1, 0);
        lv_obj_set_style_opa(grip, LV_OPA_60, LV_PART_MAIN);
    }

    resetDial();
    lv_obj_move_foreground(_dial);
}

void CodexMicroView::renderPage()
{
    for (std::size_t index = 0; index < _page_roots.size(); ++index) {
        if (index == static_cast<std::size_t>(_page)) {
            lv_obj_remove_flag(_page_roots[index], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_page_roots[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
    _page_dirty = true;
    lv_obj_move_foreground(_touch_control);
    if (_mic_screen != nullptr && _mic_active) {
        lv_obj_move_foreground(_mic_screen);
    }
    if (_pairing_screen != nullptr && !lv_obj_has_flag(_pairing_screen, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_move_foreground(_pairing_screen);
    }
    ESP_LOGI(Tag, "page=%s", _page == Page::Command ? "command" : "agent");
}

void CodexMicroView::setPage(Page page)
{
    if (_page == page) {
        return;
    }
    const int64_t started_at = esp_timer_get_time();
    releaseActiveInputs();
    _page = page;
    renderPage();
    ESP_LOGI(Tag, "page switch=%lldus", static_cast<long long>(esp_timer_get_time() - started_at));
}

void CodexMicroView::togglePage()
{
    if (!_functional_enabled) {
        return;
    }
    setPage(_page == Page::Command ? Page::Agent : Page::Command);
}

bool CodexMicroView::ready() const
{
    return _root != nullptr && _page_roots[0] != nullptr && _page_roots[1] != nullptr;
}

bool CodexMicroView::functionalEnabled() const
{
    return _functional_enabled;
}

bool CodexMicroView::micActive() const
{
    return _mic_active;
}

CodexMicroView::Page CodexMicroView::currentPage() const
{
    return _page;
}

bool CodexMicroView::setPageForDebug(Page page)
{
    if (!_functional_enabled || !ready()) {
        return false;
    }
    setMicActive(false);
    setPage(page);
    return _page == page;
}

void CodexMicroView::setInputSuppressed(bool suppressed)
{
    if (_input_suppressed == suppressed) {
        return;
    }
    if (suppressed) {
        releaseActiveInputs();
    }
    _input_suppressed = suppressed;
}

void CodexMicroView::setMicActive(bool active)
{
    active = active && _functional_enabled;
    if (_mic_active == active || _mic_screen == nullptr) {
        return;
    }

    _mic_active = active;
    GetHAL().setMicrophoneMeterEnabled(_mic_active);
    if (_mic_active) {
        _mic_smoothed_level   = 0.0f;
        _mic_last_update_tick = 0;
        lv_obj_remove_flag(_mic_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_mic_screen);
        updateMicMeter();
    } else {
        lv_obj_add_flag(_mic_screen, LV_OBJ_FLAG_HIDDEN);
        constexpr int MicMeterCenterY = 307;
        for (lv_obj_t* bar : _mic_bars) {
            if (bar != nullptr) {
                lv_obj_set_y(bar, MicMeterCenterY - 3);
                lv_obj_set_height(bar, 6);
                lv_obj_set_style_opa(bar, LV_OPA_30, LV_PART_MAIN);
            }
        }
        lv_obj_move_foreground(_touch_control);
    }
}

void CodexMicroView::updateConnection(const CodexMicroState& state)
{
    if (state.connected != _functional_enabled) {
        if (state.connected) {
            _functional_enabled = true;
            if (_pairing_screen != nullptr) {
                lv_obj_add_flag(_pairing_screen, LV_OBJ_FLAG_HIDDEN);
            }
            setPage(Page::Command);
            ESP_LOGI(Tag, "pairing complete; functional pages enabled");
        } else {
            releaseActiveInputs();
            _functional_enabled = false;
            setMicActive(false);
            if (_pairing_screen != nullptr) {
                lv_obj_remove_flag(_pairing_screen, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(_pairing_screen);
            }
            ESP_LOGW(Tag, "connection lost; pairing screen active");
        }
    }

    if (!state.connected) {
        const std::size_t active_dot = static_cast<std::size_t>((lv_tick_get() / 350U) % _pairing_dots.size());
        for (std::size_t index = 0; index < _pairing_dots.size(); ++index) {
            if (_pairing_dots[index] != nullptr) {
                lv_obj_set_style_opa(_pairing_dots[index], index == active_dot ? LV_OPA_COVER : LV_OPA_30,
                                     LV_PART_MAIN);
            }
        }
        if (_pairing_core != nullptr) {
            lv_obj_set_style_opa(_pairing_core, active_dot == 1 ? LV_OPA_60 : LV_OPA_COVER, LV_PART_MAIN);
        }
        if (_pairing_pulse != nullptr) {
            lv_obj_set_style_shadow_opa(_pairing_pulse, active_dot == 1 ? LV_OPA_20 : LV_OPA_10, LV_PART_MAIN);
        }
    }
}

void CodexMicroView::updateAmbientLighting(const CodexMicroLight& light)
{
    if (light.effect == CodexMicroLightEffect::Off || light.color == 0 || light.brightness <= 0.01f) {
        if (_ambient_visible) {
            for (lv_obj_t* layer : _ambient_layers) {
                lv_obj_set_style_arc_opa(layer, LV_OPA_TRANSP, LV_PART_INDICATOR);
            }
            _ambient_visible = false;
        }
        return;
    }

    const uint8_t base_brightness =
        static_cast<uint8_t>(std::lround(std::clamp(light.brightness, 0.0f, 1.0f) * 255.0f));
    if (!_ambient_visible || light.color != _ambient_base_color || base_brightness != _ambient_base_brightness) {
        const float brightness = static_cast<float>(base_brightness) / 255.0f;
        const uint32_t color   = softenedAmbientColor(light.color, brightness);
        for (std::size_t index = 0; index < _ambient_layers.size(); ++index) {
            lv_obj_set_style_arc_color(_ambient_layers[index], lv_color_hex(color), LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(_ambient_layers[index], AmbientLayerOpacity[index], LV_PART_INDICATOR);
        }
        _ambient_base_color      = light.color;
        _ambient_base_brightness = base_brightness;
        _ambient_visible         = true;
    }
}

void CodexMicroView::updateCommandLighting(const CodexMicroState& state)
{
    const CodexMicroLight& light = state.keys;
    const bool enabled     = light.effect != CodexMicroLightEffect::Off && light.color != 0 && light.brightness > 0.01f;
    const float seconds    = static_cast<float>(lv_tick_get()) / 1000.0f;
    const float brightness = animatedBrightness(light, seconds);
    const uint32_t color   = enabled ? scaledColor(light.color, brightness) : 0xFFFFFA;
    int snake_slot         = -1;
    if (enabled && light.effect == CodexMicroLightEffect::Snake) {
        const float speed = light.speed > 0.05f ? light.speed : 0.5f;
        snake_slot = static_cast<int>(seconds * (2.0f + 6.0f * speed)) % static_cast<int>(CommandControls.size());
    }

    bool visuals_changed = _page_dirty;
    for (std::size_t i = 0; i < CommandControls.size(); ++i) {
        const bool lit           = enabled && (snake_slot < 0 || static_cast<int>(i) == snake_slot);
        visuals_changed          = visuals_changed || _command_lit[i] != lit || _command_light_colors[i] != color;
        _command_lit[i]          = lit;
        _command_light_colors[i] = color;
    }
    if (visuals_changed && _page_roots[static_cast<std::size_t>(Page::Command)] != nullptr) {
        lv_obj_invalidate(_page_roots[static_cast<std::size_t>(Page::Command)]);
    }
    _command_last_update_tick = lv_tick_get();
}

void CodexMicroView::updateAgentLights(const CodexMicroState& state)
{
    const float seconds = static_cast<float>(lv_tick_get()) / 1000.0f;
    for (std::size_t i = 0; i < state.threads.size(); ++i) {
        const CodexMicroLight& light = state.threads[i];
        const float brightness       = animatedBrightness(light, seconds);
        const bool assigned          = lightAssigned(light);
        const bool visible           = assigned && light.brightness > 0.01f;
        const uint32_t color         = visible ? scaledColor(light.color, brightness) : AgentOff;
        if (_agent_dots[i] != nullptr) {
            stylePanel(_agent_dots[i], color, visible ? color : 0x979E9A, LV_RADIUS_CIRCLE, 1);
            lv_obj_set_style_shadow_width(_agent_dots[i], visible ? 4 : 0, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(_agent_dots[i], visible ? LV_OPA_20 : LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_shadow_color(_agent_dots[i], lv_color_hex(color), LV_PART_MAIN);
        }
        if (_agent_labels[i] != nullptr) {
            const char* label = lightStatus(light);
            if (std::strcmp(lv_label_get_text(_agent_labels[i]), label) != 0) {
                lv_label_set_text(_agent_labels[i], label);
            }
        }
    }
    _agent_last_update_tick = lv_tick_get();
}

bool CodexMicroView::interactionActive() const
{
    const auto key_active = [](const KeyContext& context) { return context.active; };
    return _joystick_active || _dial_pressed || _touch_pressed || _mic_active ||
           std::any_of(_command_contexts.begin(), _command_contexts.end(), key_active) ||
           std::any_of(_agent_contexts.begin(), _agent_contexts.end(), key_active);
}

void CodexMicroView::invalidateCommandSegment(std::size_t slot)
{
    lv_obj_t* page = _page_roots[static_cast<std::size_t>(Page::Command)];
    if (page == nullptr || slot >= _command_contexts.size()) {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(page, &coords);
    const lv_area_t area = commandSegmentArea(coords.x1 + DisplayCenter, coords.y1 + DisplayCenter, slot);
    lv_obj_invalidate_area(page, &area);
}

void CodexMicroView::updateMicMeter()
{
    if (!_mic_active) {
        return;
    }
    const uint32_t tick = lv_tick_get();
    if (_mic_last_update_tick != 0 && tick - _mic_last_update_tick < 120U) {
        return;
    }
    _mic_last_update_tick = tick;

    const float raw_level = GetHAL().getMicrophoneLevel();
    const float smoothing = raw_level > _mic_smoothed_level ? 0.58f : 0.18f;
    _mic_smoothed_level += (raw_level - _mic_smoothed_level) * smoothing;

    constexpr float Center        = 4.0f;
    constexpr int MicMeterCenterY = 307;
    for (std::size_t index = 0; index < _mic_bars.size(); ++index) {
        lv_obj_t* bar = _mic_bars[index];
        if (bar == nullptr) {
            continue;
        }
        const float falloff = std::fabs(static_cast<float>(index) - Center) / Center;
        const float amount  = std::clamp(_mic_smoothed_level * (1.0f - falloff * 0.34f), 0.0f, 1.0f);
        const int height    = std::max(6, static_cast<int>(std::lround(54.0f * amount)));
        lv_obj_set_y(bar, MicMeterCenterY - height / 2);
        lv_obj_set_height(bar, height);
        lv_obj_set_style_opa(bar, static_cast<lv_opa_t>(76 + amount * 179.0f), LV_PART_MAIN);
    }
}

void CodexMicroView::update(const CodexMicroState& state)
{
    const uint32_t tick = lv_tick_get();
    updateDialReturn();
    updateMicMeter();
    const bool state_changed      = state.revision != _last_state_revision;
    const int8_t connection_phase = !state.connected ? static_cast<int8_t>((tick / 350U) % 3U) : 3;
    if (state_changed || connection_phase != _last_connection_phase) {
        updateConnection(state);
        _last_connection_phase = connection_phase;
    }

    const bool input_active = interactionActive();
    if (!sameLight(state.ambient, _ambient_light)) {
        _ambient_light = state.ambient;
        updateAmbientLighting(_ambient_light);
    }

    if (_dial_host_press_active && static_cast<int32_t>(lv_tick_get() - _dial_release_tick) >= 0) {
        GetCodexMicroBle().sendKey(CodexMicroControl::EncoderPress, CodexMicroKeyAction::Release);
        _dial_host_press_active = false;
    }

    if (_page == Page::Command) {
        const bool keys_animated    = state.keys.effect == CodexMicroLightEffect::Breath ||
                                      state.keys.effect == CodexMicroLightEffect::ShallowBreath ||
                                      state.keys.effect == CodexMicroLightEffect::Snake;
        const bool keys_refresh_due = tick - _command_last_update_tick >= AnimatedLightingRefreshPeriodMs;
        if (state_changed || _page_dirty || (keys_animated && keys_refresh_due && !input_active)) {
            updateCommandLighting(state);
        }
    } else if (_page == Page::Agent) {
        const bool threads_animated =
            std::any_of(state.threads.begin(), state.threads.end(), [](const CodexMicroLight& light) {
                return light.effect == CodexMicroLightEffect::Breath ||
                       light.effect == CodexMicroLightEffect::ShallowBreath;
            });
        const bool threads_refresh_due = tick - _agent_last_update_tick >= AnimatedLightingRefreshPeriodMs;
        if (state_changed || _page_dirty || (threads_animated && threads_refresh_due && !input_active)) {
            updateAgentLights(state);
        }
    }
    _page_dirty          = false;
    _last_state_revision = state.revision;
}

void CodexMicroView::releaseActiveInputs()
{
    for (KeyContext& context : _command_contexts) {
        if (context.active) {
            GetCodexMicroBle().sendKey(context.control, CodexMicroKeyAction::Release, context.agent);
        }
        context.active = false;
    }
    for (KeyContext& context : _agent_contexts) {
        if (context.active) {
            GetCodexMicroBle().sendKey(context.control, CodexMicroKeyAction::Release, context.agent);
        }
        context.active = false;
    }
    if (_page_roots[static_cast<std::size_t>(Page::Command)] != nullptr) {
        lv_obj_invalidate(_page_roots[static_cast<std::size_t>(Page::Command)]);
    }
    releaseJoystick();
    if (_dial_host_press_active) {
        GetCodexMicroBle().sendKey(CodexMicroControl::EncoderPress, CodexMicroKeyAction::Release);
        _dial_host_press_active = false;
    }
    resetDial();
}

void CodexMicroView::keyEvent(lv_event_t* event)
{
    auto* context = static_cast<KeyContext*>(lv_event_get_user_data(event));
    if (context == nullptr || context->owner == nullptr) {
        return;
    }
    if (context->owner->_input_suppressed) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED && !context->owner->_functional_enabled) {
        return;
    }
    if (code == LV_EVENT_PRESSED && !context->active) {
        context->active = true;
        playFeedback(context->control, context->agent);
        GetCodexMicroBle().sendKey(context->control, CodexMicroKeyAction::Press, context->agent);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && context->active) {
        GetCodexMicroBle().sendKey(context->control, CodexMicroKeyAction::Release, context->agent);
        context->active = false;
    }
    if (context->agent < 0) {
        const auto begin       = context->owner->_command_contexts.begin();
        const std::size_t slot = static_cast<std::size_t>(context - &(*begin));
        context->owner->invalidateCommandSegment(slot);
    }
}

void CodexMicroView::commandDeckEvent(lv_event_t* event)
{
    auto* owner = static_cast<CodexMicroView*>(lv_event_get_user_data(event));
    if (owner == nullptr || lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }

    lv_obj_t* object  = lv_event_get_target_obj(event);
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);
    const int center_x = coords.x1 + DisplayCenter;
    const int center_y = coords.y1 + DisplayCenter;

    lv_draw_vector_dsc_t* vector = lv_draw_vector_dsc_create(layer);
    lv_vector_path_t* path       = owner->_command_path;
    if (vector == nullptr || path == nullptr) {
        ESP_LOGE(Tag, "unable to draw command vector path");
        if (vector != nullptr) {
            lv_draw_vector_dsc_delete(vector);
        }
        return;
    }

    // Fast / Approve / Decline / Fork map to top / left / right / bottom.
    constexpr std::array<uint8_t, 4> QuarterTurns = {0, 3, 1, 2};
    lv_draw_vector_dsc_set_stroke_join(vector, LV_VECTOR_STROKE_JOIN_ROUND);
    for (std::size_t index = 0; index < QuarterTurns.size(); ++index) {
        const lv_area_t segment_area = commandSegmentArea(center_x, center_y, index);
        const lv_area_t& clip_area   = layer->buf_area;
        const bool intersects        = segment_area.x1 <= clip_area.x2 && segment_area.x2 >= clip_area.x1 &&
                                       segment_area.y1 <= clip_area.y2 && segment_area.y2 >= clip_area.y1;
        if (!intersects) {
            continue;
        }
        const bool active     = owner->_command_contexts[index].active;
        const uint32_t border = owner->_command_lit[index] ? owner->_command_light_colors[index] : KeyBorder;

        buildCommandSegmentPath(path, center_x, center_y + static_cast<int>(CommandShadowOffsetY), QuarterTurns[index]);
        lv_draw_vector_dsc_set_fill_color(vector, lv_color_black());
        lv_draw_vector_dsc_set_fill_opa(vector, CommandShadowFillOpacity);
        lv_draw_vector_dsc_set_stroke_color(vector, lv_color_black());
        lv_draw_vector_dsc_set_stroke_width(vector, CommandShadowStrokeWidth);
        lv_draw_vector_dsc_set_stroke_opa(vector, CommandShadowStrokeOpacity);
        lv_draw_vector_dsc_add_path(vector, path);
        lv_vector_path_clear(path);

        buildCommandSegmentPath(path, center_x, center_y, QuarterTurns[index]);
        lv_grad_stop_t fill_stops[2] = {};
        fill_stops[0].color          = lv_color_hex(active ? KeyPressedHigh : KeyHigh);
        fill_stops[0].opa            = LV_OPA_COVER;
        fill_stops[0].frac           = 0;
        fill_stops[1].color          = lv_color_hex(active ? KeyPressedLow : Key);
        fill_stops[1].opa            = LV_OPA_COVER;
        fill_stops[1].frac           = 255;
        const float gradient_start_x = static_cast<float>(center_x) - CommandPathCenter;
        const float gradient_start_y = static_cast<float>(center_y) - CommandPathCenter;
        lv_draw_vector_dsc_set_fill_opa(vector, LV_OPA_COVER);
        lv_draw_vector_dsc_set_fill_linear_gradient(vector, gradient_start_x, gradient_start_y,
                                                    gradient_start_x + CommandCanvasSize,
                                                    gradient_start_y + CommandCanvasSize);
        lv_draw_vector_dsc_set_fill_gradient_color_stops(vector, fill_stops, 2);
        lv_draw_vector_dsc_set_fill_gradient_spread(vector, LV_VECTOR_GRADIENT_SPREAD_PAD);
        lv_draw_vector_dsc_set_stroke_color(vector, lv_color_hex(border));
        lv_draw_vector_dsc_set_stroke_width(vector, CommandBorderStrokeWidth);
        lv_draw_vector_dsc_set_stroke_opa(vector, owner->_command_lit[index] ? LV_OPA_COVER : LV_OPA_TRANSP);
        lv_draw_vector_dsc_add_path(vector, path);
        lv_vector_path_clear(path);
    }

    lv_draw_vector(vector);
    lv_draw_vector_dsc_delete(vector);
}

void CodexMicroView::joystickHitTestEvent(lv_event_t* event)
{
    lv_hit_test_info_t* info = lv_event_get_hit_test_info(event);
    lv_obj_t* object         = lv_event_get_target_obj(event);
    if (info == nullptr || info->point == nullptr || object == nullptr) {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);
    const int dx = info->point->x - (coords.x1 + JoystickSize / 2);
    const int dy = info->point->y - (coords.y1 + JoystickSize / 2);
    info->res    = dx * dx + dy * dy <= JoystickPressRadius * JoystickPressRadius;
}

void CodexMicroView::iconEvent(lv_event_t* event)
{
    auto* context = static_cast<IconContext*>(lv_event_get_user_data(event));
    if (context == nullptr || context->owner == nullptr || lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }

    lv_obj_t* object  = lv_event_get_target_obj(event);
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);
    const int cx         = (coords.x1 + coords.x2) / 2;
    const int cy         = (coords.y1 + coords.y2) / 2;
    const uint32_t color = context->icon == Icon::Fingerprint
                               ? (lv_obj_has_state(object, LV_STATE_PRESSED) ? Green : Fingerprint)
                               : KeyInk;

    switch (context->icon) {
        case Icon::Fast:
            drawLine(layer, cx + 4, cy - 24, cx - 12, cy - 2, 3, color);
            drawLine(layer, cx - 12, cy - 2, cx - 1, cy - 2, 3, color);
            drawLine(layer, cx - 1, cy - 2, cx - 5, cy + 24, 3, color);
            drawLine(layer, cx - 5, cy + 24, cx + 14, cy - 5, 3, color);
            drawLine(layer, cx + 14, cy - 5, cx + 3, cy - 5, 3, color);
            drawLine(layer, cx + 3, cy - 5, cx + 4, cy - 24, 3, color);
            break;
        case Icon::Approve:
            drawArc(layer, cx, cy, 23, 0, 360, 3, color);
            drawLine(layer, cx - 12, cy, cx - 3, cy + 9, 3, color);
            drawLine(layer, cx - 3, cy + 9, cx + 14, cy - 11, 3, color);
            break;
        case Icon::Decline:
            drawArc(layer, cx, cy, 23, 0, 360, 3, color);
            drawLine(layer, cx - 10, cy - 10, cx + 10, cy + 10, 3, color);
            drawLine(layer, cx + 10, cy - 10, cx - 10, cy + 10, 3, color);
            break;
        case Icon::NewChat:
            drawLine(layer, cx - 23, cy, cx - 12, cy, 3, color);
            drawLine(layer, cx - 12, cy, cx + 8, cy - 15, 3, color);
            drawLine(layer, cx - 12, cy, cx + 8, cy + 15, 3, color);
            drawLine(layer, cx + 8, cy - 15, cx + 20, cy - 15, 3, color);
            drawLine(layer, cx + 8, cy + 15, cx + 20, cy + 15, 3, color);
            drawLine(layer, cx + 20, cy - 15, cx + 14, cy - 21, 3, color);
            drawLine(layer, cx + 20, cy - 15, cx + 14, cy - 9, 3, color);
            drawLine(layer, cx + 20, cy + 15, cx + 14, cy + 9, 3, color);
            drawLine(layer, cx + 20, cy + 15, cx + 14, cy + 21, 3, color);
            break;
        case Icon::Mic:
            drawOutline(layer, cx - 10, cy - 25, cx + 10, cy + 8, 10, 3, color);
            drawArc(layer, cx, cy + 4, 18, 0, 180, 3, color);
            drawLine(layer, cx, cy + 22, cx, cy + 29, 3, color);
            drawLine(layer, cx - 11, cy + 29, cx + 11, cy + 29, 3, color);
            break;
        case Icon::Fingerprint:
            drawArc(layer, cx, cy + 9, 20, 195, 345, 2, color);
            drawArc(layer, cx, cy + 9, 15, 190, 20, 2, color);
            drawArc(layer, cx, cy + 9, 10, 175, 45, 2, color);
            drawArc(layer, cx, cy + 9, 5, 160, 55, 2, color);
            drawLine(layer, cx - 18, cy + 9, cx - 20, cy + 20, 2, color);
            drawLine(layer, cx + 9, cy + 11, cx + 5, cy + 25, 2, color);
            break;
    }
}

void CodexMicroView::dialTrackEvent(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }
    lv_obj_t* object  = lv_event_get_target_obj(event);
    lv_layer_t* layer = lv_event_get_layer(event);
    lv_area_t coords;
    lv_obj_get_coords(object, &coords);
    const int center_x = coords.x1 + DisplayCenter;
    const int center_y = coords.y1 + DisplayCenter;

    drawArc(layer, center_x, center_y, DialRadius, DialStartDegrees, 359, 8, 0x000000);
    drawArc(layer, center_x, center_y, DialRadius, 0, DialEndDegrees - 360, 8, 0x000000);
    drawArc(layer, center_x, center_y, DialRadius, DialStartDegrees, 359, 4, ArcTrack);
    drawArc(layer, center_x, center_y, DialRadius, 0, DialEndDegrees - 360, 4, ArcTrack);
}

void CodexMicroView::dialHitTestEvent(lv_event_t* event)
{
    auto* owner              = static_cast<CodexMicroView*>(lv_event_get_user_data(event));
    lv_hit_test_info_t* info = lv_event_get_hit_test_info(event);
    if (owner == nullptr || owner->_dial_thumb == nullptr || info == nullptr || info->point == nullptr) {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(owner->_dial_thumb, &coords);
    const int dx = info->point->x - ((coords.x1 + coords.x2) / 2);
    const int dy = info->point->y - ((coords.y1 + coords.y2) / 2);
    info->res    = dx * dx + dy * dy <= DialPressRadius * DialPressRadius;
}

void CodexMicroView::updateJoystickFromPoint(const lv_point_t& point)
{
    if (_joystick == nullptr || _joystick_knob == nullptr) {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(_joystick, &coords);
    const float dx              = static_cast<float>(point.x - (coords.x1 + JoystickSize / 2));
    const float dy              = static_cast<float>(point.y - (coords.y1 + JoystickSize / 2));
    constexpr float MaxTravel   = static_cast<float>(JoystickTravelLimit);
    const float raw_distance    = std::hypot(dx, dy);
    const float visual_distance = std::min(1.0f, raw_distance / MaxTravel);
    const float host_distance   = std::clamp(
        (visual_distance - JoystickHostDeadZone) / (JoystickHostFullScale - JoystickHostDeadZone), 0.0f, 1.0f);
    float angle = _joystick_angle;
    if (raw_distance >= 0.5f) {
        angle = std::atan2(dy, dx) / 6.28318530718f;
        if (angle < 0.0f) {
            angle += 1.0f;
        }
    }
    const float scale = raw_distance > MaxTravel ? MaxTravel / raw_distance : 1.0f;
    const int knob_x  = JoystickNeutralPosition + static_cast<int>(std::lround(dx * scale));
    const int knob_y  = JoystickNeutralPosition + static_cast<int>(std::lround(dy * scale));
    lv_obj_set_pos(_joystick_knob, knob_x, knob_y);

    const float angle_delta   = std::fabs(angle - _joystick_angle);
    const float wrapped_delta = std::min(angle_delta, 1.0f - angle_delta);
    const bool report_changed = std::fabs(host_distance - _joystick_distance) >= JoystickDistanceSendThreshold ||
                                (host_distance > 0.0f && wrapped_delta >= JoystickAngleSendThreshold);
    const uint32_t tick       = lv_tick_get();
    const bool report_due = _joystick_last_send_tick == 0 || tick - _joystick_last_send_tick >= JoystickReportPeriodMs;
    if (report_changed && report_due) {
        _joystick_angle          = angle;
        _joystick_distance       = host_distance;
        _joystick_last_send_tick = tick;
        GetCodexMicroBle().sendJoystick(angle, host_distance);
    }
}

void CodexMicroView::releaseJoystick()
{
    if (_joystick_active && _joystick_distance > 0.0f) {
        GetCodexMicroBle().sendJoystick(_joystick_angle, 0.0f);
    }
    _joystick_active         = false;
    _joystick_distance       = 0.0f;
    _joystick_last_send_tick = 0;
    if (_joystick_knob != nullptr) {
        lv_obj_set_pos(_joystick_knob, JoystickNeutralPosition, JoystickNeutralPosition);
    }
}

void CodexMicroView::joystickEvent(lv_event_t* event)
{
    auto* owner = static_cast<CodexMicroView*>(lv_event_get_user_data(event));
    if (owner == nullptr) {
        return;
    }
    if (owner->_input_suppressed) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (!owner->_functional_enabled) {
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            owner->releaseJoystick();
        }
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t* indev = lv_event_get_indev(event);
        if (indev == nullptr) {
            return;
        }
        if (!owner->_joystick_active) {
            owner->_joystick_active = true;
            playFeedback(CodexMicroControl::EncoderPress);
        }
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        owner->updateJoystickFromPoint(point);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        owner->releaseJoystick();
    }
}

void CodexMicroView::setDialVisualStep(float step)
{
    if (_dial == nullptr || _dial_thumb == nullptr) {
        return;
    }
    _dial_visual_step = std::clamp(step, 0.0f, static_cast<float>(DialStepCount));
    const float degrees =
        static_cast<float>(DialStartDegrees) +
        static_cast<float>(DialEndDegrees - DialStartDegrees) * (_dial_visual_step / static_cast<float>(DialStepCount));
    const float radians = degrees * 0.01745329252f;
    const int center_x  = DisplayCenter + static_cast<int>(std::lround(std::cos(radians) * DialRadius));
    const int center_y  = DisplayCenter + static_cast<int>(std::lround(std::sin(radians) * DialRadius));
    lv_obj_set_pos(_dial_thumb, center_x - DialThumbWidth / 2, center_y - DialThumbHeight / 2);

    float rotation = std::fmod(degrees + 90.0f, 360.0f);
    if (rotation < 0.0f) {
        rotation += 360.0f;
    }
    lv_obj_set_style_transform_rotation(_dial_thumb, static_cast<int>(std::lround(rotation * 10.0f)), LV_PART_MAIN);
}

void CodexMicroView::updateDialFromPoint(const lv_point_t& point)
{
    if (_root == nullptr) {
        return;
    }
    lv_area_t root_coords;
    lv_obj_get_coords(_root, &root_coords);
    const float dx    = static_cast<float>(point.x - (root_coords.x1 + DisplayCenter));
    const float dy    = static_cast<float>(point.y - (root_coords.y1 + DisplayCenter));
    float raw_degrees = std::atan2(dy, dx) * 57.295779513f;
    if (raw_degrees < 0.0f) {
        raw_degrees += 360.0f;
    }

    const float current_degrees =
        static_cast<float>(DialStartDegrees) +
        static_cast<float>(DialEndDegrees - DialStartDegrees) * (_dial_visual_step / static_cast<float>(DialStepCount));
    float unwrapped = raw_degrees;
    for (float candidate : {raw_degrees - 360.0f, raw_degrees, raw_degrees + 360.0f}) {
        if (std::fabs(candidate - current_degrees) < std::fabs(unwrapped - current_degrees)) {
            unwrapped = candidate;
        }
    }
    const float clamped_degrees =
        std::clamp(unwrapped, static_cast<float>(DialStartDegrees), static_cast<float>(DialEndDegrees));
    const int next_step =
        static_cast<int>(std::lround((clamped_degrees - static_cast<float>(DialStartDegrees)) /
                                     static_cast<float>(DialEndDegrees - DialStartDegrees) * DialStepCount));
    if (next_step == _dial_step) {
        return;
    }

    _dial_rotating          = true;
    const int direction     = next_step > _dial_step ? 1 : -1;
    const int emitted_steps = std::abs(next_step - _dial_step);
    _dial_step              = next_step;
    if (emitted_steps > 0) {
        // Queue the batch so a fast drag never performs a burst of HID writes
        // inside LVGL's touch callback. The worker still emits every official
        // encoder detent in order.
        GetCodexMicroBle().sendEncoderSteps(direction, static_cast<uint16_t>(emitted_steps));
        const uint32_t tick = lv_tick_get();
        if (_dial_last_feedback_tick == 0 || tick - _dial_last_feedback_tick >= DialFeedbackPeriodMs) {
            playDialRatchetFeedback(direction);
            _dial_last_feedback_tick = tick;
        }
        ESP_LOGD(Tag, "arc-slider rotate direction=%d steps=%d", direction, emitted_steps);
    }
    setDialVisualStep(static_cast<float>(_dial_step));
}

void CodexMicroView::beginDialReturn()
{
    if (std::fabs(_dial_visual_step - static_cast<float>(DialCenterStep)) < 0.01f) {
        resetDial();
        return;
    }
    _dial_returning         = true;
    _dial_return_start_step = _dial_visual_step;
    _dial_return_tick       = lv_tick_get();
    if (_dial_thumb != nullptr) {
        stylePanel(_dial_thumb, ArcThumbReturning, ArcThumbReturningBorder, 9, 2);
    }
}

void CodexMicroView::updateDialReturn()
{
    if (!_dial_returning) {
        return;
    }
    const uint32_t elapsed = lv_tick_get() - _dial_return_tick;
    const float progress   = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(DialReturnDurationMs));
    const float remaining  = 1.0f - progress;
    const float eased      = 1.0f - remaining * remaining * remaining;
    const float step = _dial_return_start_step + (static_cast<float>(DialCenterStep) - _dial_return_start_step) * eased;
    setDialVisualStep(step);
    if (progress >= 1.0f) {
        resetDial();
    }
}

void CodexMicroView::resetDial()
{
    _dial_pressed            = false;
    _dial_rotating           = false;
    _dial_returning          = false;
    _dial_step               = DialCenterStep;
    _dial_last_feedback_tick = 0;
    setDialVisualStep(static_cast<float>(DialCenterStep));
    if (_dial_thumb != nullptr) {
        stylePanel(_dial_thumb, ArcThumb, ArcThumbBorder, 9, 2);
    }
}

void CodexMicroView::releaseDialGesture()
{
    if (!_dial_pressed) {
        return;
    }
    const uint32_t held = lv_tick_get() - _dial_press_tick;
    if (!_dial_rotating) {
        if (_dial_host_press_active) {
            GetCodexMicroBle().sendKey(CodexMicroControl::EncoderPress, CodexMicroKeyAction::Release);
        }
        GetCodexMicroBle().sendKey(CodexMicroControl::EncoderPress, CodexMicroKeyAction::Press);
        _dial_host_press_active = true;
        _dial_release_tick      = lv_tick_get() + (held >= 500U ? 520U : 45U);
        ESP_LOGI(Tag, "arc-slider %s gesture=%ums", held >= 500U ? "settings-hold" : "press",
                 static_cast<unsigned>(held));
    }
    _dial_pressed  = false;
    _dial_rotating = false;
    beginDialReturn();
}

void CodexMicroView::dialEvent(lv_event_t* event)
{
    auto* owner = static_cast<CodexMicroView*>(lv_event_get_user_data(event));
    if (owner == nullptr) {
        return;
    }
    if (owner->_input_suppressed) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (!owner->_functional_enabled) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        owner->_dial_returning  = false;
        owner->_dial_step       = static_cast<int>(std::lround(owner->_dial_visual_step));
        owner->_dial_pressed    = true;
        owner->_dial_rotating   = false;
        owner->_dial_press_tick = lv_tick_get();
        if (owner->_dial_thumb != nullptr) {
            owner->stylePanel(owner->_dial_thumb, ArcThumbActive, ArcThumbActiveBorder, 9, 2);
        }
        playFeedback(CodexMicroControl::EncoderPress);
    } else if (code == LV_EVENT_PRESSING && owner->_dial_pressed) {
        lv_indev_t* indev = lv_event_get_indev(event);
        if (indev == nullptr) {
            return;
        }
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        owner->updateDialFromPoint(point);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        owner->releaseDialGesture();
    }
}

void CodexMicroView::touchEvent(lv_event_t* event)
{
    auto* owner = static_cast<CodexMicroView*>(lv_event_get_user_data(event));
    if (owner == nullptr) {
        return;
    }
    if (owner->_input_suppressed) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(event);
    if (!owner->_functional_enabled) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        owner->_touch_pressed    = true;
        owner->_touch_press_tick = lv_tick_get();
        playFeedback(CodexMicroControl::EncoderPress);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && owner->_touch_pressed) {
        const uint32_t held   = lv_tick_get() - owner->_touch_press_tick;
        owner->_touch_pressed = false;
        if (held >= 3000U) {
            const bool requested = GetCodexMicroBle().resetPairing();
            ESP_LOGW(Tag, "touch sensor pairing-reset duration=%ums requested=%d", static_cast<unsigned>(held),
                     requested ? 1 : 0);
        } else {
            ESP_LOGI(Tag, "touch sensor tap duration=%ums (single BLE channel)", static_cast<unsigned>(held));
        }
    }
}

}  // namespace view
