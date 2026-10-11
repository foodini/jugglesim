// spacemouse_probe.cpp - logs what a 3Dconnexion SpaceMouse (or any multi-axis controller)
// sends through Windows Raw Input, to find out whether JuggleSim can read it directly (without
// 3Dconnexion's SDK), and how this model lays out its reports.
//
// Build:  tools\build_probe.bat      Run:  tools\spacemouse_probe.exe
//
// It lists the HID devices it can see, then for 30 seconds prints every report from
// multi-axis controllers (also written to spacemouse_probe.log next to the .exe). Move the cap
// in every direction (slide, push/pull, tilt, roll, spin), press the buttons, and also try it
// with another window (say JuggleSim) in front: the probe listens in the background too.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

FILE* g_log = nullptr;

void say(const char* format, ...) {
    char text[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    std::fputs(text, stdout);
    if (g_log) std::fputs(text, g_log);
}

std::string deviceName(HANDLE device) {
    UINT size = 0;
    ::GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, nullptr, &size);
    std::string name(size, '\0');
    if (size > 0 && ::GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, &name[0], &size) != static_cast<UINT>(-1))
        name.resize(std::strlen(name.c_str()));
    return name;
}

void listDevices() {
    UINT count = 0;
    ::GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    std::vector<RAWINPUTDEVICELIST> devices(count);
    if (count == 0 || ::GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1)) {
        say("Couldn't list the raw input devices.\n");
        return;
    }
    say("HID devices (VID/PID, usage page/usage):\n");
    for (const RAWINPUTDEVICELIST& d : devices) {
        if (d.dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info = {};
        info.cbSize = sizeof(info);
        UINT size = sizeof(info);
        if (::GetRawInputDeviceInfoA(d.hDevice, RIDI_DEVICEINFO, &info, &size) == static_cast<UINT>(-1)) continue;
        const unsigned vid = info.hid.dwVendorId, pid = info.hid.dwProductId;
        const bool multiAxis = info.hid.usUsagePage == 1 && info.hid.usUsage == 8;
        const bool connexion = vid == 0x256F || vid == 0x046D;  // 3Dconnexion (and older Logitech-made ones)
        say("  %04X/%04X  page %u usage %u%s%s  handle %p\n    %s\n", vid, pid, info.hid.usUsagePage,
            info.hid.usUsage, multiAxis ? "  <- multi-axis controller" : "", connexion ? "  <- 3Dconnexion?" : "",
            d.hDevice, deviceName(d.hDevice).c_str());
    }
    say("\n");
}

int16_t axis(const BYTE* p) { return static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8)); }

void logReport(HANDLE device, const BYTE* data, DWORD size) {
    // At most about 30 lines a second for each kind of report (they come much faster, and a
    // move and a rotation report often arrive together); count what's skipped.
    static DWORD lastTick[256] = {};
    static int skippedOf[256] = {};
    const BYTE id = data[0];
    const DWORD now = ::GetTickCount();
    if (now - lastTick[id] < 33 && id != 3) {
        ++skippedOf[id];
        return;
    }
    lastTick[id] = now;
    const int skipped = skippedOf[id];
    skippedOf[id] = 0;
    say("%6lu ms  dev %p  %2lu bytes:", static_cast<unsigned long>(now), device, static_cast<unsigned long>(size));
    for (DWORD i = 0; i < size; ++i) say(" %02X", data[i]);
    // Guesses at the layout: report 1 = move (x y z), or all six axes on newer models; report 2
    // = rotation (rx ry rz); report 3 = buttons.
    if (size >= 7 && (data[0] == 1 || data[0] == 2))
        say("   [%s %6d %6d %6d]", data[0] == 1 ? "T" : "R", axis(data + 1), axis(data + 3), axis(data + 5));
    if (size >= 13 && data[0] == 1)
        say(" [R %6d %6d %6d]", axis(data + 7), axis(data + 9), axis(data + 11));
    if (data[0] == 3) say("   [buttons]");
    if (skipped > 0) say("  (+%d skipped)", skipped);
    say("\n");
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT) {
        UINT size = 0;
        ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        std::vector<BYTE> buffer(size);
        if (size > 0 && ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size,
                                          sizeof(RAWINPUTHEADER)) == size) {
            const RAWINPUT* input = reinterpret_cast<const RAWINPUT*>(buffer.data());
            if (input->header.dwType == RIM_TYPEHID) {
                const RAWHID& hid = input->data.hid;
                for (DWORD r = 0; r < hid.dwCount; ++r)
                    logReport(input->header.hDevice, hid.bRawData + r * hid.dwSizeHid, hid.dwSizeHid);
            }
        }
    }
    return ::DefWindowProcA(hwnd, msg, wParam, lParam);
}

}  // namespace

int main() {
    char exePath[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string logPath(exePath);
    const size_t slash = logPath.find_last_of("\\/");
    logPath = (slash == std::string::npos ? std::string() : logPath.substr(0, slash + 1)) + "spacemouse_probe.log";
    g_log = std::fopen(logPath.c_str(), "w");

    listDevices();

    WNDCLASSA wc = {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = ::GetModuleHandleA(nullptr);
    wc.lpszClassName = "SpaceMouseProbe";
    ::RegisterClassA(&wc);
    HWND hwnd = ::CreateWindowA(wc.lpszClassName, "SpaceMouse probe", 0, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);

    // Multi-axis controllers (generic desktop page, usage 8), in the background too.
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 1;
    rid.usUsage = 8;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd;
    if (!::RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        say("RegisterRawInputDevices failed (error %lu).\n", static_cast<unsigned long>(::GetLastError()));
        return 1;
    }
    say("Listening for 30 seconds: move the cap every way, press the buttons, and try it with another\n"
        "window in front. (Nothing printed while you move it means Raw Input can't see it.)\n\n");

    const DWORD start = ::GetTickCount();
    MSG msg;
    while (::GetTickCount() - start < 30000) {
        while (::PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageA(&msg);
        }
        ::Sleep(5);
    }
    say("\nDone. The log is in %s\n", logPath.c_str());
    if (g_log) std::fclose(g_log);
    return 0;
}
