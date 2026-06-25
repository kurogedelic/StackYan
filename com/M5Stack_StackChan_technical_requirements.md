# M5Stack StackChan 技術要件リサーチ

作成日: 2026-06-25

目的: StackYan を M5Stack StackChan / Stack-chan 系ハードウェア上で動かす前提で、実装前に必要な技術要件、既知仕様、未確定点、初期検証項目を整理する。

## 1. 要約

StackChan は CoreS3 ベースの ESP32-S3 デバイスで、表示、タッチ、音声入出力、IMU、microSD、電源管理、RGB LED、IR、サーボ、NFC などを持つ小型ロボットである。

StackYan では、StackChan を「AI キャラクター」ではなく「物理 Tool Server」として扱う。したがって、技術要件の中心は以下になる。

- ESP32-S3 / CoreS3 上で安定起動すること
- HAL が各ハードウェアを安全に抽象化すること
- Storage が config / workflow / memory / logs / assets を保持できること
- `stackyan.local` のローカル HTTP API から Tool を呼べること
- LLM / TTS / STT は ToolRegistry の利用者または入出力アダプタとして後段に載せること

## 2. ハードウェア仕様

公式情報と BSP README から確認できる主要仕様。

| 項目 | 要件 / 仕様 |
|---|---|
| Main controller | ESP32-S3 |
| CPU | Xtensa dual-core 32-bit LX7, 240 MHz |
| Flash | 16 MB |
| PSRAM | 8 MB Quad PSRAM |
| Wireless | 2.4 GHz Wi-Fi 802.11 b/g/n, Bluetooth 5 LE |
| Wired | USB CDC, full-speed USB OTG, GPIO, UART, I2C |
| Display | 2.0 inch IPS LCD, 320x240, 65536 colors, ILI9342C |
| Touch | Capacitive multi-touch, FT6336U |
| Camera | GC0308, 640x480, 0.3 MP |
| Audio input | Dual microphones, ES7210 audio codec |
| Audio output | 1 W speaker, AW88298 16-bit I2S amplifier |
| IMU | BMI270 + BMM150, accel / gyro / magnetometer |
| Light/proximity | LTR-553ALS-WA |
| Body touch | Three-zone touch panel, Si12T |
| NFC | ST25R3916 |
| RGB LED | WS2812C x 12, two rows |
| IR | IRM56384 receiver + IR transmitter |
| Motors | 360-degree horizontal feedback servo, 90-degree vertical feedback servo |
| Battery | 550 mAh battery |
| PMU / RTC | AXP2101, BM8563 |
| Expansion | microSD slot, Grove x 3, LEGO-compatible mounting holes |

## 3. 開発環境要件

公式 Arduino ドキュメントで繰り返し示されている前提。

- Board selection: `M5CoreS3`
- M5Stack board manager:
  - 一般例: `>= 3.2.2`
  - Battery example: `>= 3.3.7`
- M5StackChan library: `>= 1.0.0`
- M5Unified library: `>= 0.2.11`
- IR example: `IRremote` library `>= 4.5.0`
- 対応開発環境:
  - UiFlow2
  - Arduino IDE
  - PlatformIO
  - ESP-IDF

StackYan は既存 `Stackchan_docs` が ESP-IDF project として成立しているため、v1 は ESP-IDF component 方式を第一候補にする。

ローカル `Stackchan_docs` で確認済み:

- `sdkconfig.defaults`: `CONFIG_IDF_TARGET="esp32s3"`
- `sdkconfig.defaults`: custom partition table enabled
- `sdkconfig.defaults`: 16 MB flash
- `sdkconfig.defaults`: PSRAM enabled
- `partitions.csv`: `storage` FAT partition 1 MB
- `components/stackchu_hal`: C++17 ESP-IDF component

## 4. 既存 StackChu HAL の位置づけ

参照元:

```text
/Users/kurogedelic/Desktop/Stackchan_docs/components/stackchu_hal
```

既存 HAL は StackYan にとって有用。v1 では再発明せず、まずこの HAL を取り込む。

### 4.1 提供 interface

- `stackchu::Hal`
- `IBacklight`
- `IServo`
- `IRgb`
- `IMotionSensor`
- `IPower`
- `IIr`
- `IStorage`

### 4.2 実装

- `BacklightAxp2101.cpp`
- `ServoFtBus.cpp`
- `RgbSk6812.cpp`
- `MotionBmi270.cpp`
- `PowerAxp2101.cpp`
- `IrNec.cpp`
- `StorageFatFs.cpp`

### 4.3 初期化順

`Hal::begin()` は以下の順序。

1. Backlight
2. Power
3. Storage
4. Motion
5. RGB
6. IR
7. Servo

Servo が最後なのは重要。起動時の急動作を避けるため、StackYan でもこの方針を維持する。

### 4.4 既存 HAL の安全仕様

