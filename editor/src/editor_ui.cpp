#include "editor_ui.h"

#include "sol/host.h"
#include "sol/log.h"
#include "sol/reflect.h"
#include "sol/scene/node.h"
#include "sol/scene/node3d.h"
#include "sol/scene/mesh_node.h"
#include "sol/scene/model_node.h"
#include "sol/render/material.h"
#include "sol/scene/scene_manager.h"
#include "sol/scene/lua_component.h"
#include "sol/script/script_engine.h"
#include "sol/perf/profiler.h"   // Stage 3-D-1
#include "style/ue5_theme.h"
#include "style/sol_widgets.h"
#include "style/fa_icons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QProcess>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

// ── Stage-1 helpers ─────────────────────────────────────────────────────────

// Returns a section-accent colour based on a keyword match in the section name.
static ImU32 sectionAccentColor(const char* name) {
    // toLower inline (no locale needed)
    std::string n(name);
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (n.find("transform") != std::string::npos ||
        n.find("position")  != std::string::npos ||
        n.find("rotation")  != std::string::npos ||
        n.find("scale")     != std::string::npos)  return IM_COL32(220, 130,  50, 255); // Orange
    if (n.find("mesh")     != std::string::npos ||
        n.find("model")    != std::string::npos ||
        n.find("material") != std::string::npos)   return IM_COL32( 60, 130, 200, 255); // Blue
    if (n.find("physics")  != std::string::npos ||
        n.find("collider") != std::string::npos ||
        n.find("body")     != std::string::npos)   return IM_COL32( 80, 190,  80, 255); // Green
    if (n.find("light")    != std::string::npos)   return IM_COL32(220, 200,  50, 255); // Yellow
    if (n.find("script")   != std::string::npos ||
        n.find("lua")      != std::string::npos)   return IM_COL32(160,  80, 200, 255); // Purple
    if (n.find("camera")   != std::string::npos)   return IM_COL32( 80, 200, 200, 255); // Cyan
    if (n.find("world")    != std::string::npos ||
        n.find("environment") != std::string::npos) return IM_COL32( 80, 180, 120, 255); // Teal
    if (n.find("audio")    != std::string::npos)   return IM_COL32(200, 120,  80, 255); // Amber
    return IM_COL32(100, 130, 180, 255); // Default gray-blue
}

// CollapsingHeader with a 3px left-side accent strip coloured by keyword.
static bool collapsingHeaderAccented(const char* label, ImGuiTreeNodeFlags flags = 0) {
    bool open = ImGui::CollapsingHeader(label, flags);
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(mn, {mn.x + 3.0f, mx.y},
        sectionAccentColor(label));
    return open;
}

// Forward declaration — defined below after icon defines
static const char* nodePrefix(const char* typeName);

// Node type categories — used by all Add Node menus.
// Keep in sync with NodeFactory::register_builtin_node_types().
struct NodeCat {
    const char* label;
    const char* const* types;
    int count;
};
static const char* kCatBase[]    = { "Node3D" };
static const char* kCatMesh[]    = { "MeshNode", "ModelNode" };
static const char* kCatLights[]  = { "PointLight", "DirectionalLight" };
static const char* kCatCamera[]  = { "Camera3D" };
static const char* kCatPhysics[] = { "StaticBody3D", "RigidBody3D", "CharacterBody3D",
                                     "Area3D", "CollisionShape3D" };
static const char* kCatScript[]  = { "ScriptNode" };
static const char* kCatAudio[]   = { "AudioStreamPlayer", "AudioStreamPlayer3D" };
static const char* kCatEnv[]     = { "WorldEnvironment", "SceneInstance" };

static const NodeCat kNodeCategories[] = {
    { "Base",        kCatBase,    1 },
    { "Mesh",        kCatMesh,    2 },
    { "Lights",      kCatLights,  2 },
    { "Camera",      kCatCamera,  1 },
    { "Physics",     kCatPhysics, 5 },
    { "Script",      kCatScript,  1 },
    { "Audio",       kCatAudio,   2 },
    { "Environment", kCatEnv,     2 },
};

// Renders categorised "Add Node" menu items (submenus per category).
// Call this inside an already-open ImGui menu or popup context.
static void drawAddNodeCategories(sol::EngineHost* host, sol::Node* parentNode) {
    for (const auto& cat : kNodeCategories) {
        if (ImGui::BeginMenu(cat.label)) {
            for (int i = 0; i < cat.count; ++i) {
                const char* type = cat.types[i];
                // Prefix icon + type name in the menu item
                char label[128];
                std::snprintf(label, sizeof(label), "%s  %s", nodePrefix(type), type);
                if (ImGui::MenuItem(label)) {
                    if (host && host->is_open()) {
                        if (sol::Node* n = host->create_node(type, parentNode))
                            host->set_selected_node(n);
                    }
                }
            }
            ImGui::EndMenu();
        }
    }
}

struct InputTextCallbackUserData {
    std::string* str;
};

int inputTextCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<InputTextCallbackUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string* str = userData->str;
        IM_ASSERT(data->Buf == str->data());
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}

bool inputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0) {
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;
    if (value.capacity() == 0) {
        value.reserve(64);
    }
    InputTextCallbackUserData userData{&value};
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, inputTextCallback, &userData);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string currentProjectPath() {
    std::error_code ec;
    const auto path = std::filesystem::current_path(ec);
    return ec ? std::string{} : path.string();
}

std::string toProjectRelativePath(const std::filesystem::path& path) {
    std::error_code ec;
    const auto root = std::filesystem::current_path(ec);
    std::filesystem::path relative = path;
    if (!ec) {
        relative = std::filesystem::relative(path, root, ec);
        if (ec) {
            relative = path;
        }
    }

    std::string text = relative.string();
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

static const char* nodePrefix(const char* typeName) {
    if (std::strcmp(typeName, "ModelNode") == 0)          return ICON_FA_CUBE;
    if (std::strcmp(typeName, "MeshNode") == 0)           return ICON_FA_CUBE;
    if (std::strcmp(typeName, "DirectionalLight") == 0)   return ICON_FA_SUN;
    if (std::strcmp(typeName, "PointLight") == 0)         return ICON_FA_LIGHTBULB;
    if (std::strcmp(typeName, "Camera3D") == 0)           return ICON_FA_CAMERA;
    if (std::strcmp(typeName, "WorldEnvironment") == 0)   return ICON_FA_GLOBE;
    if (std::strcmp(typeName, "Node3D") == 0)             return ICON_FA_LAYER_GROUP;
    if (std::strcmp(typeName, "ScriptNode") == 0)         return ICON_FA_CODE;
    if (std::strcmp(typeName, "StaticBody3D") == 0)       return ICON_FA_LOCK;
    if (std::strcmp(typeName, "RigidBody3D") == 0)        return ICON_FA_CIRCLE;
    if (std::strcmp(typeName, "CharacterBody3D") == 0)    return ICON_FA_PERSON;
    if (std::strcmp(typeName, "Area3D") == 0)             return ICON_FA_EXPAND;
    if (std::strcmp(typeName, "CollisionShape3D") == 0)   return ICON_FA_SQUARE;
    if (std::strcmp(typeName, "AudioStreamPlayer") == 0)  return ICON_FA_MUSIC;
    if (std::strcmp(typeName, "AudioStreamPlayer3D") == 0)return ICON_FA_VOLUME_HIGH;
    if (std::strcmp(typeName, "SceneInstance") == 0)      return ICON_FA_LINK;
    if (std::strcmp(typeName, "FlyCamController") == 0)   return ICON_FA_EYE;
    return ICON_FA_BOLT;
}

bool isModelExtension(const std::string& extLower) {
    return extLower == ".glb" || extLower == ".gltf" || extLower == ".fbx" || extLower == ".blend";
}

struct AccentPreset { const char* name; ImVec4 color; };
static const AccentPreset kAccents[] = {
    { "Blue",   {0.537f, 0.706f, 0.980f, 1.0f} },
    { "Orange", {0.980f, 0.550f, 0.160f, 1.0f} },
    { "Green",  {0.200f, 0.780f, 0.350f, 1.0f} },
    { "Purple", {0.710f, 0.380f, 0.980f, 1.0f} },
    { "Rose",   {0.980f, 0.400f, 0.500f, 1.0f} },
};

} // namespace

// ---------------------------------------------------------------------------
// Editor Preferences — load / save / apply
// ---------------------------------------------------------------------------

void EditorUI::loadPreferences() {
    QSettings s("SolEngine", "Editor");
    m_prefs.fontScale     = s.value("prefs/fontScale",     1.0f).toFloat();
    m_prefs.accentPreset  = s.value("prefs/accentPreset",  0).toInt();
    m_prefs.uiDensity     = s.value("prefs/uiDensity",     1).toInt();
    m_prefs.frameRounding = s.value("prefs/frameRounding", 3.0f).toFloat();
    m_prefs.camSpeedMult  = s.value("prefs/camSpeedMult",  1.0f).toFloat();
    m_prefs.camInvertY    = s.value("prefs/camInvertY",    false).toBool();
    m_prefs.camFov        = s.value("prefs/camFov",        60.0f).toFloat();
    m_prefs.showFps       = s.value("prefs/showFps",       true).toBool();
    m_prefs.confirmDelete = s.value("prefs/confirmDelete", true).toBool();
    m_prefs.autosave      = s.value("prefs/autosave",      false).toBool();
    m_prefs.autosaveMin   = s.value("prefs/autosaveMin",   2).toInt();
    m_prefs.undoLimit     = s.value("prefs/undoLimit",     100).toInt();
}

void EditorUI::savePreferences() {
    QSettings s("SolEngine", "Editor");
    s.setValue("prefs/fontScale",     m_prefs.fontScale);
    s.setValue("prefs/accentPreset",  m_prefs.accentPreset);
    s.setValue("prefs/uiDensity",     m_prefs.uiDensity);
    s.setValue("prefs/frameRounding", m_prefs.frameRounding);
    s.setValue("prefs/camSpeedMult",  m_prefs.camSpeedMult);
    s.setValue("prefs/camInvertY",    m_prefs.camInvertY);
    s.setValue("prefs/camFov",        m_prefs.camFov);
    s.setValue("prefs/showFps",       m_prefs.showFps);
    s.setValue("prefs/confirmDelete", m_prefs.confirmDelete);
    s.setValue("prefs/autosave",      m_prefs.autosave);
    s.setValue("prefs/autosaveMin",   m_prefs.autosaveMin);
    s.setValue("prefs/undoLimit",     m_prefs.undoLimit);
}

void EditorUI::applyPreferences() {
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = m_prefs.fontScale;

    ImGuiStyle& style  = ImGui::GetStyle();
    style.FrameRounding  = m_prefs.frameRounding;
    style.WindowRounding = m_prefs.frameRounding + 1.0f;
    style.PopupRounding  = m_prefs.frameRounding + 1.0f;
    style.TabRounding    = m_prefs.frameRounding + 1.0f;
    style.GrabRounding   = m_prefs.frameRounding;

    switch (m_prefs.uiDensity) {
    case 0: style.ItemSpacing = {6,2};  style.FramePadding = {4,2};  break;
    case 1: style.ItemSpacing = {8,4};  style.FramePadding = {6,3};  break;
    case 2: style.ItemSpacing = {10,6}; style.FramePadding = {8,5};  break;
    default: break;
    }

    const ImVec4& a  = kAccents[m_prefs.accentPreset].color;
    ImVec4 full      = a;
    ImVec4 bright    = {std::min(a.x*1.15f,1.f), std::min(a.y*1.15f,1.f), std::min(a.z*1.15f,1.f), 1.f};
    ImVec4 mid       = {a.x*0.70f, a.y*0.70f, a.z*0.70f, 1.f};
    ImVec4* c        = style.Colors;
    c[ImGuiCol_ScrollbarGrabHovered] = mid;
    c[ImGuiCol_ScrollbarGrabActive]  = full;
    c[ImGuiCol_CheckMark]            = full;
    c[ImGuiCol_SliderGrab]           = full;
    c[ImGuiCol_SliderGrabActive]     = bright;
    c[ImGuiCol_ButtonActive]         = full;
    c[ImGuiCol_HeaderActive]         = full;
    c[ImGuiCol_TabHovered]           = {a.x, a.y, a.z, 0.8f};
    c[ImGuiCol_DockingPreview]       = {a.x, a.y, a.z, 0.7f};
}

// ---------------------------------------------------------------------------
// Undo / Redo helpers
// ---------------------------------------------------------------------------

size_t EditorUI::fieldTypeSize(sol::FieldType type) const {
    switch (type) {
    case sol::FieldType::Bool:    return sizeof(bool);
    case sol::FieldType::Int:
    case sol::FieldType::EnumInt: return sizeof(int);
    case sol::FieldType::Float:   return sizeof(float);
    case sol::FieldType::Vec3:
    case sol::FieldType::Color3:  return sizeof(float) * 3;
    case sol::FieldType::Vec4:
    case sol::FieldType::Color4:  return sizeof(float) * 4;
    default:                      return 0;
    }
}

void EditorUI::captureFieldValue(sol::FieldType type, void* ptr,
                                  std::vector<uint8_t>& raw_out, std::string& str_out) const {
    if (type == sol::FieldType::String || type == sol::FieldType::AssetPath) {
        str_out = *static_cast<std::string*>(ptr);
        return;
    }
    const size_t sz = fieldTypeSize(type);
    raw_out.resize(sz);
    std::memcpy(raw_out.data(), ptr, sz);
}

void EditorUI::pushUndoField(sol::Node* node, const std::string& field, sol::FieldType type,
                               const std::vector<uint8_t>& before_raw, const std::string& before_str,
                               const std::vector<uint8_t>& after_raw,  const std::string& after_str) {
    if (m_undo_idx + 1 < static_cast<int>(m_undo_stack.size())) {
        m_undo_stack.erase(m_undo_stack.begin() + m_undo_idx + 1, m_undo_stack.end());
    }
    UndoEntry e;
    e.type        = UndoType::FieldChange;
    e.node        = node;
    e.field       = field;
    e.field_type  = type;
    e.before_raw  = before_raw;
    e.before_str  = before_str;
    e.after_raw   = after_raw;
    e.after_str   = after_str;
    m_undo_stack.push_back(std::move(e));
    m_undo_idx = static_cast<int>(m_undo_stack.size()) - 1;
    if (m_undo_stack.size() > 200) {
        m_undo_stack.erase(m_undo_stack.begin());
        m_undo_idx = static_cast<int>(m_undo_stack.size()) - 1;
    }
}

void EditorUI::pushUndoRename(sol::Node* node, const std::string& before, const std::string& after) {
    if (before == after) return;
    if (m_undo_idx + 1 < static_cast<int>(m_undo_stack.size())) {
        m_undo_stack.erase(m_undo_stack.begin() + m_undo_idx + 1, m_undo_stack.end());
    }
    UndoEntry e;
    e.type        = UndoType::Rename;
    e.node        = node;
    e.before_name = before;
    e.after_name  = after;
    m_undo_stack.push_back(std::move(e));
    m_undo_idx = static_cast<int>(m_undo_stack.size()) - 1;
}

