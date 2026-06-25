# StackYan v1 設計プラン

## 0. 設計の芯

StackYan v1 は、M5Stack StackChan 系ハードウェア上で動く「物理 Tool Server」として設計する。

中心に置くのは Agent ではなく `ToolRegistry`。人間の Web UI、ローカル REST API、将来の XiaoZhi / Claude / GLM / その他 LLM Agent は、すべて同じ Tool API の利用者として扱う。

v1 の目的は「AI キャラクターを作る」ことではなく、「物理デバイス上の安全で観測可能な Tool 実行基盤を作る」こと。

## 1. v1 全体アーキテクチャ

```text
Human Browser
  |
  |  stackyan.local
  v
Web Tool Test UI
  |
  v
Local API Server  <---- future Agent clients
  |
  v
ToolRegistry
  |
  +-- C++ Tool: servo.move
  +-- C++ Tool: rgb.set
  +-- C++ Tool: motion.read
  +-- C++ Tool: power.status
  +-- C++ Tool: ir.send
  +-- C++ Tool: storage.list
  +-- JSON Workflow Tool: workflow.run:name
  |
  v
Service Layer
  |
  v
HAL Layer
  |
  v
M5Stack / StackChan hardware
```

v1 では Tool 呼び出しの経路を一本化する。

- Web UI からの手動テスト
- curl などからの REST 実行
- 将来の LLM / Agent 実行
- JSON Workflow からの内部実行

これらをすべて `ToolRegistry.call(name, input)` に収束させる。

設計決定:

- Web UI, LLM/Agent, Workflow, CLI は必ず ToolRegistry を入口にする
- HAL / Service を外部層から直接呼ぶ経路を作らない
- Tool 呼び出し API は `POST /api/invoke` を primary にする
- `POST /api/tools/{name}` は互換用の薄いラッパーに留める
- Tool schema は `name`, `title`, `description`, `parameters` を持ち、OpenAI Function Calling 互換に近づける
- イベントは EventBus に集約し、Workflow / Agent / WebSocket は将来 EventBus を購読する

将来のイベント経路:

```text
HAL / Services
  |
  v
EventBus
  |
  +-- Workflow
  +-- Agent
  +-- WebSocket
  +-- Web UI
```

イベント例:

- `motion.changed`
- `audio.threshold`
- `button.a`
- `wifi.connected`
- `power.low`
- `tool.invoked`

## 2. レイヤ構造

### 2.1 HAL

HAL はハードウェアの薄い抽象化層。

StackYan v1 では、既存の StackChu HAL 実装案を第一候補として取り込む。

参照元:

```text
/Users/kurogedelic/Desktop/Stackchan_docs/components/stackchu_hal
```

既存 HAL は `stackchu::Hal` ファサードを持ち、interface と実装が分離されている。

- `IBacklight` / `BacklightAxp2101`
- `IServo` / `ServoFtBus`
- `IRgb` / `RgbSk6812`
- `IMotionSensor` / `MotionBmi270`
- `IPower` / `PowerAxp2101`
- `IIr` / `IrNec`
- `IStorage` / `StorageFatFs`

StackYan 側では HAL を再発明せず、まず `stackchu::Hal` を `HardwareService` または各 Service の依存として包む。

責務:

- ピン、I2C、PWM、GPIO、ADC、IR、Storage、電源状態などの低レベル操作
- M5Stack / StackChan の機種差分吸収
- 実機がない場合の stub / mock 実装

StackYan 側で扱う HAL interface:

- `stackchu::IServo`
- `stackchu::IRgb`
- `stackchu::IMotionSensor`
- `stackchu::IPower`
- `stackchu::IIr`
- `stackchu::IStorage`
- `stackchu::IBacklight`
- Display / Button は v1 の必要に応じて追加する

HAL は JSON や REST を知らない。Tool 名も知らない。純粋に C++ API として扱う。

既存 HAL の重要な性質:

- `Hal::begin()` は Backlight, Power, Storage, Motion, RGB, IR, Servo の順で初期化する
- Servo は最後に初期化される
- 各サブシステムの begin/mount に失敗しても全体起動は継続する
- Storage は `begin()` ではなく `mount()`
- Servo は FEETech バスサーボ前提で、垂直軸 5-85 度クランプと過熱時 torque off を持つ
- RGB は SK6812 12 個で、輝度上限 `RGB_BRIGHTNESS_LIMIT = 0.4`
- Motion は BMI270 で、加速度/ジャイロ値を公式最大レンジにクランプする
- Storage は現状 FATFS + wear levelling の `/storage` mount

