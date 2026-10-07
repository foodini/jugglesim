// app.h - JuggleSim itself: the panes, editing, playback and drawing, the same on every
// platform. See platform.h for what each platform provides and how it starts the app.
#pragma once

// Runs the app until the user quits. Call with the window's OpenGL context current, the ImGui
// context created and ImGui's platform backend initialized. Returns the process exit code.
int runApp();
