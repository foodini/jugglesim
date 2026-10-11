// platform.h - what the app needs from the operating system.
//
// The app (app.cpp) is the same everywhere. Each platform provides these functions, plus the
// program's entry point, in its own file: main.cpp for Windows (Win32 + WGL), main_glfw.cpp
// for macOS (GLFW). Only one of them is built into a given program.
//
// The platform's entry point creates the window and its OpenGL context, creates the ImGui
// context and initializes ImGui's platform backend, then calls runApp() (app.h), which runs
// until the user quits. Afterwards the platform shuts its backend down and closes the window.
#pragma once

#include <string>

// Handles pending window-system events (input goes to ImGui's platform backend). Returns
// false when the user has closed the window, so the app should quit.
bool platformPollEvents();
// True once each time the user asks to close the window (its close box, Alt+F4, Cmd+Q): the
// window stays open until the app quits, so it can ask about unsaved work first.
bool platformTakeCloseRequest();
// Sets the window's title (UTF-8).
void platformSetWindowTitle(const char* title);

// A 3D mouse (a 3Dconnexion SpaceMouse, or any multi-axis controller), read directly (no
// vendor SDK): how far its cap is pushed along, and turned about, each of its axes, from -1 to
// 1 (0 at rest; the device's own axes: x to the right, y away from you, z up), and its buttons
// (bit 0: the left one). Not present: there's none, or this platform doesn't read them yet.
struct SpaceMouseState {
    bool present = false;
    float move[3] = {0.0f, 0.0f, 0.0f};
    float turn[3] = {0.0f, 0.0f, 0.0f};  // about x (tilt toward / away from you), y (roll), z (spin)
    unsigned buttons = 0;
};
SpaceMouseState platformSpaceMouse();

// True while the window is minimized (the app then skips drawing and sleeps a little).
bool platformIsMinimized();
void platformSleepMilliseconds(int milliseconds);

// Starts an ImGui frame for the platform backend (window size, mouse, keyboard, time).
void platformNewFrame();

// The size of the window's drawable area in pixels. On a high-DPI (Retina) display this is
// larger than ImGui's DisplaySize, which is in points.
void platformFramebufferSize(int* width, int* height);

// Shows the frame just drawn.
void platformSwapBuffers();

// Tells the user about an error that stops the app from starting.
void platformShowError(const char* title, const char* message);

// The built-in pattern library (data/patterns.txt, compiled into the program). Empty if it's
// missing.
std::string platformBuiltInPatternText();

// An OpenGL function, by name ("glCreateShader"), from the current context. Used by gl::load
// on platforms other than Windows (which has its own lookup).
void* platformGetProcAddress(const char* name);
