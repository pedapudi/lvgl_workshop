---
name: Report Issue to lvgl_cpp
description: Guide for filing GitHub issues for the lvgl_cpp library.
---

# Report Issue to lvgl_cpp

Use this skill when you encounter a clear bug, usability issue, or missing feature in the `lvgl_cpp` library during development.

## Steps

1.  **Verify the Issue**
    *   Ensure the issue is not caused by your own code or configuration.
    *   Reproduce the issue with a minimal code example if possible.
    *   Check if the issue is already strictly in upstream LVGL (C library). If so, it might not be an `lvgl_cpp` issue unless `lvgl_cpp` prevents the fix.

2.  **Check Existing Issues**
    *   Use `gh issue list --repo pedapudi/lvgl_cpp --search "query"` (if `gh` available) or `search_web` to ensure the issue hasn't been reported.
    *   Repository URL: https://github.com/pedapudi/lvgl_cpp

3.  **Draft the Issue**
    *   **Title**: Clear and descriptive (e.g., "Compilation error with gcc 11 due to missing include").
    *   **Body**:
        *   **Description**: What happened?
        *   **Reproduction**: Minimal code snippet.
        *   **Environment**: ESP-IDF version, LVGL version.
        *   **Logs**: Relevant compiler error or runtime crash log.

4.  **Action**
    *   **Check `gh` CLI**: Run `gh auth status` to verify GitHub CLI authentication.
    *   **If authenticated**:
        *   Submit the issue directly: `gh issue create --repo pedapudi/lvgl_cpp --title "TITLE" --body "BODY"`.
        *   Notify the user that the issue has been reported (include the link).
    *   **If NOT authenticated**:
        *   Draft the issue in a markdown block.
        *   Notify the user and ask them to post it to https://github.com/pedapudi/lvgl_cpp/issues.
