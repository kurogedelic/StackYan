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
- Face/Avatar state tools exposed through ToolRegistry
- JSON config tools for `/stackyan/config.json`
- Minimal sequential JSON Workflow tools

Not implemented in this step:

- LLM / Agent
- XiaoZhi
- TTS / STT
- Avatar / face rendering attachment
- Workflow conditionals / loops / event triggers
- Dynamic plugins
- Lua / JIT
- OTA

## Tools

- `rgb.set`
- `rgb.clear`
- `servo.move`
- `servo.home`
- `servo.readState`
- `servo.disableTorque`
- `servo.stop`
- `motion.read`
- `power.status`
- `storage.info`
- `storage.exists`
- `storage.list`
- `storage.mkdir`
- `storage.read`
- `storage.write`
- `storage.remove`
- `config.read`
- `config.write`
- `config.patch`
- `config.apply`
- `workflow.list`
- `workflow.read`
- `workflow.write`
- `workflow.wait`
- `workflow.validate`
- `workflow.run`
- `face.getState`
- `face.setExpression`
- `face.setVowel`
- `face.setCaption`
- `face.clearCaption`
- `face.setPalette`
- `face.reset`

Servo tools are marked `dangerous` in schema metadata.

### State / Query Tools

These tools do not primarily send control commands; they return current device, runtime, storage, config, or workflow state.

- `motion.read`
- `power.status`
- `servo.readState`
- `storage.info`
- `storage.exists`
- `storage.list`
- `storage.read`
- `config.read`
- `face.getState`
- `workflow.list`
- `workflow.read`
- `workflow.validate`

Planned aggregate query tools:

- `system.status`
- `system.snapshot`

`system.snapshot` should return a single combined view of power, motion, face state, storage, network, and tool/runtime metadata for Web UI and Agent clients.

## API

All APIs are unauthenticated in this first implementation and are intended only for trusted local LAN/development use.

### `GET /api/status`

Returns device/runtime status, storage mount status, and tool count.

### `GET /api/capabilities`

Returns StackYan role and v1 disabled areas.

### `GET /api/events`

Returns recent in-memory events.

This is the main query path for runtime history, including `tool.invoked`, `workflow.started`, `workflow.step`, and `workflow.finished`.

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

The Web UI includes a Face Tools panel. It invokes `face.*` tools through the same `POST /api/invoke` path as every other client. The current firmware stores face state only; the real avatar renderer is intentionally attached later.

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

- RGB state is shown in the Web UI and printed to the console.
- Servo horizontal/vertical angles are retained and shown in the Web UI.
- Motion returns deterministic dummy values.
- Power returns deterministic dummy battery values.
- Storage maps to the local `./sd` folder.
- SDL/GFX window is not implemented yet; the current simulator uses HTTP UI plus console output.

Simulator-only state endpoint:

```text
GET /api/sim/state
```

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
9. Run `storage.info`.
10. Run `storage.write`, `storage.read`, and `storage.list` under `/stackyan/tools/smoke`.
11. Run `config.read`.
12. Run `config.patch` with a small `avatar` object, then `config.apply`.
13. Run `workflow.write` with a small workflow containing `rgb.set`, `workflow.wait`, and `face.setExpression`.
14. Run `workflow.validate` for that workflow.
15. Run `workflow.run` for that workflow.
16. For workflows containing dangerous tools such as `servo.move`, confirm `workflow.validate` first and pass `allow_dangerous: true` only after checking hardware safety.
17. Run `rgb.set` with low brightness.
18. Run `servo.move` only after confirming power, servo pins, and safe range.
19. Run `face.setExpression` and confirm the Tool result changes state. Face rendering is not attached yet.

## Storage Layout

Created under HAL storage root:

```text
/stackyan/config.json
/stackyan/tools/
/stackyan/workflows/
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
- microSD is not used yet; current storage is internal Flash FATFS via wear levelling.
