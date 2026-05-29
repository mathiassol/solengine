#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "sol/input/input_manager.h"
#include "sol/project/project.h"
#include "sol/log.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

namespace sol {

// ---------------------------------------------------------------------------
// Static GLFW scroll callback — one InputManager per process.
// ---------------------------------------------------------------------------
static InputManager* s_scroll_instance = nullptr;

static void glfw_scroll_cb(GLFWwindow*, double /*xoff*/, double yoff) {
    if (s_scroll_instance)
        s_scroll_instance->on_scroll(static_cast<float>(yoff));
}

// ---------------------------------------------------------------------------
// Map "KEY_*" string from project.sol to GLFW key code.
// Mirrors the integer constants set in the Lua Input table.
// ---------------------------------------------------------------------------
static int key_name_to_glfw(const std::string& name) {
    // Single letter: KEY_A..KEY_Z
    if (name.size() == 5 && name.substr(0, 4) == "KEY_") {
        char c = name[4];
        if (c >= 'A' && c <= 'Z') return static_cast<int>(c);   // 65-90
        if (c >= '0' && c <= '9') return static_cast<int>(c);   // 48-57
    }

    if (name == "KEY_SPACE")     return 32;
    if (name == "KEY_ESCAPE")    return 256;
    if (name == "KEY_ENTER")     return 257;
    if (name == "KEY_TAB")       return 258;
    if (name == "KEY_BACKSPACE") return 259;
    if (name == "KEY_RIGHT")     return 262;
    if (name == "KEY_LEFT")      return 263;
    if (name == "KEY_DOWN")      return 264;
    if (name == "KEY_UP")        return 265;
    if (name == "KEY_F1")        return 290;
    if (name == "KEY_F2")        return 291;
    if (name == "KEY_F3")        return 292;
    if (name == "KEY_F4")        return 293;
    if (name == "KEY_F5")        return 294;
    if (name == "KEY_F6")        return 295;
    if (name == "KEY_F7")        return 296;
    if (name == "KEY_F8")        return 297;
    if (name == "KEY_F9")        return 298;
    if (name == "KEY_F10")       return 299;
    if (name == "KEY_F11")       return 300;
    if (name == "KEY_F12")       return 301;
    if (name == "KEY_LSHIFT")    return 340;
    if (name == "KEY_LCTRL")     return 341;
    if (name == "KEY_LALT")      return 342;
    if (name == "KEY_RSHIFT")    return 344;
    if (name == "KEY_RCTRL")     return 345;
    if (name == "KEY_RALT")      return 346;
    if (name == "KEY_COMMA")     return 44;
    if (name == "KEY_PERIOD")    return 46;
    if (name == "KEY_SLASH")     return 47;
    if (name == "KEY_SEMICOLON") return 59;
    if (name == "KEY_EQUAL")     return 61;
    if (name == "KEY_MINUS")     return 45;

    SOL_WARN("InputManager: unknown key name '" + name + "'");
    return -1;
}

// ---------------------------------------------------------------------------
InputManager::InputManager() {
    s_scroll_instance = this;
}

InputManager::~InputManager() {
    if (s_scroll_instance == this)
        s_scroll_instance = nullptr;
}

void InputManager::set_window(GLFWwindow* window) {
    if (window)
        glfwSetScrollCallback(window, glfw_scroll_cb);
}

void InputManager::load_actions(const ProjectConfig& cfg) {
    for (const auto& ac : cfg.input_actions) {
        InputAction action;
        action.name = ac.name;

        for (const auto& b : ac.bindings) {
            ActionBinding ab;
            ab.deadzone = b.deadzone;
            ab.positive = b.positive;

            if (b.type == "key") {
                ab.type = BindingType::Key;
                ab.code = key_name_to_glfw(b.key);
            } else if (b.type == "mouse_button") {
                ab.type = BindingType::MouseButton;
                if (b.button == "RIGHT")       ab.code = 1;
                else if (b.button == "MIDDLE") ab.code = 2;
                else                           ab.code = 0; // LEFT
            } else if (b.type == "mouse_wheel") {
                ab.type = (b.wheel == "DOWN") ? BindingType::MouseWheelDown
                                              : BindingType::MouseWheelUp;
            } else if (b.type == "gamepad_button") {
                ab.type = BindingType::GamepadButton;
                ab.code = b.code;
            } else if (b.type == "gamepad_axis") {
                ab.type = BindingType::GamepadAxis;
                ab.code = b.code;
            }

            action.bindings.push_back(ab);
        }

        m_actions[ac.name]      = std::move(action);
        m_strength[ac.name]     = 0.0f;
        m_prev_strength[ac.name]= 0.0f;
    }

    SOL_INFO("InputManager: loaded " + std::to_string(m_actions.size()) + " actions");
}

// ---------------------------------------------------------------------------
void InputManager::begin_frame() {
    m_prev_strength = m_strength;
    m_scroll        = m_scroll_accumulator;
    m_scroll_accumulator = 0.0f;
}

void InputManager::update(GLFWwindow* window) {
    m_window = window;

    if (!window) {
        m_mouse_delta = {};
        for (auto& [name, s] : m_strength) s = 0.0f;
        return;
    }

    // Mouse delta
    double cx = 0.0, cy = 0.0;
    glfwGetCursorPos(window, &cx, &cy);
    if (m_first_frame) {
        m_last_cursor = {static_cast<float>(cx), static_cast<float>(cy)};
        m_first_frame = false;
        m_mouse_delta = {};
    } else {
        m_mouse_delta = {
            static_cast<float>(cx) - m_last_cursor.x,
            static_cast<float>(cy) - m_last_cursor.y
        };
        m_last_cursor = {static_cast<float>(cx), static_cast<float>(cy)};
    }

    // Gamepad (joystick 0)
    if (glfwJoystickPresent(GLFW_JOYSTICK_1)) {
        int axis_count   = 0;
        int button_count = 0;
        const float*         axes    = glfwGetJoystickAxes   (GLFW_JOYSTICK_1, &axis_count);
        const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &button_count);
        if (axes)    m_gamepad_axes.assign   (axes,    axes    + axis_count);
        if (buttons) m_gamepad_buttons.assign(buttons, buttons + button_count);
    } else {
        m_gamepad_axes.clear();
        m_gamepad_buttons.clear();
    }

