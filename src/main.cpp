// main.cpp - JuggleSim on Windows: the entry point and the platform functions (platform.h),
// using a Win32 window, a WGL OpenGL context and ImGui's Win32 backend. Everything else is in
// app.cpp, shared with the other platforms.

#include "app.h"
#include "gl_funcs.h"
#include "platform.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Declared (commented out) in imgui_impl_win32.h; must be forward-declared by the app.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

HWND g_hwnd = nullptr;
bool g_closeRequested = false;  // the close box was clicked (see platformTakeCloseRequest)

HDC g_hdc = nullptr;
HGLRC g_hglrc = nullptr;
int g_width = 1600;
int g_height = 900;

// The 3D mouse (SpaceMouse): the latest reports, read through Raw Input (WM_INPUT) while the
// window is in front. Its axes say how far the cap is pushed (not how far it moved), from about
// -350 to 350, and drop to 0 when it's let go. Report 1 is the move (x y z; on some newer
// models all six axes), 2 the turn, 3 the buttons.
struct SpaceMouseRaw {
    bool seen = false;
    int16_t move[3] = {0, 0, 0};
    int16_t turn[3] = {0, 0, 0};
    unsigned buttons = 0;
    DWORD lastReport = 0;  // GetTickCount() of the last axis report
};
SpaceMouseRaw g_spaceMouse;

int16_t reportAxis(const BYTE* p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

void readSpaceMouse(LPARAM lParam) {
    UINT size = 0;
    ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) return;
    std::vector<BYTE> buffer(size);
    if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size)
        return;
    const RAWINPUT* input = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (input->header.dwType != RIM_TYPEHID) return;
    const RAWHID& hid = input->data.hid;
    for (DWORD r = 0; r < hid.dwCount; ++r) {
        const BYTE* report = hid.bRawData + r * hid.dwSizeHid;
        const DWORD length = hid.dwSizeHid;
        if (length >= 7 && (report[0] == 1 || report[0] == 2)) {
            int16_t* axes = report[0] == 1 ? g_spaceMouse.move : g_spaceMouse.turn;
            for (int i = 0; i < 3; ++i) axes[i] = reportAxis(report + 1 + 2 * i);
            if (report[0] == 1 && length >= 13)  // all six axes in one report
                for (int i = 0; i < 3; ++i) g_spaceMouse.turn[i] = reportAxis(report + 7 + 2 * i);
            g_spaceMouse.lastReport = ::GetTickCount();
            g_spaceMouse.seen = true;
        } else if (length >= 2 && report[0] == 3) {
            g_spaceMouse.buttons = report[1];
            g_spaceMouse.seen = true;
        }
    }
}

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
        case WM_INPUT:
            readSpaceMouse(lParam);
            break;  // (DefWindowProc cleans up after it)
        case WM_CLOSE:
            g_closeRequested = true;  // the app decides (it may ask about unsaved work first)
            return 0;
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


}  // namespace

// ---- platform.h

bool platformPollEvents() {
    bool quit = false;
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) quit = true;
    }
    return !quit;
}

SpaceMouseState platformSpaceMouse() {
    SpaceMouseState s;
    s.present = g_spaceMouse.seen;
    // Reports keep coming while the cap is pushed; if they stop (the window lost the input),
    // treat it as let go.
    const bool fresh = ::GetTickCount() - g_spaceMouse.lastReport < 500;
    for (int i = 0; i < 3; ++i) {
        // The device's z points down and y toward you: flip them so z is up and y away.
        const float sign = i == 0 ? 1.0f : -1.0f;
        s.move[i] = fresh ? sign * std::clamp(g_spaceMouse.move[i] / 350.0f, -1.0f, 1.0f) : 0.0f;
        s.turn[i] = fresh ? sign * std::clamp(g_spaceMouse.turn[i] / 350.0f, -1.0f, 1.0f) : 0.0f;
    }
    s.buttons = g_spaceMouse.buttons;
    return s;
}

bool platformTakeCloseRequest() {
    const bool requested = g_closeRequested;
    g_closeRequested = false;
    return requested;
}

void platformSetWindowTitle(const char* title) {
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    if (length <= 0) return;
    std::vector<wchar_t> wide(static_cast<size_t>(length));
    ::MultiByteToWideChar(CP_UTF8, 0, title, -1, wide.data(), length);
    ::SetWindowTextW(g_hwnd, wide.data());
}

bool platformIsMinimized() { return ::IsIconic(g_hwnd) != FALSE; }

void platformSleepMilliseconds(int milliseconds) { ::Sleep(static_cast<DWORD>(milliseconds)); }

void platformNewFrame() { ImGui_ImplWin32_NewFrame(); }

void platformFramebufferSize(int* width, int* height) {
    *width = g_width;
    *height = g_height;
}

void platformSwapBuffers() { ::SwapBuffers(g_hdc); }

void platformShowError(const char* title, const char* message) {
    ::MessageBoxA(nullptr, message, title, MB_ICONERROR);
}

// The built-in pattern library (data/patterns.txt), compiled into the executable as a
// resource. Empty if it's missing.
std::string platformBuiltInPatternText() {
    HRSRC resource = ::FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PATTERN_LIBRARY), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!resource) return std::string();
    HGLOBAL loaded = ::LoadResource(nullptr, resource);
    const DWORD size = ::SizeofResource(nullptr, resource);
    const void* data = loaded ? ::LockResource(loaded) : nullptr;
    if (!data || size == 0) return std::string();
    return std::string(static_cast<const char*>(data), static_cast<size_t>(size));
}

void* platformGetProcAddress(const char*) { return nullptr; }  // (gl::load uses WGL directly)

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

    g_hwnd = ::CreateWindowW(wc.lpszClassName, L"JuggleSim", WS_OVERLAPPEDWINDOW, 100, 100, g_width, g_height,
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd || !createGLContext(g_hwnd)) {
        ::MessageBoxA(nullptr, "Failed to create an OpenGL context.", "JuggleSim", MB_ICONERROR);
        return 1;
    }
    enableVSync();

    // A 3D mouse (multi-axis controller: generic desktop page, usage 8), while we're in front.
    RAWINPUTDEVICE spaceMouse = {};
    spaceMouse.usUsagePage = 1;
    spaceMouse.usUsage = 8;
    spaceMouse.dwFlags = 0;
    spaceMouse.hwndTarget = g_hwnd;
    ::RegisterRawInputDevices(&spaceMouse, 1, sizeof(spaceMouse));  // (nothing to do if it fails)

    ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplWin32_InitForOpenGL(g_hwnd);

    const int exitCode = runApp();

    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyGLContext(g_hwnd);
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return exitCode;
}
