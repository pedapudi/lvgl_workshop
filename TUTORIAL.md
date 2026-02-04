# Animation workshop: ESP32-S3 and LVGL optimization guide

Welcome to the Animation Workshop! This tutorial will walk you through the process of taking a basic SVG animation and optimizing it for the ESP32-S3 and XIAO Round Display, moving from a stuttering **7 FPS** to a fluid **26 FPS**.

In this workshop, you will implement an application that renders and animates three vector assets: a **Hummingbird**, a **Raccoon**, and a **Whale**. You'll learn how to handle complex SVG rendering on resource-constrained hardware by navigating through four progressive optimization phases.

### 🌐 The ecosystem
This project leverages a powerful open-source stack designed for high-performance embedded graphics:
*   **[ESP32-S3 (Seeed Studio XIAO)](https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html)**: A dual-core MCU with vector instructions and 8MB of Octal PSRAM.
*   **[ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)**: The official development framework for Espressif SoCs.
*   **[LVGL](https://lvgl.io/)**: The most popular open-source embedded graphics library.
*   **`lvgl_cpp`**: A modern C++20 wrapper for LVGL that provides type safety and idiomatic abstractions for objects, animations, and displays.

---

## 🛠️ How to follow this guide
This workshop uses a "Software Throttle" to simulate hardware limitations without forcing you to recompile the bootloader constantly. You can switch implementation levels in two ways:
1.  **The workshop way**: Use `idf.py menuconfig` -> `Animation Workshop` to toggle phases 1-4.
2.  **The manual way**: Modify `#define WORKSHOP_PHASE` in `main/workshop_config.h`.

---

## 📊 Phase overview
The workshop progresses through four stages of optimization, moving from naive functional implementation to expert-level PSRAM utilization.

| Phase | Title | Key Optimizations | Buffer Strategy | Rendering Mode |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 1** | Baseline | 160MHz CPU / 20MHz SPI | 1x Full Frame (Internal) | Full Refresh |
| **Phase 2** | Foundation | 240MHz CPU / 80MHz SPI | 1x Full Frame (Internal) | Full Refresh |
| **Phase 3** | Parallelism | Double Buffering | 2x Partial Strip (Internal) | Partial Refresh |
| **Phase 4** | Expert | Octal PSRAM / SIMD | 2x Full Frame (PSRAM) | Partial Refresh |

---

## Phase 1: The baseline (naive implementation)
**Goal:** Display an SVG with minimal configuration.

In this phase, we focus on functional correctness. We use LVGL's **ThorVG** engine to decode and render SVG paths. However, because we haven't optimized the system, the CPU is slow (160MHz) and the rendering mode is set to **Full Refresh** (redrawing every pixel every frame), causing significant overhead.

### ⚙️ Real-world ESP-IDF configuration
To achieve this baseline in a production project, you would set:
- **CPU frequency**: `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160` in `sdkconfig`.
- **LVGL task stack**: Set as a parameter in your `xTaskCreate` call (typically `8192`).
- **Display bus**: Set `.pclk_hz = 20 * 1000 * 1000` (20MHz) in your `esp_lcd_panel_io_spi_config_t` struct.
- **Optimization**: `CONFIG_COMPILER_OPTIMIZATION_DEFAULT` (-Og).

### 💻 Implementation
Setting up the hardware and SVG display in Phase 1 requires orchestrating the display driver and the LVGL porting layer.

**Hardware and port initialization (`app_main`):**
```cpp
// Initialize the GC9A01 SPI display
Gc9a01 display_hw(display_cfg);
display_hw.init();

// Initialize the LVGL porting layer with SRAM-only buffers
LvglPort::Config lvgl_config;
lvgl_config.malloc_caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
lvgl_config.double_buffered = false;

LvglPort lvgl_port(lvgl_config);
lvgl_port.init(display_hw.get_panel_handle(), display_hw.get_io_handle());
```

**SVG display logic:**
```cpp
// 1. skip SVG header metadata to find the XML start tag
const char* raw_svg_ptr = hummingbird_svg;
while (*raw_svg_ptr && *raw_svg_ptr != '<') raw_svg_ptr++;

// 2. create an image descriptor for ThorVG
static lvgl::ImageDescriptor bird_dsc(75, 75, LV_COLOR_FORMAT_RAW,
                                    (const uint8_t*)raw_svg_ptr,
                                    strlen(raw_svg_ptr) + 1);

// 3. display the SVG on an image object
auto hummingbird = std::make_unique<lvgl::Image>(parent);
hummingbird->set_src(bird_dsc).center();
```

**Result:** ~7 FPS. The static hummingbird renders successfully, but attempting to run the raccoon animation will result in a **Stack Overflow crash**. The default 8KB stack is insufficient for the recursive calculations required to scale and rotate complex SVG paths.

---

## Phase 2: Hardware foundation
**Goal:** Maximize the ESP32-S3's raw clock speeds.

SVG rendering is a "Math Problem." By increasing the CPU frequency, we give the vector engine more cycles to calculate Bezier curves.

### ⚡ The strategy
1.  **CPU boost**: Increase frequency from 160MHz to **240MHz**.
2.  **SPI overclock**: Increase the display highway speed from 20MHz to **80MHz**.
3.  **Compiler power**: Enable **-O3** performance optimizations to speed up the vector math engine.

### ⚙️ Real-world ESP-IDF configuration
- **CPU speed**: `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240` in `sdkconfig`.
- **Display bus**: `.pclk_hz = 80 * 1000 * 1000` (80MHz) in your SPI config. 
    > **Note**: 80MHz is the absolute limit for the S3's SPI bus. Higher is not supported by the hardware, and lower speeds cause visible "tearing" as the bus cannot keep up with the frame updates.
- **Optimization level**: Set `CONFIG_COMPILER_OPTIMIZATION_PERF=y` in your configuration to enable `-O3`.
- **Byte correction**: In your `flush_cb`, implement a loop to swap bytes from Little-Endian (CPU) to Big-Endian (LCD):
```cpp
void LvglPort::flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    auto* port = (LvglPort*)lv_display_get_user_data(disp);
    uint16_t* buf = (uint16_t*)px_map;
    uint32_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    for (uint32_t i = 0; i < len; i++) {
        uint16_t color = buf[i];
        buf[i] = (color << 8) | (color >> 8);
    }

    esp_lcd_panel_draw_bitmap(port->panel_handle_, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, buf);
}
```

**Result:** ~12 FPS. The movement is noticeably smoother.

---

## Phase 3: Parallel logic and double buffering
**Goal:** Eliminate screen tearing and decouple the CPU from the display.

### ⚡ The strategy
*   **Double buffering**: We allocate **two** separate buffers. While the hardware DMA (Direct Memory Access) is sending Buffer A to the screen, the CPU can immediately start drawing Buffer B.
*   **Stack boost**: We increase the LVGL task stack to **64KB**. Vector graphics use recursion; an 8KB stack will overflow and crash when scaling complex assets.

### ⚙️ Real-world ESP-IDF configuration
- **DMA enable**: Ensure `MALLOC_CAP_DMA` is used during buffer allocation.
- **LVGL buffers**: When calling `lv_display_set_buffers`, provide two pointers instead of one.
- **Buffer mode**: Set `LV_DISPLAY_RENDER_MODE_PARTIAL`.

### 💻 Implementation
Moving to Phase 3 requires enabling double buffering and increasing the task stack to prevent stack overflows during complex SVG scaling.

**Double buffer allocation:**
```cpp
// Allocate two strike buffers in internal memory
lvgl_config.double_buffered = true;
lvgl_config.render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;

// Use small strip buffers (usually 1/10th or 1/20th of the screen)
// to fit both buffers in fast internal SRAM.
lvgl_config.full_frame = false; 
```

**Recursion-safe stack boost:**
```cpp
// Increasing from 8KB (Standard) to 64KB (Vector-Safe)
lvgl_config.task_stack_size = 65536; 
```

**Result:** ~18 FPS. No more flickering! However, we hit the **"Tiling Problem"**: small buffers (20 lines) force ThorVG to re-calculate the entire image 12 times to fill the 240-line screen.

---

## Phase 4: Expert optimization (the 26 FPS secret)
**Goal:** Eliminate "Tiling Overhead" using Large Octal PSRAM.

### ⚡ The strategy
1.  **Full-frame buffers**: We move the buffers to the 8MB **Octal PSRAM** and increase them to a **Full Frame** (240x240 pixels).
    *   *The Gain*: ThorVG renders the Raccoon in a **Single Pass**. Even though PSRAM is slightly slower than SRAM, avoiding 12x re-calculation is a massive win.
2.  **Xtensa intrinsics**: We replace the manual swap loop with `__builtin_bswap16`, a hardware instruction that swaps bits in a **Single Cycle**.

### ⚙️ Real-world ESP-IDF configuration
- **PSRAM init**: `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`.
- **Memory caps**: Use `MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM` for 115KB buffer allocation.
- **Compiler power**: Enable `CONFIG_COMPILER_OPTIMIZATION_PERF=y` and `CONFIG_COMPILER_OPTIMIZATION_LTO=y`. 

**Result:** **26 FPS**. Smooth, high-fidelity SVG animation.
 
### 🧠 Deep Dive: Explicit Hardware Intrinsics
The most expensive part of the rendering pipeline is the **Flush Callback**. For every frame, we must swap the bytes of every pixel (Endianness correction) and invert colors (LCD panel requirement).

Instead of a standard C math loop, Phase 4 uses:
```cpp
buf[i] = __builtin_bswap16(buf[i]);
```
*   **`__builtin_bswap16`**: This is a compiler intrinsic that tells the CPU to use a dedicated hardware instruction (`BE`) to swap bytes in a single clock cycle.
*   **⚠️ The NOT Pitfall (`~`)**: You might see examples using `~__builtin_bswap16()`. This is used for "Active-Low" LCD panels that require bitwise inversion to display colors correctly. If your colors look like a "photo negative," remove the `~` operator.

**Posterity Note:** During Phase 1 implementation, using the `~` inversion on standard GC9A01 panels often results in inverted colors. Always verify your panel's logic level before applying bitwise negation in the flush loop.

---

## 🎨 The Animation Engine: Bringing SVGs to Life

Our UI doesn't just display static images; it reproduces high-fidelity motion from SVG specifications using `lvgl_cpp`.

### The SVG-to-LVGL Bridge
SVG animations often use "Cubic Bezier" curves (defined as `keySplines`) for fluid motion. LVGL has a built-in Bezier engine, but it's traditionally used for internal easing. We've exposed this logic to our UI to perfectly match the biological movement defined in the SVG assets:

```cpp
static int32_t svg_bezier_path(const lv_anim_t* a, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    // Maps time (0..duration) to curve progress (0..1024)
    int32_t t = lv_map(a->act_time, 0, a->duration, 0, 1024);
    int32_t step = lv_cubic_bezier(t, x1, y1, x2, y2);
    // Interpolates current value based on curve progress
    return a->start_value + ((step * range) >> 10);
}
```

### Layered Motion (The Whale)
The whale animation isn't one single movement. It’s a **composition of transformations**:
1.  **Bobbing**: A vertical translation using a smooth ease-in-out curve.
2.  **Tilting**: A rhythmic rotation (mapping 0.1 degree units in LVGL) using the same biological timing as the bobbing.

By layering these simple `lvgl::Animation` objects, we create a feeling of weight and fluid movement that feels "alive" rather than mechanical.

### 🧪 Expert configuration deep dive: sdkconfig.defaults
For high-performance animations, your `sdkconfig` is just as important as your code. Here is the breakdown of the settings used in this workshop:

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

---

---

## Phase 5: Native architecture (The 30+ FPS standard)
**Goal:** Professional-grade driver with "Large Partial" SRAM buffering and 32-bit SWAR bit-swapping.

While **Phase 4** brute-forced performance using massive PSRAM buffers, it introduced latency due to external memory wait-states. **Phase 5** reaches the **30+ FPS milestone** by shifting to a "Mobile-Grade" architecture: using high-speed **Internal SRAM** buffers but making them large enough to minimize tiling overhead.

### ⚡ The Strategy
1.  **"Large Partial" Buffering**: We allocate **1/2 Screen buffers** (240x120) in Internal SRAM. This achieves the best of both worlds: higher bandwidth than PSRAM and only a 2x tiling multiplier (compared to 12x in Phase 3).
2.  **32-bit SWAR Processing**: We implement **SIMD-within-a-Register (SWAR)** in the driver's flush logic. This allows us to swap and invert two pixels (32 bits) in the same time it previously took to do one pixel (16 bits) using standard intrinsics.
3.  **Core Mobility**: By removing task pinning (`tskNO_AFFINITY`), we allow the FreeRTOS scheduler to saturate the S3's dual CPU cores—one core can handle the heavy ThorVG rasterization while the other services the high-frequency SPI DMA interrupts.

### 💻 Implementation
Phase 5 introduces the `Esp32Spi` native driver, which moves the buffer management and low-level optimization directly into the `lvgl_cpp` driver ecosystem.

**The "SRAM vs PSRAM" Trade-off:**
In Phase 5, we prefer two **57KB Internal SRAM** buffers over two **115KB PSRAM** buffers. Even though the "Full Frame" in PSRAM eliminates tiling entirely, the faster access speed of Internal RAM at 240MHz CPU speeds allows ThorVG to complete its 2-pass render faster than a 1-pass render in external memory.

**32-bit SWAR Optimization (`esp32_spi.cpp`):**
Instead of processing 16-bit pixels, we cast the buffer to `uint32_t` and use bitwise masks to swap two pixels at once:
```cpp
auto swap = [](uint32_t v) {
  // Swaps bytes for TWO pixels simultaneously
  return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8);
};
```
This loop is unrolled 8 times (processing 16 pixels per iteration), ensuring the CPU can feed the 80MHz SPI bus without stalling.

### 🧠 Why "Native" is Professional
In professional embedded projects, you want your graphics engine to be "blind" to the hardware. **Phase 5** enforces this by:
- **Formal Linkage**: Locking the `esp_lcd` hardware handles to the `lv_display_t` object via `user_data`.
- **Zero Middleware**: The `LvglPort` no longer manages memory or flushing; it simply passes the requirements to the `Esp32Spi` driver and starts the engine.

**Result:** **30+ FPS**. Fluid, professional-grade animation that hits the absolute limits of the XIAO Round Display's SPI bus.

---

## 📊 Final performance summary
| Phase | Key Optimization | Target FPS | Primary Learning |
| :--- | :--- | :--- | :--- |
| **Phase 1** | Baseline | 7 FPS | SPI 20MHz / Full Refresh |
| **Phase 2** | Foundation | 12 FPS | CPU 240MHz / SPI 80MHz |
| **Phase 3** | Parallelism | 18 FPS | Double Buffering / DMA |
| **Phase 4** | Expert Tuning | 26 FPS | PSRAM / Full-Frame Buffers |
| **Phase 5** | **Native** | **30+ FPS** | **Internal SRAM / 32-bit SWAR** |

---

### Final note
In a production app, you should aim for **Phase 4** parameters from day one. The "journey" is for teaching, but the destination is always **Full-Frame PSRAM buffering** on the ESP32-S3. Source files contain verbose pedagogical comments to explain why certain C++ patterns were chosen.
