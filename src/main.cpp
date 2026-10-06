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
#include "juggle_sim.h"
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

#include <algorithm>
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

// ---- Camera -------------------------------------------------------------------------------
//
// By default the camera frames the pattern: it looks at the juggler from the front (they face
// +Z) with the bottom of the view just below the lowest point the hands reach and the top just
// above the highest point any ball reaches. The user can orbit (drag) and zoom (wheel); those
// are kept as offsets from the automatic framing, so changing patterns re-frames from wherever
// the user is looking. Home (over the juggler) or View > Reset Camera clears them.

constexpr float kCameraFovY = 35.0f * kPi / 180.0f;
constexpr float kHandPlaneZ = 0.36f;  // the hands and most flights are in this plane

struct CameraControl {
    // User offsets.
    float yaw = 0.0f;    // radians; positive swings the camera toward the juggler's left (+X)
    float pitch = 0.0f;  // radians; positive looks down from above
    float zoom = 1.0f;   // distance multiplier; < 1 is closer
    // Automatic framing, eased toward the pattern's extents so changes glide instead of jumping.
    bool framed = false;
    float centerY = 1.4f;
    float distance = 3.0f;
};

struct CameraView {
    Vec3 position;
    Vec3 target;
    float farPlane;
};

void resetCamera(CameraControl& c) {
    c.yaw = 0.0f;
    c.pitch = 0.0f;
    c.zoom = 1.0f;
}

// Advances the automatic framing toward `extents` and returns the camera to render with.
CameraView updateCamera(CameraControl& c, const SceneExtents& extents, float aspect, float dt) {
    const float bottom = extents.lowestHandY - 0.04f;
    const float top = extents.highestPropY + 0.06f;
    const float tanHalf = std::tan(kCameraFovY * 0.5f);
    const float fitHeight = 0.5f * (top - bottom) / tanHalf;
    const float fitWidth = (extents.halfWidth + 0.05f) / (tanHalf * aspect);
    const float wantDistance = std::max(fitHeight, fitWidth);
    const float wantCenterY = 0.5f * (bottom + top);
    if (!c.framed) {
        c.centerY = wantCenterY;
        c.distance = wantDistance;
        c.framed = true;
    } else {
        const float ease = 1.0f - std::exp(-dt * 5.0f);
        c.centerY += (wantCenterY - c.centerY) * ease;
        c.distance += (wantDistance - c.distance) * ease;
    }
    CameraView v;
    v.target = Vec3(0.0f, c.centerY, kHandPlaneZ);
    const float d = c.distance * c.zoom;
    const Vec3 dir(std::sin(c.yaw) * std::cos(c.pitch), std::sin(c.pitch),
                   std::cos(c.yaw) * std::cos(c.pitch));
    v.position = v.target + dir * d;
    v.farPlane = d * 3.0f + 50.0f;
    return v;
}

Vec3 colorToVec3(ImU32 c) {
    const ImVec4 f = ImGui::ColorConvertU32ToFloat4(c);
    return Vec3(f.x, f.y, f.z);
}

