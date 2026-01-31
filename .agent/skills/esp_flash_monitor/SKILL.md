---
name: ESP Flash and Monitor
description: Safe workflow for flashing and monitoring ESP32 without leaving hanging processes.
---

# ESP Flash and Monitor

Use this skill to build, flash, and monitor the ESP32 application. This ensures strict adherence to the "no hanging processes" rule.

## 1. Build
*   Always ensure the project builds before trying to flash.
*   Command: `idf.py build`
*   **Troubleshooting**: If `idf.py` is not found, try invoking `get_idf` first (if available as an alias) or source the export script: `. $HOME/esp-idf/export.sh`.

## 2. Flash
*   Flash the project to the board.
*   Command: `idf.py -p <PORT> flash`
*   *Note: If port is unknown, list /dev/tty* to find it.*

## 3. Monitor (Safe Mode)
*   **CRITICAL**: Do not run `idf.py monitor` effectively "forever" in a background task without a plan to kill it.
*   **Recommendation**: Run monitor with a timeout to capture startup logs, then exit.
*   **Command Pattern**:
    ```bash
    timeout 10s idf.py -p <PORT> monitor
    ```
    *This captures 10 seconds of output (enough to see Hello World) and then self-terminates.*

## 4. Analysis & Troubleshooting
*   Read the output of the monitor command.
*   **Hardware Verification**: If you see errors like `I2C transaction failed`, `invalid response`, or `timeout`:
    *   **CHECK PINS**: Verify the pin numbers in code match the specific hardware revision (e.g., Xiao ESP32C3 vs DevKitM-1).
    *   **External Factors**: Check wiring, pull-up resistors, and strapping pins.
*   Check for "Hello, World" or specific success criteria.
*   Check for stack traces or valid I2C initialization logs.
