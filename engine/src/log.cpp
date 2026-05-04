#include "sol/log.h"
#include <cstdio>

namespace sol::log {
static void write(const char* lvl, std::string_view msg) {
    std::fprintf(stderr, "[sol][%s] %.*s\n", lvl, (int)msg.size(), msg.data());
}
void info (std::string_view m) { write("info ", m); }
void warn (std::string_view m) { write("warn ", m); }
void error(std::string_view m) { write("error", m); }
} // namespace sol::log
