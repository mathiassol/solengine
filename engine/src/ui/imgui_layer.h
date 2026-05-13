#pragma once

#include <imgui.h>

struct GLFWwindow;

namespace sol {

class ImGuiLayer {
public:
    bool init(GLFWwindow* window);
    bool init_editor();
    void shutdown();

    void begin_frame();
    void begin_frame_editor(int w, int h, float dt);
    void end_frame();

    bool initialized() const { return m_initialized; }
    bool is_editor_mode() const { return m_editor_mode; }

private:
    bool m_initialized = false;
    bool m_editor_mode = false;
};

} // namespace sol
