#pragma once

#include "sol/host.h"
#include "sol/log.h"
#include "sol/reflect.h"
#include "undo_redo.h"

namespace sol { class LuaComponent; }

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

// ── Editor Document Tab System ───────────────────────────────────────────────
enum class EditorTabType {
    SceneEditor,
    MaterialEditor,
    ShaderEditor,
    ModelViewer,
    TextureViewer,
    ScriptEditor,
};

struct EditorTab {
    int            id;
    std::string    title;
    EditorTabType  type;
    std::string    asset_path;
    bool           open = true;

    // Per-tab state (for SceneEditor tabs)
    sol::Node*     selected_node    = nullptr;
    char           hierarchy_search[128] = {};

    // Per-tab editor camera
    glm::vec3      cam_pos   {0.0f, 5.0f, 15.0f};
    float          cam_yaw   = 0.0f;
    float          cam_pitch = -15.0f;

    // Hot scene slot ID — each SceneEditor tab owns one slot in EngineHost.
    // -1 = not yet assigned (non-SceneEditor tabs always -1).
    int            scene_slot_id = -1;

    // Per-tab viewport toolbar state
    int  viewport_shading = 0;  // 0=Lit, 1=Unlit (maps debug_view→1), 2=Wireframe(stub)
    int  debug_view       = 0;  // 0=Off..7=CascadeDebug, only active when viewport_shading==0
};

class QString;
class QProcess;

// ── Editor Preferences ────────────────────────────────────────────────────────
struct EditorPrefs {
    // Appearance
    float fontScale      = 1.0f;   // applied to io.FontGlobalScale
    int   accentPreset   = 0;      // 0=Blue,1=Orange,2=Green,3=Purple,4=Rose
    int   uiDensity      = 1;      // 0=Compact,1=Normal,2=Comfortable
    float frameRounding  = 3.0f;
    // Viewport
    float camSpeedMult   = 1.0f;
    bool  camInvertY     = false;
    float camFov         = 60.0f;
    bool  showFps        = true;
    // General
    bool  confirmDelete  = true;
    bool  autosave       = false;
    int   autosaveMin    = 2;
    int   undoLimit      = 100;
};

class EditorUI {
public:
    explicit EditorUI(sol::EngineHost* host);
    ~EditorUI();

    std::function<void()> drawFn();

    static std::string openFileDialog(const std::string& filter, const std::string& title = "Open File");
    static std::string saveFileDialog(const std::string& filter, const std::string& title = "Save File");

    void undo();
    void redo();

    void setWindowMoveCallback    (std::function<void()>       fn) { m_wnd_move     = std::move(fn); }
    void setWindowMaximizeCallback(std::function<void()>       fn) { m_wnd_maximize = std::move(fn); }
    void setWindowMinimizeCallback(std::function<void()>       fn) { m_wnd_minimize = std::move(fn); }
    void setWindowCloseCallback   (std::function<void()>       fn) { m_wnd_close    = std::move(fn); }
    void setIsMaximizedCallback   (std::function<bool()>       fn) { m_wnd_is_max   = std::move(fn); }

    // Camera state callbacks — wired by ViewportWidget so EditorUI can save/restore
    // per-tab camera when the active SceneEditor tab changes.
    void setGetCameraCallback(std::function<std::tuple<glm::vec3,float,float>()> fn) { m_get_camera = std::move(fn); }
    void setSetCameraCallback(std::function<void(glm::vec3,float,float)>         fn) { m_set_camera = std::move(fn); }

private:
    void draw();
    void drawTitleBar();
    void drawDockspace();
    void drawHierarchyPanel();
    void drawInspectorPanel();
    void drawConsolePanel();
    void drawAssetBrowserPanel();

    void drawWorldSettingsPanel();
    void drawStatisticsPanel();
    void drawSequencerPanel();
    void drawBuildOutputPanel();
    void drawProjectSettingsModal();
    void drawEditorPreferencesModal();
    void drawStatusBar();
    void drawProfilerPanel();       // Stage 3-D-3
    void draw_undo_redo_menu();     // Stage 3-D-2 (stub; Edit menu handles field undo)

    void drawEditorTabBar();
    void drawSubEditorStub(const EditorTab& tab);
    void drawViewportToolbar();
    void openEditorTab(EditorTabType type, const std::string& title, const std::string& asset_path = {});
    bool drawPanelHeader(const char* label, int detach_idx);

    void drawNodeFields(sol::Node* node);
    void drawNodeTree(sol::Node* node, const char* filter = nullptr);

    sol::EngineHost* m_host{};

    std::function<void()> m_wnd_move;
    std::function<void()> m_wnd_maximize;
    std::function<void()> m_wnd_minimize;
    std::function<void()> m_wnd_close;
    std::function<bool()> m_wnd_is_max;

    std::function<std::tuple<glm::vec3,float,float>()> m_get_camera;
    std::function<void(glm::vec3,float,float)>         m_set_camera;

    struct LogEntry {
        sol::log::Level level;
        std::string msg;
    };
    std::vector<LogEntry> m_log_entries;
    std::mutex m_log_mutex;
    bool m_log_scroll_to_bottom = false;
    bool m_filter_info = true;
    bool m_filter_warn = true;
    bool m_filter_error = true;

    // --- Undo / Redo stack -------------------------------------------------
    enum class UndoType { FieldChange, Rename, GizmoTransform };

