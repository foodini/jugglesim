# mac.mk - builds JuggleSim on macOS (and, given the X11 development packages, Linux). Usually
# run through build_mac.sh; or directly, with make -f mac.mk. Windows uses jugglesim.sln /
# build.bat instead.
#
# Needs a C++17 compiler and make: on macOS, Apple's Command Line Tools
# (xcode-select --install). The window library, GLFW, is built from third_party/glfw (a git
# submodule, like ImGui), so nothing else has to be installed.
#
#   make -f mac.mk              build for this Mac's processor: build/<config>/<arch>/jugglesim
#   make -f mac.mk app          ...and wrap it as bin/mac/JuggleSim.app
#   make -f mac.mk universal    JuggleSim.app for both Apple Silicon and Intel Macs (to hand out)
#   make -f mac.mk run          build and start it
#   make -f mac.mk clean
#
# CONFIG=Debug for an unoptimized build with debug info (default: Release).

CONFIG ?= Release
UNAME := $(shell uname -s)
ARCH ?= $(shell uname -m)

IMGUI := third_party/imgui
GLFW := third_party/glfw
BUILD := build/$(CONFIG)/$(ARCH)
GENERATED := build/generated

ifeq ($(CONFIG),Debug)
  OPT := -O0 -g
else
  OPT := -O2 -DNDEBUG
endif

# ---- Sources

# Everything in src/ except the Windows entry point (main.cpp); main_glfw.cpp is this one.
APP_SRCS := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
IMGUI_SRCS := $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp \
              $(IMGUI)/imgui_widgets.cpp $(IMGUI)/imgui_demo.cpp \
              $(IMGUI)/backends/imgui_impl_glfw.cpp $(IMGUI)/backends/imgui_impl_opengl3.cpp
# GLFW's own source lists (from its src/CMakeLists.txt), per platform.
GLFW_COMMON := context.c init.c input.c monitor.c platform.c vulkan.c window.c egl_context.c \
               osmesa_context.c null_init.c null_monitor.c null_window.c null_joystick.c

ifeq ($(UNAME),Darwin)
  GLFW_PLATFORM := cocoa_time.c posix_module.c posix_thread.c cocoa_init.m cocoa_joystick.m \
                   cocoa_monitor.m cocoa_window.m nsgl_context.m
  GLFW_DEFS := -D_GLFW_COCOA
  # Runs on macOS 11 (Big Sur) and later.
  PLATFORM_FLAGS := -arch $(ARCH) -mmacosx-version-min=11.0
  LDLIBS := -framework Cocoa -framework IOKit -framework CoreFoundation -framework OpenGL
else
  GLFW_PLATFORM := posix_time.c posix_module.c posix_thread.c posix_poll.c x11_init.c \
                   x11_monitor.c x11_window.c xkb_unicode.c glx_context.c linux_joystick.c
  GLFW_DEFS := -D_GLFW_X11
  PLATFORM_FLAGS :=
  LDLIBS := -lGL -ldl -lpthread -lm
endif
GLFW_SRCS := $(addprefix $(GLFW)/src/,$(GLFW_COMMON) $(GLFW_PLATFORM))

# The built-in pattern library, compiled in from data/patterns.txt.
PATTERNS_SRC := $(GENERATED)/builtin_patterns.cpp

OBJS := $(patsubst %,$(BUILD)/obj/%.o,$(APP_SRCS) $(IMGUI_SRCS) $(GLFW_SRCS) $(PATTERNS_SRC))
DEPS := $(OBJS:.o=.d)
BIN := $(BUILD)/jugglesim

INCLUDES := -Isrc -I$(IMGUI) -I$(IMGUI)/backends -I$(GLFW)/include
# (GLFW_INCLUDE_NONE: nothing here wants GLFW to include the old OpenGL header for it.)
CXXFLAGS += -std=c++17 $(OPT) $(PLATFORM_FLAGS) $(INCLUDES) -DGLFW_INCLUDE_NONE -MMD -MP
CFLAGS += $(OPT) $(PLATFORM_FLAGS) $(GLFW_DEFS) -I$(GLFW)/include -MMD -MP
LDFLAGS += $(PLATFORM_FLAGS)

APP := bin/mac/JuggleSim.app

.PHONY: all app universal run clean check-submodules
all: check-submodules $(BIN)

check-submodules:
	@test -f $(IMGUI)/imgui.h || { echo "third_party/imgui is missing: run 'git submodule update --init'"; exit 1; }
	@test -f $(GLFW)/include/GLFW/glfw3.h || { echo "third_party/glfw is missing: run 'git submodule update --init'"; exit 1; }

$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/obj/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.m.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(PATTERNS_SRC): data/patterns.txt
	@mkdir -p $(dir $@)
	@printf '// Generated from data/patterns.txt by mac.mk. Do not edit.\n' > $@
	@printf 'extern const char kBuiltInPatternText[];\nconst char kBuiltInPatternText[] = R"JSPATTERNS(' >> $@
	@cat $< >> $@
	@printf ')JSPATTERNS";\n' >> $@

# ---- macOS app bundle

APP_PLIST := $(APP)/Contents/Info.plist
define INFO_PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>JuggleSim</string>
  <key>CFBundleDisplayName</key><string>JuggleSim</string>
  <key>CFBundleIdentifier</key><string>com.jugglesim.JuggleSim</string>
  <key>CFBundleExecutable</key><string>JuggleSim</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
endef
export INFO_PLIST

# The app for this Mac's processor.
app: all
	@mkdir -p $(APP)/Contents/MacOS
	cp $(BIN) $(APP)/Contents/MacOS/JuggleSim
	@echo "$$INFO_PLIST" > $(APP_PLIST)
	codesign --force --sign - $(APP)
	@echo "Built $(APP)"

# The app for both Apple Silicon and Intel Macs: each built separately, then joined.
universal: check-submodules
	$(MAKE) -f mac.mk CONFIG=$(CONFIG) ARCH=arm64 all
	$(MAKE) -f mac.mk CONFIG=$(CONFIG) ARCH=x86_64 all
	@mkdir -p $(APP)/Contents/MacOS
	lipo -create -output $(APP)/Contents/MacOS/JuggleSim \
	    build/$(CONFIG)/arm64/jugglesim build/$(CONFIG)/x86_64/jugglesim
	@echo "$$INFO_PLIST" > $(APP_PLIST)
	codesign --force --sign - $(APP)
	@echo "Built $(APP) (Apple Silicon and Intel)"

run: all
	./$(BIN)

clean:
	rm -rf build bin/mac

-include $(DEPS)