- サブシステムごとの begin / mount が失敗しても全体起動は継続
- Storage は `begin()` ではなく `mount()`
- Servo は FEETech 系バスサーボ前提
- Servo 垂直軸は 5-85 度にクランプ
- Servo は温度上限で torque off
- RGB 輝度は `RGB_BRIGHTNESS_LIMIT = 0.4` にクランプ
- Motion は BMI270 の公式最大レンジにクランプ
- Storage は FATFS + wear levelling で `/storage` に mount

## 5. ピン / バス要件

既存 `HalPins.h` による現状。

### 5.1 I2C

- I2C number: 0
- SDA: GPIO12
- SCL: GPIO11
- Frequency: 400 kHz

I2C address:

- BMI270: `0x68`
- AXP2101: `0x34`
- AW9523: `0x58`
- FT6336U: `0x38`

### 5.2 RGB

- LED count: 12
- Data line: GPIO2 仮
- RMT TX channel: 0
- 要確認: 公式仕様は WS2812C x 12 だが、ローカル HAL のコメントでは SK6812 / NeoPixel 互換扱い。実機で timing と data pin を確認する。

### 5.3 IR

- RX: GPIO10
- TX: GPIO5 が公式 Arduino example に出る
- 既存 HAL の `HalPins.h` では TX GPIO44 仮

要件:

- StackYan 実装前に IR TX pin を BSP / 実機で確定する
- RX GPIO10 は公式 example と既存 HAL が一致している

### 5.4 Servo

- UART: UART1 仮
- TX: GPIO17 仮
- RX: GPIO18 仮
- Baud: 1 Mbps
- Servo ID:
  - horizontal: 1
  - vertical: 2

要件:

- StackChan BSP または実機で UART pin と half-duplex 構成を確認する
- SCS / STS 系のレジスタ差分を実機で読む
- Tool 化する前に `readState` が valid になることを確認する

### 5.5 Storage

ローカル ESP-IDF project の `partitions.csv`:

```csv
nvs,      data, nvs,      0x9000,  0x6000,
phy_init, data, phy,      0xF000,  0x1000,
factory,  app,  factory,  0x10000, 0x300000,
storage,  data, fat,              , 0x100000,
```

既存 HAL はこの `storage` パーティションを `/storage` に mount する。

StackYan v1 初期では microSD を必須にしない。内蔵 flash 上の FATFS / `/storage` を primary とし、microSD は後段の `IStorage` 別実装として扱う。

## 6. Audio / LLM / TTS / STT 要件

StackChan は音声入出力を持つため、将来的に STT / TTS / LLM integration に向いている。

ただし StackYan v1 では AI 統合を中心にしない。

### 6.1 Mic

公式 Arduino example は `M5Unified` の `Mic_Class` を使う。

重要点:

- sample rate 例: 17 kHz
- 録音バッファは大きくなりやすいので PSRAM 利用を前提にする
- 公式 example には「マイクとスピーカーは同時利用できないため speaker を止める」という趣旨の処理がある

StackYan 要件:

- STT は v1.5 以降
- v1 では `audio.micStatus` 程度の read-only Tool まで
- 録音 Tool を入れる場合は、Speaker と排他制御する
- raw PCM を Tool response に直接載せない。Storage に保存して path を返す

### 6.2 Speaker / TTS

公式 Arduino example は `M5Unified` の `Speaker_Class` を使い、tone 出力が確認例になっている。

StackYan 要件:

- v1 では `speaker.tone` または `audio.beep` 程度に留める
- TTS は v1.5 以降
- TTS は `tts.speak` Tool として扱うが、実装は cloud / local / prerecorded の adapter に分ける
- Speaker と Mic の排他を Service 層で管理する

### 6.3 LLM

StackYan の LLM は中心ではない。LLM は `/api/tools` を見て Tool を呼ぶクライアント。

要件:

- LLM 固有 schema にしない
- OpenAI / Claude / GLM / XiaoZhi は adapter として後から追加
- ToolRegistry schema は人間と Agent の両方に使える形にする
- long-running Tool には timeout / cancellation / execution log が必要

## 7. Local Server / stackyan.local 要件

StackYan の外部境界はローカル HTTP server。

必須:

- mDNS hostname: `stackyan.local`
- `GET /api/status`
- `GET /api/tools`
- `POST /api/tool/{name}`
- Web Tool Test UI

推奨:

- AP mode fallback: `StackYan-Setup`
- Wi-Fi config を `/storage/config/network.json` に保存
- device name を変更可能にする
- API response に request id / elapsed ms / error code を含める
- HAL 初期化状態を `/api/status` に含める

制約:

- LAN 内前提でも dangerous Tool は UI で確認を入れる
- Servo / IR / file delete / restart / Wi-Fi config change は dangerous metadata を付ける
- 最初から HTTPS や外部公開を目指さない