要検証点:

- `HalPins.h` の `RGB_DIN`, `IR_TX_PIN`, `SERVO_UART_TX/RX`, `BACKLIGHT_PWM` は要実機/BSP 確認
- `ServoFtBus.cpp` の SCS/STS レジスタ配置はサーボ機種差分があるため実機で read/write 確認が必要
- `PowerAxp2101.cpp` の電圧換算コメントに実機確認前提が残っている
- Storage は `format_if_mount_failed = true` なので、初期開発では便利だが、v1 安定版では破壊的フォーマット方針を再確認する

### 2.2 Service

Service は HAL を使って「意味のある操作」にまとめる層。

例:

- `ServoService`
  - 可動範囲制限
  - 速度制限
  - 現在姿勢の保持
  - neutral / lookLeft / lookRight などの抽象姿勢
- `RgbService`
  - 色名、明るさ、アニメーション状態
- `PowerService`
  - バッテリー、USB給電、低電圧判定
- `StorageService`
  - `/storage` 上の JSON / log / preset / workflow access
- `AvatarService`
  - 表情、表示テキスト、目線、口パクなど

Service はハードウェア保護の中心。Tool から直接 HAL を叩かない。

ただし StackChu HAL はすでに一部の安全機構を持っている。StackYan の Service 層は、それを上書きするのではなく追加の policy と状態管理を担当する。

例:

- `ServoService`
  - HAL の 5-85 度クランプを前提に、StackYan 側の pose 名、速度制限、cooldown、emergency stop を追加する
- `RgbService`
  - HAL の輝度上限を前提に、プリセット色、アニメーション、UI 表示名を追加する
- `StorageService`
  - HAL の `/storage` ファイル API を前提に、JSON read/write、atomic write、schema version、ログローテーションを追加する
- `PowerService`
  - HAL の PMU read/monitor を前提に、Tool 向けの簡潔な status JSON に変換する

### 2.3 Tool

Tool は外部に公開できる最小の操作単位。

例:

- `servo.setPose`
- `servo.move`
- `rgb.setColor`
- `motion.read`
- `power.status`
- `ir.send`
- `storage.list`
- `storage.readText`
- `avatar.setExpression`
- `avatar.say`
- `workflow.run`

Tool は C++ 実装を基本とする。v1 では動的バイナリプラグイン、Lua、JIT は扱わない。

### 2.4 ToolRegistry

StackYan の中心。

責務:

- Tool の登録
- Tool schema の提供
- Tool 呼び出しのディスパッチ
- 入力 JSON の検証
- 実行結果の統一
- エラー形式の統一
- 実行ログの記録
- permission / safety metadata の保持
- Workflow Tool の登録

重要な方針:

- Agent 専用 API にしない
- Web UI も Agent も同じ registry を使う
- Workflow も Tool と同じ形で登録できる
- Tool の discovery は `GET /api/tools` に集約する

### 2.5 Local API Server

ESP32 上のローカル HTTP サーバー。

責務:

- `GET /api/tools`
- `POST /api/tool/{name}`
- `GET /api/status`
- Web UI の静的ファイル配信
- 設定画面
- mDNS 名 `stackyan.local` の提供

v1 では外部公開を前提にしない。LAN 内の開発・テスト用 API として扱う。

### 2.6 Web Tool Test UI

人間が Tool を直接確認するための UI。

v1 の最小機能:

- Tool 一覧表示
- schema 表示
- JSON input の編集
- 実行ボタン
- response / error 表示
- device status 表示
- Wi-Fi / mDNS / device name などの設定画面

重要なのは、Web UI が特別な内部 API を使わないこと。必ず `GET /api/tools` と `POST /api/tool/{name}` を使う。

### 2.7 Avatar

Avatar は ToolRegistry の利用者であり、必要なら Service を持つ。

v1 では最小限でよい。

- 表情を変える
- 短いテキストを表示する
- 簡単な状態アイコンを出す

AI との会話や音声対話は v1 の中心ではない。

Avatar Tool 例:

- `avatar.setExpression`
- `avatar.showText`
- `avatar.clear`

### 2.8 Memory

Memory は Agent の長期記憶ではなく、まずはデバイス状態・設定・ログ・Workflow 保存場所として扱う。

v1 の Memory:

- device config
- Wi-Fi config
- tool execution log
- saved workflow JSON
- user presets
- servo calibration
- RGB presets
- IR codes

保存先は StorageService 経由にする。

