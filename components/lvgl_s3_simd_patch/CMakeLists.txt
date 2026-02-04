idf_component_register(
    SRCS "src/lv_draw_sw_asm_shim.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
)

# 1. Locate the Managed Component Source Directory
# We assume it follows the standard layout in managed_components/espressif__esp_lvgl_port
set(MANAGED_PORT_PATH "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_lvgl_port")

if(NOT EXISTS "${MANAGED_PORT_PATH}")
    message(WARNING "[SIMD Patch] Managed component not found at ${MANAGED_PORT_PATH}. SIMD will be disabled.")
    return()
endif()

# 2. Glob the Assembly Files (Stealing them from the managed component)
# We only care about S3 for now as per the project requirements
if(CONFIG_IDF_TARGET_ESP32S3)
    message(STATUS "[SIMD Patch] Enabling S3 SIMD Assembly Patch")
    
    file(GLOB_RECURSE ASM_SRCS "${MANAGED_PORT_PATH}/src/lvgl9/simd/*_esp32s3.S")
    file(GLOB_RECURSE ASM_MACROS "${MANAGED_PORT_PATH}/src/lvgl9/simd/lv_macro_*.S")

    # Add them to OUR component's source list
    target_sources(${COMPONENT_LIB} PRIVATE ${ASM_SRCS} ${ASM_MACROS})
    
    # 3. Force Linkage (The Linker Trick)
    # We must force the linker to include these symbols, or they will be discarded
    # because they are only referenced via macros in a static library.
    set(FORCE_SYMBOLS 
        "-u lv_color_blend_to_rgb565_shim"
        "-u lv_color_blend_to_rgb888_shim"
        "-u lv_rgb565_blend_normal_to_rgb565_shim"
        "-u lv_rgb888_blend_normal_to_rgb888_shim"
        "-u lv_color_blend_to_argb8888_esp"
        "-u lv_color_blend_to_rgb565_esp"
        "-u lv_color_blend_to_rgb888_esp"
        "-u lv_rgb565_blend_normal_to_rgb565_esp"
        "-u lv_rgb888_blend_normal_to_rgb888_esp"
    )
    
    foreach(symbol ${FORCE_SYMBOLS})
        set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${symbol})
    endforeach()

    # 4. Header Injection (The Circular Dependency Fix)
    # LVGL needs to see our 'include' directory to find lv_draw_sw_asm_custom.h
    # but it doesn't know about this component. We manually inject it.
    idf_component_get_property(lvgl_lib "lvgl__lvgl" COMPONENT_LIB)
    if(lvgl_lib)
        target_include_directories(${lvgl_lib} PUBLIC "include")
        message(STATUS "[SIMD Patch] Injected include path into ${lvgl_lib}")
    else()
        message(WARNING "[SIMD Patch] Could not find lvgl__lvgl library target to inject headers.")
    endif()
    
else()
    message(STATUS "[SIMD Patch] Target is not ESP32-S3. Patch disabled.")
endif()