void EditorUI::pushUndoEntry(UndoEntry e) {
    if (m_undo_idx + 1 < static_cast<int>(m_undo_stack.size())) {
        m_undo_stack.erase(m_undo_stack.begin() + m_undo_idx + 1, m_undo_stack.end());
    }
    m_undo_stack.push_back(std::move(e));
    m_undo_idx = static_cast<int>(m_undo_stack.size()) - 1;
    if (m_undo_stack.size() > 200) {
        m_undo_stack.erase(m_undo_stack.begin());
        m_undo_idx = static_cast<int>(m_undo_stack.size()) - 1;
    }
}

void EditorUI::applyUndoEntry(const UndoEntry& e, bool reverse) {
    if (!e.node || !m_host) return;
    if (e.type == UndoType::GizmoTransform) {
        const glm::vec3& pos   = reverse ? e.before_pos   : e.after_pos;
        const glm::vec3& rot   = reverse ? e.before_rot   : e.after_rot;
        const glm::vec3& scale = reverse ? e.before_scale : e.after_scale;
        m_host->set_field(e.node, "position", &pos);
        m_host->set_field(e.node, "rotation", &rot);
        m_host->set_field(e.node, "scale",    &scale);
        return;
    }
    if (e.type == UndoType::Rename) {
        const std::string& target = reverse ? e.before_name : e.after_name;
        m_host->rename_node(e.node, target);
        return;
    }
    if (e.field_type == sol::FieldType::String || e.field_type == sol::FieldType::AssetPath) {
        const std::string& val = reverse ? e.before_str : e.after_str;
        m_host->set_field(e.node, e.field, &val);
    } else {
        const auto& raw = reverse ? e.before_raw : e.after_raw;
        if (!raw.empty()) {
            m_host->set_field(e.node, e.field, raw.data());
        }
    }
}

void EditorUI::undo() {
    if (m_undo_idx < 0) return;
    applyUndoEntry(m_undo_stack[m_undo_idx], true);
    --m_undo_idx;
}

void EditorUI::redo() {
    if (m_undo_idx + 1 >= static_cast<int>(m_undo_stack.size())) return;
    ++m_undo_idx;
    applyUndoEntry(m_undo_stack[m_undo_idx], false);
}

void EditorUI::clearSceneState() {
    // Wipe all raw Node* state that becomes dangling after a scene is replaced.
    m_undo_stack.clear();
    m_undo_idx = -1;
    m_drag_capture = {};
    m_rename_active = false;
    m_rename_node = nullptr;
    m_delete_confirm_open = false;
    m_delete_confirm_node = nullptr;
    m_pending_hdr_node = nullptr;
    m_pending_hdr_dialog = false;
    m_pending_asset_node = nullptr;
    m_pending_asset_dialog = false;
    m_pending_lua_node = nullptr;
    m_pending_lua_dialog = false;
    m_pending_new_script_node = nullptr;
    m_pending_comp_lua_dialog = false;
    m_pending_comp_lua_node   = nullptr;
    m_pending_comp_lua_ptr    = nullptr;
    m_pending_new_comp_node   = nullptr;
    m_context_menu_node = nullptr;
    m_script_buffers.clear(); // force reload from disk in new scene
    m_script_dirty.clear();
    for (auto& tab : m_editor_tabs)
        tab.selected_node = nullptr;
    m_mat_target_node      = nullptr;
    m_mat_selected_submesh = -1;
    m_mat_cache_valid      = false;
}

EditorUI::EditorUI(sol::EngineHost* host)
    : m_host(host) {
    sol::log::set_sink([this](sol::log::Level level, std::string_view msg) {
        std::lock_guard lock(m_log_mutex);
        m_log_entries.push_back({level, std::string(msg)});
        if (m_log_entries.size() > 2000) {
            m_log_entries.erase(m_log_entries.begin(), m_log_entries.begin() + (m_log_entries.size() - 2000));
        }
        m_log_scroll_to_bottom = true;
    });

    if (m_host) {
        m_host->set_gizmo_undo_callback([this](sol::Node3D* node,
                glm::vec3 bp, glm::vec3 br, glm::vec3 bs,
                glm::vec3 ap, glm::vec3 ar, glm::vec3 as) {
            UndoEntry e;
            e.type         = UndoType::GizmoTransform;
            e.node         = node;
            e.before_pos   = bp; e.before_rot   = br; e.before_scale   = bs;
            e.after_pos    = ap; e.after_rot     = ar; e.after_scale    = as;
            pushUndoEntry(std::move(e));
        });
    }

    if (m_host && m_host->is_open()) {
        m_asset_browser_path = currentProjectPath();
        m_project_root_path = m_asset_browser_path;
    }

    // Initialize with the always-present Scene Editor tab
    m_editor_tabs.push_back({0, "Scene Editor", EditorTabType::SceneEditor, "", true});
    m_active_tab_id = 0;
    m_next_tab_id   = 1;
    // Assign the first hot scene slot (adopts the already-loading main scene)
    if (m_host && m_host->is_open()) {
        m_editor_tabs.back().scene_slot_id = m_host->create_scene_slot();
    }

    // Apply UE5 theme
    sol::ApplyUE5Theme(ImGui::GetStyle());

    loadPreferences();
    applyPreferences();
}

EditorUI::~EditorUI() {
    savePreferences();
    stopGameProcess();
    sol::log::set_sink({});
}

void EditorUI::startGameProcess() {
    stopGameProcess(); // kill any existing run

    // Save scene so the game loads the latest state
    if (m_host && m_host->is_open()) m_host->save_scene();

    // sol.exe lives next to sol_editor.exe
    QString solExe = QCoreApplication::applicationDirPath() + "/sol.exe";
    QString projectDir = m_project_root_path.empty()
        ? QDir::currentPath()
        : QString::fromStdString(m_project_root_path);

    m_game_process = new QProcess();
    m_game_process->setWorkingDirectory(projectDir);

    // Merge stdout+stderr so both land in our console
    m_game_process->setProcessChannelMode(QProcess::MergedChannels);

    // Pipe output to the editor console
    QObject::connect(m_game_process, &QProcess::readyReadStandardOutput,
        m_game_process, [this]() {
            const QByteArray data = m_game_process->readAllStandardOutput();
            const std::string text = data.toStdString();
            // Append each line as an INFO log entry
            std::string line;
            for (char c : text) {
                if (c == '\n') {
                    if (!line.empty()) {
                        std::lock_guard lock(m_log_mutex);
                        m_log_entries.push_back({sol::log::Level::Info, "[Game] " + line});
                        m_log_scroll_to_bottom = true;
                        line.clear();
                    }
                } else {
                    line += c;
                }
            }
            if (!line.empty()) {
                std::lock_guard lock(m_log_mutex);
                m_log_entries.push_back({sol::log::Level::Info, "[Game] " + line});
                m_log_scroll_to_bottom = true;
            }
        });

    QObject::connect(m_game_process, &QProcess::finished,
        m_game_process, [this](int exitCode, QProcess::ExitStatus) {
            {
                std::lock_guard lock(m_log_mutex);
                m_log_entries.push_back({sol::log::Level::Info,
                    "[Game] Process exited (code " + std::to_string(exitCode) + ")"});
                m_log_scroll_to_bottom = true;
            }
            m_play_mode = false;
            m_game_process->deleteLater();
            m_game_process = nullptr;
        });

    m_game_process->start(solExe, {"run", projectDir});
    if (!m_game_process->waitForStarted(3000)) {
        std::lock_guard lock(m_log_mutex);
        m_log_entries.push_back({sol::log::Level::Error,
            "[Game] Failed to launch: " + solExe.toStdString()});
        m_log_scroll_to_bottom = true;
        m_game_process->deleteLater();
        m_game_process = nullptr;
        m_play_mode = false;
    }
}

void EditorUI::stopGameProcess() {
    if (!m_game_process) return;
    m_game_process->disconnect();
    if (m_game_process->state() != QProcess::NotRunning) {
        m_game_process->terminate();
        if (!m_game_process->waitForFinished(2000))
            m_game_process->kill();
    }
    delete m_game_process;
    m_game_process = nullptr;
    m_play_mode = false;
}

std::function<void()> EditorUI::drawFn() {
    return [this]() { draw(); };
}

std::string EditorUI::openFileDialog(const std::string& filter, const std::string& title) {
    const QString path = QFileDialog::getOpenFileName(
        nullptr,
        QString::fromStdString(title),
        QString(),
        QString::fromStdString(filter));
    return path.toStdString();
}

std::string EditorUI::saveFileDialog(const std::string& filter, const std::string& title) {
    const QString path = QFileDialog::getSaveFileName(
        nullptr,
        QString::fromStdString(title),
        QString(),
        QString::fromStdString(filter));
    return path.toStdString();
}

