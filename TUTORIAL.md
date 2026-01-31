# Animation Workshop: ESP32-S3 & LVGL Optimization Guide

Welcome to the Animation Workshop! This tutorial will walk you through the process of taking a basic SVG animation and optimizing it for the ESP32-S3 and XIAO Round Display, moving from a stuttering **7 FPS** to a fluid **26 FPS**.

---

## 🛠️ How to Follow This Guide
This workshop uses a "Software Throttle" to simulate hardware limitations without forcing you to recompile the bootloader constantly. You can switch implementation levels in two ways:
1.  **The Workshop Way**: Use `idf.py menuconfig` -> `Animation Workshop` to toggle phases 1-4.
2.  **The Manual Way**: Modify `#define WORKSHOP_PHASE` in `main/workshop_config.h`.

---

## Phase 1: The Baseline (Naive Implementation)
**Goal:** Display an SVG with minimal configuration.

In this phase, we focus on functional correctness. We use LVGL's **ThorVG** engine to decode and render SVG paths. However, because we haven't optimized the system, the CPU is slow (160MHz), the screen buffers are tiny, and the image re-renders constantly.

### ⚙️ Real-world ESP-IDF Configuration
To achieve this baseline in a production project, you would set:
- **CPU Frequency**: `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160` in `sdkconfig`.
- **LVGL Task Stack**: Set as a parameter in your `xTaskCreate` call (typically `8192`).
- **Display Bus**: Set `.pclk_hz = 20 * 1000 * 1000` (20MHz) in your `esp_lcd_panel_io_spi_config_t` struct.
- **Optimization**: `CONFIG_COMPILER_OPTIMIZATION_DEFAULT` (-Og).

**Result:** ~7 FPS. The animation works, but colors look inverted (Magenta/Cyan) because we haven't corrected the byte order.

---

## Phase 2: Hardware Foundation
**Goal:** Maximize the ESP32-S3's raw clock speeds.

SVG rendering is a "Math Problem." By increasing the CPU frequency, we give the vector engine more cycles to calculate Bezier curves. We also fix the color inversion.

### ⚡ The Strategy
1.  **CPU Boost**: Increase frequency from 160MHz to **240MHz**.
2.  **SPI Overclock**: Increase the display highway speed from 20MHz to **80MHz**.

### ⚙️ Real-world ESP-IDF Configuration
- **CPU Speed**: `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240` in `sdkconfig`.
- **Display Bus**: `.pclk_hz = 80 * 1000 * 1000` (80MHz) in your SPI config.
- **Byte Correction**: In your `flush_cb`, implement a loop to swap bytes: `buf[i] = (color << 8) | (color >> 8)`.

**Result:** ~12 FPS. The colors are now correct, and the movement is noticeably smoother.

---

## Phase 3: Parallel Logic & Double Buffering
**Goal:** Eliminate screen tearing and decouple the CPU from the display.

### ⚡ The Strategy
*   **Double Buffering**: We allocate **two** separate buffers. While the hardware DMA (Direct Memory Access) is sending Buffer A to the screen, the CPU can immediately start drawing Buffer B.
*   **Stack Boost**: We increase the LVGL task stack to **64KB**. Vector graphics use recursion; an 8KB stack will overflow and crash when scaling complex assets.

### ⚙️ Real-world ESP-IDF Configuration
- **DMA Enable**: Ensure `MALLOC_CAP_DMA` is used during buffer allocation.
- **LVGL Buffers**: When calling `lv_display_set_buffers`, provide two pointers instead of one.
- **Buffer Mode**: Set `LV_DISPLAY_RENDER_MODE_PARTIAL`.

**Result:** ~18 FPS. No more flickering! However, we hit the **"Tiling Problem"**: small buffers (20 lines) force ThorVG to re-calculate the entire image 12 times to fill the 240-line screen.

---

## Phase 4: Expert Optimization (The 26 FPS Secret)
**Goal:** Eliminate "Tiling Overhead" using Large Octal PSRAM.

