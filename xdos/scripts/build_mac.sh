#!/usr/bin/env bash
# build_mac.sh — configure and build XDOS natively on macOS, into build-mac/.
#
# Works whether or not the Xcode licence has been accepted.  Until it is,
# /usr/bin/{c++,make,git} are shims that refuse to run, so this points CMake
# straight at the real Command Line Tools compiler, make and SDK instead.
# CMake and Catch2 (needed for the test target) are bootstrapped once into
# ~/.xdos-toolchain when they are not already available.
#
# Usage:
#   scripts/build_mac.sh            # Release build
#   scripts/build_mac.sh --clean    # wipe build-mac/ first
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "$(uname -s)" != Darwin ]]; then
    echo "build_mac.sh is for macOS — on Linux use:  cmake -B build && cmake --build build -j" >&2
    exit 1
fi

BUILD=build-mac
TC="$HOME/.xdos-toolchain"
CLT=/Library/Developer/CommandLineTools
CATCH2_TAG=v3.7.1

[[ "${1:-}" == --clean ]] && rm -rf "$BUILD"

# ── toolchain: the /usr/bin shims, or the real binaries behind them ─────────
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release)
if /usr/bin/make --version >/dev/null 2>&1; then
    GIT=git
else
    if [[ ! -x "$CLT/usr/bin/clang++" ]]; then
        echo "Command Line Tools not found — install them with:  xcode-select --install" >&2
        exit 1
    fi
    echo "==> Xcode licence not accepted — using $CLT directly"
    echo "    (run 'sudo xcodebuild -license accept' once and this detour goes away)"
    GIT="$CLT/usr/bin/git"
    CMAKE_ARGS+=(
        -DCMAKE_CXX_COMPILER="$CLT/usr/bin/clang++"
        -DCMAKE_MAKE_PROGRAM="$CLT/usr/bin/make"
        -DCMAKE_OSX_SYSROOT="$CLT/SDKs/MacOSX.sdk"
    )
fi

# ── cmake ────────────────────────────────────────────────────────────────────
if command -v cmake >/dev/null 2>&1 && cmake --version >/dev/null 2>&1; then
    CMAKE=cmake
else
    CMAKE="$TC/venv/bin/cmake"
    if [[ ! -x "$CMAKE" ]]; then
        # /usr/bin/python3 is itself a licence-blocked shim; prefer Homebrew's.
        PY=""
        for cand in /opt/homebrew/bin/python3 /usr/local/bin/python3 python3; do
            if "$cand" -c 'import venv' >/dev/null 2>&1; then PY="$cand"; break; fi
        done
        [[ -n "$PY" ]] || { echo "no working python3 to bootstrap cmake — install cmake manually" >&2; exit 1; }
        echo "==> bootstrapping cmake into $TC/venv"
        mkdir -p "$TC"
        "$PY" -m venv "$TC/venv"
        "$TC/venv/bin/pip" install -q cmake
    fi
fi

# ── Catch2 v3, for the xdos_tests target ─────────────────────────────────────
# Prefer a system install; only bootstrap when there is none, and only point
# CMake at the bootstrapped copy in that case - passing a prefix path to a
# directory CMake does not need produces an unused-variable warning on every run.
SYSTEM_CATCH2=""
for d in /opt/homebrew /usr/local /usr; do
    if [[ -f "$d/lib/cmake/Catch2/Catch2Config.cmake" ]]; then SYSTEM_CATCH2="$d"; break; fi
done

if [[ -n "$SYSTEM_CATCH2" ]]; then
    :   # nothing to do; find_package(Catch2) will locate it
elif [[ ! -f "$TC/catch2/lib/cmake/Catch2/Catch2Config.cmake" ]]; then
    echo "==> bootstrapping Catch2 $CATCH2_TAG into $TC/catch2"
    rm -rf "$TC/Catch2-src" "$TC/Catch2-build"
    "$GIT" -c advice.detachedHead=false clone -q --depth 1 --branch "$CATCH2_TAG" \
        https://github.com/catchorg/Catch2.git "$TC/Catch2-src"
    "$CMAKE" -S "$TC/Catch2-src" -B "$TC/Catch2-build" "${CMAKE_ARGS[@]}" \
        -DCMAKE_INSTALL_PREFIX="$TC/catch2" -DBUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF >/dev/null
    "$CMAKE" --build "$TC/Catch2-build" -j "$(sysctl -n hw.ncpu)" >/dev/null
    "$CMAKE" --install "$TC/Catch2-build" >/dev/null
fi
if [[ -z "$SYSTEM_CATCH2" ]]; then
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$TC/catch2")
fi

# ── configure + build ────────────────────────────────────────────────────────
echo "==> configuring $BUILD/"
"$CMAKE" -S . -B "$BUILD" "${CMAKE_ARGS[@]}" | grep -E "tests target|Build files" || true
echo "==> building"
"$CMAKE" --build "$BUILD" -j "$(sysctl -n hw.ncpu)"
echo
echo "built into $BUILD/  —  run the stack with:  scripts/run_stack.sh --power-on"