void EditorUI::draw() {
    applyPreferences();

    // Undo / Redo shortcuts (only when not in text input)
    if (!ImGui::GetIO().WantTextInput) {
        const bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            undo();
        }
        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                     (ImGui::IsKeyDown(ImGuiKey_LeftShift) && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) {
            redo();
        }
    }

    if (!ImGui::GetIO().WantTextInput) {
        const bool ctrl2 = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        if (ctrl2 && ImGui::IsKeyPressed(ImGuiKey_D, false) && m_host && m_host->selected_node()) {
            sol::Node* dup = m_host->duplicate_node(m_host->selected_node());
            if (dup) m_host->set_selected_node(dup);
        }
    }

    // Update FPS history for statistics graph
    if (m_host && m_host->is_open()) {
        m_fps_history_timer += ImGui::GetIO().DeltaTime;
        if (m_fps_history_timer >= 0.05f) {
            m_fps_history_timer = 0.0f;
            m_fps_history[m_fps_history_idx] = m_host->frame_fps();
            m_fps_history_idx = (m_fps_history_idx + 1) % kFpsHistoryLen;
        }
    }

    if (m_pending_hdr_dialog) {
        m_pending_hdr_dialog = false;
        const std::string path = openFileDialog("HDR Images (*.hdr *.exr);;All Files (*.*)", "Open HDR Image");
        if (!path.empty() && m_pending_hdr_node) {
            m_host->set_field(m_pending_hdr_node, m_pending_hdr_field, &path);
        }
        m_pending_hdr_node = nullptr;
        m_pending_hdr_field.clear();
    }

    if (m_pending_asset_dialog) {
        m_pending_asset_dialog = false;
        const std::string path = openFileDialog("3D Models (*.glb *.gltf *.fbx *.blend);;All Files (*.*)", "Open Asset");
        if (!path.empty() && m_pending_asset_node) {
            m_host->set_field(m_pending_asset_node, m_pending_asset_field, &path);
        }
        m_pending_asset_node = nullptr;
        m_pending_asset_field.clear();
    }

    if (m_pending_lua_dialog) {
        m_pending_lua_dialog = false;
        const std::string path = openFileDialog("Lua Scripts (*.lua);;All Files (*.*)", "Open Lua Script");
        if (!path.empty() && m_pending_lua_node) {
            sol::Node* n = m_pending_lua_node;
            if (m_host->engine().has_script())
                m_host->engine().script().node_detach(n); // detach any existing instance first
            n->script_path = path;
            if (m_host->engine().has_script())
                m_host->engine().script().node_ready(n, m_host->engine());
        }
        m_pending_lua_node = nullptr;
    }

    if (m_pending_new_script_node) {
        sol::Node* target = m_pending_new_script_node;
        m_pending_new_script_node = nullptr;
        const std::string path = saveFileDialog("Lua Scripts (*.lua)", "Create New Script");
        if (!path.empty()) {
            // Write template if file doesn't exist
            FILE* f = std::fopen(path.c_str(), "rb");
            const bool exists = f != nullptr;
            if (f) std::fclose(f);
            if (!exists) {
                FILE* wf = std::fopen(path.c_str(), "wb");
                if (wf) {
                    const std::string tpl =
                        "local M = {}\nM.__index = M\n\n"
                        "function M:on_ready()\n"
                        "    -- called once when the scene starts\n"
                        "end\n\n"
                        "function M:on_update(dt)\n"
                        "    -- called every frame; dt = delta time in seconds\n"
                        "end\n\n"
                        "return M\n";
                    std::fwrite(tpl.c_str(), 1, tpl.size(), wf);
                    std::fclose(wf);
                }
            }
            if (m_host->engine().has_script())
                m_host->engine().script().node_detach(target);
            target->script_path = path;
            if (m_host->engine().has_script())
                m_host->engine().script().node_ready(target, m_host->engine());
            openScriptEditorTab(path);
        }
    }

    // Component browse dialog (change script path of existing LuaComponent)
    if (m_pending_comp_lua_dialog) {
        m_pending_comp_lua_dialog = false;
        const std::string path = openFileDialog("Lua Scripts (*.lua);;All Files (*.*)", "Open Lua Script");
        if (!path.empty() && m_pending_comp_lua_node && m_pending_comp_lua_ptr) {
            sol::LuaComponent* lc = m_pending_comp_lua_ptr;
            if (m_host->engine().has_script())
                m_host->engine().script().component_detach(lc);
            lc->set_script_path(path);
            if (m_host->engine().has_script())
                m_host->engine().script().component_ready(lc, m_host->engine());
        }
        m_pending_comp_lua_node = nullptr;
        m_pending_comp_lua_ptr  = nullptr;
    }

    // New LuaComponent: browse for script and add to node
    if (m_pending_new_comp_node) {
        sol::Node* target = m_pending_new_comp_node;
        m_pending_new_comp_node = nullptr;
        const std::string path = openFileDialog("Lua Scripts (*.lua);;All Files (*.*)", "Select Script for Lua Component");
        if (!path.empty()) {
            sol::Node*        t    = target;
            std::string       p    = path;
            sol::EngineHost*  host = m_host;
            m_comp_undo_stack.push({
                "Add LuaComponent",
                [t, p, host]() {
                    auto comp = std::make_unique<sol::LuaComponent>(p);
                    sol::LuaComponent* raw = static_cast<sol::LuaComponent*>(
                        t->add_component(std::move(comp)));
                    if (host && host->engine().has_script())
                        host->engine().script().component_ready(raw, host->engine());
                },
                [t, host]() {
                    sol::LuaComponent* lc = t->get_component<sol::LuaComponent>();
                    if (lc && host && host->engine().has_script())
                        host->engine().script().component_detach(lc);
                    t->remove_component("LuaComponent");
                }
            });
        }
    }

    // Push per-tab viewport settings to renderer
    if (m_host && m_host->is_open()) {
        for (auto& t : m_editor_tabs) {
            if (t.id == m_active_tab_id && t.type == EditorTabType::SceneEditor) {
                auto& rs = m_host->engine().renderer().settings();
                // Unlit = show albedo only (debug_view 1)
                if (t.viewport_shading == 1) {
                    rs.debug_view = 1;
                } else {
                    rs.debug_view = t.debug_view;
                }
                rs.wireframe_mode = (t.viewport_shading == 2);
                break;
            }
        }
    }

    // Show/hide gizmo based on active tab type
    if (m_host) {
        EditorTabType activeType = EditorTabType::SceneEditor;
        for (const auto& t : m_editor_tabs)
            if (t.id == m_active_tab_id) { activeType = t.type; break; }
        m_host->set_gizmo_visible(activeType == EditorTabType::SceneEditor);
    }

    drawTitleBar();
    drawEditorTabBar();
    drawDockspace();

    // Determine if we're in scene editor mode
    bool isSceneTab = true;
    for (const auto& t : m_editor_tabs) {
        if (t.id == m_active_tab_id) {
            isSceneTab = (t.type == EditorTabType::SceneEditor);
            break;
        }
    }

    // Always draw all panels so their dock nodes remain alive regardless of active tab
    if (m_panel_hierarchy)      drawHierarchyPanel();
    if (m_panel_inspector)      drawInspectorPanel();
    if (m_panel_world_settings) drawWorldSettingsPanel();
    if (m_panel_console)        drawConsolePanel();
    if (m_panel_assets)         drawAssetBrowserPanel();
    if (m_panel_statistics)     drawStatisticsPanel();
    if (m_panel_sequencer)      drawSequencerPanel();
    if (m_panel_build_output)   drawBuildOutputPanel();
    if (m_show_profiler)        drawProfilerPanel();   // Stage 3-D-3

    // Sub-editor occupies central area for non-scene tabs
    if (!isSceneTab) {
        for (const auto& tab : m_editor_tabs) {
            if (tab.id == m_active_tab_id) {
                drawSubEditorStub(tab);
                break;
            }
        }
    }
    drawViewportToolbar();
    drawStatusBar();

    if (m_delete_confirm_open) {
        ImGui::OpenPopup("Delete Node?");
        m_delete_confirm_open = false;
    }

    if (ImGui::BeginPopupModal("Delete Node?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (m_delete_confirm_node) {
            ImGui::Text("Delete '%s' and all its children?", m_delete_confirm_node->name.c_str());
        } else {
            ImGui::TextUnformatted("Delete selected node?");
        }
        ImGui::Spacing();
        if (ImGui::Button("Delete", ImVec2(80.0f, 0.0f))) {
            if (m_delete_confirm_node && m_delete_confirm_node->parent()) {
                if (m_host->selected_node() == m_delete_confirm_node) {
                    m_host->set_selected_node(nullptr);
                }
                m_host->remove_node(m_delete_confirm_node);
            }
            m_delete_confirm_node = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f))) {
            m_delete_confirm_node = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorUI::drawTitleBar() {
    constexpr float kTitleH    = 32.0f;
    constexpr float kBtnW      = 40.0f;
    constexpr float kBtnH      = kTitleH;

    ImGuiViewport* vp   = ImGui::GetMainViewport();
    const ImVec2   vpPos  = vp->WorkPos;
    const ImVec2   vpSize = vp->WorkSize;

    ImGui::SetNextWindowPos(vpPos);
    ImGui::SetNextWindowSize(ImVec2(vpSize.x, kTitleH));
    ImGui::SetNextWindowViewport(vp->ID);

    const ImGuiWindowFlags tbFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar| ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,   sol::UE5Colors::BgTitlebar);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg,  sol::UE5Colors::BgTitlebar);
    ImGui::Begin("##TitleBar", nullptr, tbFlags);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        // App name
        ImGui::PushStyleColor(ImGuiCol_Text, sol::UE5Colors::Accent);
        ImGui::TextUnformatted("Sol");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 12.0f);

        // File menu
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {}
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                if (m_host && m_host->is_open()) m_host->save_scene();
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                if (m_host && m_host->is_open()) {
                    const std::string p = saveFileDialog("Scene Files (*.solscene);;All Files (*.*)", "Save Scene As");
                    if (!p.empty()) m_host->save_scene(p);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) { QApplication::quit(); }
            ImGui::EndMenu();
        }

        // Edit menu
        if (ImGui::BeginMenu("Edit")) {
            const bool canUndo = m_undo_idx >= 0;
            const bool canRedo = m_undo_idx + 1 < static_cast<int>(m_undo_stack.size());
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, m_host && m_host->selected_node())) {
                sol::Node* dup = m_host->duplicate_node(m_host->selected_node());
                if (dup) m_host->set_selected_node(dup);
            }
            if (ImGui::MenuItem("Delete", "Del", false, m_host && m_host->selected_node())) {
                sol::Node* sel = m_host ? m_host->selected_node() : nullptr;
                if (sel && sel->parent()) {
                    if (!m_prefs.confirmDelete) {
                        if (m_host->selected_node() == sel) m_host->set_selected_node(nullptr);
                        m_host->remove_node(sel);
                    } else {
                        m_delete_confirm_open = true; m_delete_confirm_node = sel;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Editor Preferences...")) m_show_editor_prefs    = true;
            if (ImGui::MenuItem("Project Settings..."))   m_show_project_settings = true;
            ImGui::EndMenu();
        }

        // Scene menu
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::BeginMenu("Add Node")) {
                drawAddNodeCategories(m_host, nullptr);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        // View menu
        if (ImGui::BeginMenu("View")) {
            ImGui::SeparatorText("Panels");
            ImGui::MenuItem("Hierarchy",      nullptr, &m_panel_hierarchy);
            ImGui::MenuItem("Inspector",      nullptr, &m_panel_inspector);
            ImGui::MenuItem("World Settings", nullptr, &m_panel_world_settings);
            ImGui::MenuItem("Console",        nullptr, &m_panel_console);
            ImGui::MenuItem("Asset Browser",  nullptr, &m_panel_assets);
            ImGui::MenuItem("Statistics",     nullptr, &m_panel_statistics);
            ImGui::MenuItem("Profiler",       nullptr, &m_show_profiler);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) m_first_layout = true;
            ImGui::SeparatorText("Editors");
            if (ImGui::MenuItem("Material Editor"))
                openEditorTab(EditorTabType::MaterialEditor, "Material Editor");
            if (ImGui::MenuItem("Shader Editor"))
                openEditorTab(EditorTabType::ShaderEditor,   "Shader Editor");
            if (ImGui::MenuItem("Model Viewer"))
                openEditorTab(EditorTabType::ModelViewer,    "Model Viewer");
            ImGui::EndMenu();
        }

        // Tools menu
        if (ImGui::BeginMenu("Tools")) {
            ImGui::MenuItem("Profiler", nullptr, &m_show_profiler);
            ImGui::EndMenu();
        }

        // Build menu
        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Build All",   "Ctrl+B", false, false)) {}
            if (ImGui::MenuItem("Build Output")) m_panel_build_output = true;
            ImGui::EndMenu();
        }

        // Help menu
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About SolEngine")) ImGui::OpenPopup("About SolEngine");
            ImGui::EndMenu();
        }

        // Centre drag zone
        const float rightW  = 3 * kBtnW + 140.0f;
        const float cursorX = ImGui::GetCursorPosX();
        const float spacerW = vpSize.x - cursorX - rightW;

        if (spacerW > 0.0f) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.04f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
            ImGui::Button("##TitleDrag", ImVec2(spacerW, kTitleH - 2.0f));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                if (m_wnd_move) m_wnd_move();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (m_wnd_maximize) m_wnd_maximize();
            }
        }

        // Scene name + FPS
        ImGui::SameLine(0, 4.0f);
        if (m_host && m_host->is_open()) {
            sol::Scene* sc = m_host->engine().scene_manager().current();
            const char* sceneName = (sc && !sc->name.empty()) ? sc->name.c_str() : "No Scene";
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  %.0f fps", sceneName, m_host->frame_fps());
            ImGui::PushStyleColor(ImGuiCol_Text, sol::UE5Colors::TextDim);
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 12.0f);
        }

        // Window control buttons
        auto winBtn = [&](const char* label, ImVec4 hovCol, std::function<void()> fn) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  hovCol);
            if (ImGui::Button(label, ImVec2(kBtnW, kTitleH - 2.0f))) fn();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 0);
        };

        winBtn(" _ ", ImVec4(1,1,1,0.1f), [&]{ if (m_wnd_minimize) m_wnd_minimize(); });
        const bool isMax = m_wnd_is_max && m_wnd_is_max();
        winBtn(isMax ? " \xe2\x96\xa1 " : " \xe2\x96\xa1 ", ImVec4(1,1,1,0.1f),
               [&]{ if (m_wnd_maximize) m_wnd_maximize(); });
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f,0.15f,0.15f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f,0.10f,0.10f,1.0f));
        if (ImGui::Button(" X ", ImVec2(kBtnW, kTitleH - 2.0f)))
            if (m_wnd_close) m_wnd_close();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::EndMenuBar();
    }

    // Modals
    if (ImGui::BeginPopupModal("About SolEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("SolEngine Editor");
        ImGui::TextDisabled("Qt6 backend  +  Vulkan/ImGui");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    drawProjectSettingsModal();
    drawEditorPreferencesModal();

    ImGui::End();
}

void EditorUI::drawDockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 pos = viewport->WorkPos;
    ImVec2 size = viewport->WorkSize;

    const float toolbarHeight  = 0.0f;   // toolbar is now in tab bar
    const float tabBarHeight   = 62.0f;  // titlebar(32) + tabbar(30)
    const float statusBarHeight = 22.0f;
    pos.y += toolbarHeight + tabBarHeight;
    size.y = std::max(0.0f, size.y - toolbarHeight - tabBarHeight - statusBarHeight);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags dockFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##DockspaceHost", nullptr, dockFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockId = ImGui::GetID("MainDockspace");
    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    m_dockspace_id = dockId;

    if (m_first_layout) {
        m_first_layout = false;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, size);

        ImGuiID center = dockId;

        // Left: Hierarchy (20%)
        ImGuiID left;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, &left, &center);

        // Right: Inspector + World Settings tabbed (24%)
        ImGuiID right;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.285f, &right, &center);

        // Bottom: Console group | Assets group (30% of remaining)
        ImGuiID bottom;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, &bottom, &center);

        // Split bottom: left 55% = console/build group, right 45% = assets/stats/seq group
        ImGuiID bottom_left, bottom_right;
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.55f, &bottom_left, &bottom_right);

        // Dock only non-detached panels
        if (!m_panel_detached[0]) ImGui::DockBuilderDockWindow("Hierarchy",      left);

        if (!m_panel_detached[1]) ImGui::DockBuilderDockWindow("Inspector",      right);
        if (!m_panel_detached[2]) ImGui::DockBuilderDockWindow("World Settings", right);

        if (!m_panel_detached[3]) ImGui::DockBuilderDockWindow("Console",        bottom_left);
        if (!m_panel_detached[7]) ImGui::DockBuilderDockWindow("Build Output",   bottom_left);

        if (!m_panel_detached[4]) ImGui::DockBuilderDockWindow("Asset Browser",  bottom_right);
        if (!m_panel_detached[5]) ImGui::DockBuilderDockWindow("Statistics",     bottom_right);
        if (!m_panel_detached[6]) ImGui::DockBuilderDockWindow("Sequencer",      bottom_right);

        ImGui::DockBuilderFinish(dockId);
    }

    ImGui::End();
}

void EditorUI::openScriptEditorTab(const std::string& path) {
    if (path.empty()) return;
    // Reuse any existing ScriptEditor tab — update it in-place rather than stacking tabs.
    for (auto& tab : m_editor_tabs) {
        if (tab.type == EditorTabType::ScriptEditor) {
            tab.asset_path = path;
            tab.title = std::filesystem::path(path).filename().string();
            m_focus_tab_id = tab.id;
            return;
        }
    }
    openEditorTab(EditorTabType::ScriptEditor,
                  std::filesystem::path(path).filename().string(), path);
}

void EditorUI::openEditorTab(EditorTabType type, const std::string& title, const std::string& asset_path) {
    // SceneEditor tabs are never deduped — multiple scene tabs are allowed.
    // All other tab types dedup by (type, asset_path) so you don't get two identical viewers.
    if (type != EditorTabType::SceneEditor) {
        for (const auto& t : m_editor_tabs) {
            if (t.type == type && t.asset_path == asset_path) {
                m_focus_tab_id = t.id;
                return;
            }
        }
    }
    const int newId = m_next_tab_id++;
    EditorTab newTab;
    newTab.id         = newId;
    newTab.title      = title;
    newTab.type       = type;
    newTab.asset_path = asset_path;
    newTab.open       = true;
    // Allocate a hot scene slot for new SceneEditor tabs
    if (type == EditorTabType::SceneEditor && m_host && m_host->is_open()) {
        newTab.scene_slot_id = m_host->create_scene_slot();
    }
    m_editor_tabs.push_back(std::move(newTab));
    m_focus_tab_id = newId;
}

