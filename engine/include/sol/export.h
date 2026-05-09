#pragma once

// SOL_STATIC_BUILD: engine + game compiled into one exe — no import/export decorators needed
#if defined(SOL_STATIC_BUILD)
    #define SOL_API
#elif defined(_WIN32)
    #if defined(SOL_ENGINE_BUILD)
        #define SOL_API __declspec(dllexport)
    #else
        #define SOL_API __declspec(dllimport)
    #endif
#else
    #define SOL_API __attribute__((visibility("default")))
#endif
