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

// BUFFER STRATEGY:
// FullFrame (Phase 1, 2, 4): Renders all 240 rows at once.
// - Phase 1, 2: Internal SRAM (115KB).
// - Phase 4: PSRAM (115KB).
// PartialStrip (Phase 3, 5): Renders chunks to fit double-buffering in Internal
// SRAM.
static constexpr BufferMode BUFFER_MODE =
    (WORKSHOP_PHASE == 3 || WORKSHOP_PHASE == 5) ? BufferMode::PartialStrip
                                                 : BufferMode::FullFrame;

// RENDERING MODE:
// Phase 1-2: Full refresh (Naive/Foundation - redraws everything).
// Phase 3-5: Partial refresh (Optimized/Expert - redraws changed areas).
static constexpr lvgl::Display::RenderMode LVGL_RENDER_MODE =
    (WORKSHOP_PHASE <= 2) ? lvgl::Display::RenderMode::Full
                          : lvgl::Display::RenderMode::Partial;

// MEMORY ALLOCATION CAPABILITIES:
// INTERNAL (Phase 1-3, 5): High speed but limited capacity (~320KB).
// SPIRAM (Phase 4): Uses the 8MB Octal PSRAM. Slower than SRAM, but allows
// for massive buffers.
static constexpr uint32_t ALLOC_CAPS =
    (WORKSHOP_PHASE == 4) ? (MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM)
                          : (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

// CONCURRENCY (DOUBLE BUFFERING):
// Phase 3+ enables a second buffer. This allows the CPU to calculate the next
// frame while the SPI controller is busy sending the current frame to the
// screen via DMA.
static constexpr bool USE_DOUBLE_BUFFERING = (WORKSHOP_PHASE >= 3);

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
