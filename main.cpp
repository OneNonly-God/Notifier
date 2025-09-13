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

enum ThemeType { THEME_DARK, THEME_LIGHT, THEME_CUSTOM };
ThemeType currentTheme = THEME_CUSTOM;
const std::string DELIMITER = "--------------";
const std::string NOTES_FILE = "notes.txt";

// Application state
struct AppState {
    std::vector<std::string> notes;
    int selectedNote = -1;
    bool needsSave = false;
    bool showAboutDialog = false;
    bool showConfirmDelete = false;
    int noteToDelete = -1;
    bool firstRun = true;
    
    // Input buffers
    char inputBuffer[8192] = "";
    char editBuffer[8192] = "";
    char searchBuffer[256] = "";
    
    // UI state
    bool focusEditor = false;
    bool focusNewNote = false;
};

AppState g_appState;

// Forward declarations
void SetupCustomStyle();
void HandleKeyboardShortcuts();
void LoadNotes();
void SaveNotes();
void SetupInitialDockingLayout();

// Utility function to get note title
std::string GetNoteTitle(const std::string& note, size_t maxLength = 30) {
    if (note.empty()) return "[Empty Note]";
    
    std::istringstream stream(note);
    std::string firstLine;
    std::getline(stream, firstLine);
    
    if (firstLine.empty()) return "[Empty Note]";
    
    // Trim whitespace
    firstLine.erase(0, firstLine.find_first_not_of(" \t\r\n"));
    firstLine.erase(firstLine.find_last_not_of(" \t\r\n") + 1);
    
    if (firstLine.length() > maxLength) {
        return firstLine.substr(0, maxLength) + "...";
    }
    return firstLine;
}

void LoadNotes() {
    g_appState.notes.clear();
    
    std::ifstream file(NOTES_FILE);
    if (!file.is_open()) {
        std::cout << "Notes file not found. Will create a new one when saving.\n";
        return;
    }

    std::string line, note;
    while (std::getline(file, line)) {
        if (line == DELIMITER) {
            if (!note.empty()) {
                // Remove trailing newline if present
                if (!note.empty() && note.back() == '\n') {
                    note.pop_back();
                }
                g_appState.notes.push_back(note);
                note.clear();
            }
        } else {
            if (!note.empty()) note += "\n";
            note += line;
        }
    }
    
    // Add the last note if it doesn't end with delimiter
    if (!note.empty()) {
        if (!note.empty() && note.back() == '\n') {
            note.pop_back();
        }
        g_appState.notes.push_back(note);
    }
    
    file.close();
    std::cout << "Loaded " << g_appState.notes.size() << " notes from " << NOTES_FILE << "\n";
}

void SaveNotes() {
    std::ofstream file(NOTES_FILE, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to save notes to " << NOTES_FILE << "\n";
        return;
    }
    
    for (size_t i = 0; i < g_appState.notes.size(); ++i) {
        file << g_appState.notes[i];
        if (!g_appState.notes[i].empty() && g_appState.notes[i].back() != '\n') {
            file << "\n";
        }
        file << DELIMITER << "\n";
    }
    
    file.close();
    std::cout << "Saved " << g_appState.notes.size() << " notes to " << NOTES_FILE << "\n";
    g_appState.needsSave = false;
}

void SetupCustomStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Spacing and sizing
    style.WindowRounding = 8.0f;
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
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 12.0f;

    // Custom color scheme
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                = ImVec4(0.92f, 0.92f, 0.94f, 1.0f);
    colors[ImGuiCol_TextDisabled]        = ImVec4(0.44f, 0.44f, 0.47f, 1.0f);
    colors[ImGuiCol_WindowBg]            = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    colors[ImGuiCol_ChildBg]             = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    colors[ImGuiCol_PopupBg]             = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
    colors[ImGuiCol_Border]              = ImVec4(0.25f, 0.25f, 0.29f, 0.5f);
    colors[ImGuiCol_BorderShadow]        = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    colors[ImGuiCol_FrameBg]             = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]      = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgActive]       = ImVec4(0.25f, 0.25f, 0.28f, 1.0f);
    colors[ImGuiCol_TitleBg]             = ImVec4(0.06f, 0.06f, 0.08f, 1.0f);
    colors[ImGuiCol_TitleBgActive]       = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.06f, 0.06f, 0.08f, 0.75f);
    colors[ImGuiCol_MenuBarBg]           = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    colors[ImGuiCol_ScrollbarBg]         = ImVec4(0.05f, 0.05f, 0.07f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab]       = ImVec4(0.25f, 0.25f, 0.29f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.35f, 0.35f, 0.39f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.45f, 0.49f, 1.0f);
    colors[ImGuiCol_CheckMark]           = ImVec4(0.3f, 0.5f, 0.8f, 1.0f);
    colors[ImGuiCol_SliderGrab]          = ImVec4(0.3f, 0.5f, 0.8f, 1.0f);
    colors[ImGuiCol_SliderGrabActive]    = ImVec4(0.4f, 0.6f, 0.9f, 1.0f);
    colors[ImGuiCol_Button]              = ImVec4(0.2f, 0.35f, 0.55f, 1.0f);
    colors[ImGuiCol_ButtonHovered]       = ImVec4(0.3f, 0.45f, 0.65f, 1.0f);
    colors[ImGuiCol_ButtonActive]        = ImVec4(0.4f, 0.55f, 0.75f, 1.0f);
    colors[ImGuiCol_Header]              = ImVec4(0.2f, 0.3f, 0.6f, 0.8f);
    colors[ImGuiCol_HeaderHovered]       = ImVec4(0.3f, 0.4f, 0.7f, 1.0f);
    colors[ImGuiCol_HeaderActive]        = ImVec4(0.4f, 0.5f, 0.8f, 1.0f);
    colors[ImGuiCol_Separator]           = ImVec4(0.25f, 0.25f, 0.29f, 1.0f);
    colors[ImGuiCol_SeparatorHovered]    = ImVec4(0.35f, 0.35f, 0.39f, 1.0f);
    colors[ImGuiCol_SeparatorActive]     = ImVec4(0.45f, 0.45f, 0.49f, 1.0f);
    colors[ImGuiCol_ResizeGrip]          = ImVec4(0.25f, 0.25f, 0.29f, 0.3f);
    colors[ImGuiCol_ResizeGripHovered]   = ImVec4(0.35f, 0.35f, 0.39f, 0.6f);
    colors[ImGuiCol_ResizeGripActive]    = ImVec4(0.45f, 0.45f, 0.49f, 0.9f);
    colors[ImGuiCol_Tab]                 = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_TabHovered]          = ImVec4(0.25f, 0.25f, 0.29f, 1.0f);
    colors[ImGuiCol_TabActive]           = ImVec4(0.18f, 0.18f, 0.21f, 1.0f);
    colors[ImGuiCol_TabUnfocused]        = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_DockingPreview]      = ImVec4(0.3f, 0.5f, 0.8f, 0.7f);
    colors[ImGuiCol_DockingEmptyBg]      = ImVec4(0.05f, 0.05f, 0.07f, 1.0f);
    colors[ImGuiCol_PlotLines]           = ImVec4(0.61f, 0.61f, 0.64f, 1.0f);
    colors[ImGuiCol_PlotLinesHovered]    = ImVec4(1.00f, 0.43f, 0.35f, 1.0f);
    colors[ImGuiCol_PlotHistogram]       = ImVec4(0.90f, 0.70f, 0.00f, 1.0f);
    colors[ImGuiCol_PlotHistogramHovered]= ImVec4(1.00f, 0.60f, 0.00f, 1.0f);
    colors[ImGuiCol_TextSelectedBg]      = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget]      = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]        = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]   = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]    = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void HandleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Only process shortcuts when no input widget is active
    if (!io.WantTextInput) {
        // Ctrl+N - New Note
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
            g_appState.notes.push_back("");
            g_appState.selectedNote = g_appState.notes.size() - 1;
            g_appState.editBuffer[0] = '\0';
            g_appState.needsSave = true;
            g_appState.focusEditor = true;
        }
        
        // Ctrl+S - Save
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (g_appState.needsSave) {
                SaveNotes();
            }
        }
        
        // F5 - Reload
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            LoadNotes();
            g_appState.selectedNote = -1;
            g_appState.needsSave = false;
        }
        
        // Delete key - Delete selected note (with confirmation)
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && g_appState.selectedNote >= 0) {
            g_appState.showConfirmDelete = true;
            g_appState.noteToDelete = g_appState.selectedNote;
        }
        
        // Escape - Clear search or deselect note
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (g_appState.searchBuffer[0] != '\0') {
                g_appState.searchBuffer[0] = '\0';
            } else if (g_appState.selectedNote >= 0) {
                g_appState.selectedNote = -1;
                g_appState.editBuffer[0] = '\0';
            }
        }
    }
}

