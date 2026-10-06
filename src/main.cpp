// main.cpp - JuggleSim entry point.
//
// Win32 window + WGL OpenGL context + Dear ImGui (Win32 and OpenGL3 backends).
//
// Layout (below the main menu bar):
//   +----------------------+----------------------+
//   |                      |                      |
//   |    ladder diagram    |   3D juggler view    |
//   |                      |  (plain GL viewport) |
//   +----------------------+                      |
//   |  siteswap entry      |                      |
//   +----------------------+----------------------+

#include "color_vision.h"
#include "gl_funcs.h"
#include "juggler_figure.h"
#include "ladder_view.h"
#include "math3d.h"
#include "mesh.h"
#include "pattern.h"
#include "renderer.h"
#include "settings.h"
#include "siteswap.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Declared (commented out) in imgui_impl_win32.h; must be forward-declared by the app.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

HDC g_hdc = nullptr;
HGLRC g_hglrc = nullptr;
int g_width = 1600;
int g_height = 900;

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                g_width = LOWORD(lParam);
                g_height = HIWORD(lParam);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // disable ALT application menu
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool createGLContext(HWND hwnd) {
    g_hdc = ::GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    int format = ::ChoosePixelFormat(g_hdc, &pfd);
    if (format == 0 || !::SetPixelFormat(g_hdc, format, &pfd)) return false;

    // A legacy-created context is a compatibility profile at the driver's highest version,
    // which is enough for our GLSL 330 shaders. (Core-profile creation via
    // wglCreateContextAttribsARB can come later if we ever need it.)
    g_hglrc = ::wglCreateContext(g_hdc);
    if (!g_hglrc) return false;
    return ::wglMakeCurrent(g_hdc, g_hglrc) != FALSE;
}

void enableVSync() {
    using PFNWGLSWAPINTERVALEXTPROC = BOOL(WINAPI*)(int);
    PFNWGLSWAPINTERVALEXTPROC swapInterval =
        reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(::wglGetProcAddress("wglSwapIntervalEXT"));
    if (swapInterval) swapInterval(1);
}

void destroyGLContext(HWND hwnd) {
    ::wglMakeCurrent(nullptr, nullptr);
    if (g_hglrc) ::wglDeleteContext(g_hglrc);
    if (g_hdc) ::ReleaseDC(hwnd, g_hdc);
    g_hglrc = nullptr;
    g_hdc = nullptr;
}

// Undo/redo history of the pattern, kept as siteswap text (which also records the loop
// period: 531 and 531531 are different entries). One step per closed edit chain, period
// change, or siteswap committed in the text box.
struct PatternHistory {
    std::vector<std::string> undo;
    std::vector<std::string> redo;
    std::string current;  // text of the pattern being shown

    // Call when the pattern has changed to `next` by a user action.
    void record(const std::string& next) {
        if (next == current) return;
        undo.push_back(current);
        if (undo.size() > 1000) undo.erase(undo.begin());
        redo.clear();
        current = next;
    }
};

// Pixel rectangle in GL convention (origin at the bottom-left of the window).
struct GLRect {
    int x, y, w, h;
};

void renderJugglerView(Renderer& renderer, const Primitives& prims, const JugglerPose& pose,
                       const GLRect& rect) {
    if (rect.w <= 0 || rect.h <= 0) return;

    glViewport(rect.x, rect.y, rect.w, rect.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, rect.w, rect.h);
    glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    // Camera in front of the juggler (who faces +Z), looking back at them.
    const float aspect = static_cast<float>(rect.w) / static_cast<float>(rect.h);
    const Mat4 proj = Mat4::perspective(35.0f * kPi / 180.0f, aspect, 0.05f, 100.0f);
    const Mat4 view = Mat4::lookAt({0.0f, 1.15f, 3.4f}, {0.0f, 0.95f, 0.0f}, {0.0f, 1.0f, 0.0f});

    renderer.beginScene(proj * view, {-0.4f, -1.0f, -0.6f});

    // Floor disc.
    renderer.drawMesh(prims.cylinder,
                      Mat4::translation({0.0f, -0.01f, 0.0f}) * Mat4::scaling({1.2f, 0.01f, 1.2f}),
                      {0.30f, 0.32f, 0.36f});

    drawJuggler(renderer, prims, pose, Mat4::identity(), JugglerStyle{});

    renderer.endScene();
}

}  // namespace