### 2.9 Agent

v1 では Agent は実装しない。

ただし設計上は以下を想定しておく。

- XiaoZhi
- Claude
- GLM
- ブラウザ上の人間
- ローカルスクリプト
- 将来のオンデバイス LLM

Agent は `GET /api/tools` で schema を見て、`POST /api/tool/{name}` で Tool を呼ぶクライアントである。

## 3. 最小実装の順番

### Phase 0: Skeleton

目的: ToolRegistry 中心の骨格を作る。

1. ESP-IDF component 方式または既存 StackChu HAL が動くビルド方式を決める
2. `components/stackchu_hal` を取り込む、または git subtree/submodule 相当で参照する
3. `stackchu::Hal hal; hal.begin();` まで起動する
4. Serial log
5. `Tool`, `ToolRegistry`, `ToolResult` の C++ 型
6. fake Tool `system.ping`
7. `GET /api/status`
8. `GET /api/tools`
9. `POST /api/tool/system.ping`

この段階では、HAL の begin は呼んでもよいが、サーボを動かす Tool は作らない。

### Phase 0.5: HAL bring-up verification

目的: StackChu HAL を StackYan の土台として使えるか確認する。

1. `HalPins.h` の要確認ピンを BSP / 実機で照合
2. `GET /api/status` に HAL サブシステムの初期化結果を出す
3. `power.status` 相当の read-only Tool を先に作る
4. `motion.read` 相当の read-only Tool を作る
5. Storage mount と `/storage` 書き込みを確認する
6. RGB を低輝度で点灯確認する
7. Servo はまだ neutral/readState までに留める

### Phase 1: First Hardware Tools

目的: 物理 Tool Server として実機を動かす。

1. StackChu HAL の `IRgb` を使う `RgbService`
2. `rgb.setColor`
3. StackChu HAL の `IPower` を使う `PowerService`
4. `power.status`
5. StackChu HAL の `IMotionSensor` を使う `motion.read`
6. Web UI から RGB Tool を実行

最初は危険度が低い RGB と status から始める。

### Phase 2: Servo Safety

目的: StackChan 系の可動部を安全に扱う。

1. StackChu HAL の `IServo` / `ServoFtBus` を使う
2. `ServoService`
3. calibration config
4. min / max / neutral
5. `servo.setPose`
6. `servo.move`
7. `servo.stop` / `servo.disableTorque`
8. Web UI に safety hint を表示

Servo は最初から制限、初期姿勢、停止手段を入れる。

### Phase 3: Storage / Config / Web UI

目的: 再起動しても使える Tool Server にする。

1. StackChu HAL の `IStorage` / `StorageFatFs` を使う
2. `StorageService`
3. `/storage/config/device.json`
4. `/storage/workflows/*.json`
5. `/storage/presets/*.json`
6. Web 設定 UI
7. mDNS `stackyan.local`

### Phase 4: Workflow as Tool

目的: データ定義の Workflow を ToolRegistry に載せる。

1. Workflow JSON schema
2. Workflow loader
3. `workflow.run`
4. Workflow を擬似 Tool として `GET /api/tools` に出す
5. Web UI から Workflow 実行

### Phase 5: Agent-ready API

目的: 外部 Agent が呼びやすい形に整える。

1. schema の安定化
2. error code 整理
3. tool execution log
4. request id
5. permission metadata
6. OpenAPI 風 JSON export の検討

## 4. Tool インターフェース設計

### 4.1 C++ 側の概念

Tool は以下を持つ。

- name
- title
- description
- input schema
- output schema
- permissions
- timeout
- dangerous flag
- handler

概念例:

```cpp
class Tool {
public:
  virtual const ToolSchema& schema() const = 0;
  virtual ToolResult call(const JsonVariantConst& input, ToolContext& ctx) = 0;
};
```

`ToolContext` には以下を入れる。

- services
- storage
- logger
- request id
- caller type
- dry run flag

### 4.2 ToolResult

すべての Tool は同じ result 形式を返す。

成功:

```json
{
  "ok": true,
  "result": {
    "message": "pong"
  }
}
```

失敗:

```json
{
  "ok": false,
  "error": {
    "code": "INVALID_ARGUMENT",
    "message": "angle is out of range",
    "details": {
      "field": "angle",
      "min": -45,
      "max": 45
    }
  }
}
```

### 4.3 Tool 名の規則

`domain.action` を基本にする。

例:

