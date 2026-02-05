# Animation workshop: SVG animations on Seeed round display

This project demonstrates high-performance SVG animations using **LVGL v9**, **ThorVG**, and **LVGL_CPP** on the **Seeed Studio XIAO ESP32S3** paired with the **Round Display (240x240)**.

> [!IMPORTANT]
> **Start here**: This repository is designed as a companion to the [Animation workshop tutorial](TUTORIAL.md). The core value of this project is found within that guide, which walks you through five distinct phases of optimization.

## 🚀 Quick start

1. **Hardware**: Seeed Studio XIAO ESP32S3 + Seeed Round Display Shield.
2. **Environment**: ESP-IDF v6.1.
3. **Build and flash**:
   ```bash
   idf.py build
   idf.py -p /dev/ttyACM0 flash monitor
   ```

## 🧩 The stack: platform agnostic vs. embedded
It is important to distinguish between the generic C++ wrappers and the specific hardware drivers used in this workshop:

- **`lvgl_cpp` (library)**: A generic, platform-agnostic C++20 wrapper for LVGL. It runs on Linux, Windows, STM32, or ESP32. It provides the clean, object-oriented API (e.g., `lvgl::Display`, `lvgl::Image`).
- **`main/sys/lvgl_port.cpp` (project code)**: The specific "bridge" code that connects the generic `lvgl_cpp` library to the ESP32-S3 hardware (DMA, SPI, PSRAM).
This workshop is designed as a guided journey from a "naive" implementation (7 FPS) to a "premium" high-performance animation (30 FPS). You can switch between these optimization levels to see and feel the impact of different hardware and software strategies.

### 🛠️ How to select a phase
You can choose your optimization level in two ways:

1.  **Menuconfig (recommended)**:
    -   Run `idf.py menuconfig`.
    -   Navigate to `Animation Workshop`.
    -   Find the **Workshop Optimization Phase** setting and enter a number from **1** to **5**.
    -   Save and re-flash.

2.  **Header override**:
    -   Open `main/workshop_config.h`.
    -   The file maps Kconfig macros to technical constants. You can manually force a phase here by redefining `WORKSHOP_PHASE`.

### 📊 The phase matrix
| Phase | Bottleneck | Target FPS | Primary Learning |
| :--- | :--- | :--- | :--- |
| **Phase 1** | SPI bus and CPU | ~9 FPS | Baseline implementation. CPU wait-loops on display I/O. |
| **Phase 2** | Graphics Bus | ~15 FPS | Boosting SPI to 80MHz. Identifying the "Tiling Problem". |
| **Phase 3** | Raster Pipeline | ~9 FPS (Regression!) | Double-buffering reduces tear but tiling overhead hurts FPS. |
| **Phase 4** | Expert Tuning | ~25 FPS | Full-frame buffers in PSRAM and Xtensa SIMD Intrinsics. |
| **Phase 5** | **Native** | **~30+ FPS** | **"Large Partial" Internal SRAM Buffering**: Bypassing PSRAM latency with SIMD. |

---

## 💀 Postmortems: lessons learned

### Postmortem 1: the "ghost in the machine" (GPIO 43/44 collision)
*   **Issue**: The display would flicker, and the touch controller would return random timeouts.
*   **Cause**: GPIO 43 and 44 are the default pins for Hardware UART0. Logging bit-banged signals directly into the display controllers.
*   **Fix**: Switched to **Native USB Serial/JTAG Controller** and disabled UART console.

### Postmortem 2: I2C and watchdog (the CPU starvation)
*   **Issue**: Complex SVG rendering would cause I2C timeouts.
*   **Cause**: Vector rendering occupied 100% of the CPU, starving the I2C driver task.
*   **Fix**: Boosted CPU to 240MHz and added a mandatory **5ms delay** in the LVGL task loop.

### Postmortem 3: memory vs. performance (the buffer trade-off)
*   **Issue**: Larger display buffers (e.g., 80 lines) caused watchdog resets.
*   **Cause**: Buffers were stealing the internal RAM required by ThorVG for vertex calculations.
*   **Fix**: Settled on **20-line double buffers**.

### Postmortem 4: stack-allocated lambda crashes
*   **Issue**: Switching animals caused sporadic crashes.
*   **Cause**: C++ lambda objects were being destroyed while LVGL still held pointers to them.
*   **Fix**: Switched to raw C function pointers for animation callbacks.

### Postmortem 5: the case of the missing SIMD (14 FPS regression)
*   **Issue**: Despite enabling `CONFIG_LV_DRAW_SW_ASM_CUSTOM`, performance fell from ~30 FPS to ~14 FPS in Phase 5. Visual artifacts (corruption) appeared when forcing assembly usage.
*   **Root cause 1 (silent build failure)**: The `espressif__esp_lvgl_port` component had a hard version check (`< 9.2.0`) in `CMakeLists.txt`, causing it to silently skip compiling the S3-optimized assembly files for our LVGL 9.4 build.
*   **Root cause 2 (API mismatch)**:
    *   **Signatures**: LVGL v9.4 hook macros (e.g., `LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB888`) require 3 arguments (`dsc`, `dest_px`, `src_px`), whereas the generic assembly routines only accepted 1.
    *   **Struct layout**: The assembly code expects a specific legacy struct layout (starting with `opa`). LVGL 9.4's `lv_draw_sw_blend_fill_dsc_t` starts with `dest_buf`. Passing the pointer directly caused the assembly to interpret the destination pointer as opacity, leading to garbage reads.
    *   **Color format**: The ESP32 assembly `fill` routines only support **32-bit colors** (reading R, G, B bytes). Passing a pointer to a 16-bit `lv_color_t` caused the assembly to read adjacent stack memory (garbage) as color channels.
*   **Fix**: Implemented an **External SIMD Patch Component** (`components/lvgl_s3_simd_patch`).
    *   **Architecture**: We keep `espressif__esp_lvgl_port` as a pristine `managed_component`.
    *   **Injection**: The patch component locates the S3 assembly files within `managed_components` and compiles them externally.
    *   **Shim**: It builds a C shim (`shim.c`) to bridge the struct and color format mismatches.
    *   **Linking**: It uses header injection and `-u` linker flags to wire everything into the final binary.
*   **Result**: Restored stable **30+ FPS** while keeping dependencies clean.

## ✅ Best practices
1.  **USB console**: Always use USB Serial/JTAG on the S3 to avoid GPIO conflicts.
2.  **Vector sizing**: Render SVGs at a small base resolution (e.g., 180x180) and let LVGL scale them.
3.  **Double buffering**: Use internal DMA memory and the `__builtin_bswap16` intrinsic.
