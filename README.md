# HardwareOneAuthMinimal

# HardwareOnev2

Minimal ESP32 Arduino sketch implementing a basic web interface protected by HTTP Basic Authentication using the native `WebServer` class.

## Features

- Protected root route `/` requiring HTTP Basic Auth
- Public route `/public`
- Hardcoded WiFi credentials for simplicity
- Zero external dependencies beyond the ESP32 Arduino core

## Files

- `HardwareOnev2.ino` — main sketch

## Requirements

- Arduino IDE (or Arduino CLI)
- ESP32 board support installed (Boards Manager: `esp32` by Espressif Systems)

## Quick Start

1. Open `HardwareOnev2/HardwareOnev2.ino` in Arduino IDE.
2. Edit WiFi and auth constants at the top of the sketch:
   - `WIFI_SSID`, `WIFI_PASS`
   - `AUTH_USER`, `AUTH_PASS`
3. Select your board (e.g., `ESP32 Dev Module`) and the correct serial port.
4. Upload the sketch.
5. Open Serial Monitor at 115200 baud to see the assigned IP.
6. Visit `http://<esp32-ip>/` in your browser. You will be prompted for credentials.
7. Public endpoint available at `http://<esp32-ip>/public`.

## Notes

- HTTP Basic Auth is simple and not encrypted. Use only on trusted networks unless you add TLS/HTTPS (requires additional setup not included here).
- To change what is protected, adjust handlers in `setup()` (e.g., require auth on additional routes).
- If WiFi connection times out, the server still starts; the IP will become valid once WiFi connects.

## License

MIT