- `system.ping`
- `system.restart`
- `power.status`
- `rgb.setColor`
- `servo.setPose`
- `servo.move`
- `avatar.showText`
- `storage.list`
- `workflow.run`
- `workflow.demoWave`

大文字小文字は固定し、URL ではそのまま `{name}` に使えるようにする。

## 5. Tool schema の形式

JSON Schema 風にする。ただし v1 では完全な JSON Schema 実装を目指さない。

`GET /api/tools` の例:

```json
{
  "tools": [
    {
      "name": "rgb.setColor",
      "title": "Set RGB color",
      "description": "Set the built-in RGB LED color.",
      "parameters": {
        "type": "object",
        "required": ["r", "g", "b"],
        "properties": {
          "r": { "type": "integer", "minimum": 0, "maximum": 255 },
          "g": { "type": "integer", "minimum": 0, "maximum": 255 },
          "b": { "type": "integer", "minimum": 0, "maximum": 255 },
          "brightness": { "type": "integer", "minimum": 0, "maximum": 255, "default": 64 }
        }
      },
      "output_schema": {
        "type": "object",
        "properties": {
          "applied": { "type": "boolean" }
        }
      },
      "permissions": ["hardware.rgb"],
      "dangerous": false,
      "timeout_ms": 500
    }
  ]
}
```

v1 の schema 対応範囲:

- `type`: object, string, integer, number, boolean, array
- `required`
- `properties`
- `default`
- `minimum`
- `maximum`
- `enum`
- `description`

高度な `$ref`, `oneOf`, `anyOf`, `patternProperties` は v1 では不要。

## 6. Local API 案

### 6.1 GET /api/status

デバイスとサーバーの状態を返す。

```json
{
  "device": {
    "name": "stackyan",
    "model": "m5stack-core2",
    "firmware": "0.1.0",
    "uptime_ms": 123456
  },
  "network": {
    "hostname": "stackyan",
    "mdns": "stackyan.local",
    "ip": "192.168.1.20",
    "wifi_connected": true
  },
  "storage": {
    "mounted": true,
    "backend": "fatfs-wl",
    "root": "/storage",
    "free_bytes": 10485760
  },
  "hal": {
    "backlight": true,
    "power": true,
    "storage": true,
    "motion": true,
    "rgb": true,
    "ir": false,
    "servo": false
  },
  "tools": {
    "count": 8
  }
}
```

### 6.2 GET /api/tools

登録済み Tool の schema を返す。

Query option:

- `?include=all`
- `?domain=servo`
- `?dangerous=false`

最初は query option なしでよい。

### 6.3 GET /api/tools/{name}

単一 Tool の schema を返す。

### 6.4 POST /api/invoke

Primary API。Tool を RPC 形式で実行する。

Request:

```json
{
  "tool": "rgb.set",
  "args": {
    "r": 255,
    "g": 32,
    "b": 0,
    "brightness": 64
  }
}
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

理由:

- MCP / OpenAI Tool Calling / Claude Tool / XiaoZhi の `{ tool, args }` 形式に近い
- HTTP endpoint が増えすぎない
- Workflow からの内部呼び出しも同じ形にできる

### 6.5 POST /api/tools/{name}

互換 API。body 全体を args として扱い、内部では `POST /api/invoke` と同じ ToolRegistry 実行経路に流す。

### 6.6 GET /api/events

EventBus の直近イベントを返す。v1 初期は in-memory ring buffer でよい。

```json
{
  "events": [
    {
      "timestamp_ms": 123456,
      "type": "tool.invoked",
      "source": "ToolRegistry",
      "payload": {
        "tool": "rgb.set",
        "ok": true,
        "elapsed_ms": 12
      }
    }
  ]
}
```

### 6.7 追加候補

v1 後半または v1.1 で検討。

- `GET /api/logs`
- `GET /api/config`
- `POST /api/config`
- `GET /api/workflows`
- `POST /api/workflow/{name}`
- `POST /api/restart`
- WebSocket event stream

ただし ToolRegistry 中心を崩さないため、実行系 API はなるべく `POST /api/invoke` に寄せる。

## 7. JSON Workflow の入れ方

Workflow は Service や HAL に入れない。ToolRegistry に登録できる「データ定義 Tool」として扱う。

### 7.1 Workflow の位置

```text
Storage
  /storage/workflows/wave.json
    |
    v
WorkflowLoader
    |
    v
WorkflowTool
    |
    v
