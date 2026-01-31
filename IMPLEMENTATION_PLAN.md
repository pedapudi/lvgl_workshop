# Implementation Plan: High-Performance SVG Rendering

This plan details the technical steps to implement the advanced optimization roadmap described in the `README.md`.

---

## 🚫 Technical Gaps & Challenges: The FreeRTOS Conflict
During implementation, we identified a critical instability (Post-boot Panic) when enabling `CONFIG_LV_OS_FREERTOS`.

- **The Clash**: LVGL v9 introduces its own OS abstraction layer. On the ESP32, this can conflict with the way ESP-IDF manages its native FreeRTOS tasks and internal atomic operations.
- **The Gap**: Enabling multiple "Draw Units" in LVGL depends on this OS layer. Without a robust, ESP-specific wrapper for `lv_os.h`, parallel rendering remains unsafe.
- **The Pivot**: We are shifting focus to **CPU-bound optimizations** (Compiler/LTO) and **Memory-bound optimizations** (SPIRAM Caching) which do not require the unstable OS abstraction.

---

## Phase 1: Compiler & Build System Tuning (ACTIVE)
**Goal**: Maximize instruction throughput for math-heavy vector rasterization.

1.  **Performance Optimization Level**:
    - Locate `idf.py menuconfig` -> `Component config` -> `ESP32S3-specific`.
    - Set `Optimization Level` to `-O3` (Performance).
    - Ensure `CONFIG_COMPILER_OPTIMIZATION_PERF=y` is set in `sdkconfig`.
2.  **Link Time Optimization (LTO)**:
    - Enable LTO to allow cross-module function inlining.
    - Set `CONFIG_COMPILER_OPTIMIZATION_LTO=y`.

---

## Phase 2: Parallel Drawing (DEFERRED)
**Goal**: Parallelize ThorVG rasterization across both Xtensa cores.
**Status**: Blocked by FreeRTOS integration panic. Re-evaluate once LVGL/ESP-IDF maintainers provide a certified port.

---

## Phase 3: SPIRAM Image Caching
**Goal**: Cache rasterized SVG frames in large PSRAM to avoid redundant CPU work.

1.  **Memory Allocation**:
    - Ensure `CONFIG_SPIRAM_SUPPORT=y` and `MALLOC_CAP_SPIRAM` is functional.
2.  **LVGL Cache Configuration**:
    - Set `CONFIG_LV_CACHE_DEF_SIZE` to `2097152` (2MB).
    - Configure LVGL to use the `psram_malloc` allocator for the image cache.
3.  **Asset Strategy**:
    - Increase cache weight for SVG assets in `lv_conf.h` to prioritize them over static PNG components.

---

## Phase 4: ThorVG Arena Persistence
**Goal**: Prevent memory exhaustion by moving the ThorVG scratchpad to PSRAM.

1.  **Custom Allocator**:
    - Implement a `thorvg_malloc` override that targets `MALLOC_CAP_SPIRAM`.
    - This allows for virtually unlimited path complexity at a minor latency cost compared to internal SRAM.

---

## Phase 5: Verification & Diagnostics
1.  **CPU Profiling**:
    - Enable `CONFIG_LV_USE_PERF_MONITOR=y` to display FPS and CPU usage on-screen.
2.  **Bus Monitoring**:
    - Periodically log SPI frequency using `esp_clk_tree_src_get_freq_hz(internal_bus)`.
3.  **Visual Benchmarking**:
    - Compare "Hummingbird" vs "Raccoon" (scaled) to ensure frame-flipping is perfectly asynchronous.
