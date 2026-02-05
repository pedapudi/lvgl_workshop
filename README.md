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

- **`lvgl_cpp` (Library)**: A generic, platform-agnostic C++20 wrapper for LVGL. It runs on Linux, Windows, STM32, or ESP32. It provides the clean, object-oriented API (e.g., `lvgl::Display`, `lvgl::Image`).
- **`main/sys/lvgl_port.cpp` (Project Code)**: The specific "bridge" code that connects the generic `lvgl_cpp` library to the ESP32-S3 hardware (DMA, SPI, PSRAM).
This workshop is designed as a guided journey from a "naive" implementation (7 FPS) to a "premium" high-performance animation (26 FPS). You can switch between these optimization levels to see and feel the impact of different hardware and software strategies.

### 🛠️ How to select a phase
You can choose your optimization level in two ways:

1.  **Menuconfig (Recommended)**:
    -   Run `idf.py menuconfig`.
    -   Navigate to `Animation Workshop`.
    -   Find the **Workshop Optimization Phase** setting and enter a number from **1** to **4**.
    -   Save and re-flash.

2.  **Header Override**:
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

## 🏗️ Architectural design: "chassis vs. engine"
To ensure stability during the workshop, we use a decoupled configuration architecture:

1.  **The Chassis (`sdkconfig.defaults`)**: These are the "Static" hardware configurations (PSRAM enabled, 240MHz CPU, 2MB Image Cache). We keep this at 100% capacity to avoid long recompiles or bootloader changes.
2.  **The Engine (`workshop_config.h`)**: This is the "Dynamic" layer. It reads the selected `WORKSHOP_PHASE` and programmatically "throttles" the hardware to simulate different optimization levels.

### 🔗 Code references
-   **`main/workshop_config.h`**: The central registry. It maps the user's phase selection to technical parameters like `SPI_BUS_SPEED_HZ` and `ALLOC_CAPS`.
-   **`main/sys/lvgl_port.cpp`**: The orchestration layer. In Phase 5, it hands off display management to the native driver.
-   **`external/lvgl_cpp/display/drivers/esp32_spi.cpp`**: The **Phase 5 Native Driver**. It implements 32-bit SWAR bit-swapping and direct DMA management.

---

### 🛠️ Hardware synergy
-   **Dual-Core Xtensa® LX7 (240MHz)**: We parallelize rasterization by offloading half of the screen's tiles to Core 1.
-   **Internal SRAM (512KB)**: Phase 5 utilizes **1/2 Screen Partial Buffers** (57.6KB x 2) in high-speed Internal RAM to hit zero-wait cycle throughput.
-   **External PSRAM (8MB Octal SPI)**: Used for heavy image caching and large SVG asset storage.
-   **SIMD Accelerations**: ThorVG utilizes S3-specific math speedups for vector calculations.

### ⚡ High-speed pipeline: zero-copy native
1.  **Dynamic Scheduling (Core 0/1)**: Unlike Phase 4, Phase 5 uses `tskNO_AFFINITY`. This allows the FreeRTOS scheduler to balance the heavy ThorVG rasterization (Core 1) and high-frequency SPI interrupts (Core 0) dynamically.
2.  **32-bit SWAR Optimization**: In Phase 5, color processing (Byte Swapping + Inversion) is handled 2 pixels at a time using **SIMD-within-a-Register (SWAR)** techniques. This ensures that processing two full pixels takes no more cycles than one.
3.  **Low-Latency Internal SRAM**: By shifting from Full-Frame PSRAM (Phase 4) back to a **"Large Partial"** buffer in Internal SRAM, we eliminate the wait-states associated with external memory.
4.  **Native Hardware Linkage**: The `Esp32Spi` driver formally links the `esp_lcd` handles to the `lvgl::Display` object, removing the performance overhead of generic "Porting Layer" lookups.

### 📈 Advanced optimization roadmap
The following strategies are planned for further performance gains:
1.  **Compiler Tuning**: Shift from `-Os` to `-O3` and enable **Link Time Optimization (LTO)** for maximum throughput.
2.  **Parallel Drawing**: Increase `LV_DRAW_SW_DRAW_UNIT_CNT` to `2` and allocate **32KB stack** per draw thread to handle ThorVG recursion.
3.  **SPIRAM Caching**: Configure a 2MB image cache in PSRAM to turn expensive vector re-rasterization into simple memory copies for repeating frames.
4.  **ThorVG Arena Management**: Move the ThorVG internal scratchpad to PSRAM if complex SVGs exceed internal memory limits.

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
*   **Root Cause 1 (Silent Build Failure)**: The `espressif__esp_lvgl_port` component had a hard version check (`< 9.2.0`) in `CMakeLists.txt`, causing it to silently skip compiling the S3-optimized assembly files for our LVGL 9.4 build.
*   **Root Cause 2 (API Mismatch)**:
    *   **Signatures**: LVGL v9.4 hook macros (e.g., `LV_DRAW_SW_RGB888_BLEND_NORMAL_TO_RGB888`) require 3 arguments (`dsc`, `dest_px`, `src_px`), whereas the generic assembly routines only accepted 1.
    *   **Struct Layout**: The assembly code expects a specific legacy struct layout (starting with `opa`). LVGL 9.4's `lv_draw_sw_blend_fill_dsc_t` starts with `dest_buf`. Passing the pointer directly caused the assembly to interpret the destination pointer as opacity, leading to garbage reads.
    *   **Color Format**: The ESP32 assembly `fill` routines only support **32-bit colors** (reading R, G, B bytes). Passing a pointer to a 16-bit `lv_color_t` caused the assembly to read adjacent stack memory (garbage) as color channels.
*   **Fix**: Implemented an **External SIMD Patch Component** (`components/lvgl_s3_simd_patch`).
    *   **Architecture**: We keep `espressif__esp_lvgl_port` as a pristine `managed_component`.
    *   **Injection**: The patch component locates the S3 assembly files within `managed_components` and compiles them externally.
    *   **Shim**: It builds a C shim (`shim.c`) to bridge the struct and color format mismatches.
    *   **Linking**: It uses header injection and `-u` linker flags to wire everything into the final binary.
*   **Result**: Restored stable **30+ FPS** while keeping dependencies clean.

## ✅ Best practices
1.  **USB Console**: Always use USB Serial/JTAG on the S3 to avoid GPIO conflicts.
2.  **Vector Sizing**: Render SVGs at a small base resolution (e.g., 180x180) and let LVGL scale them.
3.  **Double Buffering**: Use internal DMA memory and the `__builtin_bswap16` intrinsic.

---

## 🚫 What not to do
- **Don't use GPIO 43/44 for I/O** without disabling the UART console.
- **Don't render full-screen SVGs at 240x240** during animations.
- **Don't block the I2C bus**. Always use a timeout for operations.
- **Don't initialize the touch chip immediately**. Wait for its internal boot.
