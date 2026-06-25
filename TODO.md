# TODO

## Networking

- Add ESP-NOW transport investigation and design.
  - Decide whether ESP-NOW is a peer Tool transport, an EventBus transport, or both.
  - Keep ToolRegistry as the only execution entry point.
  - Candidate events: `espnow.peer.connected`, `espnow.message.received`, `espnow.tool.invoke.requested`.
  - Candidate tools: `espnow.scanPeers`, `espnow.send`, `espnow.broadcast`.

## Storage

- Add optional `StorageSdCard` HAL implementation later.
  - Keep internal Flash FATFS as the default v1 storage.
  - Use microSD for large assets, long logs, face packs, and memory snapshots when available.
  - Preserve the same `IStorage` and `storage.*` Tool API across both backends.

## State / Query

- Add aggregate state query tools.
  - `system.status`: compact runtime status as a Tool.
  - `system.snapshot`: combined power, motion, face, storage, network, and tool metadata.
- Keep query tools read-only unless explicitly marked otherwise.
- Consider adding `network.status` after STA Wi-Fi config exists.

## Avatar / Face

- Attach the real M5GFX/StackChan face renderer to `AvatarService`.
- Add mac_simulator SDL/GFX renderer for the same `AvatarService` state.
- Move face expression parameters into JSON-configurable assets.
- Consider compatibility aliases only if needed:
  - `avatar.setExpression` -> `face.setExpression`
  - `avatar.setVowel` -> `face.setVowel`
  - `avatar.setCaption` -> `face.setCaption`
  - `avatar.setPalette` -> `face.setPalette`
- Keep Web UI / Workflow / Agent access routed through ToolRegistry.

## Workflow

- Add conditionals and simple variable interpolation.
- Add EventBus-triggered workflow execution.
- Add workflow author metadata and trust levels for stored workflows.