    struct UndoEntry {
        UndoType type;
        sol::Node* node;
        // FieldChange:
        std::string   field;
        sol::FieldType field_type{};
        std::vector<uint8_t> before_raw, after_raw;
        std::string          before_str, after_str;
        // Rename:
        std::string before_name, after_name;
        // GizmoTransform:
        glm::vec3 before_pos{}, before_rot{}, before_scale{};
        glm::vec3 after_pos{},  after_rot{},  after_scale{};
    };

    struct DragCapture {
        sol::Node* node = nullptr;
        std::string field;
        std::vector<uint8_t> start_raw;
        std::string          start_str;
        bool active = false;
    };

    void pushUndoField(sol::Node* node, const std::string& field, sol::FieldType type,
                       const std::vector<uint8_t>& before_raw, const std::string& before_str,
                       const std::vector<uint8_t>& after_raw,  const std::string& after_str);
    void pushUndoRename(sol::Node* node, const std::string& before, const std::string& after);
    void pushUndoEntry(UndoEntry e);
    void applyUndoEntry(const UndoEntry& e, bool reverse);
    void captureFieldValue(sol::FieldType type, void* ptr,
                           std::vector<uint8_t>& raw_out, std::string& str_out) const;
    size_t fieldTypeSize(sol::FieldType type) const;

    std::vector<UndoEntry> m_undo_stack;
    int                    m_undo_idx = -1;

    DragCapture m_drag_capture;

    // --- Hierarchy search --------------------------------------------------
    char m_hierarchy_search[128] = {};

    std::string m_asset_browser_path;
    std::string m_asset_browser_selected;
    std::string m_project_root_path;

    bool m_first_layout = true;

    bool m_pending_hdr_dialog = false;
    std::string m_pending_hdr_field;
    sol::Node* m_pending_hdr_node = nullptr;
    bool m_pending_asset_dialog = false;
    std::string m_pending_asset_field;
    sol::Node* m_pending_asset_node = nullptr;
    bool m_pending_lua_dialog = false;
    sol::Node* m_pending_lua_node = nullptr;
    sol::Node* m_pending_new_script_node = nullptr;  // triggers save-dialog to create+attach a new script

    // Component editing state
    bool               m_pending_comp_lua_dialog = false;
    sol::Node*         m_pending_comp_lua_node   = nullptr;
    sol::LuaComponent* m_pending_comp_lua_ptr    = nullptr;
    sol::Node*         m_pending_new_comp_node   = nullptr;

    // Script editor: per-path text buffers (heap char array for InputTextMultiline)
    std::unordered_map<std::string, std::vector<char>> m_script_buffers;
    std::unordered_map<std::string, bool>              m_script_dirty;

    void openScriptEditorTab(const std::string& path);
    // Clears all editor state that holds raw Node* — called on every scene load
    // to prevent dangling-pointer crashes (undo stack, rename, delete, drag, etc.)
    void clearSceneState();

    bool m_play_mode = false;
    QProcess* m_game_process = nullptr;
    void startGameProcess();
    void stopGameProcess();

    bool m_show_add_child_popup = false;
    sol::Node* m_context_menu_node = nullptr;

    bool m_rename_active = false;
    sol::Node* m_rename_node = nullptr;
    char m_rename_buf[256] = {};

    bool m_delete_confirm_open = false;
    sol::Node* m_delete_confirm_node = nullptr;

    // ── Material Editor state ─────────────────────────────────────────────────
    sol::Node* m_mat_target_node      = nullptr;
    int        m_mat_selected_submesh = -1;
    bool       m_mat_cache_valid      = false;
    // 64×64 RGBA preview pixels (ImU32 = ABGR) — uses 48×48 region, extra is safe
    ImU32      m_mat_preview_pixels[64 * 64] = {};

    // Panel visibility toggles
    bool m_panel_hierarchy      = true;
    bool m_panel_inspector      = true;
    bool m_panel_console        = true;
    bool m_panel_assets         = true;
    bool m_panel_world_settings = true;
    bool m_panel_statistics     = false;
    bool m_panel_sequencer      = false;
    bool m_panel_build_output   = false;
    bool m_show_profiler        = false;  // Stage 3-D-3

    // Stage 3-D-2: undo/redo for component add/remove operations
    UndoRedoStack m_comp_undo_stack;

    // ── Editor Document Tab System ────────────────────────────────────────────
    std::vector<EditorTab> m_editor_tabs;
    int                    m_active_tab_id = 0;
    int                    m_focus_tab_id  = -1; // one-shot: request focus for a tab without overriding ImGui selection
    int                    m_next_tab_id   = 1;

    // ── Panel Pop-Out (detach) State ──────────────────────────────────────────
    // 0=Hierarchy,1=Inspector,2=WorldSettings,3=Console,
    // 4=Assets,5=Statistics,6=Sequencer,7=BuildOutput
    bool m_panel_detached[8] = {};

    // Modal visibility
    bool m_show_project_settings  = false;
    bool m_show_editor_prefs      = false;

    // Viewport shading mode: moved to per-tab EditorTab::viewport_shading

    // Platform target stub
    int  m_platform_target = 0; // 0=Windows x64

    // FPS history for statistics graph
    static constexpr int kFpsHistoryLen = 128;
    float m_fps_history[kFpsHistoryLen] = {};
    int   m_fps_history_idx = 0;
    float m_fps_history_timer = 0.0f;

    // Editor prefs
    EditorPrefs m_prefs;
    void loadPreferences();
    void savePreferences();
    void applyPreferences();

    // Dockspace central node tracking (for script editor placement)
    ImGuiID m_dockspace_id       = 0;
    float   m_script_font_scale  = 1.0f;

    // Status bar message
    char m_status_message[256] = {};
};
