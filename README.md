# StackYan

StackYan is an ESP32 physical Tool Server for M5Stack StackChan/CoreS3.

StackYan is a local-first physical tool runtime. It exposes hardware capabilities through a unified Tool API so that humans, web applications, workflows, and AI agents all interact with the device in exactly the same way.

The center of the system is `ToolRegistry`, not an Agent. Humans use the same tools from the local Web UI, and future AI/LLM clients can call the same HTTP API.

All external execution paths must enter through `ToolRegistry`. Web UI, LLM/Agent clients, future Workflows, and future CLI clients must not call HAL or Services directly.

## Current Scope

Implemented:

- Existing `stackchu_hal` component import
- Tool base interfaces
- `ToolRegistry`
- Built-in HAL-backed tools
- ESP-IDF HTTP server
- RPC-style `POST /api/invoke`
- mDNS hostname `stackyan.local`
- Minimal Web Tool Test UI
- Minimal in-memory EventBus with `tool.invoked` events
- `/stackyan` storage layout creation
- Serial logging and `/stackyan/logs/tool.log` append

Not implemented in this step:

- LLM / Agent
- XiaoZhi
- TTS / STT
- Avatar / face rendering
- JSON Workflow
- Dynamic plugins
- Lua / JIT
- OTA

## Tools

- `rgb.set`
- `rgb.clear`
- `servo.move`
- `servo.home`
- `motion.read`
- `power.status`
- `storage.read`
- `storage.write`

Servo tools are marked `dangerous` in schema metadata.

## API

All APIs are unauthenticated in this first implementation and are intended only for trusted local LAN/development use.

### `GET /api/status`

Returns device/runtime status, storage mount status, and tool count.

### `GET /api/capabilities`

Returns StackYan role and v1 disabled areas.

### `GET /api/events`

Returns recent in-memory events.

### `GET /api/tools`

Returns all registered tool schemas.

### `GET /api/tools/{name}`

Returns one tool schema.

### `POST /api/invoke`

Invokes a tool with an RPC-style body.

```json
{
  "tool": "rgb.set",
  "args": {
    "r": 0,
    "g": 80,
    "b": 255,
    "brightness": 64
  }
}
```

This is the primary invocation API.

### `POST /api/tools/{name}`

Compatibility endpoint. It invokes the same ToolRegistry path with the request body as args.

Example:

```sh
curl -X POST http://stackyan.local/api/invoke \
  -H 'Content-Type: application/json' \
  -d '{"tool":"rgb.set","args":{"r":0,"g":80,"b":255,"brightness":64}}'
```

Response:

```json
{
  "ok": true,
  "tool": "rgb.set",
  "elapsed_ms": 12,
  "result": {
    "applied": true
  }
}
```

## Web UI

Open:

```text
http://stackyan.local/
```

If mDNS does not resolve while using the setup AP, connect to `StackYan-Setup` and use:

```text
http://192.168.4.1/
```

## Build

### ESP-IDF firmware

```sh
source /Users/kurogedelic/esp/esp-idf/export.sh
idf.py build
```

The current build target is ESP32-S3/CoreS3.

### mac_simulator

The macOS simulator runs the same ToolRegistry and built-in Tool implementations against simulated HAL interfaces.

Requirements:

- CMake
- Homebrew `cjson`

Build:

```sh
cmake -S simulator -B build-sim
cmake --build build-sim -j
```

Run:

```sh
./build-sim/stackyan_sim
```

Open:

```text
http://localhost:8080/
```

Simulator storage lives under:

```text
./sd/stackyan/
```

Simulator HAL behavior:

- RGB state is printed to the console.
- Servo horizontal/vertical angles are retained and printed.
- Motion returns deterministic dummy values.
- Power returns deterministic dummy battery values.
- Storage maps to the local `./sd` folder.
- SDL/GFX window is not implemented yet; the current simulator uses HTTP UI plus console output.

## Flash

```sh
source /Users/kurogedelic/esp/esp-idf/export.sh
idf.py -p PORT flash monitor
```

Replace `PORT` with the StackChan/CoreS3 serial port.

## First Hardware Test

1. Flash and open serial monitor.
2. Confirm `StackYan booting...`.
3. Confirm HAL begin logs.
4. Confirm `StackYan ready: http://stackyan.local/`.
5. Connect to `StackYan-Setup`.
6. Open `http://stackyan.local/` or `http://192.168.4.1/`.
7. Run `motion.read`.
8. Run `power.status`.
9. Run `rgb.set` with low brightness.
10. Run `servo.move` only after confirming power, servo pins, and safe range.

## Storage Layout

Created under HAL storage root:

```text
/stackyan/config.json
/stackyan/tools/
/stackyan/logs/
/stackyan/memory/
```

Tool calls append to:

```text
/stackyan/logs/tool.log
```

## Known Hardware Adjustment Points

- RGB data pin is still inherited from `stackchu_hal` and marked as provisional.
- IR TX differs between references: official example uses GPIO5, local HAL uses GPIO44 provisional.
- Servo UART TX/RX are provisional in `HalPins.h`.
- Servo SCS/STS register map may need adjustment for the actual motor model.
- Backlight path may be AXP2101 or GPIO fallback depending on board behavior.
- Current network path starts an open setup AP; STA Wi-Fi config is not implemented yet.
