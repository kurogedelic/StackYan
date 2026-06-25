// IPower — 電源・バッテリー抽象（AXP2101 PMU）
//
// ユーザ質問「バッテリー容量や充電状態のリッスンはできる？」→ はい。
// モデルは「ポーリング + コールバック」:
//   - startMonitor() でバックグラウンド定期読出し
//   - onLevel(threshold,...) で残量低下通知
//   - onChargeChange(...)    で充電状態変化通知
#pragma once

#include <cstdint>
#include <functional>

namespace stackchu {

enum class ChargeState : uint8_t {
    Discharging = 0,  // 放電中
    Charging    = 1,  // 充電中
    Standby     = 2,  // 充電終了/待機
    Full        = 3,  // 満充電
    Unknown     = 0xFF,
};

struct PowerState {
    uint8_t     level          = 0;     // 残量 [%] 0–100
    ChargeState charge         = ChargeState::Unknown;
    float       batteryVoltage = 0.0f;  // [V]
    int16_t     batteryCurrent = 0;     // [mA] 正=放電, 負=充電
    float       temperatureC   = 0.0f;  // [℃]
    bool        usbConnected   = false; // VBUS 検出
};

using PowerCallback = std::function<void(const PowerState&)>;

class IPower {
public:
    virtual ~IPower() = default;

    // 初期化。PMU との I2C 通信を確認する。成功で true。
    virtual bool begin() = 0;

    // その瞬間の状態を同期的に取得。
    virtual PowerState read() const = 0;

    // バックグラウンド定期読出しを開始/停止。
    virtual void startMonitor(uint32_t intervalMs = 5000) = 0;
    virtual void stopMonitor() = 0;

    // 残量が lowPercent 以下になったら都度呼ばれる。
    virtual void onLevel(uint8_t lowPercent, PowerCallback cb) = 0;
    // 充電状態が変化したら呼ばれる。
    virtual void onChargeChange(PowerCallback cb) = 0;
};

}  // namespace stackchu
