#!/bin/sh
# build_mac.sh - builds JuggleSim on macOS (see mac.mk for the details).
#
#   ./build_mac.sh             JuggleSim.app for this Mac's processor (bin/mac/JuggleSim.app)
#   ./build_mac.sh universal   JuggleSim.app for Apple Silicon and Intel Macs, plus a zip of it
#                              to hand out (bin/mac/JuggleSim-mac.zip)
#   ./build_mac.sh debug       unoptimized, with debug info
#   ./build_mac.sh run         build and start it (its messages appear in this terminal)
#   ./build_mac.sh clean
#
# Needs Apple's Command Line Tools: xcode-select --install
set -e
cd "$(dirname "$0")"

if ! command -v make > /dev/null 2>&1 || ! command -v c++ > /dev/null 2>&1; then
    echo "The compiler isn't installed. Install Apple's Command Line Tools with:"
    echo "    xcode-select --install"
    exit 1
fi
if [ -d .git ] && { [ ! -f third_party/imgui/imgui.h ] || [ ! -f third_party/glfw/include/GLFW/glfw3.h ]; }; then
    git submodule update --init
fi

JOBS=$(sysctl -n hw.ncpu 2> /dev/null || echo 4)
case "${1:-release}" in
    release)   make -f mac.mk -j"$JOBS" app ;;
    debug)     make -f mac.mk -j"$JOBS" CONFIG=Debug app ;;
    universal)
        make -f mac.mk -j"$JOBS" universal
        rm -f bin/mac/JuggleSim-mac.zip
        ditto -c -k --keepParent bin/mac/JuggleSim.app bin/mac/JuggleSim-mac.zip
        echo "Zipped for handing out: bin/mac/JuggleSim-mac.zip"
        ;;
    run)       make -f mac.mk -j"$JOBS" app && ./bin/mac/JuggleSim.app/Contents/MacOS/JuggleSim ;;
    clean)     make -f mac.mk clean ;;
    *)
        echo "usage: ./build_mac.sh [release|debug|universal|run|clean]"
        exit 1
        ;;
esac
