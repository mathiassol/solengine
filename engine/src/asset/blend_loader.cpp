#include "asset/blend_loader.h"
#include "asset/gltf_loader.h"
#include "sol/log.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <algorithm>

namespace sol {

namespace fs = std::filesystem;

// ── Blender discovery ────────────────────────────────────────────────────────

std::string BlendLoader::find_blender() const {
    // 1. Explicit env var override
    const char* env = std::getenv("BLENDER_EXE");
    if (env && *env && fs::exists(env)) return env;

#ifdef _WIN32
    // 2. Enumerate "C:\Program Files\Blender Foundation\Blender X.Y\blender.exe"
    //    Pick the lexicographically highest version directory.
    const std::string base = "C:/Program Files/Blender Foundation";
    if (fs::exists(base)) {
        std::string best_path, best_ver;
        for (auto& entry : fs::directory_iterator(base)) {
            auto exe = entry.path() / "blender.exe";
            if (fs::exists(exe)) {
                std::string ver = entry.path().filename().string();
                if (best_ver.empty() || ver > best_ver) {
                    best_ver  = ver;
                    best_path = exe.string();
                }
            }
        }
        if (!best_path.empty()) return best_path;
    }
#else
    for (const char* p : {
        "/usr/bin/blender",
        "/usr/local/bin/blender",
        "/Applications/Blender.app/Contents/MacOS/blender"
    }) {
        if (fs::exists(p)) return p;
    }
#endif
    return "";
}

// ── Cache path: <blend_dir>/<stem>.cached.glb ────────────────────────────────

std::string BlendLoader::get_cache_path(const std::string& blend_path) const {
    fs::path p(blend_path);
    fs::path cache = p.parent_path() / (p.stem().string() + ".cached.glb");
    // Normalise to forward slashes so the path is clean in logs and Python args
    std::string s = cache.string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

bool BlendLoader::needs_export(const std::string& blend_path,
                                const std::string& cache_path) const {
    if (!fs::exists(cache_path)) return true;
    return fs::last_write_time(blend_path) > fs::last_write_time(cache_path);
}

// ── Export via Blender CLI ────────────────────────────────────────────────────

bool BlendLoader::export_to_glb(const std::string& blend_path,
                                 const std::string& output_path) const {
    std::string blender = find_blender();
    if (blender.empty()) {
        SOL_ERROR("BlendLoader: Blender not found. "
                  "Install Blender or set the BLENDER_EXE environment variable.");
        return false;
    }

    // Write a small Python script to a temp file.
    // Blender is invoked with: blender --background file.blend --python script.py -- output.glb
    fs::path tmp_script = fs::temp_directory_path() / "sol_blend_export.py";
    {
        std::ofstream f(tmp_script);
        if (!f) {
            SOL_ERROR("BlendLoader: could not write temp script: " + tmp_script.string());
            return false;
        }
        // Use only stable, version-agnostic parameters.
        // UVs, normals, tangents, and materials are exported by default in all
        // Blender 3.x/4.x glTF exporter versions — no explicit flags needed.
        f << "import bpy, sys\n"
             "try:\n"
             "    idx = sys.argv.index('--')\n"
             "    args = sys.argv[idx + 1:]\n"
             "except ValueError:\n"
             "    args = []\n"
             "if not args:\n"
             "    raise SystemExit('BlendLoader: no output path passed')\n"
             "bpy.ops.export_scene.gltf(\n"
             "    filepath=args[0],\n"
             "    export_format='GLB',\n"
             ")\n"
             "print('[BlendLoader] exported:', args[0])\n";
    }

    // Build command — quote all paths to handle spaces.
    // On Windows, std::system() runs through cmd.exe which requires the whole
    // command to be wrapped in an outer pair of quotes when the exe path itself
    // is quoted (otherwise cmd.exe splits on the space inside the exe path).
    auto q = [](const std::string& s) -> std::string { return "\"" + s + "\""; };
    std::string inner = q(blender)
                      + " --background " + q(blend_path)
                      + " --python "     + q(tmp_script.string())
                      + " -- "           + q(output_path);
#ifdef _WIN32
    std::string cmd = "cmd /C \"" + inner + "\"";
#else
    std::string cmd = inner;
#endif

    SOL_INFO("BlendLoader: exporting '" + fs::path(blend_path).filename().string()
             + "' (this may take a minute the first time)...");

    int ret = std::system(cmd.c_str());
    fs::remove(tmp_script);

    if (ret != 0) {
        SOL_ERROR("BlendLoader: Blender exited with code " + std::to_string(ret));
        return false;
    }
    if (!fs::exists(output_path)) {
        SOL_ERROR("BlendLoader: Blender finished but output not found: " + output_path);
        return false;
    }

    SOL_INFO("BlendLoader: cached GLB written -> " + output_path);
    return true;
}

// ── Public entry point ────────────────────────────────────────────────────────

std::shared_ptr<GltfModel> BlendLoader::load(const std::string& blend_path,
                                              const ModelLoadParams& /*params*/) {
    if (!fs::exists(blend_path)) {
        SOL_ERROR("BlendLoader: file not found: " + blend_path);
        return nullptr;
    }

    std::string cache = get_cache_path(blend_path);

    if (needs_export(blend_path, cache)) {
        if (!export_to_glb(blend_path, cache))
            return nullptr;
    } else {
        SOL_INFO("BlendLoader: using cached GLB: " + fs::path(cache).filename().string());
    }

    GltfLoader gltf;
    return gltf.load(cache);
}

} // namespace sol
