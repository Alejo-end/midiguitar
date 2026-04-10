---
name: arduino
description: Arduino development helper — compile, upload, monitor, and debug sketches on ESP32-S3
user_invocable: true
---

# Arduino Development Skill

You are helping with Arduino/ESP32-S3 development. Follow the user's instruction and use the context below.

## Board Configuration
- Board: **Seeed XIAO ESP32-S3**
- USB Mode: **USB-OTG (TinyUSB)**
- Arduino CLI or IDE compilation
- Common serial ports: `/dev/cu.usbmodem*`

## Available Actions

Based on the user's request, perform ONE of the following:

### If "compile" or "verify":
1. Find the `.ino` file in the current project or the path the user specifies
2. Run: `arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 --build-property "build.extra_flags=-DARDUINO_USB_MODE=0 -DARDUINO_USB_CDC_ON_BOOT=0" <sketch_dir>`
3. Report errors clearly — show the file, line number, and a fix suggestion for each error

### If "upload" or "flash":
1. List available ports: `ls /dev/cu.usbmodem* 2>/dev/null`
2. If no port found, tell the user to hold BOOT, press RESET, release BOOT
3. Upload: `arduino-cli upload --fqbn esp32:esp32:XIAO_ESP32S3 -p <port> <sketch_dir>`

### If "monitor" or "serial":
1. Tell the user to run: `! arduino-cli monitor -p <port> -c baudrate=115200` (they need to run it interactively)

### If "install" or "library":
1. Search: `arduino-cli lib search <query>`
2. Install: `arduino-cli lib install "<library_name>"`

### If "debug" or "fix":
1. Read the sketch file
2. Look for common ESP32-S3 issues:
   - Wrong GPIO numbers (XIAO D-pin vs raw GPIO mapping)
   - I2C not initialized before sensor begin
   - USB/BLE init order issues
   - Missing `INPUT_PULLUP` on buttons/encoder
   - ISR functions missing `IRAM_ATTR`
   - Volatile variables not used for ISR-shared state
3. Suggest fixes with specific line numbers

### If "pins" or "pinout":
Show the XIAO ESP32-S3 pin mapping:
```
XIAO Pin | GPIO | Common Use
---------|------|----------
D0       | 1    | Spare
D1       | 2    | Digital out
D2       | 3    | Digital in
D3       | 4    | Digital in
D4       | 5    | I2C SDA
D5       | 6    | I2C SCL
D6       | 43   | Digital out
D7       | 44   | Digital out
D8       | 7    | Digital/interrupt
D9       | 8    | Digital/interrupt
D10      | 9    | Digital in
```

## Code Style Rules
- Use `static` for file-scope globals
- Use `constrain()` not manual clamping
- Use `INPUT_PULLUP` for buttons/switches (active LOW)
- ISR functions need `IRAM_ATTR` and `volatile` shared vars
- Prefer `#define` for pin numbers, not `const int`
- Keep `.ino` files self-contained (single file preferred for small projects)
- Use `millis()` for timing, never `delay()` in `loop()`
