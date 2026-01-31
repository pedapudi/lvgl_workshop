#pragma once

#include "esp_heap_caps.h"
#include "lvgl.h"
#include "sdkconfig.h"

/**
 * WORKSHOP CONFIGURATION REGISTRY
 * ------------------------------
 * This file serves as the "Engine" of the workshop. It maps abstract
 * "Phases" (from Kconfig or headers) to specific hardware and software
 * performance parameters.
 */

/**
 * WORKSHOP_PHASE: The central engine switch.
 * -----------------------------------------
 * This pulls the phase from Kconfig (int) or defaults to 4.
 * Phase 1: Naive (160MHz, Single Buffer, Internal RAM)
 * Phase 2: Foundation (240MHz, 80MHz SPI, High Stack)
 * Phase 3: Parallelism (Double Buffering)
 * Phase 4: Expert (Full-Frame PSRAM, SIMD Intrinsics)
 */
#ifdef CONFIG_WORKSHOP_PHASE
#define WORKSHOP_PHASE CONFIG_WORKSHOP_PHASE
#else
#define WORKSHOP_PHASE 4
#endif

namespace Workshop {

enum class BufferMode { PartialStrip, FullFrame };
/**
 * PERFORMANCE STRATEGY
 * --------------------
 * We use `static constexpr` values to ensure that the compiler can optimize
 * out unreachable code paths for the selected phase.
 */

// CPU FREQUENCY:
// 160MHz (Phase 1) is the standard low-power speed.
// 240MHz (Phase 2+) is the maximum for the ESP32-S3 and essential for vector
// rasterization.
static constexpr int CPU_FREQ_MHZ = (WORKSHOP_PHASE >= 2) ? 240 : 160;

// SPI BUS SPEED:
// 20MHz (Phase 1) is safe for most modern SPI devices.
// 80MHz (Phase 2+) is the absolute hardware limit of the S3's SPIRAM.
static constexpr uint32_t SPI_BUS_SPEED =
    (WORKSHOP_PHASE >= 2) ? (80 * 1000 * 1000) : (20 * 1000 * 1000);

// BUFFER STRATEGY:
// PartialStrip (Phase 1-3): Renders 20 rows at a time to fit in fast Internal
// SRAM. FullFrame (Phase 4): Renders all 240 rows at once in PSRAM to eliminate
// tiling overhead.
static constexpr BufferMode BUFFER_MODE =
    (WORKSHOP_PHASE <= 2 || WORKSHOP_PHASE >= 4) ? BufferMode::FullFrame
                                                 : BufferMode::PartialStrip;

// RENDERING MODE:
// Phase 1-2: Full refresh (Naive/Foundation - redraws everything).
// Phase 3-4: Partial refresh (Optimized/Expert - redraws changed areas).
static constexpr lv_display_render_mode_t LVGL_RENDER_MODE =
    (WORKSHOP_PHASE <= 2) ? LV_DISPLAY_RENDER_MODE_FULL
                          : LV_DISPLAY_RENDER_MODE_PARTIAL;

// MEMORY ALLOCATION CAPABILITIES:
// INTERNAL (Phase 1-3): High speed but limited capacity (~320KB).
// SPIRAM (Phase 4): Uses the 8MB Octal PSRAM. Slower than SRAM, but allows for
// massive buffers.
static constexpr uint32_t ALLOC_CAPS =
    (WORKSHOP_PHASE >= 4) ? (MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM)
                          : (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

// CONCURRENCY (DOUBLE BUFFERING):
// Phase 3+ enables a second buffer. This allows the CPU to calculate the next
// frame while the SPI controller is busy sending the current frame to the
// screen via DMA.
static constexpr bool USE_DOUBLE_BUFFERING = (WORKSHOP_PHASE >= 3);

// TASK STACK DEPTH:
// Vector graphics engines (ThorVG) use recursion for path parsing and scaling.
// 8KB (Phase 1) is standard, but will CRASH when scaling complex SVGs.
// 64KB (Phase 2+) provides the headroom needed for complex animations.
static constexpr uint32_t LVGL_STACK_SIZE =
    (WORKSHOP_PHASE >= 2) ? 64 * 1024 : 8 * 1024;

// COMPILER OPTIMIZATIONS (BYTE SWAPPING):
// SIMD Intrinsics (Phase 4): Replaces manual loops with a single-cycle hardware
// instruction
// (`__builtin_bswap16`) to swap Little-Endian CPU bytes for the Big-Endian LCD.
static constexpr bool USE_XTENSA_INTRINSICS = (WORKSHOP_PHASE >= 4);

}  // namespace Workshop
