# Animation Workshop: SVG Animations on Seeed Round Display

This project demonstrates high-performance SVG animations using **LVGL v9**, **ThorVG**, and **LVGL_CPP** on the **Seeed Studio XIAO ESP32S3** paired with the **Round Display (240x240)**.

## 🚀 Quick Start

1. **Hardware**: Seeed Studio XIAO ESP32S3 + Seeed Round Display Shield.
2. **Environment**: ESP-IDF v6.1.
3. **Build & Flash**:
   ```bash
   idf.py build
   idf.py -p /dev/ttyACM0 flash monitor
   ```

## 🎓 The Optimization Journey
This workshop is designed as a guided journey from a "naive" implementation (7 FPS) to a "premium" high-performance animation (26 FPS). You can switch between these optimization levels to see and feel the impact of different hardware and software strategies.

### 🛠️ How to Select a Phase
You can choose your optimization level in two ways:

1.  **Menuconfig (Recommended)**:
    -   Run `idf.py menuconfig`.
    -   Navigate to `Animation Workshop`.
    -   Find the **Workshop Optimization Phase** setting and enter a number from **1** to **4**.
    -   Save and re-flash.

2.  **Header Override**:
    -   Open `main/workshop_config.h`.
    -   The file maps Kconfig macros to technical constants. You can manually force a phase here by redefining `WORKSHOP_PHASE`.

### 📊 The Phase Matrix
| Phase | Bottleneck | Target FPS | Primary Learning |
| :--- | :--- | :--- | :--- |
| **Phase 1** | SPI Bus & CPU | ~7 FPS | Baseline implementation. CPU wait-loops on display I/O. |
| **Phase 2** | Graphics Bus | ~12 FPS | Boosting SPI to 80MHz. Identifying the "Tiling Problem". |
| **Phase 3** | Raster Pipeline | ~18 FPS | Double-buffering & DMA. Decoupling CPU from Display. |
| **Phase 4** | Expert Tuning | ~26 FPS | Full-frame buffers in PSRAM & Xtensa SIMD Intrinsics. |

---

## 🏗️ Architectural Design: "Chassis vs. Engine"
To ensure stability during the workshop, we use a decoupled configuration architecture:

1.  **The Chassis (`sdkconfig.defaults`)**: These are the "Static" hardware configurations (PSRAM enabled, 240MHz CPU, 2MB Image Cache). We keep this at 100% capacity to avoid long recompiles or bootloader changes.
2.  **The Engine (`workshop_config.h`)**: This is the "Dynamic" layer. It reads the selected `WORKSHOP_PHASE` and programmatically "throttles" the hardware to simulate different optimization levels.

### 🔗 Code References
- **`main/workshop_config.h`**: The central registry. It maps the user's phase selection to technical parameters like `SPI_BUS_SPEED_HZ` and `ALLOC_CAPS`.
- **`main/sys/lvgl_port.cpp`**: This is where memory is allocated. It looks at the configuration to decide whether to use high-speed Internal SRAM (Phases 1-3) or the large-capacity Octal PSRAM (Phase 4).
- **`main/main.cpp`**: The conductor. It applies the bus-speed constraints to the display driver during initialization.

---


### 🛠️ Hardware Synergy
- **Dual-Core Xtensa® LX7 (240MHz)**: We parallelize rasterization by offloading half of the screen's tiles to Core 1.
- **Internal SRAM (512KB)**: Reserved for critical high-speed DMA "Double Buffers".
- **External PSRAM (8MB Octal SPI)**: Used for heavy image caching and large SVG asset storage.
- **SIMD Accelerations**: ThorVG utilizes S3-specific math speedups for vector calculations.

### ⚡ High-Speed Pipeline: Full-Frame DMA
1.  **Core Pinning (Task Isolation)**: High-frequency graphics tasks are pinned to **Core 1**. Core 0 is reserved for system tasks, Wi-Fi, and background I/O to prevent watchdog triggers and UI stuttering.
2.  **Full-Frame Buffering**: Utilizing the S3's **Octal PSRAM**, we allocate two **240x240 buffers** (115KB each). This eliminates the "Tiling Problem" where ThorVG would otherwise have to re-decode the SVG 12 times per frame for standard 20-line strip buffers.
3.  **Xtensa Intrinsics**: Using `__builtin_bswap16` for single-instruction byte swapping to match the display's Big-Endian requirement while inverting the color space for the GC9A01.

### 📈 Advanced Optimization Roadmap
The following strategies are planned for further performance gains:
1.  **Compiler Tuning**: Shift from `-Os` to `-O3` and enable **Link Time Optimization (LTO)** for maximum throughput.
2.  **Parallel Drawing**: Increase `LV_DRAW_SW_DRAW_UNIT_CNT` to `2` and allocate **32KB stack** per draw thread to handle ThorVG recursion.
3.  **SPIRAM Caching**: Configure a 2MB image cache in PSRAM to turn expensive vector re-rasterization into simple memory copies for repeating frames.
4.  **ThorVG Arena Management**: Move the ThorVG internal scratchpad to PSRAM if complex SVGs exceed internal memory limits.

---

## 💀 Postmortems: Lessons Learned

### Postmortem 1: The "Ghost in the Machine" (GPIO 43/44 Collision)
*   **Issue**: The display would flicker, and the touch controller would return random timeouts.
*   **Cause**: GPIO 43 and 44 are the default pins for Hardware UART0. Logging bit-banged signals directly into the display controllers.
*   **Fix**: Switched to **Native USB Serial/JTAG Controller** and disabled UART console.

### Postmortem 2: I2C & Watchdog (The CPU Starvation)
*   **Issue**: Complex SVG rendering would cause I2C timeouts.
*   **Cause**: Vector rendering occupied 100% of the CPU, starving the I2C driver task.
*   **Fix**: Boosted CPU to 240MHz and added a mandatory **5ms delay** in the LVGL task loop.

### Postmortem 3: Memory vs. Performance (The Buffer Trade-off)
*   **Issue**: Larger display buffers (e.g., 80 lines) caused watchdog resets.
*   **Cause**: Buffers were stealing the internal RAM required by ThorVG for vertex calculations.
*   **Fix**: Settled on **20-line double buffers**.

### Postmortem 4: Stack-Allocated Lambda Crashes
*   **Issue**: Switching animals caused sporadic crashes.
*   **Cause**: C++ lambda objects were being destroyed while LVGL still held pointers to them.
*   **Fix**: Switched to raw C function pointers for animation callbacks.

## ✅ Best Practices
1.  **USB Console**: Always use USB Serial/JTAG on the S3 to avoid GPIO conflicts.
2.  **Vector Sizing**: Render SVGs at a small base resolution (e.g., 180x180) and let LVGL scale them.
3.  **Double Buffering**: Use internal DMA memory and the `__builtin_bswap16` intrinsic.

---

## 🚫 What NOT to do
- **Don't use GPIO 43/44 for I/O** without disabling the UART console.
- **Don't render full-screen SVGs at 240x240** during animations.
- **Don't block the I2C bus**. Always use a timeout for operations.
- **Don't initialize the touch chip immediately**. Wait for its internal boot.