int main() {
    // Note: not DPI-aware yet, so Windows scales the window on high-DPI displays.
    // Proper per-monitor DPI handling (crisp fonts) is a later polish item.

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"JuggleSimWindow";
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"JuggleSim", WS_OVERLAPPEDWINDOW, 100, 100,
                                g_width, g_height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd || !createGLContext(hwnd)) {
        ::MessageBoxA(nullptr, "Failed to create an OpenGL context.", "JuggleSim", MB_ICONERROR);
        return 1;
    }

    std::string missing;
    if (!gl::load(&missing)) {
        std::string msg = "Missing OpenGL functions (driver too old?):\n" + missing;
        ::MessageBoxA(nullptr, msg.c_str(), "JuggleSim", MB_ICONERROR);
        destroyGLContext(hwnd);
        return 1;
    }
    enableVSync();

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // fixed layout; nothing worth persisting yet
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL3_Init();

    Renderer renderer;
    std::string error;
    if (!renderer.init(&error)) {
        ::MessageBoxA(nullptr, error.c_str(), "JuggleSim: shader error", MB_ICONERROR);
        return 1;
    }
    Primitives prims;
    prims.create();
    const JugglerPose pose = makeNeutralPose();

    AppSettings settings = loadSettings();

    // The pattern (throw events) is the source of truth for juggling. The siteswap text box
    // generates it when the user types, and is rewritten from it when the pattern changes some
    // other way (e.g. the toolbar's period control). While the text is invalid, the last valid
    // pattern is kept but not shown.
    char siteswapText[256] = "531";
    Siteswap parsed = parseSiteswap(siteswapText);
    Pattern pattern = patternFromSiteswap(parsed);
    bool patternValid = parsed.valid;
    LadderEditState ladderEdit;  // drag-editing and pan/zoom state for the ladder
    PatternHistory history;
    history.current = siteswapText;

    // Replaces the pattern with the one written in `text` (used by undo/redo).
    auto showPatternText = [&](const std::string& text) {
        cancelLadderEdit(ladderEdit);
        std::snprintf(siteswapText, sizeof(siteswapText), "%s", text.c_str());
        parsed = parseSiteswap(siteswapText);
        patternValid = parsed.valid;
        if (parsed.valid) pattern = patternFromSiteswap(parsed);
    };
    auto undo = [&]() {
        if (ladderEdit.chain.active) {  // an open chain is just cancelled, like Esc
            cancelLadderEdit(ladderEdit);
            return;
        }
        if (history.undo.empty()) return;
        history.redo.push_back(history.current);
        history.current = history.undo.back();
        history.undo.pop_back();
        showPatternText(history.current);
    };
    auto redo = [&]() {
        if (history.redo.empty()) return;
        cancelLadderEdit(ladderEdit);
        history.undo.push_back(history.current);
        history.current = history.redo.back();
        history.redo.pop_back();
        showPatternText(history.current);
    };
    bool showImGuiDemo = false;
    bool showColorPreview = false;

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (::IsIconic(hwnd)) {
            ::Sleep(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // --- Keyboard shortcuts (not while typing: the text box has its own undo) ---
        if (!io.WantTextInput && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift) undo();
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift)) redo();
        }

        // --- Main menu bar ---
        float menuHeight = 0.0f;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) done = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                                    !history.undo.empty() || ladderEdit.chain.active))
                    undo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !history.redo.empty())) redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Ladder View", "Home", false, !ladderViewIsDefault(ladderEdit)))
                    resetLadderView(ladderEdit);
                ImGui::Separator();
                if (ImGui::BeginMenu("Color Vision")) {
                    for (int i = 0; i < static_cast<int>(ColorVisionMode::Count); ++i) {
                        const ColorVisionMode mode = static_cast<ColorVisionMode>(i);
                        if (ImGui::MenuItem(colorVisionModeName(mode), nullptr,
                                            settings.colorVision == mode) &&
                            settings.colorVision != mode) {
                            settings.colorVision = mode;
                            saveSettings(settings);
                        }
                    }
                    ImGui::Separator();
                    ImGui::MenuItem("Preview all modes...", nullptr, &showColorPreview);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                ImGui::MenuItem("ImGui demo window", nullptr, &showImGuiDemo);
                ImGui::EndMenu();
            }
            menuHeight = ImGui::GetWindowSize().y;
            ImGui::EndMainMenuBar();
        }

        // --- Pane layout ---
        const ImGuiStyle& style = ImGui::GetStyle();
        const float W = io.DisplaySize.x;
        const float H = io.DisplaySize.y;
        const float top = menuHeight;
        const float leftWidth = std::floor(W * 0.5f);
        const float entryHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeight() +
                                  style.WindowPadding.y * 2.0f;
        const ImGuiWindowFlags paneFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus;

        // Ladder diagram (top of left half).
        ImGui::SetNextWindowPos(ImVec2(0.0f, top));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, H - top - entryHeight));
        // The ladder handles the mouse wheel itself (pan/zoom), so the pane mustn't scroll.
        if (ImGui::Begin("Ladder", nullptr, paneFlags | ImGuiWindowFlags_NoScrollWithMouse)) {
            const LadderToolbarRequest request = drawLadderToolbar(pattern, patternValid, ladderEdit);
            if (request.resetView) resetLadderView(ladderEdit);
            if (request.newPeriodBeats > 0) {
                cancelLadderEdit(ladderEdit);
                pattern = withPeriod(pattern, request.newPeriodBeats);
                std::string text;
                if (patternToSiteswap(pattern, &text)) {
                    std::snprintf(siteswapText, sizeof(siteswapText), "%s", text.c_str());
                    parsed = parseSiteswap(siteswapText);
                    history.record(text);
                }
            }
            const LadderEditResult edited =
                drawLadderDiagram(pattern, patternValid, settings.colorVision, ladderEdit);
            if (edited.committed) {
                // A closed edit chain is always a valid siteswap; double-check before using it.
                Pattern editedPattern = patternFromLoopValues(edited.loop);
                editedPattern = withPeriod(editedPattern, shortestPeriodBeats(editedPattern));
                std::string text;
                if (patternToSiteswap(editedPattern, &text) && parseSiteswap(text).valid) {
                    pattern = editedPattern;
                    std::snprintf(siteswapText, sizeof(siteswapText), "%s", text.c_str());
                    parsed = parseSiteswap(siteswapText);
                    history.record(text);
                }
            }
        }
        ImGui::End();

        // Siteswap entry (bottom of left half).
        ImGui::SetNextWindowPos(ImVec2(0.0f, H - entryHeight));
        ImGui::SetNextWindowSize(ImVec2(leftWidth, entryHeight));
        if (ImGui::Begin("Siteswap entry", nullptr, paneFlags)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Siteswap");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##siteswap", siteswapText, sizeof(siteswapText))) {
                cancelLadderEdit(ladderEdit);
                parsed = parseSiteswap(siteswapText);
                patternValid = parsed.valid;
                if (parsed.valid) pattern = patternFromSiteswap(parsed);
            }
            // Typing is one undo step, recorded when you press Enter or leave the box (so
            // undoing "531" doesn't step back through "53" and "5").
            if (ImGui::IsItemDeactivatedAfterEdit() && parsed.valid) history.record(siteswapText);
            if (patternValid)
                ImGui::TextDisabled("%d objects, period %d", ballCount(pattern),
                                    loopPeriodBeats(pattern));
            else if (!parsed.error.empty())
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(errorTextColor(settings.colorVision)),
                                   "%s", parsed.error.c_str());
            else
                ImGui::TextUnformatted("");
        }
        ImGui::End();

        if (showColorPreview) drawColorVisionPreview(&showColorPreview, settings.colorVision);
        if (showImGuiDemo) ImGui::ShowDemoWindow(&showImGuiDemo);

        ImGui::Render();

        // --- Draw ---
        glViewport(0, 0, g_width, g_height);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Right half, below the menu bar. GL's origin is bottom-left.
        const int viewX = static_cast<int>(leftWidth);
        const GLRect jugglerRect{viewX, 0, g_width - viewX, g_height - static_cast<int>(top)};
        renderJugglerView(renderer, prims, pose, jugglerRect);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ::SwapBuffers(g_hdc);
    }

    prims.destroy();
    renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyGLContext(hwnd);
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