void renderJugglerView(Renderer& renderer, const Primitives& prims, const JugglerScene& scene,
                       const CameraView& camera, ColorVisionMode colorVision, const GLRect& rect) {
    if (rect.w <= 0 || rect.h <= 0) return;

    glViewport(rect.x, rect.y, rect.w, rect.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.x, rect.y, rect.w, rect.h);
    glClearColor(0.16f, 0.17f, 0.20f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    // Camera in front of the juggler (who faces +Z), looking back at them.
    const float aspect = static_cast<float>(rect.w) / static_cast<float>(rect.h);
    const Mat4 proj = Mat4::perspective(kCameraFovY, aspect, 0.05f, camera.farPlane);
    const Mat4 view = Mat4::lookAt(camera.position, camera.target, {0.0f, 1.0f, 0.0f});
    const Mat4 viewProj = proj * view;

    renderer.beginScene(viewProj, {-0.4f, -1.0f, -0.6f});

    // Floor disc.
    renderer.drawMesh(prims.cylinder,
                      Mat4::translation({0.0f, -0.01f, 0.0f}) * Mat4::scaling({1.2f, 0.01f, 1.2f}),
                      {0.30f, 0.32f, 0.36f});

    JugglerPose pose = makeNeutralPose();
    poseBody(pose, scene.body);
    poseArmsForPalms(pose, scene.palmRight, scene.palmLeft, scene.body.intensity);
    drawJuggler(renderer, prims, pose, Mat4::identity(), JugglerStyle{});

    // Balls, in the same colors as the ladder.
    for (const BallState& b : scene.balls) {
        const Vec3 color = colorToVec3(ballStyle(colorVision, b.ball).color);
        renderer.drawMesh(prims.sphere,
                          Mat4::translation(b.center) *
                              Mat4::scaling({kBallRadius, kBallRadius, kBallRadius}),
                          color);
    }
    renderer.endScene();

    // Trails: a wide faint glow plus a narrow bright core, both fading toward the old end and
    // dashed like the ball's lines on the ladder (so balls stay distinguishable without color).
    std::vector<GlowVertex> glow;
    for (const Trail& t : scene.trails) {
        const BallStyle style = ballStyle(colorVision, t.ball);
        const Vec3 color = colorToVec3(style.color);
        const float* dash = nullptr;
        int dashCount = 0;
        dashPatternLengths(style.dash, &dash, &dashCount);
        const float dashUnit = 0.03f;  // meters per dash unit
        appendRibbon(glow, t.points, t.fade, color, 0.22f, 0.05f, camera.position, dash, dashCount, dashUnit);
        appendRibbon(glow, t.points, t.fade, color * 0.6f + Vec3(0.4f, 0.4f, 0.4f), 0.85f, 0.014f,
                     camera.position, dash, dashCount, dashUnit);
    }
    renderer.drawGlow(viewProj, glow);
}

// The transport icons, drawn into a button's rectangle.
enum class TransportIcon { StepBack, Play, Pause, StepForward };

void drawTransportIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, TransportIcon icon, ImU32 color) {
    const float h = max.y - min.y;
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float s = h * 0.28f;  // half-size of the icon
    auto triangleRight = [&](float cx) {
        dl->AddTriangleFilled(ImVec2(cx - s * 0.7f, c.y - s), ImVec2(cx + s * 0.8f, c.y),
                              ImVec2(cx - s * 0.7f, c.y + s), color);
    };
    auto triangleLeft = [&](float cx) {
        dl->AddTriangleFilled(ImVec2(cx + s * 0.7f, c.y - s), ImVec2(cx - s * 0.8f, c.y),
                              ImVec2(cx + s * 0.7f, c.y + s), color);
    };
    auto bar = [&](float cx) {
        dl->AddRectFilled(ImVec2(cx - s * 0.18f, c.y - s), ImVec2(cx + s * 0.18f, c.y + s), color);
    };
    switch (icon) {
        case TransportIcon::StepBack: bar(c.x - s * 0.75f); triangleLeft(c.x + s * 0.35f); break;
        case TransportIcon::Play: triangleRight(c.x + s * 0.1f); break;
        case TransportIcon::Pause: bar(c.x - s * 0.4f); bar(c.x + s * 0.4f); break;
        case TransportIcon::StepForward: triangleRight(c.x - s * 0.35f); bar(c.x + s * 0.75f); break;
    }
}

bool transportButton(const char* id, TransportIcon icon, float size) {
    const bool pressed = ImGui::Button(id, ImVec2(size * 1.6f, size));
    drawTransportIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                      icon, ImGui::GetColorU32(ImGuiCol_Text));
    return pressed;
}

