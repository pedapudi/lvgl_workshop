---
name: integrate_external_library
description: How to integrate third-party libraries that are not native ESP-IDF components (Adapter Pattern).
---

# Integrating Incompatible External Libraries (Adapter Pattern)

Use this skill when a library you want to use contains a `CMakeLists.txt` with `project()` or other logic incompatible with the ESP-IDF component build system.

## The Problem
ESP-IDF's build system attempts to include every folder in `components/` via `add_subdirectory()`. If a library treats itself as a standalone project (calls `project()`), CMake will error out.

## The Solution: The Adapter Pattern

We place the library in `external/` (ignored by IDF) and create a "shim" component in `components/` to compile it.

### Step 1: Clone to `external/`
Do not put it in `components/`.
```bash
mkdir -p external
git submodule add <repo_url> external/<lib_name>
```

### Step 2: Create the Adapter Component
Create a standard component structure:
```
components/
  <lib_name>_adapter/
    CMakeLists.txt
    idf_component.yml (optional)
```

### Step 3: Write the Adapter CMakeLists.txt
This script manually finds the source files in `external/` and registers them.

```cmake
# components/<lib_name>_adapter/CMakeLists.txt

# 1. Glob sources from the external directory
file(GLOB_RECURSE SOURCES 
    "../../external/<lib_name>/src/*.c"
    "../../external/<lib_name>/src/*.cpp"
)

# 2. Register as a component
idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "../../external/<lib_name>/include"
                                    "../../external/<lib_name>/src"
                       REQUIRES <any_dependencies>)

# 3. Apply necessary flags (e.g., C++ standards or warning suppressions)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-cast-function-type)
```

### Step 4: Use in Main
Add the adapter to your `main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.cpp"
                       PRIV_REQUIRES <lib_name>_adapter ...)
```

## Troubleshooting
*   **Include Paths**: Ensure `INCLUDE_DIRS` covers all folders containing headers you need.
*   **Compile Errors**: You may need to disable specific warnings if the external library isn't strict.
66: 
67: ## C++ Wrapper Libraries (e.g., lvgl_cpp)
68: When integrating a C++ wrapper for a C component:
69: 1.  Ensure the underlying C component (e.g., `lvgl`) is imported normally (e.g., via `idf_component.yml`).
70: 2.  The adapter component (`lvgl_cpp_adapter`) should `REQUIRE` the C component.
71: 3.  If the wrapper uses templates or extensive inline code, ensure `INCLUDE_DIRS` is comprehensive.
72: 4.  **C++20**: Many modern wrappers require C++20. Ensure `target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)` is set.