bool EditorUI::drawPanelHeader(const char* label, int detach_idx) {
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const float  w  = ImGui::GetContentRegionAvail().x;
    const float  h  = ImGui::GetFrameHeight() + 6.0f;

    // Reserve layout space for the entire band
    ImGui::Dummy(ImVec2(w, h));
    const ImVec2 afterPos = ImGui::GetCursorScreenPos();

    // Background band
    dl->AddRectFilled(p, {p.x + w, p.y + h},
        ImGui::ColorConvertFloat4ToU32(sol::UE5Colors::BgTitlebar));
    // Left accent strip
    dl->AddRectFilled(p, {p.x + 3.0f, p.y + h},
        ImGui::ColorConvertFloat4ToU32(sol::UE5Colors::Accent));
    // Bottom separator line
    dl->AddLine({p.x, p.y + h - 1.0f}, {p.x + w, p.y + h - 1.0f},
        ImGui::ColorConvertFloat4ToU32(sol::UE5Colors::Border));

    // Panel label (drawn directly into the draw list, no ImGui item)
    const ImVec2 textSz = ImGui::CalcTextSize(label);
    dl->AddText({p.x + 8.0f, p.y + (h - textSz.y) * 0.5f},
        ImGui::ColorConvertFloat4ToU32(sol::UE5Colors::Text), label);

    // Detach button – right-aligned inside the band
    const bool  isDetached = m_panel_detached[detach_idx];
    const char* btnTxt     = isDetached ? "[<]" : "[>]";
    const float btnW       = ImGui::CalcTextSize(btnTxt).x
                             + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos({p.x + w - btnW - 4.0f,
                               p.y + (h - ImGui::GetFrameHeight()) * 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.45f, 0.8f));
    char btn[48];
    std::snprintf(btn, sizeof(btn), "%s##detach_%d", btnTxt, detach_idx);
    if (ImGui::SmallButton(btn)) {
        m_panel_detached[detach_idx] = !isDetached;
        m_first_layout = true;
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(isDetached ? "Re-dock panel" : "Pop out as floating window");

    // Restore cursor to below the reserved band
    ImGui::SetCursorScreenPos(afterPos);
    return true;
}

void EditorUI::drawEditorTabBar() {
    constexpr float kTitleH  = 32.0f;
    constexpr float kTabH    = 30.0f;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + kTitleH));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kTabH));
    ImGui::SetNextWindowViewport(vp->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoScrollbar| ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     ImVec2(2.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, sol::UE5Colors::BgTabBar);
    ImGui::Begin("##TabBarHost", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 3.0f);
    if (ImGui::BeginTabBar("##EditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        int newActiveId = m_active_tab_id;

        for (auto it = m_editor_tabs.begin(); it != m_editor_tabs.end(); ) {
            bool open = it->open;
            const bool isActive = (it->id == m_active_tab_id);

            ImGuiTabItemFlags itemFlags = ImGuiTabItemFlags_None;
            if (m_focus_tab_id == it->id) { itemFlags |= ImGuiTabItemFlags_SetSelected; m_focus_tab_id = -1; }

            char tabLabel[128];
            const bool tabDirty = (it->type == EditorTabType::ScriptEditor &&
                                   m_script_dirty.count(it->asset_path) &&
                                   m_script_dirty.at(it->asset_path));
            std::snprintf(tabLabel, sizeof(tabLabel), "%s%s###tab_%d",
                          it->title.c_str(), tabDirty ? " *" : "", it->id);

            bool* pOpen = (it->type == EditorTabType::SceneEditor) ? nullptr : &open;

            if (ImGui::BeginTabItem(tabLabel, pOpen, itemFlags)) {
                if (!isActive) {
                    // ── Save OUTGOING (currently active) SceneEditor tab ────────────────
                    for (auto& t : m_editor_tabs) {
                        if (t.id == m_active_tab_id && t.type == EditorTabType::SceneEditor) {
                            t.selected_node = m_host ? m_host->selected_node() : nullptr;
                            std::memcpy(t.hierarchy_search, m_hierarchy_search, sizeof(m_hierarchy_search));
                            if (m_get_camera) {
                                auto [p, y, pi] = m_get_camera();
                                t.cam_pos   = p;
                                t.cam_yaw   = y;
                                t.cam_pitch = pi;
                            }
                            break;
                        }
                    }

                    newActiveId = it->id;

                    // ── Restore INCOMING SceneEditor tab ───────────────────────────────
                    if (it->type == EditorTabType::SceneEditor && m_host && m_host->is_open()) {
                        clearSceneState();
                        if (it->scene_slot_id >= 0) {
                            // Hot swap — just move scene pointers, no on_ready/on_destroy
                            m_host->activate_scene_slot(it->scene_slot_id);
                        } else if (!it->asset_path.empty()) {
                            // Fallback: slot not assigned yet, load from disk
                            m_host->load_scene(it->asset_path);
                        }
                        m_host->set_selected_node(it->selected_node);
                        if (m_set_camera)
                            m_set_camera(it->cam_pos, it->cam_yaw, it->cam_pitch);
                    }
                    std::memcpy(m_hierarchy_search, it->hierarchy_search, sizeof(m_hierarchy_search));
                }
                ImGui::EndTabItem();
            }

            if (!open) {
                // Free the hot scene slot if this was a SceneEditor tab
                if (it->type == EditorTabType::SceneEditor &&
                    it->scene_slot_id >= 0 && m_host && m_host->is_open()) {
                    m_host->destroy_scene_slot(it->scene_slot_id);
                }
                it->open = false;
                it = m_editor_tabs.erase(it);
            } else {
                ++it;
            }
        }

        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
            // Count existing SceneEditor tabs to generate a numbered title
            int sceneCount = 0;
            for (const auto& t : m_editor_tabs)
                if (t.type == EditorTabType::SceneEditor) ++sceneCount;
            char newTitle[32];
            std::snprintf(newTitle, sizeof(newTitle), "Scene %d", sceneCount + 1);
            openEditorTab(EditorTabType::SceneEditor, newTitle);
        }

        m_active_tab_id = newActiveId;
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(2);

    ImGui::End();
}

void EditorUI::drawViewportToolbar() {
    // Find active tab
    EditorTab* active = nullptr;
    for (auto& t : m_editor_tabs) {
        if (t.id == m_active_tab_id) { active = &t; break; }
    }
    if (!active) return;
    // Script editor gets no toolbar
    if (active->type == EditorTabType::ScriptEditor) return;

    // Get viewport bounds from central dockspace node
    ImGuiDockNode* centralNode = (m_dockspace_id != 0)
        ? ImGui::DockBuilderGetCentralNode(m_dockspace_id) : nullptr;
    ImVec2 vpPos, vpSize;
    if (centralNode && centralNode->Size.x > 10.0f) {
        vpPos  = centralNode->Pos;
        vpSize = centralNode->Size;
    } else {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        vpPos  = ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.20f, vp->WorkPos.y + 64.0f);
        vpSize = ImVec2(vp->WorkSize.x * 0.55f, vp->WorkSize.y - 90.0f);
    }

    const float pad = 8.0f;
    const float tbH = 30.0f;

    ImGui::SetNextWindowPos(ImVec2(vpPos.x + pad, vpPos.y + pad), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vpSize.x - pad * 2.0f, tbH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f); // we draw our own bg

    const ImGuiWindowFlags tbFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));

    bool visible = ImGui::Begin("##VpToolbar", nullptr, tbFlags);

    if (visible) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wMin = ImGui::GetWindowPos();
        const ImVec2 wMax = ImVec2(wMin.x + ImGui::GetWindowWidth(), wMin.y + tbH);
        // Draw toolbar background with rounded corners
        dl->AddRectFilled(wMin, wMax, IM_COL32(22, 22, 28, 215), 5.0f);
        dl->AddRect(wMin, wMax, IM_COL32(60, 60, 75, 160), 5.0f, 0, 1.0f);

        const bool isScene = (active->type == EditorTabType::SceneEditor);

        // Vertical separator helper
        auto VSep = [&]() {
            ImGui::SameLine(0, 6);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float fh = ImGui::GetFrameHeight();
            const float h = fh * 0.65f;
            dl->AddLine(
                ImVec2(p.x, p.y + (fh - h) * 0.5f),
                ImVec2(p.x, p.y + (fh + h) * 0.5f),
                IM_COL32(80, 80, 95, 200), 1.0f);
            ImGui::Dummy(ImVec2(6.0f, fh));
            ImGui::SameLine(0, 6);
        };

        // vertically centre content
        ImGui::SetCursorPos(ImVec2(8.0f, (tbH - ImGui::GetFrameHeight()) * 0.5f));

        if (isScene) {
            // ── Gizmo ─────────────────────────────────────────────────────────
            int gizmoOp = 0;
            if (m_host) switch (m_host->gizmo_mode()) {
                case sol::EditorGizmoMode::Rotate: gizmoOp = 1; break;
                case sol::EditorGizmoMode::Scale:  gizmoOp = 2; break;
                default: break;
            }
            auto gizBtn = [&](const char* lbl, int op, const char* tip) {
                const bool act = (gizmoOp == op);
                if (act) ImGui::PushStyleColor(ImGuiCol_Button, sol::UE5Colors::Accent);
                if (ImGui::SmallButton(lbl) && m_host && m_host->is_open())
                    m_host->set_gizmo_operation(op);
                if (act) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(0, 2);
            };
            gizBtn("T", 0, "Translate [T]");
            gizBtn("R", 1, "Rotate [R]");
            gizBtn("S", 2, "Scale [Y]");
            if (m_host && m_host->is_open()) {
                const bool loc = m_host->gizmo_space_local();
                if (ImGui::SmallButton(loc ? "L" : "W")) m_host->set_gizmo_space(!loc);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Local/World space");
            }
            VSep();

            // ── Shading mode ───────────────────────────────────────────────────
            static const char* kShading[] = { "Lit", "Unlit", "Wireframe" };
            ImGui::SetNextItemWidth(74.0f);
            ImGui::Combo("##shading", &active->viewport_shading, kShading, 3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shading mode\nUnlit shows albedo only\nWireframe: stub (coming soon)");
            ImGui::SameLine(0, 4);

            // ── Debug view ─────────────────────────────────────────────────────
            ImGui::BeginDisabled(active->viewport_shading != 0);
            static const char* kDebugViews[] = {
                "Debug: Off", "Albedo", "Normals", "Metallic",
                "Roughness", "AO", "Emissive", "Cascade Debug"
            };
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("##dbg", &active->debug_view, kDebugViews, 8);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Debug buffer visualisation\n(disabled in Unlit/Wireframe mode)");
            ImGui::EndDisabled();
            VSep();

            // ── Play / Stop ────────────────────────────────────────────────────
            if (!m_play_mode) {
                ImGui::PushStyleColor(ImGuiCol_Button, sol::UE5Colors::Green);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.75f, 0.35f, 1.0f));
                if (ImGui::SmallButton(ICON_FA_PLAY " Play")) { m_play_mode = true; startGameProcess(); }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play Scene");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, sol::UE5Colors::Red);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
                if (ImGui::SmallButton(ICON_FA_STOP " Stop")) stopGameProcess();
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop Game");
            }
            VSep();

            // ── Camera speed ───────────────────────────────────────────────────
            ImGui::TextDisabled("Cam");
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(50.0f);
            ImGui::DragFloat("##cs", &m_prefs.camSpeedMult, 0.05f, 0.1f, 20.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera fly speed multiplier");

            // ── Save (right-aligned) ───────────────────────────────────────────
            const float saveW = 46.0f;
            const float rightX = ImGui::GetWindowWidth() - saveW - 10.0f;
            if (ImGui::GetCursorPosX() < rightX) ImGui::SameLine();
            ImGui::SetCursorPosX(rightX);
            if (ImGui::SmallButton("Save")) {
                if (m_host && m_host->is_open()) m_host->save_scene();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save Scene (Ctrl+S)");

        } else {
            // ── Non-scene stubs: minimal label ─────────────────────────────────
            ImGui::TextDisabled("%s", active->title.c_str());
        }
    }

    ImGui::PopStyleVar(4);
    ImGui::End();
}

