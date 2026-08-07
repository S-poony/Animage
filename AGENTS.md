# AI Agent UI Iteration Guide

This guide describes how AI coding agents can rapidly iterate on Animage's QML user interface by capturing and inspecting screenshots in headless/offscreen mode without opening desktop windows or blocking the session.

---

## 1. UI Architecture Overview

- **QML Source Files**: `src/app/animage/qml/` (`Main.qml`, `Theme.qml`, `LayerPanel.qml`, `TimelinePanel.qml`, `AppToolButton.qml`, etc.)
- **C++ Backend Facade**: `AppController` (`src/app/animage/app_controller.h`) & `CanvasView` (`src/app/animage/canvas_view.h`)
- **Offscreen Harness**: `animage_qml_harness` (`src/app/animage/harness.cpp`)

---

## 2. Fast Headless Screenshot Workflow

The offscreen harness compiles the full QML user interface, renders a frame offscreen, saves it to a PNG file, and exits immediately.

### Build the Harness
```bash
cmake --build build --target animage_qml_harness
```

### Capture a Screenshot (Cross-Platform / Platform Agnostic)

Using CMake's environment launcher works identically across **Linux**, **macOS**, and **Windows**:

```bash
# Works on Linux, macOS, and Windows (CMD / PowerShell / Bash)
cmake -E env QT_QPA_PLATFORM=offscreen ./build/src/app/animage_qml_harness /tmp/shot.png
```

On Windows, the executable path is `.\build\src\app\animage_qml_harness.exe`:
```powershell
cmake -E env QT_QPA_PLATFORM=offscreen .\build\src\app\animage_qml_harness.exe shot.png
```

A second argument grabs the window at its **minimum size** instead of the
designed size — this is where overlapping panels and overflowing inspectors
show up, and it should be part of every UI iteration:

```bash
cmake -E env QT_QPA_PLATFORM=offscreen ./build/src/app/animage_qml_harness /tmp/shot-min.png min
```

The harness exits non-zero on any QML warning, so a clean run is the "the
whole window loads" smoke test.

---

## 3. Iteration Cycle for Agents

1. **Edit QML**: Modify QML files in `src/app/animage/qml/`.
2. **Rebuild & Capture**:
   ```bash
   cmake --build build --target animage_qml_harness && cmake -E env QT_QPA_PLATFORM=offscreen ./build/src/app/animage_qml_harness /tmp/shot.png
   ```
3. **Inspect Screenshot**: View `/tmp/shot.png` using file inspection or vision capabilities.
4. **Verify Tests**: Ensure test suites stay green:
   ```bash
   ctest --test-dir build --output-on-failure
   ```

---

## 4. Running the Main Application Binary

- **Executable location**: `build/bin/animage` (or `build/bin/animage.exe` on Windows)
- **Build command**:
  ```bash
  cmake --build build --target animage
  ```