void SetupInitialDockingLayout() {
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    
    // Only setup if no existing layout
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        // Clear any existing layout
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

        ImGuiID dock_left, dock_right, dock_bottom;
        
        // Split the dock space
        ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.25f, &dock_left, &dock_right);
        ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.30f, &dock_bottom, &dock_right);

        // Dock windows
        ImGui::DockBuilderDockWindow("Notes List", dock_left);
        ImGui::DockBuilderDockWindow("Editor", dock_right);
        ImGui::DockBuilderDockWindow("New Note", dock_bottom);

        ImGui::DockBuilderFinish(dockspace_id);
    }
}

void RenderMainDockSpace() {
    // Main viewport dockspace
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
    
    // Setup layout on first run
    if (g_appState.firstRun) {
        SetupInitialDockingLayout();
        g_appState.firstRun = false;
    }

    ImGui::End();
}

void RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Note", "Ctrl+N")) {
                g_appState.notes.push_back("");
                g_appState.selectedNote = g_appState.notes.size() - 1;
                g_appState.editBuffer[0] = '\0';
                g_appState.needsSave = true;
                g_appState.focusEditor = true;
            }
            if (ImGui::MenuItem("Save All", "Ctrl+S", false, g_appState.needsSave)) {
                SaveNotes();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (g_appState.needsSave) {
                    SaveNotes();
                }
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Delete Note", "Del", false, g_appState.selectedNote >= 0)) {
                g_appState.showConfirmDelete = true;
                g_appState.noteToDelete = g_appState.selectedNote;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reload Notes", "F5")) {
                LoadNotes();
                g_appState.selectedNote = -1;
                g_appState.needsSave = false;
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

        // Right side of menu bar - save indicator
        if (g_appState.needsSave) {
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 160);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[Unsaved Changes]");
        }

        ImGui::EndMainMenuBar();
    }
}

void RenderNotesList() {
    ImGui::Begin("Notes List");
    
    // Search bar
    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search notes...", g_appState.searchBuffer, IM_ARRAYSIZE(g_appState.searchBuffer));
    ImGui::PopItemWidth();
    ImGui::Separator();
    
    // Display notes
    ImGuiListClipper clipper;
    std::vector<int> filteredIndices;
    
    // Filter notes based on search
    for (int i = 0; i < (int)g_appState.notes.size(); i++) {
        if (g_appState.searchBuffer[0] == '\0') {
            filteredIndices.push_back(i);
        } else {
            std::string title = GetNoteTitle(g_appState.notes[i]);
            std::string searchLower(g_appState.searchBuffer);
            std::string titleLower = title;
            std::string contentLower = g_appState.notes[i];
            
            // Convert to lowercase for case-insensitive search
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);
            std::transform(contentLower.begin(), contentLower.end(), contentLower.begin(), ::tolower);
            
            if (titleLower.find(searchLower) != std::string::npos || 
                contentLower.find(searchLower) != std::string::npos) {
                filteredIndices.push_back(i);
            }
        }
    }
    
    // Use clipper for performance with large lists
    clipper.Begin(filteredIndices.size());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            int noteIndex = filteredIndices[row];
            std::string title = GetNoteTitle(g_appState.notes[noteIndex]);
            
            char label[300];
            snprintf(label, sizeof(label), "%d. %s", noteIndex + 1, title.c_str());
            
            if (ImGui::Selectable(label, g_appState.selectedNote == noteIndex)) {
                g_appState.selectedNote = noteIndex;
                // Copy note content to edit buffer
                strncpy(g_appState.editBuffer, g_appState.notes[g_appState.selectedNote].c_str(), sizeof(g_appState.editBuffer) - 1);
                g_appState.editBuffer[sizeof(g_appState.editBuffer) - 1] = '\0';
                g_appState.focusEditor = true;
            }
            
            // Context menu
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Delete")) {
                    g_appState.showConfirmDelete = true;
                    g_appState.noteToDelete = noteIndex;
                }
                if (ImGui::MenuItem("Duplicate")) {
                    g_appState.notes.insert(g_appState.notes.begin() + noteIndex + 1, g_appState.notes[noteIndex]);
                    g_appState.needsSave = true;
                }
                ImGui::EndPopup();
            }
        }
    }
    
    ImGui::Separator();
    ImGui::Text("Total: %zu notes", g_appState.notes.size());
    if (g_appState.searchBuffer[0] != '\0') {
        ImGui::Text("Filtered: %zu notes", filteredIndices.size());
    }
    
    ImGui::End();
}

