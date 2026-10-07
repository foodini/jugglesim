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

## Layout

```
jugglesim.sln / jugglesim.vcxproj
jugglesim.rc          resources compiled into the exe (the pattern library)
build.bat             command-line build (see above)
src/
  main.cpp            Win32 window, WGL context, ImGui setup, pane layout, main loop
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
  color_vision.*      color-vision modes; per-ball color + marker shape + dash pattern
  draw_helpers.*      markers, dashed curves, arrowheads for ImGui draw lists
  settings.*          user preferences in %APPDATA%\JuggleSim\settings.ini
data/patterns.txt     the built-in pattern library (compiled into the exe by jugglesim.rc)
docs/                 user documentation: ladder.md, juggler.md, patterns.md
docs/design/          design notes (multi_juggler.md: passing, linking, choreography)
third_party/imgui/    git submodule
```

Adding a new OpenGL 2.0+ function: add one line to `JS_GL_FUNCTIONS` in `gl_funcs.h`, then call
it as `gl::FunctionName(...)`.

## Conventions

- World units are meters, +Y is up.
- A juggler faces +Z in their local space, so the juggler's own right hand is at -X.
- Two passing jugglers face each other along X: juggler 1 at -X facing +X, juggler 2 at +X
  facing -X. Each juggler's hands and body are worked out in their own local space and placed
  with translation(position) * rotationY(yaw).
- Ladder diagram: top rail = right hand, bottom rail = left hand, beat 1 thrown by the right.
