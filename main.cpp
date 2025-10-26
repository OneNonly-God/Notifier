#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#elif __linux__
#include <gtk/gtk.h>
#elif __APPLE__
// macOS specific headers would go here
#endif

namespace fs = std::filesystem;

enum ThemeType { THEME_DARK, THEME_LIGHT, THEME_CUSTOM };
ThemeType currentTheme = THEME_CUSTOM;

// A single open file / tab representation
struct FileTab {
    std::string filePath; // empty => unsaved new file
    std::string content;  // live editable buffer (std::string)
    bool isModified = false;
    std::filesystem::file_time_type lastModified;
    bool isReadonly = false;
};

struct AppState {
    std::vector<FileTab> tabs;
    int activeTab = -1;
    int closeTabIndex = -1;

    // UI & dialog flags
    bool needsSave = false;
    bool showAboutDialog = false;
    bool showFileDialog = false;
    bool firstRun = true;

    // File browser / UI helpers
    std::string currentPath;
    char filePathBuffer[512] = "";

    // Search buffer
    char searchBuffer[256] = "";

    // UI focus
    bool focusEditor = false;

    // Recent files
    std::vector<std::string> recentFiles;
    const size_t maxRecentFiles = 10;
};

AppState g_appState;

// Forward declarations
void SetupCustomStyle();
void HandleKeyboardShortcuts();
void SetupInitialDockingLayout();
std::string OpenFileDialog();
void OpenFile(const std::string& filepath);
void SaveFileAs(int tabIndex);
void AddToRecentFiles(const std::string& filepath);
void LoadRecentFiles();
void SaveFile(int tabIndex);
void CloseTab(int tabIndex);
void RenderTabs();
void RenderEditor();
void SaveRecentFiles();
void SaveAll();
void RenderMenuBar();
void RenderMainDockSpace();
bool IsTextFile(const std::string& filepath);
std::string ReadFileContent(const std::string& filepath);

// -----------------------------
// ImGui helper: Callback resize - FIXED
// -----------------------------
static int ImGuiStringInputCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        std::string* str = (std::string*)data->UserData;
        IM_ASSERT(str != nullptr);
        
        // Resize string to accommodate new content
        str->resize((size_t)data->BufTextLen);
        
        // CRITICAL FIX: Use data() instead of c_str() for non-const access
        data->Buf = str->data();
    }
    return 0;
}