void EditorUI::drawSubEditorStub(const EditorTab& tab) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float topOff = 62.0f;
    const float botOff = 22.0f;

    // Use the actual central dockspace node bounds if available, otherwise fall back to estimate
    ImVec2 subPos, subSize;
    ImGuiDockNode* centralNode = (m_dockspace_id != 0)
        ? ImGui::DockBuilderGetCentralNode(m_dockspace_id) : nullptr;
    if (centralNode) {
        subPos  = centralNode->Pos;
        subSize = centralNode->Size;
    } else {
        subPos  = ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.20f, vp->WorkPos.y + topOff + 2.0f);
        subSize = ImVec2(vp->WorkSize.x * 0.55f, vp->WorkSize.y - topOff - botOff - 4.0f);
    }

    ImGui::SetNextWindowPos(subPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(subSize, ImGuiCond_Always);

    const ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoBringToFrontOnFocus;

    const float bgAlpha = (tab.type == EditorTabType::ScriptEditor) ? 1.0f : 0.92f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.15f, bgAlpha));
    char winId[128];
    std::snprintf(winId, sizeof(winId), "%s###SubEditor_%d", tab.title.c_str(), tab.id);
    if (!ImGui::Begin(winId, nullptr, wf)) { ImGui::PopStyleColor(); ImGui::End(); return; }
    ImGui::PopStyleColor();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    switch (tab.type) {

    case EditorTabType::MaterialEditor: {
        // ── Resolve target node ───────────────────────────────────────────
        // Auto-track selected node unless pinned
        {
            sol::Node* sel = nullptr;
            for (auto& t2 : m_editor_tabs)
                if (t2.type == EditorTabType::SceneEditor && t2.selected_node)
                    { sel = t2.selected_node; break; }
            if (sel != m_mat_target_node) {
                m_mat_target_node      = sel;
                m_mat_selected_submesh = -1;
                m_mat_cache_valid      = false;
            }
        }

        auto* meshNode  = dynamic_cast<sol::MeshNode*> (m_mat_target_node);
        auto* modelNode = dynamic_cast<sol::ModelNode*>(m_mat_target_node);

        // ── Layout: Properties (left) | Preview (right) ──────────────────
        const float previewW = 220.0f;
        const float propsW   = avail.x - previewW - 8.0f;

        // ── Left: Properties ─────────────────────────────────────────────
        ImGui::BeginChild("##mat_props", ImVec2(propsW, -1), false);

        // Target header
        if (m_mat_target_node) {
            ImGui::TextColored({0.4f,0.9f,0.4f,1.0f}, "%s  [%s]",
                m_mat_target_node->name.c_str(),
                m_mat_target_node->type_name());
        } else {
            ImGui::TextDisabled("No node selected \xe2\x80\x94 select a MeshNode or ModelNode");
        }
        ImGui::Separator();

        // Resolve which Material* to edit
        sol::Material* mat = nullptr;

        if (meshNode) {
            if (meshNode->submesh_count() > 0) {
                // GLB with sub-meshes: show picker
                ImGui::Text("Sub-meshes (%d):", meshNode->submesh_count());
                for (int i = 0; i < meshNode->submesh_count(); i++) {
                    char lbl[32]; snprintf(lbl, sizeof(lbl), "Mesh %d##sm%d", i, i);
                    bool sel = (m_mat_selected_submesh == i);
                    if (ImGui::Selectable(lbl, sel))
                        { m_mat_selected_submesh = i; m_mat_cache_valid = false; }
                }
                ImGui::Separator();
                if (m_mat_selected_submesh < 0) m_mat_selected_submesh = 0;
                mat = meshNode->submesh_material(m_mat_selected_submesh);
            } else {
                // Primitive — single material
                mat = &meshNode->material;
                m_mat_selected_submesh = -1;
            }
        } else if (modelNode) {
            if (modelNode->submesh_count() > 0) {
                ImGui::Text("Sub-meshes (%d):", modelNode->submesh_count());
                for (int i = 0; i < modelNode->submesh_count(); i++) {
                    char lbl[32]; snprintf(lbl, sizeof(lbl), "Mesh %d##sm%d", i, i);
                    bool sel = (m_mat_selected_submesh == i);
                    if (ImGui::Selectable(lbl, sel))
                        { m_mat_selected_submesh = i; m_mat_cache_valid = false; }
                }
                ImGui::Separator();
                if (m_mat_selected_submesh < 0) m_mat_selected_submesh = 0;
                mat = modelNode->submesh_material(m_mat_selected_submesh);
            }
        }

        if (mat) {
            bool changed = false;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 5));

            // ── Base ─────────────────────────────────────────────────────
            if (collapsingHeaderAccented("Base", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushItemWidth(-120.0f);
                changed |= ImGui::ColorEdit4("Base Color##mat",
                    glm::value_ptr(mat->base_color), ImGuiColorEditFlags_DisplayRGB);
                changed |= ImGui::SliderFloat("Metallic##mat",  &mat->metallic,  0.0f, 1.0f, "%.3f");
                changed |= ImGui::SliderFloat("Roughness##mat", &mat->roughness, 0.0f, 1.0f, "%.3f");
                ImGui::PopItemWidth();
            }

            // ── Emissive ──────────────────────────────────────────────────
            if (collapsingHeaderAccented("Emissive", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushItemWidth(-120.0f);
                changed |= ImGui::ColorEdit3("Emissive##mat",
                    glm::value_ptr(mat->emissive), ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                ImGui::PopItemWidth();
            }

            // ── Transparency ──────────────────────────────────────────────
            if (collapsingHeaderAccented("Transparency")) {
                const char* alphaLabels[] = { "Opaque", "Mask", "Blend" };
                int alphaInt = (int)(uint8_t)mat->alpha_mode;
                ImGui::PushItemWidth(-120.0f);
                if (ImGui::Combo("Alpha Mode##mat", &alphaInt, alphaLabels, 3)) {
                    mat->alpha_mode = (sol::AlphaMode)(uint8_t)alphaInt;
                    changed = true;
                }
                if (mat->alpha_mode == sol::AlphaMode::Mask)
                    changed |= ImGui::SliderFloat("Alpha Cutoff##mat", &mat->alpha_cutoff, 0.0f, 1.0f, "%.3f");
                ImGui::PopItemWidth();
            }

            // ── Flags ──────────────────────────────────────────────────────
            if (collapsingHeaderAccented("Flags")) {
                changed |= ImGui::Checkbox("Lit##mat",          &mat->lit);
                ImGui::SameLine(0, 16.0f);
                changed |= ImGui::Checkbox("Double Sided##mat", &mat->double_sided);
            }

            // ── Texture Slots (primitive MeshNode only) ───────────────────
            bool showTexSlots = (meshNode && meshNode->submesh_count() == 0);
            if (showTexSlots && collapsingHeaderAccented("Textures")) {
                auto texSlot = [&](const char* label, std::string& pathRef) -> bool {
                    char buf[512];
                    strncpy(buf, pathRef.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf)-1] = '\0';
                    ImGui::PushItemWidth(-80.0f);
                    bool edited = ImGui::InputText(label, buf, sizeof(buf));
                    ImGui::PopItemWidth();
                    if (edited) pathRef = buf;
                    ImGui::SameLine();
                    char btnId[48]; snprintf(btnId, sizeof(btnId), "...##tb_%s", label);
                    if (ImGui::Button(btnId)) {
                        std::string p = EditorUI::openFileDialog(
                            "Image Files (*.png *.jpg *.jpeg *.tga *.bmp);;All Files (*.*)",
                            std::string("Select ") + label + " Texture");
                        if (!p.empty()) { pathRef = p; edited = true; }
                    }
                    return edited;
                };

                changed |= texSlot("Albedo##tslot",       meshNode->mat_albedo_path);
                changed |= texSlot("Normal Map##tslot",   meshNode->mat_normal_path);
                changed |= texSlot("Metal/Rough##tslot",  meshNode->mat_mr_path);
                changed |= texSlot("Emissive Tex##tslot", meshNode->mat_emissive_path);

                ImGui::Spacing();
                if (ImGui::Button("Reload Textures")) {
                    if (m_host) m_host->apply_mesh_material_textures(meshNode);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(loads from paths above)");
            }

            ImGui::PopStyleVar();

            // Mark preview dirty if anything changed
            if (changed) m_mat_cache_valid = false;

        } else if (!m_mat_target_node) {
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Select a MeshNode or ModelNode in the Hierarchy to edit its material here.\n\n"
                "Changes apply immediately to the scene viewport.");
        } else {
            ImGui::TextDisabled("This node has no editable material.");
        }

        ImGui::EndChild();

        // ── Right: Preview ────────────────────────────────────────────────
        ImGui::SameLine();
        ImGui::BeginChild("##mat_preview", ImVec2(previewW - 4.0f, -1), true);

        // Title
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160,160,180,255));
            ImGui::Text(ICON_FA_CIRCLE "  Material Preview");
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        if (mat) {
            // ── Recompute sphere preview if dirty ──────────────────────────
            if (!m_mat_cache_valid) {
                // PBR sphere rasteriser (CPU, 48×48 grid)
                constexpr int W = 48;
                const glm::vec3 albedo    = glm::vec3(mat->base_color);
                const float     metallic  = mat->metallic;
                const float     roughness = glm::max(mat->roughness, 0.04f);
                const glm::vec3 emissive  = mat->emissive;
                const glm::vec3 F0 = glm::mix(glm::vec3(0.04f), albedo, metallic);
                // Light
                const glm::vec3 L  = glm::normalize(glm::vec3(-0.8f, 0.9f, 1.0f));
                const glm::vec3 V  = glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec3 H  = glm::normalize(L + V);
                const float previewR = (float)(W) * 0.45f;

                for (int py = 0; py < W; py++) {
                    for (int px = 0; px < W; px++) {
                        float dx = ((float)px - W * 0.5f) / previewR;
                        float dy = ((float)py - W * 0.5f) / previewR;
                        float r2 = dx*dx + dy*dy;
                        ImU32 col;
                        if (r2 > 1.0f) {
                            col = IM_COL32(28, 28, 33, 255); // background
                        } else {
                            float z = sqrtf(1.0f - r2);
                            glm::vec3 N = glm::normalize(glm::vec3(dx, -dy, z));
                            float NdotL = glm::max(0.0f, glm::dot(N, L));
                            float NdotV = glm::max(0.001f, glm::dot(N, V));
                            float NdotH = glm::max(0.0f, glm::dot(N, H));
                            float HdotV = glm::max(0.0f, glm::dot(H, V));

                            // GGX Distribution
                            float a  = roughness * roughness;
                            float a2 = a * a;
                            float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
                            float D  = a2 / (3.14159f * d * d + 0.0001f);

                            // Fresnel-Schlick
                            glm::vec3 F = F0 + (glm::vec3(1.0f) - F0) * powf(1.0f - HdotV, 5.0f);

                            // Smith geometry
                            float k  = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
                            float GV = NdotV / (NdotV * (1.0f - k) + k);
                            float GL = NdotL / (NdotL * (1.0f - k) + k + 0.0001f);
                            float G  = GV * GL;

                            // Specular BRDF
                            glm::vec3 spec = (D * F * G) /
                                             (4.0f * NdotV * NdotL + 0.001f);

                            // Diffuse (Lambert)
                            glm::vec3 kD   = (1.0f - F) * (1.0f - metallic);
                            glm::vec3 diff = kD * albedo / 3.14159f;

                            // Combine + ambient + emissive
                            glm::vec3 lightColor = glm::vec3(2.8f);
                            glm::vec3 c = (diff + spec) * NdotL * lightColor
                                        + albedo * 0.08f   // ambient
                                        + emissive;

                            // ACES tone-map
                            c = c * (2.51f * c + 0.03f) / (c * (2.43f * c + 0.59f) + 0.14f);
                            c = glm::clamp(c, 0.0f, 1.0f);

                            col = IM_COL32(
                                (int)(c.r * 255.0f),
                                (int)(c.g * 255.0f),
                                (int)(c.b * 255.0f),
                                255);
                        }
                        m_mat_preview_pixels[py * W + px] = col;
                    }
                }
                m_mat_cache_valid = true;
            }

            // Draw preview sphere (48×48 grid → each cell = ~4px)
            {
                constexpr int W     = 48;
                const float  cellPx = (previewW - 24.0f) / (float)W;
                ImVec2 origin2 = ImGui::GetCursorScreenPos();
                ImDrawList* pdl = ImGui::GetWindowDrawList();

                // Shadow
                float sz = cellPx * W;
                pdl->AddCircleFilled(
                    ImVec2(origin2.x + sz*0.5f + 3.0f, origin2.y + sz*0.5f + 4.0f),
                    sz * 0.45f, IM_COL32(0, 0, 0, 70));

                for (int py = 0; py < W; py++) {
                    for (int px = 0; px < W; px++) {
                        ImU32 c = m_mat_preview_pixels[py * W + px];
                        pdl->AddRectFilled(
                            ImVec2(origin2.x + px * cellPx,
                                   origin2.y + py * cellPx),
                            ImVec2(origin2.x + (px+1) * cellPx,
                                   origin2.y + (py+1) * cellPx),
                            c);
                    }
                }

                ImGui::Dummy(ImVec2(sz, sz));
            }

            ImGui::Separator();
            ImGui::TextDisabled("BC: %.2f %.2f %.2f", mat->base_color.r, mat->base_color.g, mat->base_color.b);
            ImGui::TextDisabled("M:  %.2f  R: %.2f",  mat->metallic, mat->roughness);
            if (glm::length(mat->emissive) > 0.001f)
                ImGui::TextDisabled("Em: %.2f %.2f %.2f", mat->emissive.r, mat->emissive.g, mat->emissive.b);

        } else {
            ImGui::TextDisabled("Select a material to preview.");
        }

        ImGui::EndChild();
        break;
    }

    case EditorTabType::ShaderEditor: {
        ImGui::TextDisabled("Shader Editor  \xe2\x80\x94  Work in progress");
        ImGui::Separator();
        static char shaderBuf[2048] =
            "// SolEngine GLSL Shader\n"
            "// Stub \xe2\x80\x94 not yet connected to the renderer\n\n"
            "#version 450\n\n"
            "layout(location = 0) in  vec3 v_world_pos;\n"
            "layout(location = 1) in  vec3 v_normal;\n"
            "layout(location = 0) out vec4 out_color;\n\n"
            "void main() {\n"
            "    vec3 N = normalize(v_normal);\n"
            "    out_color = vec4(N * 0.5 + 0.5, 1.0);\n"
            "}\n";
        ImGui::PushFont(nullptr);
        ImGui::InputTextMultiline("##shader_src", shaderBuf, sizeof(shaderBuf),
            ImVec2(-1.0f, avail.y - 60.0f),
            ImGuiInputTextFlags_AllowTabInput);
        ImGui::PopFont();
        ImGui::BeginDisabled();
        ImGui::Button("Compile");
        ImGui::SameLine();
        ImGui::Button("Apply to Selection");
        ImGui::SameLine();
        ImGui::TextDisabled("  Shader compiler not yet wired up");
        ImGui::EndDisabled();
        break;
    }

    case EditorTabType::ModelViewer: {
        ImGui::TextDisabled("Model Viewer  \xe2\x80\x94  Work in progress");
        ImGui::Separator();
        if (!tab.asset_path.empty())
            ImGui::Text("Asset: %s", tab.asset_path.c_str());
        else
            ImGui::TextDisabled("No model loaded. Drag a .glb/.gltf from the Asset Browser.");
        ImGui::Spacing();
        ImGui::TextDisabled("Model properties, LOD settings, and collision preview will appear here.");
        ImGui::Spacing();
        ImGui::BeginDisabled();
        ImGui::Button("Import Settings");
        ImGui::SameLine();
        ImGui::Button("Generate LODs");
        ImGui::SameLine();
        ImGui::Button("Export");
        ImGui::EndDisabled();
        break;
    }

    case EditorTabType::TextureViewer: {
        ImGui::TextDisabled("Texture Viewer  \xe2\x80\x94  Work in progress");
        ImGui::Separator();
        if (!tab.asset_path.empty())
            ImGui::Text("Asset: %s", tab.asset_path.c_str());
        else
            ImGui::TextDisabled("No texture loaded.");
        ImGui::Spacing();
        ImGui::TextDisabled("Texture preview, mipmap explorer, and compression settings will appear here.");
        break;
    }

    case EditorTabType::ScriptEditor: {
        const std::string& spath = tab.asset_path;
        if (spath.empty()) {
            ImGui::TextDisabled("No script file associated with this tab.");
            break;
        }

        // Ctrl+scroll zoom (captured before InputText consumes scroll)
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f && ImGui::GetIO().KeyCtrl) {
                m_script_font_scale = std::clamp(m_script_font_scale + wheel * 0.1f, 0.5f, 4.0f);
            }
        }
        ImGui::SetWindowFontScale(m_script_font_scale);

        // Ensure buffer is loaded
        auto it = m_script_buffers.find(spath);
        if (it == m_script_buffers.end()) {
            FILE* f = std::fopen(spath.c_str(), "rb");
            if (f) {
                std::fseek(f, 0, SEEK_END);
                const long len = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                constexpr long kMaxScript = 65536;
                const long cap = std::min(len, kMaxScript) + 1;
                std::vector<char> buf(static_cast<size_t>(cap), '\0');
                std::fread(buf.data(), 1, static_cast<size_t>(len), f);
                std::fclose(f);
                m_script_buffers[spath] = std::move(buf);
                m_script_dirty[spath]   = false;
                it = m_script_buffers.find(spath);
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Cannot open: %s", spath.c_str());
                break;
            }
        }

        std::vector<char>& buf = it->second;
        if (buf.size() < 256) buf.resize(256, '\0');
        const size_t used = std::strlen(buf.data());
        if (used + 128 >= buf.size())
            buf.resize(buf.size() * 2, '\0');

        bool& dirty = m_script_dirty[spath];

        // Helper: save to disk
        auto doSave = [&]() {
            FILE* f = std::fopen(spath.c_str(), "wb");
            if (f) {
                std::fwrite(buf.data(), 1, std::strlen(buf.data()), f);
                std::fclose(f);
                dirty = false;
                sol::log::info(std::string("[Editor] Saved: ") + spath);
                if (m_host->engine().has_script())
                    m_host->engine().script().reload_script(spath);
            } else {
                sol::log::error(std::string("[Editor] Cannot write: ") + spath);
            }
        };

        // Helper: reload from disk
        auto doReload = [&]() {
            FILE* f = std::fopen(spath.c_str(), "rb");
            if (f) {
                std::fseek(f, 0, SEEK_END);
                const long len = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                constexpr long kMaxScript = 65536;
                const long cap = std::min(len, kMaxScript) + 1;
                buf.assign(static_cast<size_t>(cap), '\0');
                std::fread(buf.data(), 1, static_cast<size_t>(len), f);
                std::fclose(f);
                dirty = false;
            }
        };

        // Edit callback to mark dirty
        struct EditCbData { bool* dirty; };
        static EditCbData cbData;
        cbData.dirty = &dirty;
        auto editCb = [](ImGuiInputTextCallbackData*) -> int {
            *cbData.dirty = true;
            return 0;
        };

        // Full-area text editor
        ImGui::InputTextMultiline("##script_src", buf.data(), buf.size(),
            ImVec2(-1.0f, -1.0f),
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackEdit,
            editCb);

        // Keyboard shortcuts (window must be focused)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            const bool ctrl = ImGui::GetIO().KeyCtrl;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) doSave();
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_R, false)) doReload();
        }

        ImGui::SetWindowFontScale(1.0f);
        break;
    }

    default:
        ImGui::TextDisabled("Unknown editor type.");
        break;
    }

    ImGui::End();
}

