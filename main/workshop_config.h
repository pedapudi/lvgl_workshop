#pragma once

#include "esp_heap_caps.h"
#include "sdkconfig.h"

/**
 * WORKSHOP_PHASE: The central engine switch.
 * -----------------------------------------
 * This pulls the phase from Kconfig (int) or defaults to 4.
 */
#ifdef CONFIG_WORKSHOP_PHASE
#define WORKSHOP_PHASE CONFIG_WORKSHOP_PHASE
#else
#define WORKSHOP_PHASE 4
#endif

namespace Workshop {

/**
 * STRATEGY: Data-driven performance knobs.
 * We use constexpr to ensure these are resolved at compile time.
 */

// CPU Frequency: 160MHz (Phase 1) vs 240MHz (Maximum)
static constexpr int CPU_FREQ_MHZ = (WORKSHOP_PHASE >= 2) ? 240 : 160;

// SPI Bus Speed: 20MHz (Standard) vs 80MHz (S3 Limit)
static constexpr uint32_t SPI_BUS_SPEED =
    (WORKSHOP_PHASE >= 2) ? (80 * 1000 * 1000) : (20 * 1000 * 1000);

// Buffer Strategy: Partial Strip vs Full Frame
enum class BufferMode { PartialStrip, FullFrame };
static constexpr BufferMode BUFFER_MODE =
    (WORKSHOP_PHASE >= 4) ? BufferMode::FullFrame : BufferMode::PartialStrip;

// Memory Strategy: Internal SRAM vs Octal PSRAM
static constexpr uint32_t ALLOC_CAPS =
    (WORKSHOP_PHASE >= 4) ? (MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM)
                          : (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

// Concurrency: Single vs Double Buffering
static constexpr bool USE_DOUBLE_BUFFERING = (WORKSHOP_PHASE >= 3);

// Task Depth: SVGs need more stack as they get more complex/scaled.
// Starting with 8KB (Standard LVGL) to show the crash/bottleneck.
static constexpr uint32_t LVGL_STACK_SIZE =
    (WORKSHOP_PHASE >= 2) ? 65536 : 8192;

// Optimization: Use Xtensa Intrinsics for byte swapping
static constexpr bool USE_XTENSA_INTRINSICS = (WORKSHOP_PHASE >= 4);

}  // namespace Workshop
