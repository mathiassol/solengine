#pragma once

#include "sol/export.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

struct GLFWwindow;

namespace sol {

struct ProjectConfig;

enum class BindingType {
    Key,
    MouseButton,
    MouseWheelUp,
    MouseWheelDown,
    GamepadButton,
    GamepadAxis,
};

struct ActionBinding {
    BindingType type     = BindingType::Key;
    int         code     = 0;       // GLFW key/button/axis index
    bool        positive = true;    // gamepad axis direction
    float       deadzone = 0.15f;
};

struct InputAction {
    std::string                name;
    std::vector<ActionBinding> bindings;
};

class SOL_API InputManager {
public:
    InputManager();
    ~InputManager();

    // Call after window creation to register the GLFW scroll callback.
    void set_window(GLFWwindow* window);

    // Parse "input_actions" from project.sol and register all actions.
    void load_actions(const ProjectConfig& cfg);

    // --- Per-frame lifecycle (in this order) ---
    // 1. begin_frame()     — snapshot prev state, consume scroll accumulator
    // 2. glfwPollEvents()  — GLFW delivers raw events (scroll callback fires)
    // 3. update(window)    — sample GLFW key/mouse/gamepad; compute mouse delta
    void begin_frame();
    void update(GLFWwindow* window);

    // --- Action queries ---
    bool      is_pressed      (const std::string& action) const;
    bool      is_just_pressed (const std::string& action) const;
    bool      is_just_released(const std::string& action) const;
    float     get_strength    (const std::string& action) const;   // 0..1
    float     get_axis        (const std::string& neg, const std::string& pos) const; // -1..1
    glm::vec2 get_vector      (const std::string& neg_x, const std::string& pos_x,
                               const std::string& neg_y, const std::string& pos_y) const;

    // --- Mouse ---
    glm::vec2 get_mouse_delta() const { return m_mouse_delta; }
    float     get_scroll()      const { return m_scroll; }

    // --- Context stack (lightweight label stack, no automatic filtering) ---
    void push_context(const std::string& name);
    void pop_context (const std::string& name);
    bool has_context (const std::string& name) const;

    // --- Runtime rebinding ---
    void register_action(const std::string& name);
    void add_binding    (const std::string& action, const ActionBinding& binding);
    void clear_bindings (const std::string& action);
    void save_bindings  (const std::string& path) const;
    void load_bindings  (const std::string& path);

    // Called by the static GLFW scroll callback.
    void on_scroll(float y) { m_scroll_accumulator += y; }

private:
    float sample_binding(const ActionBinding& b) const;

    std::unordered_map<std::string, InputAction> m_actions;
    std::unordered_map<std::string, float>       m_strength;       // current frame
    std::unordered_map<std::string, float>       m_prev_strength;  // previous frame

    std::vector<std::string> m_context_stack;

    GLFWwindow* m_window            = nullptr;  // non-owning, set during update()
    glm::vec2   m_mouse_delta       = {};
    glm::vec2   m_last_cursor       = {};
    bool        m_first_frame       = true;
    float       m_scroll_accumulator= 0.0f;
    float       m_scroll            = 0.0f;

    std::vector<float>         m_gamepad_axes;
    std::vector<unsigned char> m_gamepad_buttons;
};

} // namespace sol