### ⚡ The Strategy
1.  **Full-Frame Buffers**: We move the buffers to the 8MB **Octal PSRAM** and increase them to a **Full Frame** (240x240 pixels).
    *   *The Gain*: ThorVG renders the Raccoon in a **Single Pass**. Even though PSRAM is slightly slower than SRAM, avoiding 12x re-calculation is a massive win.
2.  **Xtensa Intrinsics**: We replace the manual swap loop with `__builtin_bswap16`, a hardware instruction that swaps bits in a **Single Cycle**.

### ⚙️ Real-world ESP-IDF Configuration
- **PSRAM Init**: `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`.
- **Memory Caps**: Use `MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM` for 115KB buffer allocation.
- **Compiler Power**: Enable `CONFIG_COMPILER_OPTIMIZATION_PERF=y` and `CONFIG_COMPILER_OPTIMIZATION_LTO=y`. 

**Result:** **26 FPS**. Smooth, high-fidelity SVG animation.

### 🧪 Expert Configuration Deep Dive: `sdkconfig.defaults`
For production-ready high-performance animations, your `sdkconfig` is just as important as your code. Here is the breakdown of the settings used in this workshop:

| Config Variable | Value | Purpose & Rationale |
| :--- | :--- | :--- |
| **`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`** | `y` | **Crucial**: Offloads logging to the S3's dedicated hardware. This frees up **GPIO 43/44**, which are otherwise hardwired to the UART0 console and conflict with the display/touch pins on the Seeed shield. |
| **`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`** | `y` | Sets the CPU clock to its maximum. Vector rendering is pure math; every MHz counts. |
| **`CONFIG_COMPILER_OPTIMIZATION_PERF`** | `y` | Enables **`-O3`** optimizations. This tells the compiler to prioritize execution speed over binary size (essential for ThorVG's complex loops). |
| **`CONFIG_COMPILER_OPTIMIZATION_LTO`** | `y` | Enables **Link Time Optimization**. This allows the compiler to optimize *across* source files, potentially inlining your `flush_cb` directly into the engine's render loop. |
| **`CONFIG_SPIRAM`** | `y` | Enables the external 8MB PSRAM. Without this, you are limited to ~320KB of internal RAM, making full-frame buffering impossible. |
| **`CONFIG_SPIRAM_MODE_OCT`** | `y` | Configures the PSRAM in **Octal Mode** (8 data lines). This provides the massive bandwidth required for the CPU to read/write 240x240 pixel frames without stuttering. |
| **`CONFIG_LV_USE_THORVG`** | `y` | Enables the high-performance C++ vector engine used by LVGL for SVG rendering. |
| **`CONFIG_LV_CACHE_DEF_SIZE`** | `2097152` | Allocates a **2MB Image Cache** in PSRAM. This essentially "remembers" rendered frames, turning expensive vector math into simple memory copies for static or repeating frames. |
| **`CONFIG_PM_ENABLE`** | `y` | Enables the power management system, which allows us to programmatically adjust the CPU frequency between phases. |

---

## 📊 Final Performance Summary
| Phase | Key Optimization | Target FPS | Real-world Check |
| :--- | :--- | :--- | :--- |
| **Phase 1** | Baseline | 5-7 FPS | 160MHz CPU / 8KB Stack |
| **Phase 2** | Clock Speeds | 10-12 FPS | 240MHz CPU / 80MHz SPI |
| **Phase 3** | Parallelism | 15-18 FPS | Double Buffering / DMA |
| **Phase 4** | **Hardware Intel** | **26 FPS** | **PSRAM / SIMD Intrinsics / LTO** |

---

### Final Note for Students
In a production app, you should aim for **Phase 4** parameters from day one. The "journey" is for teaching, but the destination is always **Full-Frame PSRAM buffering** on the ESP32-S3. Source files contain verbose pedagogical comments to explain why certain C++ patterns were chosen.