// -----------------------------
// Platform file dialogs
// -----------------------------
std::string OpenFileDialog() {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "All Files\0*.*\0Text Files\0*.txt\0C++ Files\0*.cpp;*.h;*.hpp\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Open File";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
#elif __linux__
    // Use GTK file dialog on Linux
    if (!gtk_init_check(NULL, NULL)) {
        std::cerr << "GTK init failed\n";
        return "";
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open File",
                                                    NULL,
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = std::string(filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return result;
#else
    // Fallback: Use ImGui file browser
    g_appState.showFileDialog = true;
    return "";
#endif
    return "";
}

std::string SaveFileDialog(const std::string& defaultName = "") {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (!defaultName.empty()) {
        strncpy(filename, defaultName.c_str(), MAX_PATH - 1);
        filename[MAX_PATH - 1] = '\0';
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Save File As";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "txt";

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }
#elif __linux__
    if (!gtk_init_check(NULL, NULL)) {
        return "";
    }

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save File As",
                                                    NULL,
                                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (!defaultName.empty()) {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), defaultName.c_str());
    }

    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = std::string(filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    return result;
#else
    g_appState.showFileDialog = true;
    return "";
#endif
    return "";
}

// -----------------------------
// File helpers - IMPROVED
// -----------------------------
bool IsTextFile(const std::string& filepath) {
    if (filepath.empty() || !fs::exists(filepath)) return false;
    
    std::string ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::vector<std::string> textExtensions = {
        ".txt", ".md", ".markdown", ".log", ".cfg", ".ini", ".json", ".xml",
        ".html", ".htm", ".css", ".js", ".ts", ".jsx", ".tsx",
        ".cpp", ".c", ".h", ".hpp", ".cc", ".cxx", ".py", ".java",
        ".cs", ".rb", ".go", ".rs", ".swift", ".kt", ".scala",
        ".sh", ".bash", ".zsh", ".fish", ".ps1", ".bat", ".cmd",
        ".yaml", ".yml", ".toml", ".env", ".gitignore", ".dockerignore"
    };

    if (std::find(textExtensions.begin(), textExtensions.end(), ext) != textExtensions.end()) {
        return true;
    }

    if (ext.empty()) return true;

    // Binary check
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytesRead = file.gcount();
    file.close();

    for (std::streamsize i = 0; i < bytesRead; ++i) {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        // Allow tab, newline, carriage return
        if (c < 32 && c != 9 && c != 10 && c != 13) {
            if (c == 0) return false;
        }
    }

    return true;
}

std::string ReadFileContent(const std::string& filepath) {
    if (filepath.empty() || !fs::exists(filepath)) {
        std::cerr << "File does not exist: " << filepath << "\n";
        return "";
    }
    
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << "\n";
        return "";
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    const size_t maxSize = 10 * 1024 * 1024; // 10MB
    if (size > (std::streamsize)maxSize) {
        std::string content(maxSize, '\0');
        file.read(&content[0], maxSize);
        content += "\n\n[File truncated - original size: " + std::to_string(size) + " bytes]";
        return content;
    }

    std::string content((size_t)size, '\0');
    file.read(&content[0], size);

    // Normalize CRLF -> LF
    std::string normalized;
    normalized.reserve(content.size());
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r') {
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                normalized += '\n';
                ++i;
            } else {
                normalized += '\n';
            }
        } else {
            normalized += content[i];
        }
    }

    return normalized;
}

// -----------------------------
// Recent files - IMPROVED
// -----------------------------
void AddToRecentFiles(const std::string& filepath) {
    if (filepath.empty()) return;
    
    auto it = std::find(g_appState.recentFiles.begin(), g_appState.recentFiles.end(), filepath);
    if (it != g_appState.recentFiles.end()) {
        g_appState.recentFiles.erase(it);
    }

    g_appState.recentFiles.insert(g_appState.recentFiles.begin(), filepath);

    if (g_appState.recentFiles.size() > g_appState.maxRecentFiles) {
        g_appState.recentFiles.resize(g_appState.maxRecentFiles);
    }

    SaveRecentFiles();
}

void LoadRecentFiles() {
    g_appState.recentFiles.clear();
    std::ifstream file("recent_files.txt");
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line) && g_appState.recentFiles.size() < g_appState.maxRecentFiles) {
        if (!line.empty() && fs::exists(line)) {
            g_appState.recentFiles.push_back(line);
        }
    }
}

void SaveRecentFiles() {
    std::ofstream file("recent_files.txt", std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to save recent files\n";
        return;
    }
    for (const auto& path : g_appState.recentFiles) {
        file << path << "\n";
    }
}

// -----------------------------
// Save/Load tab file operations - IMPROVED
// -----------------------------
void SaveFile(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_appState.tabs.size()) {
        std::cerr << "Invalid tab index: " << tabIndex << "\n";
        return;
    }
    
    FileTab &tab = g_appState.tabs[tabIndex];

    if (tab.filePath.empty()) {
        SaveFileAs(tabIndex);
        return;
    }

    std::ofstream file(tab.filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to save file: " << tab.filePath << "\n";
        return;
    }

    file << tab.content;
    file.close();

    tab.isModified = false;
    g_appState.needsSave = false;
    
    if (fs::exists(tab.filePath)) {
        tab.lastModified = fs::last_write_time(tab.filePath);
    }
    
    AddToRecentFiles(tab.filePath);
    std::cout << "Saved: " << tab.filePath << "\n";
}

