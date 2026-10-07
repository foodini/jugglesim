// main.cpp - JuggleSim on Windows: the entry point and the platform functions (platform.h),
// using a Win32 window, a WGL OpenGL context and ImGui's Win32 backend. Everything else is in
// app.cpp, shared with the other platforms.

#include "app.h"
#include "gl_funcs.h"
#include "platform.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <string>

// Declared (commented out) in imgui_impl_win32.h; must be forward-declared by the app.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

HWND g_hwnd = nullptr;

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
