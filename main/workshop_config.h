#pragma once

#include "display/display.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
 * Manually set this value to switch between workshop phases.
 *
 * Phase 1: Defaults (160MHz, 8KB Stack, Naive Flush, 20MHz SPI)
 * Phase 2: Foundation (240MHz, 64KB Stack, 80MHz SPI)
 * Phase 3: Parallelism (Partial Double Buffering)
 * Phase 4: Expert (Full-Frame PSRAM Double Buffering, SIMD Intrinsics)
 * Phase 5: Native (Native Display Driver, SIMD SW_ASM Shim)
 */
#ifdef CONFIG_USE_KCONFIG_PHASE
#define WORKSHOP_PHASE CONFIG_WORKSHOP_PHASE
#else
#define WORKSHOP_PHASE 5
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

// MEMORY STRATEGY:
// Phase 4 uses the 8MB Octal PSRAM for massive distinct buffers.
// All other phases rely on the fast internal SRAM (~320KB).
static constexpr bool USE_PSRAM = (WORKSHOP_PHASE == 4);

static constexpr uint32_t ALLOC_CAPS =
    USE_PSRAM ? (MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM)
              : (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

// CONCURRENCY (DOUBLE BUFFERING):
// Phase 3+ enables a second buffer to decouple render time from flush time.
static constexpr bool USE_DOUBLE_BUFFERING = (WORKSHOP_PHASE >= 3);

// RENDERING MODE:
// Phase 1-2: Naive full refresh (redraws everything).
// Phase 3+: Optimized partial refresh (redraws only changed areas).
static constexpr lvgl::Display::RenderMode LVGL_RENDER_MODE =
    (WORKSHOP_PHASE >= 3) ? lvgl::Display::RenderMode::Partial
                          : lvgl::Display::RenderMode::Full;

// BUFFER SIZING:
// FullFrame: Used when we have massive RAM (Phase 4) or when we only have
// one buffer (Phase 1-2) and it fits in SRAM.
// PartialStrip: Used when we want double-buffering but are constrained by
// Internal SRAM size (Phase 3, 5).
static constexpr BufferMode BUFFER_MODE = (USE_PSRAM || !USE_DOUBLE_BUFFERING)
                                              ? BufferMode::FullFrame
                                              : BufferMode::PartialStrip;

// TASK STACK DEPTH:
// Vector graphics engines (ThorVG) use recursion for path parsing and
// scaling. 32KB (Phase 1) is recommended to prevent stack overflows during
// complex SVG rendering. 64KB (Phase 2+) provides the headroom needed for
// fluid animations.
static constexpr uint32_t LVGL_STACK_SIZE =
    (WORKSHOP_PHASE >= 2) ? 64 * 1024 : 32 * 1024;

// COMPILER OPTIMIZATIONS (BYTE SWAPPING):
// SIMD Intrinsics (Phase 4+): Replaces manual loops with a single-cycle
// hardware instruction
// (`__builtin_bswap16`) to swap Little-Endian CPU bytes for the Big-Endian
// LCD.
static constexpr bool USE_XTENSA_INTRINSICS = (WORKSHOP_PHASE >= 4);

// DRIVER STRATEGY:
// Legacy (Phase 1-4): LvglPort manages buffers and manual flushing.
// Native (Phase 5): Esp32SpiDisplay manages buffers and dedicated SPI/DMA
// logic.
static constexpr bool USE_NATIVE_DRIVER = (WORKSHOP_PHASE >= 5);

// CORE AFFINITY:
// Phase 1-4: Pin to Core 1.
// Phase 5: No Affinity (Load Balancing) to isolate ThorVG and maximize
// throughput.
static constexpr BaseType_t LVGL_TASK_CORE =
    (WORKSHOP_PHASE == 5) ? tskNO_AFFINITY : 1;

}  // namespace Workshop