void RenderEditor() {
    ImGui::Begin("Editor");
    
    if (g_appState.selectedNote >= 0 && g_appState.selectedNote < (int)g_appState.notes.size()) {
        ImGui::Text("Editing Note #%d", g_appState.selectedNote + 1);
        ImGui::Separator();
        
        // Calculate available space for text editor
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        availSize.y -= 80; // Leave room for buttons and status
        
        // Focus the editor if requested
        if (g_appState.focusEditor) {
            ImGui::SetKeyboardFocusHere();
            g_appState.focusEditor = false;
        }
        
        // Text editor
        if (ImGui::InputTextMultiline("##editor", g_appState.editBuffer, IM_ARRAYSIZE(g_appState.editBuffer),
                                      availSize, ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CtrlEnterForNewLine)) {
            g_appState.notes[g_appState.selectedNote] = g_appState.editBuffer;
            g_appState.needsSave = true;
        }
        
        ImGui::Separator();
        
        // Editor buttons
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            SaveNotes();
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(100, 0))) {
            strncpy(g_appState.editBuffer, g_appState.notes[g_appState.selectedNote].c_str(), sizeof(g_appState.editBuffer) - 1);
            g_appState.editBuffer[sizeof(g_appState.editBuffer) - 1] = '\0';
        }
        
        // Statistics
        ImGui::SameLine();
        int wordCount = 0;
        int charCount = strlen(g_appState.editBuffer);
        bool inWord = false;
        for (int i = 0; i < charCount; i++) {
            if (isspace(static_cast<unsigned char>(g_appState.editBuffer[i]))) {
                inWord = false;
            } else if (!inWord) {
                inWord = true;
                wordCount++;
            }
        }
        ImGui::Text("Words: %d | Characters: %d", wordCount, charCount);
        
    } else {
        // No note selected
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImVec2 textSize = ImGui::CalcTextSize("Select a note to edit or create a new one");
        ImGui::SetCursorPos(ImVec2((windowSize.x - textSize.x) * 0.5f, windowSize.y * 0.4f));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Select a note to edit or create a new one");
        
        ImGui::SetCursorPos(ImVec2((windowSize.x - 120) * 0.5f, windowSize.y * 0.5f));
        if (ImGui::Button("Create New Note", ImVec2(140, 0))) {
            g_appState.notes.push_back("");
            g_appState.selectedNote = g_appState.notes.size() - 1;
            g_appState.editBuffer[0] = '\0';
            g_appState.needsSave = true;
            g_appState.focusEditor = true;
        }
    }
    
    ImGui::End();
}

void RenderNewNotePanel() {
    ImGui::Begin("New Note");
    ImGui::Text("Create a new note:");
    ImGui::Separator();
    
    ImVec2 availSize = ImGui::GetContentRegionAvail();
    availSize.y -= 35;
    
    // Focus new note input if requested
    if (g_appState.focusNewNote) {
        ImGui::SetKeyboardFocusHere();
        g_appState.focusNewNote = false;
    }
    
    ImGui::InputTextMultiline("##newnote", g_appState.inputBuffer, IM_ARRAYSIZE(g_appState.inputBuffer),
                              availSize, ImGuiInputTextFlags_AllowTabInput);
    
    if (ImGui::Button("Add Note", ImVec2(120, 0)) && g_appState.inputBuffer[0] != '\0') {
        g_appState.notes.push_back(g_appState.inputBuffer);
        g_appState.inputBuffer[0] = '\0';
        g_appState.selectedNote = g_appState.notes.size() - 1;
        strncpy(g_appState.editBuffer, g_appState.notes[g_appState.selectedNote].c_str(), sizeof(g_appState.editBuffer) - 1);
        g_appState.editBuffer[sizeof(g_appState.editBuffer) - 1] = '\0';
        g_appState.needsSave = true;
        g_appState.focusEditor = true;
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(120, 0))) {
        g_appState.inputBuffer[0] = '\0';
    }
    
    ImGui::End();
}