    // Sample all action bindings
    for (auto& [name, action] : m_actions) {
        float strength = 0.0f;
        for (const auto& b : action.bindings)
            strength = std::max(strength, sample_binding(b));
        m_strength[name] = strength;
    }
}

float InputManager::sample_binding(const ActionBinding& b) const {
    switch (b.type) {
    case BindingType::Key:
        if (!m_window || b.code < 0) return 0.0f;
        return (glfwGetKey(m_window, b.code) == GLFW_PRESS) ? 1.0f : 0.0f;

    case BindingType::MouseButton:
        if (!m_window) return 0.0f;
        return (glfwGetMouseButton(m_window, b.code) == GLFW_PRESS) ? 1.0f : 0.0f;

    case BindingType::MouseWheelUp:
        return (m_scroll > 0.0f) ? std::min(m_scroll, 1.0f) : 0.0f;

    case BindingType::MouseWheelDown:
        return (m_scroll < 0.0f) ? std::min(-m_scroll, 1.0f) : 0.0f;

    case BindingType::GamepadButton:
        if (b.code < static_cast<int>(m_gamepad_buttons.size()))
            return m_gamepad_buttons[b.code] ? 1.0f : 0.0f;
        return 0.0f;

    case BindingType::GamepadAxis:
        if (b.code < static_cast<int>(m_gamepad_axes.size())) {
            float v = m_gamepad_axes[b.code];
            // Apply direction filter
            if (b.positive ? (v <= b.deadzone) : (v >= -b.deadzone)) return 0.0f;
            float range = 1.0f - b.deadzone;
            if (range < 1e-6f) return 0.0f;
            return std::clamp((std::abs(v) - b.deadzone) / range, 0.0f, 1.0f);
        }
        return 0.0f;

    default:
        return 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Action queries
// ---------------------------------------------------------------------------
bool InputManager::is_pressed(const std::string& action) const {
    auto it = m_strength.find(action);
    return it != m_strength.end() && it->second > 0.5f;
}

bool InputManager::is_just_pressed(const std::string& action) const {
    auto cur = m_strength.find(action);
    auto prv = m_prev_strength.find(action);
    bool now = (cur != m_strength.end()      && cur->second > 0.5f);
    bool was = (prv != m_prev_strength.end() && prv->second > 0.5f);
    return now && !was;
}

bool InputManager::is_just_released(const std::string& action) const {
    auto cur = m_strength.find(action);
    auto prv = m_prev_strength.find(action);
    bool now = (cur != m_strength.end()      && cur->second > 0.5f);
    bool was = (prv != m_prev_strength.end() && prv->second > 0.5f);
    return !now && was;
}

float InputManager::get_strength(const std::string& action) const {
    auto it = m_strength.find(action);
    return it != m_strength.end() ? it->second : 0.0f;
}

float InputManager::get_axis(const std::string& neg, const std::string& pos) const {
    return get_strength(pos) - get_strength(neg);
}

glm::vec2 InputManager::get_vector(const std::string& neg_x, const std::string& pos_x,
                                    const std::string& neg_y, const std::string& pos_y) const {
    glm::vec2 v(get_axis(neg_x, pos_x), get_axis(neg_y, pos_y));
    float len = glm::length(v);
    return (len > 1.0f) ? v / len : v;
}

// ---------------------------------------------------------------------------
// Context stack
// ---------------------------------------------------------------------------
void InputManager::push_context(const std::string& name) {
    m_context_stack.push_back(name);
}

void InputManager::pop_context(const std::string& name) {
    // Remove the last occurrence of name
    auto it = std::find(m_context_stack.rbegin(), m_context_stack.rend(), name);
    if (it != m_context_stack.rend())
        m_context_stack.erase((it + 1).base());
}

bool InputManager::has_context(const std::string& name) const {
    return std::find(m_context_stack.begin(), m_context_stack.end(), name)
           != m_context_stack.end();
}

// ---------------------------------------------------------------------------
// Runtime rebinding
// ---------------------------------------------------------------------------
void InputManager::register_action(const std::string& name) {
    if (m_actions.find(name) == m_actions.end()) {
        m_actions[name]       = InputAction{name, {}};
        m_strength[name]      = 0.0f;
        m_prev_strength[name] = 0.0f;
    }
}

void InputManager::add_binding(const std::string& action, const ActionBinding& binding) {
    register_action(action);
    m_actions[action].bindings.push_back(binding);
}

void InputManager::clear_bindings(const std::string& action) {
    auto it = m_actions.find(action);
    if (it != m_actions.end())
        it->second.bindings.clear();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------
void InputManager::save_bindings(const std::string& path) const {
    json j;
    for (const auto& [name, action] : m_actions) {
        json arr = json::array();
        for (const auto& b : action.bindings) {
            json bj;
            switch (b.type) {
            case BindingType::Key:
                bj["type"] = "key"; bj["code"] = b.code; break;
            case BindingType::MouseButton:
                bj["type"] = "mouse_button"; bj["code"] = b.code; break;
            case BindingType::MouseWheelUp:
                bj["type"] = "mouse_wheel"; bj["dir"] = "up"; break;
            case BindingType::MouseWheelDown:
                bj["type"] = "mouse_wheel"; bj["dir"] = "down"; break;
            case BindingType::GamepadButton:
                bj["type"] = "gamepad_button"; bj["code"] = b.code; break;
            case BindingType::GamepadAxis:
                bj["type"] = "gamepad_axis"; bj["code"] = b.code;
                bj["positive"] = b.positive; bj["deadzone"] = b.deadzone; break;
            default: break;
            }
            arr.push_back(bj);
        }
        j[name] = arr;
    }

    std::ofstream f(path);
    if (!f.is_open()) {
        SOL_WARN("InputManager: cannot write '" + path + "'");
        return;
    }
    f << j.dump(2);
    SOL_INFO("InputManager: saved bindings to '" + path + "'");
}

void InputManager::load_bindings(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        SOL_WARN("InputManager: cannot open '" + path + "'");
        return;
    }

    json j;
    try { f >> j; }
    catch (const std::exception& e) {
        SOL_WARN(std::string("InputManager: failed to parse '") + path + "': " + e.what());
        return;
    }

    for (auto& [action_name, bindings_j] : j.items()) {
        clear_bindings(action_name);
        register_action(action_name);

        for (const auto& bj : bindings_j) {
            ActionBinding ab;
            std::string type = bj.value("type", "key");

            if (type == "key") {
                ab.type = BindingType::Key;
                ab.code = bj.value("code", 0);
            } else if (type == "mouse_button") {
                ab.type = BindingType::MouseButton;
                ab.code = bj.value("code", 0);
            } else if (type == "mouse_wheel") {
                ab.type = (bj.value("dir", "up") == "down")
                          ? BindingType::MouseWheelDown : BindingType::MouseWheelUp;
            } else if (type == "gamepad_button") {
                ab.type = BindingType::GamepadButton;
                ab.code = bj.value("code", 0);
            } else if (type == "gamepad_axis") {
                ab.type     = BindingType::GamepadAxis;
                ab.code     = bj.value("code", 0);
                ab.positive = bj.value("positive", true);
                ab.deadzone = bj.value("deadzone", 0.15f);
            }

            m_actions[action_name].bindings.push_back(ab);
        }
    }

    SOL_INFO("InputManager: loaded bindings from '" + path + "'");
}

} // namespace sol