## 8. Tool 化の初期候補

read-only から始める。

### Phase A: System / Status

- `system.ping`
- `system.status`
- `hal.status`
- `storage.status`

### Phase B: Read-only hardware

- `power.status`
- `motion.read`
- `storage.list`
- `storage.readText`

### Phase C: Low-risk write

- `rgb.setColor`
- `rgb.clear`
- `backlight.setBrightness`
- `speaker.beep`

### Phase D: Risky hardware

- `servo.readState`
- `servo.setPose`
- `servo.stop`
- `servo.disableTorque`
- `ir.sendNec`

Servo は read-only / torque off / neutral から段階的に入れる。

## 9. 実機で最初に確認すること

1. USB CDC serial log が出る
2. ESP32-S3 / 16 MB flash / PSRAM が期待通り認識される
3. `Hal::begin()` が戻る
4. `/api/status` に各 HAL の ok / failed が出る
5. `/storage` mount が成功する
6. `/storage/settings/config.json` の write/read が成功する
7. `power.status` が電圧/電流を返す
8. `motion.read` が valid を返す
9. RGB 12 個が低輝度で正しく点灯する
10. IR RX GPIO10 で NEC 受信できる
11. IR TX pin を確定する
12. Servo `readState` が valid を返す
13. Servo torque off が効く
14. Servo neutral / home が安全範囲で動く
15. `stackyan.local` が解決する
16. Web UI から `system.ping` と `power.status` が呼べる

## 10. StackYan 実装への要求

### 10.1 必須

- ToolRegistry 中心
- HAL は既存 StackChu HAL を包む
- Tool から HAL を直接叩かない
- Service 層で安全制限と状態管理を行う
- Storage は `/storage` primary
- Web UI と Agent は同じ Tool API を使う
- mount / HAL 部分故障でも degraded mode で起動する

### 10.2 強く推奨

- `/api/status` に HAL subsystem status を含める
- Tool schema に `dangerous`, `permissions`, `timeout_ms` を含める
- Servo Tool は必ず speed / range / cooldown / emergency stop を持つ
- Mic / Speaker / TTS / STT は AudioService で排他制御する
- Storage write は atomic write 風にする
- Tool execution log を `/storage/logs/tool-exec.log` に残す

### 10.3 v1 で避ける

- LLM 中心設計
- XiaoZhi 専用設計
- Mic / Speaker 同時利用前提
- Servo を最初から大きく動かす
- microSD 必須設計
- `HalPins.h` の仮ピンを確定扱いすること
- 動的バイナリプラグイン / Lua / JIT
- Tool response に巨大バイナリや PCM を直接返すこと

## 11. 未確定 / 要追加調査

- RGB data pin: 既存 HAL は GPIO2 仮
- IR TX pin: 公式 example は GPIO5、既存 HAL は GPIO44 仮
- Servo UART TX/RX pin: 既存 HAL は GPIO17/GPIO18 仮
- Servo protocol register map: SCS / STS 機種差分
- Backlight pin / AXP2101 経由制御の正確な経路
- Power voltage/current 換算係数
- NFC / touch sensor / LTR553 を v1 Tool 対象にするか
- 公式 M5StackChan library と ESP-IDF HAL のどちらを最終 primary にするか
- factory firmware / mobile app との互換を考慮するか

## 12. 参照ソース

Web:

- M5Stack Store: StackChan product/spec page  
  https://shop.m5stack.com/products/stackchan-kawaii-co-created-open-source-ai-desktop-robot
- M5Stack StackChan BSP  
  https://github.com/m5stack/StackChan-BSP
- M5Stack StackChan open source repo  
  https://github.com/m5stack/StackChan
- StackChan Arduino Program Compilation & Upload  
  https://docs.m5stack.com/en/arduino/stackchan/program
- StackChan Servo  
  https://docs.m5stack.com/en/arduino/stackchan/servo
- StackChan IR NEC  
  https://docs.m5stack.com/ja/arduino/stackchan/ir_nec
- StackChan Mic  
  https://docs.m5stack.com/en/arduino/stackchan/mic
- StackChan Speaker  
  https://docs.m5stack.com/en/arduino/stackchan/speaker
- StackChan IMU  
  https://docs.m5stack.com/en/arduino/stackchan/imu
- StackChan Battery  
  https://docs.m5stack.com/en/arduino/stackchan/battery

Local:

- `/Users/kurogedelic/Desktop/Stackchan_docs/components/stackchu_hal`
- `/Users/kurogedelic/Desktop/Stackchan_docs/main/main.cpp`
- `/Users/kurogedelic/Desktop/Stackchan_docs/partitions.csv`
- `/Users/kurogedelic/Desktop/Stackchan_docs/sdkconfig.defaults`
- `/Users/kurogedelic/Desktop/Stackchan_docs/sdkconfig`

