// Stackchu HAL — ピン定義一元管理
//
// CoreS3 (ESP32-S3) + StackChan 本体のピン割り当て。
// 確定分（公式ドキュメント準拠）と、実機/BSP で要確認の箇所を 1 箇所に集約。
// ハード側変更時にここだけ直せば済むようにする。
//
// === 確認状態凡例 ===
//   [確定] : 公式ドキュメント/データシートで明示
//   [要確認] : BSP ソース等での実機検証が必要。仮ピンの可能性あり
//
// 参考:
//   - M5Stack CoreS3 公式ドキュメント
//   - StackChan-BSP (https://github.com/m5stack/StackChan-BSP)
//   - StackChan IR(NEC) ドキュメント (RX = GPIO10)
//   - CoreS3 内部 I2C: SDA=GPIO12 / SCL=GPIO11
#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"

namespace stackchu {
namespace pins {

// ===========================================================================
// I2C（内部バス：BMI270 / AXP2101 / AW9523 / タッチ FT6336U / BHI260） [確定]
// ===========================================================================
inline constexpr int I2C_NUM      = 0;        // I2C_NUM_0
inline constexpr gpio_num_t SDA   = GPIO_NUM_12;
inline constexpr gpio_num_t SCL   = GPIO_NUM_11;
inline constexpr uint32_t   FREQ_HZ = 400000; // 400kHz

// ===========================================================================
// デバイス I2C アドレス（7bit） [確定]
// ===========================================================================
inline constexpr uint8_t BMI270_ADDR  = 0x68;  // SDO=Low 時。High なら 0x69
inline constexpr uint8_t AXP2101_ADDR = 0x34;
inline constexpr uint8_t AW9523_ADDR  = 0x58;  // AI2=~0x5B / AI1=0: 0x58
inline constexpr uint8_t FT6336U_ADDR = 0x38;  // タッチコントローラ（将来用）

// ===========================================================================
// RMT (RGB LED / IR)
// ===========================================================================

// SK6812 RGB LED 12 個。 [要確認]
// StackChan 本体の RGB は AW9523 の Pin14 経由で給電されるが、
// データ線(DIN)に使う GPIO は BSP ソースで要確認。
inline constexpr gpio_num_t RGB_DIN    = GPIO_NUM_2;   // 仮: 要確認
inline constexpr int        RGB_COUNT  = 12;            // [確定] 2 列 × 6
inline constexpr int        RGB_RMT_CH = 0;             // RMT TX channel

// IR [受信=確定 / 送信=要確認]
// 受信は GPIO10（StackChan IR(NEC) 公式ドキュメント準拠）。
// 送信は IR LED を駆動する経路により 2 案:
//   (A) AW9523 の出力ポート経由
//   (B) ESP32 GPIO 直駆動
// ここでは (B) を仮定。実機で切り替え時は IIr 実装側で対応。
inline constexpr gpio_num_t IR_RX_PIN  = GPIO_NUM_10;   // [確定]
inline constexpr gpio_num_t IR_TX_PIN  = GPIO_NUM_44;   // [要確認] 仮
inline constexpr int        IR_RX_RMT  = 0;             // RMT RX channel
inline constexpr int        IR_TX_RMT  = 1;             // RMT TX channel

// ===========================================================================
// Servo（FEETech バスサーボ / UART 半二重） [要確認]
// ===========================================================================
// SCS/STS 系は 1 本のデータ線で半二重。
// UART1 を使い、TX と RX を同じ信号ラインに結線する（ドライバ内で方向制御）。
// ※StackChan 本体のサーボは 4P ゴンドコネクタではなく M5Grove/拡張ポート経由の
//   可能性があり、使用する UART とピンは BSP で要確認。
inline constexpr uart_port_t  SERVO_UART_PORT = UART_NUM_1;      // UART1
inline constexpr gpio_num_t SERVO_UART_TX   = GPIO_NUM_17;  // [要確認]
inline constexpr gpio_num_t SERVO_UART_RX   = GPIO_NUM_18;  // [要確認]
inline constexpr uint32_t   SERVO_UART_BAUD = 1000000;  // SCS/STS 標準 1Mbps

// サーボ ID（FEETech バスサーボは ID で個体識別）
inline constexpr uint8_t SERVO_ID_HORIZONTAL = 1;  // 水平（360°連続回転）
inline constexpr uint8_t SERVO_ID_VERTICAL   = 2;  // 垂直（90°可動）

// ===========================================================================
// バックライト [要確認]
// ===========================================================================
// CoreS3 の LCD バックライト制御ピン。公式の ILI9341 バックライト EN ピン。
// 実機で AXP2101 経由の場合はこのピンを使わない（実装側で切替）。
inline constexpr gpio_num_t BACKLIGHT_PWM = GPIO_NUM_38;  // [要確認] 仮

// ===========================================================================
// ストレージ（FATFS / wear-levelling）
// ===========================================================================
// WL はフラッシュ上のパーティションを使用するため GPIO ピン割当なし。
// パーティションラベルは partitions.csv の "storage" と一致させること。
inline constexpr const char* STORAGE_PART_LABEL = "storage";
inline constexpr const char* STORAGE_MOUNT_POINT = "/storage";

// ===========================================================================
// 安全限界（「壊さない」ための閾値） [確定: 公式データシート準拠]
// ===========================================================================

// BMI270
inline constexpr float ACCEL_MAX_G     = 16.0f;   // 加速度 公式最大 ±16g
inline constexpr float GYRO_MAX_DPS    = 2000.0f; // ジャイロ 公式最大 ±2000dps

// サーボ
inline constexpr float SERVO_VERTICAL_MIN_DEG = 5.0f;    // 公式推奨 下限
inline constexpr float SERVO_VERTICAL_MAX_DEG = 85.0f;   // 公式推奨 上限
inline constexpr float SERVO_OVERTEMP_C       = 65.0f;   // この温度でトルク解除

// RGB 全体の輝度上限（過電流保護）。0.0–1.0。
inline constexpr float RGB_BRIGHTNESS_LIMIT = 0.4f;

// ===========================================================================
// ※後方互換エイリアス（既存コード参照の互換用。新規コードは上記を使うこと）
// ===========================================================================
// ※ kAccelMaxG / kGyroMaxDps は Types.h 側にも定義があるため、ここでは
//    ACCEL_MAX_G / GYRO_MAX_DPS のみを正とする。Types.h 側は将来統合予定。

}  // namespace pins
}  // namespace stackchu