void EditorUI::drawHierarchyPanel() {
    ImGui::Begin("Hierarchy");
    drawPanelHeader("Hierarchy", 0);

    if (!m_host || !m_host->is_open()) {
        ImGui::TextDisabled("(no project open)");
        ImGui::End();
        return;
    }

    sol::Node* root = m_host->scene_root();
    if (!root) {
        ImGui::TextDisabled("(empty)");
        ImGui::End();
        return;
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        sol::Node* selected = m_host->selected_node();
        if (selected && selected->parent() && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            m_rename_active = true;
            m_rename_node = selected;
            std::strncpy(m_rename_buf, selected->name.c_str(), sizeof(m_rename_buf) - 1);
            m_rename_buf[sizeof(m_rename_buf) - 1] = '\0';
        }
        if (selected && selected->parent() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (!m_prefs.confirmDelete) {
                if (m_host->selected_node() == selected) m_host->set_selected_node(nullptr);
                m_host->remove_node(selected);
            } else {
                m_delete_confirm_open = true;
                m_delete_confirm_node = selected;
            }
        }
    }

    if (sol::SolIconButton(ICON_FA_PLUS " Add", "Add a new node to the scene")) {
        m_context_menu_node = root;
        m_show_add_child_popup = true;
        ImGui::OpenPopup("AddNodePopup");
    }
    ImGui::SameLine();
    {
        sol::Node* sel = m_host->selected_node();
        const bool hasScript = sel && !sel->script_path.empty();
        const ImVec4 btnCol = hasScript
            ? ImVec4(0.22f, 0.45f, 0.22f, 1.0f)
            : ImVec4(0.20f, 0.20f, 0.38f, 1.0f);
        const char* btnLabel = hasScript ? ICON_FA_PENCIL " Edit Script" : ICON_FA_CODE " Script";
        if (sol::SolIconButton(btnLabel,
                hasScript ? "Open the script attached to the selected node"
                          : "Create and attach a new Lua script to the selected node",
                btnCol)) {
            if (sel) {
                if (hasScript) openScriptEditorTab(sel->script_path);
                else           m_pending_new_script_node = sel;
            }
        }
    }
    ImGui::Separator();

    // Search filter
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search nodes...", m_hierarchy_search, sizeof(m_hierarchy_search));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseClicked(1)) {
        m_hierarchy_search[0] = '\0';
    }
    ImGui::Separator();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
    drawNodeTree(root, m_hierarchy_search[0] ? m_hierarchy_search : nullptr);
    ImGui::PopStyleVar();

    if (m_show_add_child_popup && ImGui::BeginPopup("AddNodePopup")) {
        sol::Node* parentTarget = m_context_menu_node ? m_context_menu_node : root;

        // Search filter — auto-cleared on each popup open
        static char s_add_node_filter[64] = {};
        if (ImGui::IsWindowAppearing()) {
            s_add_node_filter[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##addfilter", ICON_FA_SEARCH " Search nodes...",
                                 s_add_node_filter, sizeof(s_add_node_filter));
        ImGui::Separator();

        const bool filtering = s_add_node_filter[0] != '\0';
        if (filtering) {
            const std::string filterLow = toLower(s_add_node_filter);
            bool anyMatch = false;
            for (const auto& cat : kNodeCategories) {
                for (int i = 0; i < cat.count; ++i) {
                    const char* type = cat.types[i];
                    if (toLower(type).find(filterLow) != std::string::npos) {
                        anyMatch = true;
                        char label[128];
                        std::snprintf(label, sizeof(label), "%s  %s", nodePrefix(type), type);
                        if (ImGui::MenuItem(label)) {
                            if (sol::Node* newNode = m_host->create_node(type, parentTarget))
                                m_host->set_selected_node(newNode);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }
            if (!anyMatch)
                ImGui::TextDisabled("No matching nodes");
        } else {
            drawAddNodeCategories(m_host, parentTarget);
        }

        ImGui::EndPopup();
    } else {
        m_show_add_child_popup = false;
    }

    ImGui::End();
}

void EditorUI::drawNodeTree(sol::Node* node, const char* filter) {
    if (!node) {
        return;
    }

    // Filter: skip subtrees with no match (but always show root)
    if (filter && filter[0] != '\0' && node->parent() != nullptr) {
        std::function<bool(sol::Node*)> hasMatch = [&](sol::Node* n) -> bool {
            if (!n) return false;
            const std::string nameLow = toLower(n->name);
            const std::string filtLow = toLower(filter);
            if (nameLow.find(filtLow) != std::string::npos) return true;
            for (const auto& child : n->children()) {
                if (hasMatch(child.get())) return true;
            }
            return false;
        };
        if (!hasMatch(node)) return;
    }

    const bool hasChildren = !node->children().empty();
    const bool isSelected = node == m_host->selected_node();
    const bool isRoot = node->parent() == nullptr;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (isRoot || (filter && filter[0] != '\0')) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushID(node);

    bool open = false;
    if (m_rename_active && m_rename_node == node) {
        open = ImGui::TreeNodeEx("##node", flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_host->set_selected_node(node);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", m_rename_buf, sizeof(m_rename_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            const std::string oldName = node->name;
            m_host->rename_node(node, m_rename_buf);
            pushUndoRename(node, oldName, m_rename_buf);
            m_rename_active = false;
            m_rename_node = nullptr;
        }
        if (!ImGui::IsItemActive() && !ImGui::IsItemFocused()) {
            m_rename_active = false;
            m_rename_node = nullptr;
        }
    } else {
        char label[320];
        const char* luaTag = node->script_path.empty() ? "" : " \xf0\x9f\x93\x84"; // 📄
        std::snprintf(label, sizeof(label), "%s  %s%s##%p",
            nodePrefix(node->type_name()), node->name.c_str(),
            luaTag, static_cast<void*>(node));
        open = ImGui::TreeNodeEx(label, flags);

        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            m_host->set_selected_node(node);
            // If this node has a script and a Script Editor tab is already open, update it.
            if (!node->script_path.empty()) {
                for (auto& tab : m_editor_tabs) {
                    if (tab.type == EditorTabType::ScriptEditor) {
                        tab.asset_path = node->script_path;
                        tab.title = std::filesystem::path(node->script_path).filename().string();
                        m_focus_tab_id = tab.id;
                        break;
                    }
                }
            }
        }

        if (!isRoot && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_rename_active = true;
            m_rename_node = node;
            std::strncpy(m_rename_buf, node->name.c_str(), sizeof(m_rename_buf) - 1);
            m_rename_buf[sizeof(m_rename_buf) - 1] = '\0';
        }
    }

    if (ImGui::BeginPopupContextItem("NodeContextMenu")) {
        m_host->set_selected_node(node);
        if (ImGui::BeginMenu("Add Child")) {
            drawAddNodeCategories(m_host, node);
            ImGui::EndMenu();
        }

        if (node->parent()) {
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                sol::Node* dup = m_host->duplicate_node(node);
                if (dup) m_host->set_selected_node(dup);
            }
            if (ImGui::MenuItem("Rename")) {
                m_rename_active = true;
                m_rename_node = node;
                std::strncpy(m_rename_buf, node->name.c_str(), sizeof(m_rename_buf) - 1);
                m_rename_buf[sizeof(m_rename_buf) - 1] = '\0';
            }
            if (ImGui::MenuItem("Delete")) {
                if (!m_prefs.confirmDelete) {
                    if (m_host->selected_node() == node) m_host->set_selected_node(nullptr);
                    m_host->remove_node(node);
                } else {
                    m_delete_confirm_open = true;
                    m_delete_confirm_node = node;
                }
            }
        }
        ImGui::EndPopup();
    }

    if (hasChildren && open) {
        for (const auto& child : node->children()) {
            drawNodeTree(child.get(), filter);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void EditorUI::drawInspectorPanel() {
    ImGui::Begin("Inspector");
    drawPanelHeader("Inspector", 1);

    sol::Node* node = m_host ? m_host->selected_node() : nullptr;
    if (!node) {
        ImGui::TextDisabled("(no node selected)");
        ImGui::End();
        return;
    }

    std::string name = node->name;
    ImGui::PushItemWidth(-1.0f);
    if (inputTextString("##name", name)) {
        m_host->rename_node(node, name);
    }
    ImGui::PopItemWidth();

    // Type badge + path on same row
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.75f, 1.0f, 1.0f));
    ImGui::TextUnformatted(node->type_name());
    ImGui::PopStyleColor();
    {
        std::string path = node->name;
        const sol::Node* cur = node->parent();
        while (cur) {
            path = cur->name + " / " + path;
            cur = cur->parent();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", path.c_str());
    }
    ImGui::Separator();

    drawNodeFields(node);

    ImGui::End();
}

void EditorUI::drawNodeFields(sol::Node* node) {
    const sol::TypeDesc* desc = sol::ComponentRegistry::instance().find(node->type_name());
    if (!desc) {
        return;
    }

    if (!sol::SolSection(node->type_name())) {
        return;
    }

    ImGui::PushID(node);

    bool section_open = true;

    for (const auto& field : desc->fields) {
        if (field.type == sol::FieldType::SectionHeader) {
            section_open = sol::SolSection(field.name);
            continue;
        }
        if (!section_open) continue;
        const std::string fname = field.name;
        void* ptr = field.ptr(node);
        if (!ptr) continue;

        ImGui::PushID(fname.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(field.name);
        ImGui::SameLine(130.0f);
        const float contentWidth = ImGui::GetContentRegionAvail().x;

        // Capture pre-interaction value for undo
        std::vector<uint8_t> pre_raw;
        std::string          pre_str;
        captureFieldValue(field.type, ptr, pre_raw, pre_str);

        bool changed          = false;
        bool field_activated  = false;
        bool field_deactivated = false;

        switch (field.type) {
        case sol::FieldType::Float: {
            float v = *static_cast<float*>(ptr);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##f", &v, 0.01f)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::Int: {
            int v = *static_cast<int*>(ptr);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt("##i", &v)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::Bool: {
            bool v = *static_cast<bool*>(ptr);
            if (ImGui::Checkbox("##b", &v)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::EnumInt: {
            int v = *static_cast<int*>(ptr);
            ImGui::SetNextItemWidth(-1.0f);
            if (!field.enum_labels.empty()) {
                if (ImGui::Combo("##e", &v, field.enum_labels.data(), static_cast<int>(field.enum_labels.size()))) {
                    m_host->set_field(node, fname, &v);
                    changed = true;
                }
            } else if (ImGui::DragInt("##e", &v)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::Vec3: {
            glm::vec3 v = *static_cast<glm::vec3*>(ptr);
            if (sol::SolVec3("##v3", &v, 0.01f, &field_activated, &field_deactivated)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::Vec4: {
            glm::vec4 v = *static_cast<glm::vec4*>(ptr);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat4("##v4", glm::value_ptr(v), 0.01f)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::Color3: {
            glm::vec3 v = *static_cast<glm::vec3*>(ptr);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::ColorEdit3("##c3", glm::value_ptr(v), ImGuiColorEditFlags_Float)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::Color4: {
            glm::vec4 v = *static_cast<glm::vec4*>(ptr);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::ColorEdit4("##c4", glm::value_ptr(v), ImGuiColorEditFlags_Float)) {
                m_host->set_field(node, fname, &v);
                changed = true;
            }
            break;
        }
        case sol::FieldType::String: {
            std::string value = *static_cast<std::string*>(ptr);
            ImGui::SetNextItemWidth(-1.0f);
            if (inputTextString("##s", value)) {
                m_host->set_field(node, fname, &value);
                changed = true;
            }
            break;
        }
        case sol::FieldType::AssetPath: {
            std::string value = *static_cast<std::string*>(ptr);
            bool browse = false;
            if (sol::SolAssetPicker("##ap", value, &browse)) {
                m_host->set_field(node, fname, &value);
                changed = true;
            }
            if (browse) {
                const std::string lowerName = toLower(fname);
                const bool isHdr = lowerName.find("hdr") != std::string::npos ||
                                   lowerName.find("sky") != std::string::npos ||
                                   lowerName.find("environment") != std::string::npos;
                if (isHdr) {
                    m_pending_hdr_dialog = true;
                    m_pending_hdr_field  = fname;
                    m_pending_hdr_node   = node;
                } else {
                    m_pending_asset_dialog = true;
                    m_pending_asset_field  = fname;
                    m_pending_asset_node   = node;
                }
            }
            break;
        }
        } // end switch

        // Undo tracking: record drag start on activation, commit on deactivation
        // field_activated/field_deactivated are set by multi-item widgets (e.g. SolVec3).
        const bool item_activated   = field_activated  || ImGui::IsItemActivated();
        const bool item_deactivated = field_deactivated || ImGui::IsItemDeactivatedAfterEdit();

        if (item_activated) {
            m_drag_capture.active    = true;
            m_drag_capture.node      = node;
            m_drag_capture.field     = fname;
            m_drag_capture.start_raw = pre_raw;
            m_drag_capture.start_str = pre_str;
        }

        if (item_deactivated &&
            m_drag_capture.active &&
            m_drag_capture.node == node &&
            m_drag_capture.field == fname)
        {
            std::vector<uint8_t> after_raw;
            std::string          after_str;
            captureFieldValue(field.type, ptr, after_raw, after_str);
            pushUndoField(node, fname, field.type,
                          m_drag_capture.start_raw, m_drag_capture.start_str,
                          after_raw, after_str);
            m_drag_capture.active = false;
        }

        (void)changed;
        ImGui::PopID();
    }

    ImGui::PopID();

    // ── Universal Script Section ─────────────────────────────────────────────
    // Show a Script collapsing header on ALL nodes (script_path lives on base Node).
    ImGui::PushID("##ScriptSection");
    const bool scriptOpen = sol::SolSection("Script", 0, false);
    if (scriptOpen) {
        ImGui::PushID("script_path_row");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Script Path");
        ImGui::SameLine(130.0f);

        std::string sp = node->script_path;
        bool lua_browse = false;
        if (sol::SolAssetPicker("##sp", sp, &lua_browse)) {
            node->script_path = sp;
        }
        if (lua_browse) {
            m_pending_lua_dialog = true;
            m_pending_lua_node   = node;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse for a .lua script");
        ImGui::PopID();

        if (!node->script_path.empty()) {
            ImGui::Indent(8.0f);
            if (ImGui::SmallButton("Edit Script")) {
                // Load script content into buffer and open editor tab
                const std::string& spath = node->script_path;
                if (m_script_buffers.find(spath) == m_script_buffers.end()) {
                    FILE* f = std::fopen(spath.c_str(), "rb");
                    if (f) {
                        std::fseek(f, 0, SEEK_END);
                        const long len = std::ftell(f);
                        std::fseek(f, 0, SEEK_SET);
                        constexpr long kMaxScript = 65536;
                        const long cap = std::min(len, kMaxScript) + 1;
                        std::vector<char> buf(static_cast<size_t>(cap), '\0');
                        std::fread(buf.data(), 1, static_cast<size_t>(len), f);
                        std::fclose(f);
                        m_script_buffers[spath] = std::move(buf);
                    } else {
                        // New file — start with empty template
                        const std::string tpl =
                            "local M = {}\nM.__index = M\n\n"
                            "function M:on_ready()\nend\n\n"
                            "function M:on_update(dt)\nend\n\n"
                            "return M\n";
                        std::vector<char> buf(tpl.begin(), tpl.end());
                        buf.push_back('\0');
                        m_script_buffers[spath] = std::move(buf);
                    }
                }
                // Derive a short title from the filename
                const std::filesystem::path fp(spath);
                openScriptEditorTab(spath);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Reload")) {
                if (m_host->engine().has_script())
                    m_host->engine().script().reload_script(node->script_path);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hot-reload this script");
            ImGui::Unindent(8.0f);
        }
    }
    ImGui::PopID(); // ##ScriptSection

    // ── Components Section ─────────────────────────────────────────────────
    ImGui::PushID("##ComponentsSection");
    const bool compsOpen = ImGui::CollapsingHeader("Components",
        ImGuiTreeNodeFlags_DefaultOpen);
    if (compsOpen) {
        // Deferred removal (can't erase during range-for over same vector)
        sol::IComponent* toRemove = nullptr;

        for (const auto& comp : node->components()) {
            ImGui::PushID(comp.get());

            // Build a header label showing the type + script filename (for LuaComponent)
            char compHeader[320];
            if (std::strcmp(comp->component_type(), "LuaComponent") == 0) {
                const auto* lc = static_cast<const sol::LuaComponent*>(comp.get());
                const std::string scriptName = lc->script_path().empty()
                    ? "(no script)"
                    : std::filesystem::path(lc->script_path()).filename().string();
                std::snprintf(compHeader, sizeof(compHeader),
                    "LuaComponent  [%s]", scriptName.c_str());
            } else {
                std::snprintf(compHeader, sizeof(compHeader), "%s", comp->component_type());
            }

            // Remove button floated to the right
            const float removeW = 22.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - removeW - 6.0f);
            bool compExpanded = ImGui::CollapsingHeader(compHeader, ImGuiTreeNodeFlags_None);

            ImGui::SameLine(ImGui::GetContentRegionMax().x - removeW);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.12f, 0.12f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
            if (ImGui::SmallButton("X##rm")) toRemove = comp.get();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove component");

            if (compExpanded && std::strcmp(comp->component_type(), "LuaComponent") == 0) {
                auto* lc = static_cast<sol::LuaComponent*>(comp.get());
                ImGui::Indent(8.0f);

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Script");
                ImGui::SameLine(100.0f);

                const float browseW = 26.0f;
                ImGui::SetNextItemWidth(
                    std::max(1.0f, ImGui::GetContentRegionAvail().x - browseW - 4.0f));
                std::string sp = lc->script_path();
                if (inputTextString("##comp_sp", sp) && sp != lc->script_path()) {
                    if (m_host->engine().has_script())
                        m_host->engine().script().component_detach(lc);
                    lc->set_script_path(sp);
                    if (m_host->engine().has_script())
                        m_host->engine().script().component_ready(lc, m_host->engine());
                }
                ImGui::SameLine();
                if (ImGui::Button("...##comp_browse")) {
                    m_pending_comp_lua_dialog = true;
                    m_pending_comp_lua_node   = node;
                    m_pending_comp_lua_ptr    = lc;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse for .lua script");

                ImGui::Spacing();
                if (ImGui::SmallButton("Edit Script##comp")) {
                    if (!lc->script_path().empty())
                        openScriptEditorTab(lc->script_path());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Reload##comp")) {
                    if (m_host && m_host->engine().has_script() && !lc->script_path().empty())
                        m_host->engine().script().reload_script(lc->script_path());
                }

                ImGui::Unindent(8.0f);
            }

            ImGui::PopID();
        }

        // Deferred removal: detach Lua state first, then erase from node
        if (toRemove) {
            sol::Node*       n      = node;
            std::string      ctype  = toRemove->component_type();
            bool             is_lua = (ctype == "LuaComponent");
            std::string      cpath;
            if (is_lua)
                cpath = static_cast<sol::LuaComponent*>(toRemove)->script_path();
            sol::EngineHost* host = m_host;

            m_comp_undo_stack.push({
                "Remove " + ctype,
                // execute_fn: find & remove by type (safe for redo too)
                [n, ctype, is_lua, host]() {
                    sol::IComponent* c = nullptr;
                    for (const auto& co : n->components())
                        if (std::strcmp(co->component_type(), ctype.c_str()) == 0)
                            { c = co.get(); break; }
                    if (!c) return;
                    if (is_lua && host && host->engine().has_script())
                        host->engine().script().component_detach(
                            static_cast<sol::LuaComponent*>(c));
                    n->remove_component(c);
                },
                // undo_fn: re-add with same config
                [n, cpath, is_lua, host]() {
                    if (!is_lua) return;
                    auto comp = std::make_unique<sol::LuaComponent>(cpath);
                    sol::LuaComponent* raw = static_cast<sol::LuaComponent*>(
                        n->add_component(std::move(comp)));
                    if (host && host->engine().has_script())
                        host->engine().script().component_ready(raw, host->engine());
                }
            });
        }

        ImGui::Spacing();
        if (ImGui::Button("+ Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("AddCompPopup");

        if (ImGui::BeginPopup("AddCompPopup")) {
            if (ImGui::MenuItem("Lua Script Component")) {
                m_pending_new_comp_node = node;
            }
            ImGui::EndPopup();
        }
    }
    ImGui::PopID(); // ##ComponentsSection
}

void EditorUI::drawConsolePanel() {
    ImGui::Begin("Console");
    drawPanelHeader("Console", 3);

    if (ImGui::SmallButton("Clear")) {
        std::lock_guard lock(m_log_mutex);
        m_log_entries.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy All")) {
        std::lock_guard lock(m_log_mutex);
        std::string all;
        for (const auto& entry : m_log_entries) {
            all += entry.msg;
            all += '\n';
        }
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox("Info", &m_filter_info);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &m_filter_warn);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &m_filter_error);
    ImGui::Separator();

    ImGui::BeginChild("##console_log", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);

    {
        std::lock_guard lock(m_log_mutex);
        for (const auto& entry : m_log_entries) {
            bool show = false;
            ImVec4 color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

            switch (entry.level) {
            case sol::log::Level::Info:
                show = m_filter_info;
                color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                break;
            case sol::log::Level::Warn:
                show = m_filter_warn;
                color = ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
                break;
            case sol::log::Level::Error:
                show = m_filter_error;
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                break;
            }

            if (show) {
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(entry.msg.c_str());
                ImGui::PopStyleColor();
            }
        }
    }

    if (m_log_scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
        m_log_scroll_to_bottom = false;
    }

    ImGui::EndChild();
    ImGui::End();
}

void EditorUI::drawAssetBrowserPanel() {
    if (!ImGui::Begin("Asset Browser")) { ImGui::End(); return; }
    drawPanelHeader("Asset Browser", 4);

    if (m_asset_browser_path.empty() && m_host && m_host->is_open()) {
        m_asset_browser_path = currentProjectPath();
        m_project_root_path  = m_asset_browser_path;
    }

    if (m_asset_browser_path.empty()) {
        ImGui::TextDisabled("(no project open)");
        ImGui::End();
        return;
    }

    namespace fs = std::filesystem;

    // ── Breadcrumb bar ──
    {
        std::string displayPath = m_asset_browser_path;
        if (!m_project_root_path.empty() && displayPath.rfind(m_project_root_path, 0) == 0)
            displayPath = "Project" + displayPath.substr(m_project_root_path.size());
        std::replace(displayPath.begin(), displayPath.end(), '\\', '/');
        ImGui::TextDisabled("%s", displayPath.c_str());
    }

    // Up button
    {
        fs::path current(m_asset_browser_path);
        const bool canGoUp = current.has_parent_path()
            && current.parent_path() != current
            && !m_project_root_path.empty()
            && current.string() != m_project_root_path;
        if (!canGoUp) ImGui::BeginDisabled();
        if (ImGui::SmallButton("..  Up")) {
            m_asset_browser_path = current.parent_path().string();
        }
        if (!canGoUp) ImGui::EndDisabled();
    }

    ImGui::Separator();

    // ── Two-column layout: folder tree | file content ──
    const float treeWidth = 160.0f;
    ImGui::BeginChild("##tree_pane", ImVec2(treeWidth, 0.0f), true);
    {
        // Recursive folder tree from project root
        std::function<void(const std::string&)> drawFolderTree = [&](const std::string& dir) {
            std::error_code ec;
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_directory(ec)) continue;
                const std::string name = it->path().filename().string();
                const std::string fullPath = it->path().string();
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
                if (fullPath == m_asset_browser_path) flags |= ImGuiTreeNodeFlags_Selected;
                bool open = ImGui::TreeNodeEx(name.c_str(), flags);
                if (ImGui::IsItemClicked()) {
                    m_asset_browser_path = fullPath;
                    m_asset_browser_selected.clear();
                }
                if (open) {
                    drawFolderTree(fullPath);
                    ImGui::TreePop();
                }
            }
        };
        drawFolderTree(m_project_root_path.empty() ? m_asset_browser_path : m_project_root_path);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##content_pane", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    {
        try {
            std::vector<fs::directory_entry> entries;
            std::error_code ec;
            for (fs::directory_iterator it(m_asset_browser_path, ec), end;
                 it != end && !ec; it.increment(ec)) {
                entries.push_back(*it);
            }
            std::sort(entries.begin(), entries.end(),
                [](const fs::directory_entry& a, const fs::directory_entry& b) {
                    std::error_code ae, be;
                    const bool aDir = a.is_directory(ae), bDir = b.is_directory(be);
                    if (aDir != bDir) return aDir > bDir;
                    return a.path().filename().string() < b.path().filename().string();
                });

            for (const auto& entry : entries) {
                const std::string name = entry.path().filename().string();
                const bool isDir  = entry.is_directory();
                const std::string ext   = toLower(entry.path().extension().string());
                const bool isModel = isModelExtension(ext);
                const bool isScene = ext == ".solscene";

                const char* prefix = isDir ? "[DIR]" : "[FILE]";
                if (isModel)                               prefix = "[MDL]";
                else if (isScene)                          prefix = "[SCN]";
                else if (ext == ".hdr" || ext == ".exr")  prefix = "[HDR]";
                else if (ext == ".png" || ext == ".jpg" ||
                         ext == ".jpeg" || ext == ".tga") prefix = "[IMG]";
                else if (ext == ".lua")                    prefix = "[LUA]";

                char label[320];
                std::snprintf(label, sizeof(label), "%s  %s", prefix, name.c_str());

                const std::string fullPath = entry.path().string();
                const bool selected = m_asset_browser_selected == fullPath;

                if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_asset_browser_selected = fullPath;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (isDir) {
                            m_asset_browser_path = fullPath;
                        } else if (m_host && m_host->is_open()) {
                            const std::string rel = toProjectRelativePath(entry.path());
                            if (isModel)           m_host->instantiate_model(rel);
                            else if (isScene) {
                                clearSceneState();
                                const std::string sceneName =
                                    std::filesystem::path(rel).stem().string();
                                for (auto& t : m_editor_tabs) {
                                    if (t.id == m_active_tab_id &&
                                        t.type == EditorTabType::SceneEditor) {
                                        t.title      = sceneName;
                                        t.asset_path = rel;
                                        if (t.scene_slot_id >= 0)
                                            m_host->load_scene_into_slot(t.scene_slot_id, rel);
                                        else
                                            m_host->load_scene(rel);
                                        break;
                                    }
                                }
                            }
                            else if (ext == ".lua") openScriptEditorTab(fullPath);
                        }
                    }
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (isModel && ImGui::MenuItem("Instantiate in Scene")) {
                        if (m_host && m_host->is_open())
                            m_host->instantiate_model(toProjectRelativePath(entry.path()));
                    }
                    if (isModel && ImGui::MenuItem("Open in Model Viewer")) {
                        const std::string rel = toProjectRelativePath(entry.path());
                        std::string tabTitle = "Model: " + entry.path().filename().string();
                        openEditorTab(EditorTabType::ModelViewer, tabTitle, rel);
                    }
                    if ((ext == ".hdr" || ext == ".exr" || ext == ".png" || ext == ".jpg") &&
                        ImGui::MenuItem("Open in Texture Viewer")) {
                        std::string tabTitle = "Texture: " + entry.path().filename().string();
                        openEditorTab(EditorTabType::TextureViewer, tabTitle,
                                      toProjectRelativePath(entry.path()));
                    }
                    if (isScene && ImGui::MenuItem("Open Scene")) {
                        if (m_host && m_host->is_open()) {
                            clearSceneState();
                            const std::string rel2 = toProjectRelativePath(entry.path());
                            const std::string sceneName =
                                std::filesystem::path(rel2).stem().string();
                            for (auto& t : m_editor_tabs) {
                                if (t.id == m_active_tab_id &&
                                    t.type == EditorTabType::SceneEditor) {
                                    t.title      = sceneName;
                                    t.asset_path = rel2;
                                    if (t.scene_slot_id >= 0)
                                        m_host->load_scene_into_slot(t.scene_slot_id, rel2);
                                    else
                                        m_host->load_scene(rel2);
                                    break;
                                }
                            }
                        }
                    }
                    if (ext == ".lua" && ImGui::MenuItem("Open in Script Editor")) {
                        openScriptEditorTab(fullPath);
                    }
                    if (ext == ".lua" && ImGui::MenuItem("Attach to Selected Node")) {
                        sol::Node* sel = m_host ? m_host->selected_node() : nullptr;
                        if (sel) {
                            if (m_host->engine().has_script())
                                m_host->engine().script().node_detach(sel);
                            sel->script_path = fullPath;
                            if (m_host->engine().has_script())
                                m_host->engine().script().node_ready(sel, m_host->engine());
                        }
                    }
                    if (ext == ".lua" && ImGui::MenuItem("Add as Lua Component to Selected")) {
                        sol::Node* sel = m_host ? m_host->selected_node() : nullptr;
                        if (sel) {
                            sol::Node*       t    = sel;
                            std::string      p    = fullPath;
                            sol::EngineHost* host = m_host;
                            m_comp_undo_stack.push({
                                "Add LuaComponent",
                                [t, p, host]() {
                                    auto comp = std::make_unique<sol::LuaComponent>(p);
                                    sol::LuaComponent* raw = static_cast<sol::LuaComponent*>(
                                        t->add_component(std::move(comp)));
                                    if (host && host->engine().has_script())
                                        host->engine().script().component_ready(raw, host->engine());
                                },
                                [t, host]() {
                                    sol::LuaComponent* lc = t->get_component<sol::LuaComponent>();
                                    if (lc && host && host->engine().has_script())
                                        host->engine().script().component_detach(lc);
                                    t->remove_component("LuaComponent");
                                }
                            });
                        }
                    }
                    if (ImGui::MenuItem("Show in Explorer")) {
                        const std::string cmd = "explorer /select,\"" + fullPath + "\"";
                        std::system(cmd.c_str());
                    }
                    ImGui::EndPopup();
                }
            }
        } catch (...) {
            ImGui::TextDisabled("(unable to read directory)");
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void EditorUI::drawWorldSettingsPanel() {
    if (!ImGui::Begin("World Settings")) { ImGui::End(); return; }
    drawPanelHeader("World Settings", 2);

    if (!m_host || !m_host->is_open()) {
        ImGui::TextDisabled("(no project open)");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Scene");

    sol::Node* root = m_host->scene_root();
    if (root) {
        ImGui::TextDisabled("Root: %s", root->name.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Physics (stub)");
    static float gravity_y = -9.81f;
    ImGui::DragFloat("Gravity Y", &gravity_y, 0.1f, -50.0f, 0.0f, "%.2f m/s\xc2\xb2");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Physics gravity (not yet connected to engine)");

    ImGui::Spacing();
    ImGui::SeparatorText("Navigation (stub)");
    ImGui::TextDisabled("NavMesh: not built");
    if (ImGui::Button("Build NavMesh", ImVec2(-1.0f, 0.0f))) { /* TODO */ }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("NavMesh baking is not yet implemented");

    ImGui::Spacing();
    ImGui::SeparatorText("Auto-Save (stub)");
    static bool auto_save = false;
    static int  auto_save_interval = 5;
    ImGui::Checkbox("Enable Auto-Save", &auto_save);
    if (auto_save) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragInt("min##autosave", &auto_save_interval, 1.0f, 1, 60);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-save interval in minutes");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("LOD (stub)");
    static float lod_bias = 1.0f;
    ImGui::SliderFloat("LOD Bias", &lod_bias, 0.1f, 4.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Global LOD distance bias (not yet connected)");

    ImGui::End();
}

void EditorUI::drawStatisticsPanel() {
    if (!ImGui::Begin("Statistics")) { ImGui::End(); return; }
    drawPanelHeader("Statistics", 5);

    if (!m_host || !m_host->is_open()) {
        ImGui::TextDisabled("(no project open)");
        ImGui::End();
        return;
    }

    const float fps = m_host->frame_fps();
    const float ms  = fps > 0.0f ? 1000.0f / fps : 0.0f;
    const int   dc  = m_host->frame_draw_calls();

    ImGui::SeparatorText("Performance");

    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.1f FPS", fps);
    ImGui::PlotLines("##fps_graph",
        m_fps_history, IM_ARRAYSIZE(m_fps_history), m_fps_history_idx,
        overlay, 0.0f, 200.0f, ImVec2(-1.0f, 60.0f));
    ImGui::Text("Frame time: %.2f ms", ms);

    ImGui::Spacing();
    ImGui::SeparatorText("Rendering");

    ImGui::Text("Draw calls:  %d", dc);
    ImGui::TextDisabled("Triangles:   --");
    ImGui::TextDisabled("Vertices:    --");
    ImGui::TextDisabled("Shader vars: --");

    ImGui::Spacing();
    ImGui::SeparatorText("Memory (stub)");
    ImGui::TextDisabled("GPU VRAM:    --");
    ImGui::TextDisabled("System RAM:  --");
    ImGui::TextDisabled("Textures:    --");
    ImGui::TextDisabled("Meshes:      --");

    ImGui::Spacing();
    ImGui::SeparatorText("Scene");
    int nodeCount = 0;
    std::function<void(sol::Node*)> countNodes = [&](sol::Node* n) {
        if (!n) return;
        ++nodeCount;
        for (const auto& c : n->children()) countNodes(c.get());
    };
    if (sol::Node* r = m_host->scene_root()) countNodes(r);
    ImGui::Text("Nodes in scene: %d", nodeCount);

    ImGui::End();
}

void EditorUI::drawSequencerPanel() {
    if (!ImGui::Begin("Sequencer")) { ImGui::End(); return; }
    drawPanelHeader("Sequencer", 6);

    ImGui::BeginDisabled();
    ImGui::Button(" |< ");  ImGui::SameLine();
    ImGui::Button(" << ");  ImGui::SameLine();
    ImGui::Button("  > ");  ImGui::SameLine();
    ImGui::Button(" >> ");  ImGui::SameLine();
    ImGui::Button(" >| ");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("  00:00:00:00");

    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 center = ImVec2(
        ImGui::GetCursorScreenPos().x + avail.x * 0.5f,
        ImGui::GetCursorScreenPos().y + avail.y * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(center.x - 110.0f, center.y - 8.0f),
        IM_COL32(120, 120, 120, 200),
        "Sequencer \xe2\x80\x94 Not yet implemented");

    ImGui::End();
}

void EditorUI::drawBuildOutputPanel() {
    if (!ImGui::Begin("Build Output")) { ImGui::End(); return; }
    drawPanelHeader("Build Output", 7);

    ImGui::BeginDisabled();
    ImGui::Button("Build All");
    ImGui::SameLine();
    ImGui::Button("Clean");
    ImGui::SameLine();
    ImGui::Button("Rebuild");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("  Last build: --");
    ImGui::Separator();

    ImGui::BeginChild("##build_log", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextDisabled("Build system not yet configured.");
    ImGui::TextDisabled("Output will appear here when a build is run.");
    ImGui::EndChild();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Stage 3-D-3: Profiler panel
// ---------------------------------------------------------------------------
void EditorUI::drawProfilerPanel() {
    if (!ImGui::Begin("Profiler", &m_show_profiler)) {
        ImGui::End();
        return;
    }

    const auto& samples = sol::Profiler::instance().samples();
    if (samples.empty()) {
        ImGui::TextDisabled("No profiler data this frame.");
        ImGui::TextDisabled("Add SOL_PROFILE_SCOPE(\"Name\") markers to measure scopes.");
    } else {
        constexpr float kTarget60fps = 16.67f; // ms per frame at 60 fps

        if (ImGui::BeginTable("##prof_tbl", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Scope",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Last (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Avg  (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            for (const auto& s : samples) {
                ImGui::TableNextRow();

                // Column 0: name + progress bar proportional to 60 fps budget
                ImGui::TableSetColumnIndex(0);
                char bar_label[128];
                std::snprintf(bar_label, sizeof(bar_label),
                    "%s  (%.2f ms avg)", s.name.c_str(), s.ms_avg);
                const float frac = std::min(1.0f,
                    static_cast<float>(s.ms_avg) / kTarget60fps);
                ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f), bar_label);

                // Column 1: last raw ms
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f", s.ms);

                // Column 2: rolling avg ms
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.3f", s.ms_avg);
            }
            ImGui::EndTable();
        }
    }

    ImGui::End();
}

// Stage 3-D-2 stub: Edit menu already provides field-level undo/redo.
// m_comp_undo_stack handles component add/remove operations separately.
void EditorUI::draw_undo_redo_menu() {
    // Intentionally empty — the "Edit" menu bar entry already covers field undo.
}

void EditorUI::drawProjectSettingsModal() {
    if (m_show_project_settings) {
        ImGui::OpenPopup("Project Settings");
        m_show_project_settings = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540.0f, 420.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Project Settings", nullptr)) return;

    if (ImGui::BeginTabBar("##prj_tabs")) {
        if (ImGui::BeginTabItem("General")) {
            ImGui::Spacing();
            ImGui::TextDisabled("Project Name:");
            ImGui::SameLine();
            if (m_host && m_host->is_open())
                ImGui::TextUnformatted(m_host->project().name.c_str());
            else
                ImGui::TextDisabled("--");
            ImGui::Spacing();
            ImGui::TextDisabled("Main Scene:");
            ImGui::SameLine();
            if (m_host && m_host->is_open())
                ImGui::TextUnformatted(m_host->project().main_scene.c_str());
            else
                ImGui::TextDisabled("--");
            ImGui::Spacing();
            ImGui::SeparatorText("Window");
            if (m_host && m_host->is_open()) {
                ImGui::Text("Resolution: %d x %d",
                    m_host->project().window_width,
                    m_host->project().window_height);
                ImGui::Text("VSync: %s", m_host->project().window_vsync ? "On" : "Off");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rendering")) {
            ImGui::Spacing();
            ImGui::TextDisabled("Renderer: Vulkan (forward+ / deferred)");
            ImGui::TextDisabled("These settings will be exposed in a future update.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Input")) {
            ImGui::Spacing();
            ImGui::TextDisabled("Input mappings will be configurable here in a future update.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Plugins")) {
            ImGui::Spacing();
            ImGui::TextDisabled("Plugin management is not yet implemented.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(100.0f, 0.0f))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorUI::drawEditorPreferencesModal() {
    if (m_show_editor_prefs) {
        ImGui::OpenPopup("Editor Preferences");
        m_show_editor_prefs = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680.0f, 520.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Editor Preferences", nullptr, ImGuiWindowFlags_NoResize)) return;

    static int s_cat = 0;
    static const char* cats[] = {
        ICON_FA_EYE     " Appearance",
        ICON_FA_CAMERA  " Viewport",
        ICON_FA_GEAR    " General",
        ICON_FA_SEARCH  " Shortcuts"
    };

    // ── Sidebar ───────────────────────────────────────────────────────────────
    ImGui::BeginChild("##sidebar", ImVec2(155, -35));
    ImGui::Spacing();
    for (int i = 0; i < 4; i++) {
        bool sel = (s_cat == i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.25f, 0.38f, 1.f));
        if (ImGui::Selectable(cats[i], sel, 0, ImVec2(0, 28))) s_cat = i;
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Vertical divider line
    ImVec2 lMin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(lMin.x - 1, lMin.y),
        ImVec2(lMin.x,     lMin.y + 450),
        IM_COL32(80, 80, 90, 255));

    // ── Content area ─────────────────────────────────────────────────────────
    ImGui::BeginChild("##content", ImVec2(0, -35));

    if (s_cat == 0) {
        // ── Appearance ──────────────────────────────────────────────────────
        if (sol::SolSection("Fonts & Scale")) {
            ImGui::SetNextItemWidth(250.0f);
            ImGui::SliderFloat("Font Scale", &m_prefs.fontScale, 0.5f, 2.5f, "%.2fx");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Scales all editor text without rebuilding the font atlas");
            ImGui::SetWindowFontScale(m_prefs.fontScale);
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.f), "Preview: Aa Bb Cc 123");
            ImGui::SetWindowFontScale(1.0f);
        }
        ImGui::Spacing();
        if (sol::SolSection("Accent Color")) {
            ImGui::Spacing();
            for (int i = 0; i < 5; i++) {
                ImGui::PushID(i);
                ImVec4 col = kAccents[i].color;
                if (m_prefs.accentPreset == i)
                    ImGui::PushStyleColor(ImGuiCol_Button, col);
                else
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x*0.5f, col.y*0.5f, col.z*0.5f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x*0.85f, col.y*0.85f, col.z*0.85f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col);
                if (ImGui::Button(kAccents[i].name, ImVec2(90, 28)))
                    m_prefs.accentPreset = i;
                ImGui::PopStyleColor(3);
                ImGui::PopID();
                if (i < 4) ImGui::SameLine(0, 4);
            }
            ImGui::Spacing();
        }
        ImGui::Spacing();
        if (sol::SolSection("Panel Style")) {
            const char* densities[] = {"Compact", "Normal", "Comfortable"};
            ImGui::SetNextItemWidth(160.0f);
            ImGui::Combo("UI Density", &m_prefs.uiDensity, densities, 3);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Corner Radius", &m_prefs.frameRounding, 0.0f, 8.0f, "%.0f");
        }

    } else if (s_cat == 1) {
        // ── Viewport ────────────────────────────────────────────────────────
        if (sol::SolSection("Camera")) {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::DragFloat("Speed Multiplier", &m_prefs.camSpeedMult, 0.05f, 0.1f, 20.0f, "%.1f x");
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Field of View", &m_prefs.camFov, 30.0f, 120.0f, "%.0f deg");
            ImGui::Checkbox("Invert Y Axis", &m_prefs.camInvertY);
        }
        ImGui::Spacing();
        if (sol::SolSection("Overlays")) {
            ImGui::Checkbox("Show FPS in Status Bar", &m_prefs.showFps);
        }

    } else if (s_cat == 2) {
        // ── General ─────────────────────────────────────────────────────────
        if (sol::SolSection("Scene Editing")) {
            ImGui::Checkbox("Confirm Before Delete", &m_prefs.confirmDelete);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ask for confirmation before deleting a node");

            static const int kUndoValues[] = {50, 100, 200, 500};
            static const char* kUndoLabels[] = {"50 steps", "100 steps", "200 steps", "500 steps"};
            int undoIdx = 1;
            for (int i = 0; i < 4; i++) { if (kUndoValues[i] == m_prefs.undoLimit) { undoIdx = i; break; } }
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("Undo Limit", &undoIdx, kUndoLabels, 4))
                m_prefs.undoLimit = kUndoValues[undoIdx];
        }
        ImGui::Spacing();
        if (sol::SolSection("Auto Save")) {
            ImGui::Checkbox("Enable Auto Save", &m_prefs.autosave);
            if (m_prefs.autosave) {
                ImGui::SetNextItemWidth(160.0f);
                ImGui::Combo("Interval", &m_prefs.autosaveMin, "1 min\0" "2 min\0" "5 min\0" "10 min\0\0");
            }
        }

    } else {
        // ── Shortcuts ───────────────────────────────────────────────────────
        if (sol::SolSection("Viewport Controls")) {
            ImGui::TextDisabled("Right-click + drag     Fly camera");
            ImGui::TextDisabled("WASD                   Move camera");
            ImGui::TextDisabled("Q / E                  Move down / up");
            ImGui::TextDisabled("Arrow keys             Camera look rotation");
            ImGui::TextDisabled("Mouse wheel            Zoom");
        }
        ImGui::Spacing();
        if (sol::SolSection("Node Operations")) {
            ImGui::TextDisabled("Ctrl+Z                 Undo");
            ImGui::TextDisabled("Ctrl+Y / Shift+Z       Redo");
            ImGui::TextDisabled("Ctrl+D                 Duplicate selected node");
            ImGui::TextDisabled("Ctrl+S                 Save scene");
            ImGui::TextDisabled("T / R / Y              Gizmo: Translate / Rotate / Scale");
            ImGui::TextDisabled("Del                    Delete selected node");
            ImGui::TextDisabled("F2                     Rename selected node");
        }
    }

    ImGui::EndChild();

    // ── Bottom bar ────────────────────────────────────────────────────────────
    ImGui::Separator();
    if (ImGui::Button("Reset Defaults", ImVec2(130, 0))) {
        m_prefs = EditorPrefs{};
        applyPreferences();
    }
    ImGui::SameLine();
    const float bw = 110.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - bw * 2 - 8);
    if (ImGui::Button("Save & Close", ImVec2(bw, 0))) {
        savePreferences();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(bw, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void EditorUI::drawStatusBar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float barH = 22.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - barH));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, barH));
    ImGui::SetNextWindowViewport(vp->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoDocking  | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.067f, 0.067f, 0.11f, 1.0f));
    ImGui::Begin("##StatusBar", nullptr, flags);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // Left: scene mode + play indicator
    if (m_play_mode) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.95f, 0.3f, 1.0f));
        ImGui::TextUnformatted("  PLAY MODE");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("  EDITOR");
    }

    // Selected node path
    if (m_host && m_host->selected_node()) {
        sol::Node* sel = m_host->selected_node();
        std::string path = sel->name;
        for (sol::Node* cur = sel->parent(); cur && cur->parent(); cur = cur->parent()) {
            path = cur->name + " / " + path;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  |  %s", path.c_str());
    }

    // Right: FPS + draw calls
    if (m_host && m_host->is_open()) {
        char stats[128];
        std::snprintf(stats, sizeof(stats), "FPS: %.1f  |  Draw calls: %d  |  SolEngine v0.1",
            m_host->frame_fps(), m_host->frame_draw_calls());
        const float textW = ImGui::CalcTextSize(stats).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - textW - 8.0f);
        ImGui::TextDisabled("%s", stats);
    }

    ImGui::End();
}
