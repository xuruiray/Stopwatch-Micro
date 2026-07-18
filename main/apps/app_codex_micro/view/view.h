/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <hal/ble/codex_micro_ble.h>
#include <lvgl.h>

namespace view {

class CodexMicroView {
public:
    enum class Page : uint8_t {
        Command = 0,
        Agent,
    };

    ~CodexMicroView();

    void init(lv_obj_t* parent);
    void update(const CodexMicroState& state);
    void togglePage();
    void setMicActive(bool active);
    bool ready() const;
    bool functionalEnabled() const;
    bool micActive() const;
    Page currentPage() const;
    bool setPageForDebug(Page page);
    void setInputSuppressed(bool suppressed);

private:
    enum class Icon : uint8_t {
        Fast,
        Approve,
        Decline,
        NewChat,
        Mic,
        Fingerprint,
    };

    struct KeyContext {
        CodexMicroView* owner     = nullptr;
        CodexMicroControl control = CodexMicroControl::Agent1;
        int8_t agent              = -1;
        bool active               = false;
    };

    struct IconContext {
        CodexMicroView* owner = nullptr;
        Icon icon             = Icon::Fast;
    };

    static void keyEvent(lv_event_t* event);
    static void iconEvent(lv_event_t* event);
    static void commandDeckEvent(lv_event_t* event);
    static void joystickHitTestEvent(lv_event_t* event);
    static void joystickEvent(lv_event_t* event);
    static void dialTrackEvent(lv_event_t* event);
    static void dialHitTestEvent(lv_event_t* event);
    static void dialEvent(lv_event_t* event);
    static void touchEvent(lv_event_t* event);

    void setPage(Page page);
    void renderPage();
    void renderCommand(lv_obj_t* parent);
    void renderNavigation(lv_obj_t* parent);
    void renderAgent(lv_obj_t* parent);
    void releaseActiveInputs();

    lv_obj_t* createPageRoot();
    lv_obj_t* createKeyButton(lv_obj_t* parent, std::array<lv_obj_t*, 6>& buttons, std::array<KeyContext, 6>& contexts,
                              std::size_t slot, int x, int y, int width, int height, CodexMicroControl control,
                              int8_t agent, uint32_t background, uint32_t border, int radius);
    void createCommandButton(lv_obj_t* parent, std::size_t slot, int x, int y, int width, int height, int radius,
                             CodexMicroControl control, Icon icon);
    void stylePanel(lv_obj_t* object, uint32_t background, uint32_t border, int radius, int border_width = 1);
    void updateConnection(const CodexMicroState& state);
    void updateAmbientLighting(const CodexMicroLight& light);
    void updateCommandLighting(const CodexMicroState& state);
    void updateAgentLights(const CodexMicroState& state);
    void updateMicMeter();
    bool interactionActive() const;
    void invalidateCommandSegment(std::size_t slot);
    void updateJoystickFromPoint(const lv_point_t& point);
    void releaseJoystick();
    void setDialVisualStep(float step);
    void updateDialFromPoint(const lv_point_t& point);
    void beginDialReturn();
    void updateDialReturn();
    void resetDial();
    void releaseDialGesture();

    lv_obj_t* _root                           = nullptr;
    lv_vector_path_t* _command_path           = nullptr;
    std::array<lv_obj_t*, 14> _ambient_layers = {};
    lv_obj_t* _touch_control                  = nullptr;
    lv_obj_t* _mic_screen                     = nullptr;
    lv_obj_t* _pairing_screen                 = nullptr;
    lv_obj_t* _pairing_pulse                  = nullptr;
    lv_obj_t* _pairing_core                   = nullptr;
    std::array<lv_obj_t*, 9> _mic_bars        = {};
    std::array<lv_obj_t*, 3> _pairing_dots    = {};
    std::array<lv_obj_t*, 2> _page_roots      = {};

    std::array<KeyContext, 4> _command_contexts   = {};
    std::array<bool, 4> _command_lit              = {};
    std::array<uint32_t, 4> _command_light_colors = {};
    std::array<lv_obj_t*, 6> _agent_buttons       = {};
    std::array<lv_obj_t*, 6> _agent_labels        = {};
    std::array<lv_obj_t*, 6> _agent_dots          = {};
    std::array<KeyContext, 6> _agent_contexts     = {};
    std::array<IconContext, 6> _icon_contexts     = {};

    lv_obj_t* _joystick               = nullptr;
    lv_obj_t* _joystick_knob          = nullptr;
    bool _joystick_active             = false;
    float _joystick_angle             = 0.0f;
    float _joystick_distance          = 0.0f;
    uint32_t _joystick_last_send_tick = 0;

    lv_obj_t* _dial                   = nullptr;
    lv_obj_t* _dial_thumb             = nullptr;
    bool _dial_pressed                = false;
    bool _dial_rotating               = false;
    bool _dial_returning              = false;
    bool _dial_host_press_active      = false;
    int _dial_step                    = 18;
    float _dial_visual_step           = 18.0f;
    float _dial_return_start_step     = 18.0f;
    uint32_t _dial_press_tick         = 0;
    uint32_t _dial_release_tick       = 0;
    uint32_t _dial_return_tick        = 0;
    uint32_t _dial_last_feedback_tick = 0;

    bool _touch_pressed                = false;
    bool _mic_active                   = false;
    float _mic_smoothed_level          = 0.0f;
    uint32_t _mic_last_update_tick     = 0;
    uint32_t _command_last_update_tick = 0;
    uint32_t _agent_last_update_tick   = 0;
    CodexMicroLight _ambient_light     = {};
    uint32_t _ambient_base_color       = UINT32_MAX;
    uint8_t _ambient_base_brightness   = UINT8_MAX;
    bool _ambient_visible              = false;
    uint32_t _touch_press_tick         = 0;
    uint32_t _last_state_revision      = UINT32_MAX;
    int8_t _last_connection_phase      = -1;
    bool _functional_enabled           = false;
    bool _input_suppressed             = false;
    bool _page_dirty                   = true;
    Page _page                         = Page::Command;
};

}  // namespace view