void SaveFileAs(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_appState.tabs.size()) return;

    std::string defaultName = "untitled.txt";
    if (!g_appState.tabs[tabIndex].filePath.empty()) {
        defaultName = fs::path(g_appState.tabs[tabIndex].filePath).filename().string();
    }

    std::string filepath = SaveFileDialog(defaultName);
    if (filepath.empty()) return;

    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to Save As: " << filepath << "\n";
        return;
    }

    file << g_appState.tabs[tabIndex].content;
    file.close();

    g_appState.tabs[tabIndex].filePath = filepath;
    g_appState.tabs[tabIndex].isModified = false;
    g_appState.needsSave = false;
    
    if (fs::exists(filepath)) {
        g_appState.tabs[tabIndex].lastModified = fs::last_write_time(filepath);
    }
    
    AddToRecentFiles(filepath);
    std::cout << "Saved As: " << filepath << "\n";
}

void SaveAll() {
    for (int i = 0; i < (int)g_appState.tabs.size(); ++i) {
        if (g_appState.tabs[i].isModified) {
            SaveFile(i);
        }
    }
}

// -----------------------------
// Tab and editor management - IMPROVED
// -----------------------------
void OpenFile(const std::string& filepath) {
    if (filepath.empty()) return;
    
    if (!fs::exists(filepath)) {
        std::cerr << "File does not exist: " << filepath << "\n";
        return;
    }

    // Check if file is already open
    for (int i = 0; i < (int)g_appState.tabs.size(); ++i) {
        if (g_appState.tabs[i].filePath == filepath) {
            g_appState.activeTab = i;
            g_appState.focusEditor = true;
            return;
        }
    }

    std::string content = ReadFileContent(filepath);
    if (content.empty() && fs::file_size(filepath) > 0) {
        content = "[Binary file: " + filepath + "]\n[Size: " + std::to_string(fs::file_size(filepath)) + " bytes]\n\nThis file appears to be binary and cannot be displayed as text.";
    }

    FileTab tab;
    tab.filePath = filepath;
    tab.content = content;
    tab.lastModified = fs::last_write_time(filepath);
    tab.isReadonly = false;
    tab.isModified = false;

    g_appState.tabs.push_back(std::move(tab));
    g_appState.activeTab = (int)g_appState.tabs.size() - 1;

    AddToRecentFiles(filepath);
    g_appState.focusEditor = true;
}

void CloseTab(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_appState.tabs.size()) return;

    g_appState.tabs.erase(g_appState.tabs.begin() + tabIndex);
    
    if (g_appState.tabs.empty()) {
        g_appState.activeTab = -1;
        g_appState.needsSave = false;
    } else {
        g_appState.activeTab = std::min<int>(tabIndex, (int)g_appState.tabs.size() - 1);
        
        // Update needsSave flag
        g_appState.needsSave = false;
        for (const auto& tab : g_appState.tabs) {
            if (tab.isModified) {
                g_appState.needsSave = true;
                break;
            }
        }
    }
}

void SetupCustomStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Slightly tighter rounding for a modern look
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;

    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    ImVec4* colors = style.Colors;

    // Palette: very dark background with violet accents
    const ImVec4 bg         = ImVec4(0.04f, 0.03f, 0.06f, 1.00f); // deep violet-black
    const ImVec4 panel      = ImVec4(0.07f, 0.05f, 0.10f, 1.00f); // panel bg
    const ImVec4 panelAlt   = ImVec4(0.09f, 0.06f, 0.14f, 1.00f); // slightly lighter
    const ImVec4 accent     = ImVec4(0.58f, 0.28f, 0.86f, 1.00f); // vivid violet
    const ImVec4 accentHov  = ImVec4(0.68f, 0.40f, 0.96f, 1.00f); // hover
    const ImVec4 accentAct  = ImVec4(0.78f, 0.52f, 1.00f, 1.00f); // active
    const ImVec4 muted      = ImVec4(0.44f, 0.40f, 0.48f, 1.00f); // muted text / borders
    const ImVec4 borderCol  = ImVec4(0.18f, 0.10f, 0.24f, 0.65f);

    colors[ImGuiCol_Text]                 = ImVec4(0.96f, 0.94f, 0.99f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.42f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]             = bg;
    colors[ImGuiCol_ChildBg]              = panel;
    colors[ImGuiCol_PopupBg]              = ImVec4(0.06f, 0.04f, 0.08f, 0.95f);
    colors[ImGuiCol_Border]               = borderCol;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]              = panelAlt;
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.12f, 0.08f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.16f, 0.10f, 0.22f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.05f, 0.03f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.07f, 0.04f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.05f, 0.03f, 0.06f, 0.75f);
    colors[ImGuiCol_MenuBarBg]            = panel;
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.03f, 0.02f, 0.04f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f, 0.16f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.20f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f, 0.28f, 0.48f, 1.00f);

    // Interactive controls: violet accents
    colors[ImGuiCol_CheckMark]            = accent;
    colors[ImGuiCol_SliderGrab]           = accent;
    colors[ImGuiCol_SliderGrabActive]     = accentAct;
    colors[ImGuiCol_Button]               = ImVec4(accent.x * 0.86f, accent.y * 0.86f, accent.z * 0.86f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = accentHov;
    colors[ImGuiCol_ButtonActive]         = accentAct;

    // Headers / tabs
    colors[ImGuiCol_Header]               = ImVec4(0.36f, 0.18f, 0.50f, 0.75f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.52f, 0.24f, 0.70f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.64f, 0.34f, 0.88f, 1.00f);

    // Separators / grips
    colors[ImGuiCol_Separator]            = ImVec4(0.12f, 0.09f, 0.16f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.20f, 0.12f, 0.28f, 1.00f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.25f, 0.15f, 0.35f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.10f, 0.06f, 0.14f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.16f, 0.10f, 0.20f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.22f, 0.12f, 0.28f, 1.00f);

    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.02f, 0.01f, 0.03f, 0.60f);

    // Small polish: slightly increase contrast for selected text
    colors[ImGuiCol_PlotLines]            = ImVec4(0.62f, 0.30f, 0.88f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]     = ImVec4(0.78f, 0.44f, 0.98f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.20f, 0.10f, 0.32f, 0.90f);
}

// -----------------------------
// Keyboard shortcuts - IMPROVED
// -----------------------------
void HandleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();

    // Only process shortcuts when not typing in text fields
    if (io.WantTextInput) return;

    // Ctrl+O - Open
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        std::string filepath = OpenFileDialog();
        if (!filepath.empty()) OpenFile(filepath);
    }

    // Ctrl+N - New file / tab
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        FileTab t;
        t.filePath = "";
        t.content = "";
        t.isModified = false;
        g_appState.tabs.push_back(std::move(t));
        g_appState.activeTab = (int)g_appState.tabs.size() - 1;
        g_appState.focusEditor = true;
    }

    // Ctrl+S - Save active
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (g_appState.activeTab >= 0) SaveFile(g_appState.activeTab);
    }

    // Ctrl+Shift+S - Save As
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (g_appState.activeTab >= 0) SaveFileAs(g_appState.activeTab);
    }

    // Ctrl+W - Close active tab
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        if (g_appState.activeTab >= 0) {
            if (g_appState.tabs[g_appState.activeTab].isModified) {
                g_appState.closeTabIndex = g_appState.activeTab;
            } else {
                CloseTab(g_appState.activeTab);
            }
        }
    }

    // F5 - Reload recent files
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        LoadRecentFiles();
    }

    // Esc - Clear search or deselect
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (g_appState.searchBuffer[0] != '\0') {
            g_appState.searchBuffer[0] = '\0';
        }
    }
}

// -----------------------------
// Docking layout & main dockspace
// -----------------------------
void SetupInitialDockingLayout() {
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");

    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

        ImGuiID dock_left, dock_right;
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.20f, &dock_left, &dock_right);

        ImGui::DockBuilderDockWindow("Files", dock_left);
        ImGui::DockBuilderDockWindow("Editor", dock_right);

        ImGui::DockBuilderFinish(dockspace_id);
    }
}

void RenderMainDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (g_appState.firstRun) {
        SetupInitialDockingLayout();
        g_appState.firstRun = false;
    }

    ImGui::End();
}