// Playback clock. beat is fractional (beat 0 = the right hand's first throw) and may be
// negative after stepping back; the pattern repeats forever in both directions.
struct Playback {
    bool playing = true;
    double beat = 0.0;
};
constexpr double kStepBeats = 1.0 / 12.0;  // one arrow-key press; Shift steps a whole beat

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
    // Keyboard navigation between widgets is left off: Space and the arrow keys drive playback.
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
    Playback playback;
    JuggleParams juggleParams;
    CameraControl camera;
    // Pattern extents depend only on the loop and timing, so they're cached.
    std::vector<int> extentsLoop;
    JuggleParams extentsParams{-1.0, -1.0};
    SceneExtents extents;
    auto stepPlayback = [&](double beats) {
        playback.playing = false;
        playback.beat += beats;
    };

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

        // --- Playback clock ---
        if (playback.playing) playback.beat += static_cast<double>(io.DeltaTime) * juggleParams.bpm / 60.0;

        // --- Keyboard shortcuts (not while typing: the text box has its own undo, etc.) ---
        if (!io.WantTextInput && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift) undo();
            else if (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift)) redo();
        }
        if (!io.WantTextInput && !io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) playback.playing = !playback.playing;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) stepPlayback(io.KeyShift ? 1.0 : kStepBeats);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) stepPlayback(io.KeyShift ? -1.0 : -kStepBeats);
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
            if (ImGui::BeginMenu("Playback")) {
                if (ImGui::MenuItem(playback.playing ? "Pause" : "Play", "Space"))
                    playback.playing = !playback.playing;
                if (ImGui::MenuItem("Step Forward", "Right")) stepPlayback(kStepBeats);
                if (ImGui::MenuItem("Step Back", "Left")) stepPlayback(-kStepBeats);
                if (ImGui::MenuItem("Step Forward One Beat", "Shift+Right")) stepPlayback(1.0);
                if (ImGui::MenuItem("Step Back One Beat", "Shift+Left")) stepPlayback(-1.0);
                ImGui::Separator();
                float bpm = static_cast<float>(juggleParams.bpm);
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
                if (ImGui::SliderFloat("Tempo (BPM)", &bpm, 40.0f, 300.0f, "%.0f"))
                    juggleParams.bpm = bpm;
                float dwell = static_cast<float>(juggleParams.dwellBeats);
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
                if (ImGui::SliderFloat("Dwell (beats)", &dwell, 0.1f, 1.9f, "%.2f"))
                    juggleParams.dwellBeats = dwell;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("How long each hand holds a ball before throwing it, in beats.\n"
                                      "Each hand throws every other beat, so this is out of 2;\n"
                                      "real cascades are often around 1.3-1.6. Short throws (like 1s)\n"
                                      "automatically get a shorter dwell so they still have some flight.");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Reset Ladder View", "Home over ladder", false, !ladderViewIsDefault(ladderEdit)))
                    resetLadderView(ladderEdit);
                if (ImGui::MenuItem("Reset Camera", "Home over juggler")) resetCamera(camera);
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
                drawLadderDiagram(pattern, patternValid, settings.colorVision, ladderEdit, playback.beat);
            if (edited.startedChain) playback.playing = false;  // don't confuse the juggler mid-edit
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

        // Mouse input for the 3D view: an invisible window over the juggler pane (the 3D view
        // itself is drawn straight to OpenGL, not by ImGui).
        const float transportHeightForInput = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        ImGui::SetNextWindowPos(ImVec2(leftWidth, top));
        ImGui::SetNextWindowSize(ImVec2(W - leftWidth, H - top - transportHeightForInput));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("Juggler view input", nullptr,
                         paneFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 1.0f && avail.y > 1.0f) {
                ImGui::InvisibleButton("##juggler_view", avail);
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                    const ImVec2 d = io.MouseDelta;
                    camera.yaw -= d.x * 0.008f;
                    camera.pitch = std::clamp(camera.pitch + d.y * 0.008f, -1.45f, 1.45f);
                }
                if (ImGui::IsItemHovered()) {
                    if (io.MouseWheel != 0.0f)
                        camera.zoom = std::clamp(camera.zoom * std::pow(0.88f, io.MouseWheel), 0.15f, 6.0f);
                    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Home)) resetCamera(camera);
                    // A hint, only after the mouse has rested here a moment.
                    if (!ImGui::IsItemActive() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Drag to orbit, wheel to zoom, Home to reset");
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        // Transport bar along the bottom of the juggler pane (right half).
        const float transportHeight = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        ImGui::SetNextWindowPos(ImVec2(leftWidth, H - transportHeight));
        ImGui::SetNextWindowSize(ImVec2(W - leftWidth, transportHeight));
        if (ImGui::Begin("Transport", nullptr, paneFlags)) {
            const float h = ImGui::GetFrameHeight();
            if (transportButton("##stepback", TransportIcon::StepBack, h))
                stepPlayback(io.KeyShift ? -1.0 : -kStepBeats);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step back 1/12 beat (Left)\nShift: one beat (Shift+Left)");
            ImGui::SameLine();
            if (transportButton("##playpause", playback.playing ? TransportIcon::Pause : TransportIcon::Play, h))
                playback.playing = !playback.playing;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(playback.playing ? "Pause (Space)" : "Play (Space)");
            ImGui::SameLine();
            if (transportButton("##stepforward", TransportIcon::StepForward, h))
                stepPlayback(io.KeyShift ? 1.0 : kStepBeats);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step forward 1/12 beat (Right)\nShift: one beat (Shift+Right)");
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Beat %.2f", playback.beat + 1.0);  // beat 0 is "beat 1", as on the ladder
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
            ImGui::TextDisabled("%.0f BPM   dwell %.2f beats   (Playback menu to change)", juggleParams.bpm,
                                juggleParams.dwellBeats);
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
        const int transportPixels = static_cast<int>(transportHeight);
        const GLRect jugglerRect{viewX, transportPixels, g_width - viewX,
                                 g_height - static_cast<int>(top) - transportPixels};
        JugglerScene scene;
        if (patternValid) {
            const std::vector<int> loop = loopThrowValues(pattern);
            scene = evaluateScene(loop, computeBallOrbits(loop), juggleParams, playback.beat);
            if (loop != extentsLoop || juggleParams.bpm != extentsParams.bpm ||
                juggleParams.dwellBeats != extentsParams.dwellBeats) {
                extents = computeSceneExtents(loop, juggleParams);
                extentsLoop = loop;
                extentsParams = juggleParams;
            }
        }
        const float jugglerAspect =
            jugglerRect.h > 0 ? static_cast<float>(jugglerRect.w) / static_cast<float>(jugglerRect.h) : 1.0f;
        const CameraView cameraView = updateCamera(camera, extents, jugglerAspect, io.DeltaTime);
        renderJugglerView(renderer, prims, scene, cameraView, settings.colorVision, jugglerRect);

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
