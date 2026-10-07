# JuggleSim

3D animation of juggling patterns, specified with siteswap notation and (eventually) an
interactive ladder diagram. C++17, OpenGL, Dear ImGui, Win32. Visual Studio 2026 (toolset v145).

## This is an experiment in AI software production.

I'm a software engineer and have been waiting to write this project until I finished out some
other personal projects in my backlog. (If you're an SWE or other creative geek, you know
EXACTLY what I mean.) Having been exposed to Claude Code recently, I have been rather impressed.
I decided to try building this project entirely with Claude, though using tech with which I am
VERY familiar. I'm all-too-frequently surprised at how similar Claude's methods mirror my own,
though it has had time to look at quite of bit of stuff I've written in the past, both on 
github and not. I am left to wonder if Claude is imitating my style, or if my style has become
so generic and bland over the years that I just code like everyone else in my field.

I know there's a lot of hatred of "AI Slop" out there. You're welcome to ignore this project.
I've never worked with AI and I feel like having it do something that I wanted to do anyway
is a valid learning experience for me as an engineer at the end of my career. Objections are
noted, but I'm going to actively resist injecting any of my own changes into this codebase.
It is not an easy commitment to make. In other contexts, when using Claude, I frequently find
myself thinking, "I want to do this," either because I think I can do it faster than Claude,
or because I find the task of writing that code to be interesting enough to keep me engaged.
It was very hard to not override Claude's right-hand coordinate system, for example. This 
should be interesting.

## First-time setup

The only external dependency is [Dear ImGui](https://github.com/ocornut/imgui), included as a
git submodule. From the repository root (`D:\ronb\projects\jugglesim`):

```
git init
git submodule add https://github.com/ocornut/imgui.git third_party/imgui
```

Optionally pin ImGui to a release tag so upgrades are deliberate:

```
cd third_party/imgui
git checkout v1.92.0      (or whatever the latest release tag is)
cd ../..
git add third_party/imgui
```

Anyone cloning the repo later uses `git clone --recurse-submodules`, or runs
`git submodule update --init` after a plain clone.

Then open `jugglesim.sln` and build `Debug|x64` or `Release|x64`, or build from the command line:

```
build.bat [Debug|Release] [Build|Rebuild|Clean]      (defaults: Debug Build)
```

`build.bat` finds MSBuild with `vswhere`, writes the full output to `build.log` and only the
errors and warnings to `build_issues.log`. Output goes to
`bin\x64\<Configuration>\jugglesim.exe`. Debug builds use the console subsystem so `printf`
output is visible; Release builds are a plain windowed app.

## Building on macOS

The Mac build uses [GLFW](https://www.glfw.org/) for its window, as a second git submodule
(Windows doesn't need it). Once, from the repository root:

```
git submodule add https://github.com/glfw/glfw.git third_party/glfw
cd third_party/glfw
git checkout 3.4
cd ../..
git add third_party/glfw .gitmodules
```

(After that, a fresh clone gets it with `git clone --recurse-submodules` or
`git submodule update --init`, as with ImGui.)

You need Apple's Command Line Tools, not the whole of Xcode: `xcode-select --install`. Then:

```
./build_mac.sh              JuggleSim.app for this Mac (bin/mac/JuggleSim.app)
./build_mac.sh run          build and start it, with its messages in the terminal
./build_mac.sh universal    one app for Apple Silicon and Intel Macs, zipped to hand out
                            (bin/mac/JuggleSim-mac.zip)
./build_mac.sh debug        unoptimized, with debug info
./build_mac.sh clean
```

`build_mac.sh` runs `make -f mac.mk` (see `mac.mk`). The app runs on macOS 11 (Big Sur) and later.

Handing the app to someone else: it isn't signed with an Apple developer ID, so the first time
they open it, macOS warns that it can't check it. Right-click (or Control-click) the app,
choose **Open**, and confirm; after that it opens normally.

On a Mac, the shortcuts written with **Ctrl** in these docs use **Cmd** instead (Cmd+Z, Cmd+wheel
to zoom the ladder, and so on). Settings and your patterns live in
`~/Library/Application Support/JuggleSim`.

## Layout

```
jugglesim.sln / jugglesim.vcxproj
jugglesim.rc          resources compiled into the exe (the pattern library)
build.bat             command-line build on Windows (see above)
mac.mk, build_mac.sh  the macOS build (see above)
src/
  app.h/.cpp          the app itself: panes, menus, editing, playback, drawing (every platform)
  platform.h          what each platform provides to the app (window, input, OpenGL, files)
  main.cpp            Windows: Win32 window, WGL context, ImGui's Win32 backend
  main_glfw.cpp       macOS: GLFW window and OpenGL 3.3 core context, ImGui's GLFW backend
  gl_funcs.h/.cpp     tiny loader for the OpenGL 3.3 functions we use (no GLAD/GLEW)
  math3d.h            Vec3 / Mat4 (no GLM)
  mesh.h/.cpp         procedural meshes: sphere, cylinder, frustum, cube, lathe, ring sectors
  renderer.h/.cpp     lit solid-color shader, plus additive "glow" ribbons for trails
  juggler_figure.*    stick-figure pose, arm and leg IK, and drawing
  prop_figure.*       ball, club and ring meshes and drawing
  juggle_sim.h/.cpp   where hands, body and props are at any moment (closed-form physics)
  timing.h            ticks (5040 per beat, int64) and the TempoMap (beats <-> seconds)
  pattern.h/.cpp      throw events + sections: the source of truth for juggling
  pattern_library.*   pattern library: file format, built-in and user patterns
  pattern_library_ui.* pattern menus (adaptive tree, Find, Recent), Save and Manage dialogs
  resource.h          resource IDs (see jugglesim.rc)
  siteswap.h/.cpp     siteswap parsing/validation (vanilla and two-person passing, for now)
  ladder_view.h/.cpp  ladder diagram + its toolbar + drag editing, drawn with ImGui
  ladder_edit.h/.cpp  the edit chain behind ladder editing (pure logic, no drawing)
  loop_ops.h/.cpp     loop operations: ? sketches, beat insert/delete, paths and orbits, drawing
  color_vision.*      color-vision modes; per-ball color + marker shape + dash pattern
  draw_helpers.*      markers, dashed curves, arrowheads for ImGui draw lists
  settings.*          user preferences in %APPDATA%\JuggleSim\settings.ini
data/patterns.txt     the built-in pattern library (compiled into the exe by jugglesim.rc)
docs/                 user documentation: ladder.md, juggler.md, patterns.md
docs/design/          design notes (multi_juggler.md: passing, linking, choreography)
third_party/imgui/    git submodule
third_party/glfw/     git submodule (macOS build only)
```

Adding a new OpenGL 2.0+ function: add one line to `JS_GL_FUNCTIONS` in `gl_funcs.h`, then call
it as `gl::FunctionName(...)`.

## Conventions

- World units are meters, +Y is up.
- A juggler faces +Z in their local space, so the juggler's own right hand is at -X.
- Two passing jugglers face each other along X: juggler 1 at -X facing +X, juggler 2 at +X
  facing -X. Each juggler's hands and body are worked out in their own local space and placed
  with translation(position) * rotationY(yaw).
- Ladder diagram: vertical, time running down; per juggler a strip with the left hand's column
  on the left and the right hand's on the right; beat 1 thrown by the right hand.
