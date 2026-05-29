#include "sol/log.h"
#include <cstdio>
#include <mutex>

namespace sol::log {

static SinkFn   s_sink;
static std::mutex s_sink_mtx;

void set_sink(SinkFn fn) {
    std::lock_guard lock(s_sink_mtx);
    s_sink = std::move(fn);
}

static void write(Level level, const char* lvl_str, std::string_view msg) {
    std::fprintf(stderr, "[sol][%s] %.*s\n", lvl_str, (int)msg.size(), msg.data());
    std::fflush(stderr);

    std::lock_guard lock(s_sink_mtx);
    if (s_sink) s_sink(level, msg);
}

void info (std::string_view m) { write(Level::Info,  "info ", m); }
void warn (std::string_view m) { write(Level::Warn,  "warn ", m); }
void error(std::string_view m) { write(Level::Error, "error", m); }

} // namespace sol::log
