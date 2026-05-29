#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

struct GLFWwindow;

namespace sol {

class ImGuiLayer {
public:
    bool init(GLFWwindow* window);
    bool init_editor();
    void shutdown();

    void begin_frame();
    void begin_frame_editor(int w, int h, float dt);
    // Opens a throwaway frame before update so game node ImGui calls don't crash.
    // Call once per GPU frame, before scene::update(). Does NOT produce any output.
    void begin_frame_absorb(int w, int h, float dt);
    // Discards the throwaway frame and opens a fresh editor-only frame.
    // Call in the render lambda, after scene::render(). Follows begin_frame_absorb().
    void begin_frame_editor_after_absorb(int w, int h, float dt);
    void end_frame();
    void discard_frame();

    bool initialized() const { return m_initialized; }
    bool is_editor_mode() const { return m_editor_mode; }

private:
    bool m_initialized = false;
    bool m_editor_mode = false;
    // Saved input events for the absorb/restore pattern (see begin_frame_absorb).
    std::vector<ImGuiInputEvent> m_saved_input_events;
};

} // namespace sol
