---
name: manage_esp_idf_components
description: Best practices for adding and managing libraries in ESP-IDF projects.
---

# Managing ESP-IDF Components

Use this skill when adding new libraries or drivers to an ESP-IDF project.

## Decision Matrix

| Library Type | Recommended Method | Command/Action |
| :--- | :--- | :--- |
| **Common/Standard** (e.g., `lvgl`, `button`, `led_strip`) | **Managed Component** | `idf_component.yml` |
| **Custom/Third-Party** (Correctly formatted) | **Git Submodule** | `git submodule add <url> components/<name>` |
| **Incompatible** (Has `project()` in CMake) | **External + Adapter** | See `integrate_external_library` skill |

## 1. Using Managed Components (Preferred)
The ESP-IDF Component functionality allows you to declare dependencies in `main/idf_component.yml`.

1.  **Check Registry**: Search [components.espressif.com](https://components.espressif.com/).
2.  **Create/Edit Manifest**: `main/idf_component.yml`
    ```yaml
    dependencies:
      lvgl/lvgl: "^9.0"
      espressif/led_strip: "^2.0"
    ```
3.  **Build**: Run `idf.py build`. The build system automatically downloads dependencies to `managed_components/`.
    *   *Note*: Do NOT modify files in `managed_components/`. They are ephemeral.

## 2. Using Git Submodules (Standard Components)
Use this for libraries that are structured as proper ESP-IDF components (have `idf_component_register` in `CMakeLists.txt` and NO `project()` call).

1.  **Add Submodule**:
    ```bash
    mkdir -p components
    git submodule add https://github.com/user/lib.git components/lib_name
    ```
2.  **Build**: `idf.py build`. IDF automatically finds submodules in `components/`.

## 3. Common Pitfalls
*   **Duplicate Git Repos**: Do not `git submodule add` into a directory that is already a git repo.
*   **Incompatible CMake**: If a library has `project(name)` in its `CMakeLists.txt`, it CANNOT be in `components/`. It will break the top-level build. Use the **Adapter Pattern** (see `integrate_external_library`).
