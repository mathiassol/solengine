#include "platform/window.h"
#include "sol/log.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
    #define GLFW_EXPOSE_NATIVE_COCOA
#else
    #define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

namespace sol {

bool Window::init(const std::string& title, int w, int h) {
    if (!glfwInit()) { SOL_ERROR("glfwInit failed"); return false; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // bgfx manages its own context
    m_window = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
    if (!m_window) { SOL_ERROR("glfwCreateWindow failed"); return false; }
    m_w = w; m_h = h;
    return true;
}

void Window::shutdown() {
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; }
    glfwTerminate();
}

void Window::poll() {
    glfwPollEvents();
    if (m_window) glfwGetFramebufferSize(m_window, &m_w, &m_h);
}
bool Window::should_close() const { return glfwWindowShouldClose(m_window); }

void* Window::native_handle() const {
#if defined(_WIN32)
    return (void*)glfwGetWin32Window(m_window);
#elif defined(__APPLE__)
    return (void*)glfwGetCocoaWindow(m_window);
#else
    return (void*)(uintptr_t)glfwGetX11Window(m_window);
#endif
}

void* Window::native_display() const {
#if defined(__linux__) && !defined(__APPLE__)
    return (void*)glfwGetX11Display();
#else
    return nullptr;
#endif
}

} // namespace sol
