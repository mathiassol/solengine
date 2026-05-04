#pragma once
#include <string>

struct GLFWwindow;

namespace sol {

class Window {
public:
    bool init(const std::string& title, int w, int h);
    void shutdown();

    void poll();
    bool should_close() const;

    GLFWwindow* handle() const { return m_window; }
    void*       native_handle() const;       // HWND on Windows, etc.
    void*       native_display() const;      // wl_display / X display, nullptr on Windows
    int         width() const  { return m_w; }
    int         height() const { return m_h; }

private:
    GLFWwindow* m_window = nullptr;
    int         m_w = 0, m_h = 0;
};

} // namespace sol