ToolRegistry
```

### 7.2 Workflow JSON 案

```json
{
  "name": "demo.wave",
  "title": "Demo wave",
  "description": "Move servo and change RGB color.",
  "input_schema": {
    "type": "object",
    "properties": {
      "repeat": { "type": "integer", "default": 1, "minimum": 1, "maximum": 5 }
    }
  },
  "steps": [
    {
      "tool": "rgb.setColor",
      "input": { "r": 0, "g": 80, "b": 255 }
    },
    {
      "tool": "servo.setPose",
      "input": { "pose": "left" }
    },
    {
      "delay_ms": 300
    },
    {
      "tool": "servo.setPose",
      "input": { "pose": "right" }
    }
  ]
}
```

### 7.3 v1 の Workflow 制限

- 逐次実行のみ
- 分岐なし、ループは最小限
- 外部通信なし
- 同時実行なし
- timeout 必須
- dangerous Tool を含む Workflow には dangerous flag を継承

Workflow は便利だが、v1 では小さく保つ。最初から簡易プログラミング環境にしない。

## 8. SD / Storage の役割

Storage は「ファイルを置く場所」ではなく、StackYan の拡張性を支えるデータ層。

既存 StackChu HAL の現状では、Storage は SD ではなく ESP32 flash 上の FATFS + wear levelling を `/storage` に mount する実装である。StackYan v1 初期はこれを正として使う。

SD は後段の拡張候補にする。v1 では API とディレクトリ設計を SD に依存させず、`IStorage` 経由に閉じる。

役割:

- 設定保存
- Tool 実行ログ
- Workflow JSON
- calibration
- presets
- IR code database
- Web UI assets
- crash / boot log

推奨ディレクトリ:

```text
/storage/config/device.json
/storage/config/network.json
/storage/config/servo.json
/storage/workflows/*.json
/storage/presets/rgb/*.json
/storage/presets/servo/*.json
/storage/ir/*.json
/storage/logs/tool-exec.log
/storage/web/*
```

設計方針:

- StorageService 経由でアクセスする
- v1 初期は StackChu HAL の FATFS/wear-levelling 実装を使う
- SD 対応は `IStorage` の別実装として追加する
- 設定は atomic write を意識する
- 起動時に読み込めない設定があっても safe default で起動する
- ログ肥大化を避けるため rotation を入れる
- mount failed でも Tool Server 自体は read-only / degraded mode で起動する
- `format_if_mount_failed` は開発中は許容、本番相当では設定で無効化できるように検討する

## 9. stackyan.local / mDNS / 設定Web UI

### 9.1 mDNS

hostname は `stackyan` を初期値にする。

- URL: `http://stackyan.local/`
- API: `http://stackyan.local/api/tools`

複数台を考えて、設定で device name を変えられるようにする。

例:

- `stackyan.local`
- `stackyan-living.local`
- `stackyan-dev.local`

### 9.2 Wi-Fi 設定

v1 の現実的な方針:

1. 既存 config があれば STA 接続
2. 失敗したら AP mode
3. AP: `StackYan-Setup`
4. 設定 Web UI で SSID / password 保存
5. 再起動

### 9.3 設定 Web UI

最小画面:

- Status
- Tools
- Network
- Storage
- Servo calibration
- Workflows

Web UI は見た目よりも「確実に Tool を叩ける」ことを優先する。

## 10. 実機テストで最初に確認すべきこと

優先順:

1. Serial boot log が見える
2. Wi-Fi 接続または AP mode 起動
3. `stackyan.local` が解決できる
4. `GET /api/status` が返る
5. `GET /api/tools` が返る
6. `POST /api/tool/system.ping` が返る
7. Web UI から `system.ping` を実行できる
8. RGB を低輝度で点灯できる
9. StackChu HAL のサブシステム別 begin/mount 結果が status に出る
10. `/storage` mount と write/read が確認できる
11. Motion read が valid になる
12. Power read が現実的な値を返す
13. Servo は電源・可動範囲・neutral 確認後に初めて動かす

Servo 初回テストの注意:

- neutral だけから始める
- min / max を狭くする
- 電源電圧を見る
- `readState` で valid / temperature / voltage を見る
- 連続駆動しない
- 緊急停止 Tool を用意する

## 11. やらない方がいいこと

v1 で避けるもの:

- Agent 中心設計
- XiaoZhi 専用設計
- Claude / OpenAI / GLM 専用 schema
- 動的バイナリプラグイン
- Lua / JIT / 任意スクリプト実行
- Tool から HAL 直叩き
- Web UI だけ特別な内部 API を使う
- Workflow を最初から高機能にする
- Servo を初期実装で大きく動かす
- 認証なしの危険 Tool を LAN に広く公開する
- SD 必須設計
- 設定ファイル破損で起動不能になる設計
- API response 形式が Tool ごとにバラバラになる設計
- StackChu HAL の安全機構を無視して Tool から別経路でサーボや RGB を叩くこと
- `HalPins.h` の要確認ピンを検証しないまま「確定」として扱うこと

## 12. 最初の小さな PR 案

### PR 1: StackChu HAL import and ToolRegistry skeleton

目的:

既存 StackChu HAL を土台として取り込み、StackYan の中心が ToolRegistry であることを固定する。

含めるもの:

- project skeleton
- `components/stackchu_hal` の取り込み
- `stackchu::Hal` の begin
- HAL サブシステム初期化結果の保持
- `Tool`
- `ToolRegistry`
- `ToolSchema`
- `ToolResult`
- `SystemStatusService`
- `system.ping`
- `GET /api/status`
- `GET /api/tools`
- `POST /api/tool/system.ping`
- minimal Web Tool Test UI
- README に v1 方針を記載

含めないもの:

- Servo move Tool
- RGB write Tool
- XiaoZhi
- LLM API
- Workflow
- SD 対応
- 複雑な設定 UI

この PR の完了条件:

- 実機または emulator 相当で起動する
- `stackyan.local` または IP で `/api/status` が読める
- `/api/tools` に `system.ping` が出る
- Web UI から `system.ping` を実行できる
- ToolRegistry を経由しない実行経路がない
- `/api/status` に HAL の begin/mount 結果が出る

### PR 2: Read-only hardware tools

目的:

低リスクな read-only 物理 Tool を追加する。

含めるもの:

- `power.status`
- `motion.read`
- `storage.status`
- Web UI の Tool 実行フォーム改善

### PR 3: RGB tool

目的:

低リスクな write 系物理 Tool を追加する。

含めるもの:

- `RgbService`
- `rgb.setColor`
- `rgb.clear`
- HAL の `RGB_BRIGHTNESS_LIMIT` を UI/schema に反映
- Web UI の Tool 実行フォーム改善

### PR 4: Storage and config foundation

目的:

設定と Workflow の置き場所を作る。

含めるもの:

- `StorageService`
- FATFS `/storage` 前提の JSON helper
- `/storage/config/device.json`
- `/storage/logs/tool-exec.log`
- 設定 Web UI の最小版

### PR 5: Servo safety tools

目的:

Servo を安全制限付きで Tool 化する。

含めるもの:

- `ServoService`
- calibration config
- `servo.setPose`
- `servo.stop`
- `servo.readState`
- dangerous / safety metadata

### PR 6: JSON Workflow as Tool

目的:

Workflow を ToolRegistry に統合する。

含めるもの:

- Workflow JSON loader
- WorkflowTool
- `/storage/workflows/*.json`
- `workflow.run`
- Workflow が `/api/tools` に出る仕組み

## 13. 推奨ディレクトリ構成

実装時の候補:

```text
components/
  stackchu_hal/
src/
  main.cpp
  app/
    StackYanApp.h
    StackYanApp.cpp
  services/
    HardwareService.h
    ServoService.h
    RgbService.h
    MotionService.h
    PowerService.h
    StorageService.h
    AvatarService.h
  tools/
    Tool.h
    ToolRegistry.h
    SystemTools.h
    RgbTools.h
    ServoTools.h
    PowerTools.h
    StorageTools.h
    WorkflowTool.h
  api/
    ApiServer.h
    ApiServer.cpp
  web/
    index.html
    app.js
    style.css
  workflow/
    Workflow.h
    WorkflowLoader.h
    WorkflowRunner.h
  config/
    DeviceConfig.h
    NetworkConfig.h
```

ただし最初の PR では全部作らない。PR 1 では `components/stackchu_hal`, `tools/`, `api/`, `app/`, `services/HardwareService` 程度に絞る。

## 14. 判断基準

実装中に迷ったら、以下で判断する。

1. その機能は ToolRegistry を中心にしているか
2. 人間と Agent が同じ API で使えるか
3. ハードウェア保護が Service 層にあるか
4. 設定破損や Storage mount 失敗でも起動できるか
5. v1 に不要な実行環境を増やしていないか
6. Workflow を Tool として扱える方向に進んでいるか
7. XiaoZhi 専用構造になっていないか

StackYan v1 は、まず「小さく、硬く、観測できる Tool Server」として完成させる。その上に Agent や Avatar や Workflow を載せる。
