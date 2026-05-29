#pragma once
#include "sol/export.h"
#include <functional>
#include <string_view>

namespace sol::log {

enum class Level { Info, Warn, Error };

// Signature: void(Level, std::string_view message)
using SinkFn = std::function<void(Level, std::string_view)>;

// Install an additional log sink (e.g. editor console).
// Pass {} to clear the sink.
SOL_API void set_sink(SinkFn fn);

SOL_API void info (std::string_view msg);
SOL_API void warn (std::string_view msg);
SOL_API void error(std::string_view msg);

} // namespace sol::log

#define SOL_INFO(msg)  ::sol::log::info(msg)
#define SOL_WARN(msg)  ::sol::log::warn(msg)
#define SOL_ERROR(msg) ::sol::log::error(msg)