void RenderDialogs() {
    // Delete confirmation dialog
    if (g_appState.showConfirmDelete) {
        ImGui::OpenPopup("Delete Note?");
        g_appState.showConfirmDelete = false;
    }
    
    if (ImGui::BeginPopupModal("Delete Note?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete this note?");
        ImGui::Text("This action cannot be undone.");
        ImGui::Separator();
        
        if (ImGui::Button("Yes, Delete", ImVec2(120, 0))) {
            if (g_appState.noteToDelete >= 0 && g_appState.noteToDelete < (int)g_appState.notes.size()) {
                g_appState.notes.erase(g_appState.notes.begin() + g_appState.noteToDelete);
                if (g_appState.selectedNote >= (int)g_appState.notes.size()) {
                    g_appState.selectedNote = g_appState.notes.empty() ? -1 : g_appState.notes.size() - 1;
                }
                if (g_appState.selectedNote >= 0 && g_appState.selectedNote < (int)g_appState.notes.size()) {
                    strncpy(g_appState.editBuffer, g_appState.notes[g_appState.selectedNote].c_str(), sizeof(g_appState.editBuffer) - 1);
                    g_appState.editBuffer[sizeof(g_appState.editBuffer) - 1] = '\0';
                } else {
                    g_appState.editBuffer[0] = '\0';
                }
                g_appState.needsSave = true;
            }
            g_appState.noteToDelete = -1;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_appState.noteToDelete = -1;
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
        ImGui::Text("Notifier - A Simple Notes Application");
        ImGui::Separator();
        ImGui::Text("Version: 1.0 - Beta Edition");
        ImGui::Text("Built with C++ and Dear ImGui");
        ImGui::Separator();
        
        ImGui::Text("Features:");
        ImGui::BulletText("Create, edit, and delete notes");
        ImGui::BulletText("Fast search through notes");
        ImGui::BulletText("Keyboard shortcuts");
        ImGui::BulletText("Auto-save indicator");
        ImGui::BulletText("Multiple themes");
        ImGui::BulletText("Dockable interface");
        
        ImGui::Separator();
        ImGui::Text("Keyboard Shortcuts:");
        ImGui::BulletText("Ctrl+N: New note");
        ImGui::BulletText("Ctrl+S: Save all");
        ImGui::BulletText("F5: Reload notes");
        ImGui::BulletText("Del: Delete selected note");
        ImGui::BulletText("Esc: Clear search/deselect");
        
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    // Create window
    GLFWwindow* window = glfwCreateWindow(1400, 900, "Notifier - Enhanced Notes Application", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent(window);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    // Enable docking and viewports
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    
    // Setup font (with fallback)
    ImFont* defaultFont = io.Fonts->AddFontDefault();
    
    const char* fontPaths[] = {
        //I have a fonts folder and might add more fonts in future, this only works for linux rn :P
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
    
    ImFont* mainFont = nullptr;
    for (const char* fontPath : fontPaths) {
        if (std::filesystem::exists(fontPath)) {
            mainFont = io.Fonts->AddFontFromFileTTF(fontPath, 16.0f);
            if (mainFont) {
                io.FontDefault = mainFont;
                break;
            }
        }
    }
    
    // Setup style
    SetupCustomStyle();
    
    // Adjust style for viewports
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Initialize backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Load notes
    LoadNotes();

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Handle keyboard shortcuts
        HandleKeyboardShortcuts();

        // Render main dockspace
        RenderMainDockSpace();
        
        // Render menu bar
        RenderMenuBar();

        // Render main windows
        RenderNotesList();
        RenderEditor();
        RenderNewNotePanel();
        
        // Render dialogs
        RenderDialogs();

        // Render
        ImGui::Render();
        
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Update and render additional platform windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }

    // Save before exit
    if (g_appState.needsSave) {
        SaveNotes();
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}