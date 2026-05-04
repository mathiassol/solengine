#pragma once

// Exports for engine_core symbols consumed by engine.exe and game DLLs.
#if defined(_WIN32)
    #if defined(SOL_ENGINE_BUILD)
        #define SOL_API __declspec(dllexport)
    #else
        #define SOL_API __declspec(dllimport)
    #endif
#else
    #define SOL_API __attribute__((visibility("default")))
#endif
