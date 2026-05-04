#pragma once
#include "sol/export.h"
#include <string_view>

namespace sol::log {
SOL_API void info (std::string_view msg);
SOL_API void warn (std::string_view msg);
SOL_API void error(std::string_view msg);
} // namespace sol::log

#define SOL_INFO(msg)  ::sol::log::info(msg)
#define SOL_WARN(msg)  ::sol::log::warn(msg)
#define SOL_ERROR(msg) ::sol::log::error(msg)