// -----------------------------
// Menu bar
// -----------------------------
void RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File", "Ctrl+O")) {
                std::string path = OpenFileDialog();
                if (!path.empty()) OpenFile(path);
            }

            if (ImGui::BeginMenu("Recent Files", !g_appState.recentFiles.empty())) {
                for (const auto& filepath : g_appState.recentFiles) {
                    std::string filename = fs::path(filepath).filename().string();
                    if (ImGui::MenuItem(filename.c_str())) {
                        OpenFile(filepath);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", filepath.c_str());
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("New File", "Ctrl+N")) {
                FileTab t;
                t.filePath = "";
                t.content = "";
                t.isModified = false;
                g_appState.tabs.push_back(std::move(t));
                g_appState.activeTab = (int)g_appState.tabs.size() - 1;
                g_appState.focusEditor = true;
            }

            if (ImGui::MenuItem("Save", "Ctrl+S", false, g_appState.activeTab >= 0)) {
                if (g_appState.activeTab >= 0) SaveFile(g_appState.activeTab);
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, g_appState.activeTab >= 0)) {
                if (g_appState.activeTab >= 0) SaveFileAs(g_appState.activeTab);
            }
            if (ImGui::MenuItem("Save All")) {
                SaveAll();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Theme")) {
                if (ImGui::MenuItem("Dark", nullptr, currentTheme == THEME_DARK)) {
                    ImGui::StyleColorsDark();
                    currentTheme = THEME_DARK;
                }
                if (ImGui::MenuItem("Light", nullptr, currentTheme == THEME_LIGHT)) {
                    ImGui::StyleColorsLight();
                    currentTheme = THEME_LIGHT;
                }
                if (ImGui::MenuItem("Custom", nullptr, currentTheme == THEME_CUSTOM)) {
                    SetupCustomStyle();
                    currentTheme = THEME_CUSTOM;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                g_appState.showAboutDialog = true;
            }
            ImGui::EndMenu();
        }

        if (g_appState.needsSave) {
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 160);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[Unsaved Changes]");
        }

        ImGui::EndMainMenuBar();
    }
}

// -----------------------------
// Tabs view (files list) - IMPROVED
// -----------------------------
void RenderTabs() {
    ImGui::Begin("Files");

    if (!g_appState.tabs.empty()) {
        ImGui::Text("Open Files:");
        ImGui::Separator();
        
        for (int i = 0; i < (int)g_appState.tabs.size(); ++i) {
            FileTab &tab = g_appState.tabs[i];
            std::string title = tab.filePath.empty() 
                ? ("Untitled " + std::to_string(i + 1)) 
                : fs::path(tab.filePath).filename().string();
            
            if (tab.isModified) title = "• " + title;

            ImGui::PushID(i);
            
            bool isActive = (g_appState.activeTab == i);
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.8f, 0.8f));
            }
            
            if (ImGui::Selectable(title.c_str(), isActive)) {
                g_appState.activeTab = i;
                g_appState.focusEditor = true;
            }
            
            if (isActive) {
                ImGui::PopStyleColor();
            }
            
            // Context menu
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Close")) {
                    if (tab.isModified) {
                        g_appState.closeTabIndex = i;
                    } else {
                        CloseTab(i);
                    }
                }
                if (ImGui::MenuItem("Save", nullptr, false, tab.isModified)) {
                    SaveFile(i);
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    } else {
        ImGui::TextWrapped("No files open. Use File → Open or create a new file.");
    }

    ImGui::End();
}

