# Building XDOS Ground Station on Windows 11

The project builds natively on Windows with either **MSYS2/MinGW-w64** (recommended — closest to your Ubuntu toolchain) or **Visual Studio 2022 + MSVC**.

After building you get six `.exe` files: `xdos_gs.exe`, `xdos_obc.exe`, `xdos_eps_sim.exe`, `xdos_adcs_sim.exe`, `xdos_gps_sim.exe`, `xdos_payload_sim.exe`.

The same source tree builds on Linux (gcc/clang) and Windows from the same CMakeLists.

---

## Option A — MSYS2 / MinGW-w64 (recommended)

### One-time setup

Download MSYS2 installer from `https://www.msys2.org/` and install to the default location (`C:\msys64`). Open the **"MSYS2 MINGW64"** shortcut from the Start menu (not the plain MSYS2 shell — it matters which one).

In the MINGW64 terminal:

```bash
pacman -Syu                # update package DB (close terminal and re-open if it asks)
pacman -S --needed \
    mingw-w64-x86_64-toolchain \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-catch \
    git
```

This installs gcc 13/14, cmake, ninja, and Catch2 v3 — same versions your Ubuntu uses.

### Build

```bash
cd /d/Projects/XDOS-GSAPP/jai/xdos-cpp/xdos-cpp     # adjust to your path
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

You should see `build/xdos_gs.exe`, `build/xdos_obc.exe`, plus four sim `.exe`s. Build time on a typical laptop: 1–2 minutes.

### Run the tests

```bash
./build/xdos_tests.exe
# Expect: All tests passed (445 assertions in 57 test cases)
```

### Run the full stack

Open three MINGW64 terminals (or use Windows Terminal tabs):

```bash
# T1
./build/xdos_eps_sim.exe

# T2
./build/xdos_obc.exe

# T3
./build/xdos_gs.exe
```

You'll see the EPS sim's startup banner with the 16-channel map, the OBC bridge, and the GS CLI with subsystem menu.

---

## Option B — Visual Studio 2022 + MSVC

### One-time setup

Install **Visual Studio 2022 Community** (free) from `https://visualstudio.microsoft.com/`. During install, check the workload **"Desktop development with C++"** — this gives you MSVC, CMake, Ninja, and the Windows SDK.

For Catch2, the easiest path is **vcpkg**:

```powershell
# In PowerShell, from any directory where you want vcpkg checked out
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install catch2:x64-windows
```

### Build

Open the **"x64 Native Tools Command Prompt for VS 2022"** from Start menu, then:

```powershell
cd D:\Projects\XDOS-GSAPP\jai\xdos-cpp\xdos-cpp
rmdir /s /q build 2>nul
cmake -B build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build -j
```

The `.exe` files land in `build\` exactly as with MinGW.

### Run

```powershell
.\build\xdos_tests.exe
.\build\xdos_eps_sim.exe
```

---

## Firewall prompt on first run

The first time you run `xdos_gs.exe`, `xdos_obc.exe`, or any sim, Windows Defender Firewall will pop up asking permission to open a TCP port (10000, 10010, 10020, 10030, 10040). **Click "Allow access" — Private networks only** (don't allow on public networks).

You only need to do this once per binary.

---

## Editing from VS Code

Install **VS Code** plus the **C/C++ extension** (Microsoft) and **CMake Tools** (Microsoft) extensions. Open the folder, point CMake Tools at your kit (MinGW or MSVC), hit F7 to build, Ctrl+F5 to run.

If you're using WSL2 instead (recommended for development — see the conversation), use the **WSL** extension and edit the Linux copy directly from VS Code on Windows.

---

## Notes on differences vs the Linux build

The Windows binaries behave identically to Linux — same wire formats, same CCSDS framing, same EPS protocol, same MIB. The only differences:

- **Process management**: Use `Ctrl+C` to stop a sim or the OBC the same as Linux. Windows handles `SIGINT` for console apps.
- **Color output & UTF-8**: The binaries automatically switch the console to UTF-8 (code page 65001) and enable ANSI escape processing at startup, so box-drawing characters in banners and ANSI colors render correctly in Windows Terminal, PowerShell, MINGW64 shell, and modern ConHost. If you see escape gibberish or boxes rendered as `ΓòöΓòÉ...`, your binary is old — rebuild from current sources or pass `--no-color` (and accept that boxes will still show as multi-char garbage in non-UTF-8 consoles).
- **TCP**: Same `127.0.0.1:10000`–`10040` ports. Windows Firewall asks permission once per binary.
- **MIB JSON files**: The binary needs to find the `mib/` folder next to itself or in the parent directory. If you copy the `.exe`s to a different folder, copy `mib/` along with them.

---

## Troubleshooting

**"WSAStartup failed"** — only happens if `ws2_32.dll` isn't loadable, which essentially never happens on a normal Windows install. If it does, reinstall the C++ runtime.

**"bind 10010 failed: Only one usage of each socket address..."** — another process is listening on that port. Either kill it (`netstat -ano | findstr 10010` then `taskkill /pid <PID> /f`) or pass `--port 10011` to the sim and a matching `--eps-port 10011` to the OBC.

**Catch2 not found at configure time** — for MinGW, check `pacman -Qs catch`. For MSVC, check the `vcpkg.cmake` toolchain file path is correct.

**"undefined reference to `__imp_WSA…`"** — `ws2_32` linkage missing. Should never happen with the provided CMakeLists (we add `link_libraries(ws2_32)` on `WIN32`).

---

## Cross-compiling from Linux to Windows (for reference)

If you have a Linux box and want to produce Windows `.exe` files there:

```bash
sudo apt install g++-mingw-w64-x86-64 mingw-w64-tools
sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix

cmake -B build-win \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-win -j
```

The `cmake/mingw-w64-x86_64.cmake` toolchain file is in the source tree. This is what I used to verify the Windows port builds cleanly.
