#pragma once

struct GLFWwindow;

namespace sol {

class ImGuiLayer {
public:
    bool init(GLFWwindow* window);
    void shutdown();

    void begin_frame();
    void end_frame();   // calls ImGui::Render — actual GPU draw is a TODO (bgfx backend)

    bool initialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};

} // namespace sol