// -----------------------------
// Editor panel - FIXED
// -----------------------------
void RenderEditor() {
    ImGui::Begin("Editor");

    if (g_appState.activeTab >= 0 && g_appState.activeTab < (int)g_appState.tabs.size()) {
        FileTab &tab = g_appState.tabs[g_appState.activeTab];

        std::string noteInfo = tab.filePath.empty() 
            ? ("Untitled - Tab " + std::to_string(g_appState.activeTab + 1))
            : fs::path(tab.filePath).filename().string();
        
        if (tab.isModified) noteInfo += " (modified)";
        ImGui::Text("%s", noteInfo.c_str());
        ImGui::Separator();

        ImVec2 availSize = ImGui::GetContentRegionAvail();
        availSize.y -= 80;

        if (g_appState.focusEditor) {
            ImGui::SetKeyboardFocusHere();
            g_appState.focusEditor = false;
        }

        // CRITICAL FIX: Ensure string has capacity for editing
        // Reserve extra space for the buffer
        const size_t minCapacity = 1024;
        if (tab.content.capacity() < minCapacity) {
            tab.content.reserve(minCapacity);
        }

        std::vector<char> buf(tab.content.begin(), tab.content.end());

        ImGuiInputTextFlags flags = 
            ImGuiInputTextFlags_AllowTabInput | 
            ImGuiInputTextFlags_CallbackResize;

        // CRITICAL FIX: Use data() instead of c_str() for mutable access
        // The buffer size should be current size + extra room
        if (ImGui::InputTextMultiline(
                "##editor",
                tab.content.data(),
                tab.content.capacity() + 1,
                availSize,
                flags,
                ImGuiStringInputCallback,
                (void*)&tab.content))
        {
            tab.isModified = true;
            g_appState.needsSave = true;
        }

        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(100, 0))) {
            SaveFile(g_appState.activeTab);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As", ImVec2(100, 0))) {
            SaveFileAs(g_appState.activeTab);
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(100, 0))) {
            if (!tab.filePath.empty() && fs::exists(tab.filePath)) {
                tab.content = ReadFileContent(tab.filePath);
                tab.lastModified = fs::last_write_time(tab.filePath);
                tab.isModified = false;
            } else {
                tab.content.clear();
                tab.isModified = false;
            }
        }

        ImGui::SameLine();
        
        // IMPROVED: Better stats calculation
        size_t charCount = tab.content.length();
        int wordCount = 0;
        bool inWord = false;
        
        for (char c : tab.content) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                inWord = false;
            } else if (!inWord) {
                inWord = true;
                ++wordCount;
            }
        }

        ImGui::Text("Words: %d | Characters: %zu", wordCount, charCount);

    } else {
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImVec2 textSize = ImGui::CalcTextSize("Open a file or create a new file");
        ImGui::SetCursorPos(ImVec2((windowSize.x - textSize.x) * 0.5f, windowSize.y * 0.4f));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Open a file or create a new file");
        
        ImGui::SetCursorPos(ImVec2((windowSize.x - 300) * 0.5f, windowSize.y * 0.5f));
        if (ImGui::Button("New File", ImVec2(140, 0))) {
            FileTab t;
            t.filePath = "";
            t.content = "";
            t.isModified = false;
            g_appState.tabs.push_back(std::move(t));
            g_appState.activeTab = (int)g_appState.tabs.size() - 1;
            g_appState.focusEditor = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open File", ImVec2(140, 0))) {
            std::string path = OpenFileDialog();
            if (!path.empty()) OpenFile(path);
        }
    }

    ImGui::End();
}

// -----------------------------
// Simple ImGui file browser (fallback)
// -----------------------------
void RenderSimpleFileBrowser() {
    if (!g_appState.showFileDialog) return;

    ImGui::OpenPopup("File Browser");
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::BeginPopupModal("File Browser", &g_appState.showFileDialog)) {
        if (g_appState.currentPath.empty()) {
            g_appState.currentPath = fs::current_path().string();
        }

        ImGui::Text("Current Path: %s", g_appState.currentPath.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Up")) {
            fs::path parent = fs::path(g_appState.currentPath).parent_path();
            if (!parent.empty()) {
                g_appState.currentPath = parent.string();
            }
        }

        ImGui::Separator();

        ImGui::BeginChild("FileList", ImVec2(0, -60));
        try {
            for (const auto& entry : fs::directory_iterator(g_appState.currentPath)) {
                std::string name = entry.path().filename().string();
                if (entry.is_directory()) {
                    if (ImGui::Selectable(("[DIR] " + name).c_str())) {
                        g_appState.currentPath = entry.path().string();
                    }
                } else {
                    if (ImGui::Selectable(name.c_str())) {
                        strncpy(g_appState.filePathBuffer, entry.path().string().c_str(), 
                               sizeof(g_appState.filePathBuffer) - 1);
                        g_appState.filePathBuffer[sizeof(g_appState.filePathBuffer) - 1] = '\0';
                    }
                }
            }
        } catch (const std::exception& e) {
            ImGui::Text("Error reading directory: %s", e.what());
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::InputText("File", g_appState.filePathBuffer, sizeof(g_appState.filePathBuffer));

        if (ImGui::Button("Open")) {
            if (strlen(g_appState.filePathBuffer) > 0) {
                OpenFile(g_appState.filePathBuffer);
                g_appState.showFileDialog = false;
                g_appState.filePathBuffer[0] = '\0';
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            g_appState.showFileDialog = false;
            g_appState.filePathBuffer[0] = '\0';
        }

        ImGui::EndPopup();
    }
}

// -----------------------------
// Dialogs - IMPROVED
// -----------------------------
void RenderDialogs() {
    // Close tab confirmation
    if (g_appState.closeTabIndex >= 0) {
        ImGui::OpenPopup("Unsaved Changes");
    }

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("This file has unsaved changes.");
        ImGui::Text("Do you want to save before closing?");
        ImGui::Separator();
        
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (g_appState.closeTabIndex >= 0 && g_appState.closeTabIndex < (int)g_appState.tabs.size()) {
                SaveFile(g_appState.closeTabIndex);
                CloseTab(g_appState.closeTabIndex);
            }
            g_appState.closeTabIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
            if (g_appState.closeTabIndex >= 0 && g_appState.closeTabIndex < (int)g_appState.tabs.size()) {
                CloseTab(g_appState.closeTabIndex);
            }
            g_appState.closeTabIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_appState.closeTabIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }

    // About dialog
    if (g_appState.showAboutDialog) {
        ImGui::OpenPopup("About Notifier");
        g_appState.showAboutDialog = false;
    }

    if (ImGui::BeginPopupModal("About Notifier", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Notifier - File Editor");
        ImGui::Separator();

        ImGui::Text("Version: 1.0.1 (Fixed)");
        ImGui::Text("Built with C++ and Dear ImGui");
        ImGui::Separator();
        
        ImGui::Text("Features:");
        ImGui::BulletText("Open and edit any file type");
        ImGui::BulletText("Smart file type detection");
        ImGui::BulletText("Recent files history");
        ImGui::BulletText("Multiple file tabs");
        ImGui::BulletText("Fast search through files");
        ImGui::BulletText("Keyboard shortcuts");
        ImGui::BulletText("Auto-save indicator");
        ImGui::BulletText("Multiple themes");
        ImGui::BulletText("Dockable interface");
        
        ImGui::Separator();
        ImGui::Text("Keyboard Shortcuts:");
        ImGui::BulletText("Ctrl+O: Open file");
        ImGui::BulletText("Ctrl+N: New file");
        ImGui::BulletText("Ctrl+S: Save");
        ImGui::BulletText("Ctrl+Shift+S: Save as");
        ImGui::BulletText("Ctrl+W: Close tab");
        ImGui::BulletText("F5: Reload recent files");
        
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }

    // Fallback file browser
    RenderSimpleFileBrowser();
}

// -----------------------------
// main
// -----------------------------
int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "Notifier - File Editor", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Load fonts
    ImFont* defaultFont = io.Fonts->AddFontDefault();

    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };

    ImFont* mainFont = nullptr;
    for (const char* fp : fontPaths) {
        if (fs::exists(fp)) {
            mainFont = io.Fonts->AddFontFromFileTTF(fp, 16.0f);
            if (mainFont) { 
                io.FontDefault = mainFont; 
                break; 
            }
        }
    }

    SetupCustomStyle();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    LoadRecentFiles();

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        HandleKeyboardShortcuts();

        RenderMainDockSpace();
        RenderMenuBar();
        RenderTabs();
        RenderEditor();
        RenderDialogs();

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }

    SaveRecentFiles();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
