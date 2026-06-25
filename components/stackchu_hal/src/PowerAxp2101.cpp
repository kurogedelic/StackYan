// PowerAxp2101 — AXP2101 PMU 経由でバッテリー残量・充電状態を読む。
//
// 主なレジスタ（AXP2101 データシート準拠）:
//   0x00 STATUS:   bit2=VBUS存在, bit5=充電中, bit6=バッテリ存在
//   0x01 MODE_CHG: bit[2:0] 充電状態（01h の charge state）
//   0x7A / 0x7B / 0x1D: バッテリ電圧の上位/下位（デバイス差あり）
//   0x78: バッテリ残量 [%]（8bit 直読み ※仕様で推奨）
//   0x40: 内部 ADC 温度系のベース（簡易対応）
//
// ※レジスタ配置は AXP2101 と AXP192 で異なる。本実装は AXP2101 向け。
// ※正確なレート換算（電圧/電流のスケール）は各デバイスの初期値依存部分が
//   あるため、代表的なスケール値を実装し、実機で要調整の箇所をコメント明示。
#include "hal/IPower.h"
#include "HalPins.h"
#include "I2cBus.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace stackchu {

namespace {
constexpr const char* kTag = "power";

// AXP2101 レジスタ
constexpr uint8_t REG_STATUS       = 0x00;  // bit2:VBUS / bit5:充電中 / bit6:BAT存在
constexpr uint8_t REG_BAT_PERCENT  = 0x78;  // 残量 [%]
constexpr uint8_t REG_BAT_VH       = 0x7A;  // 電圧上位
constexpr uint8_t REG_BAT_VL       = 0x7B;  // 電圧下位
constexpr uint8_t REG_PWR_TEMP_H   = 0x36;  // 内部温度上位（参考値）
constexpr uint8_t REG_PWR_TEMP_L   = 0x37;
constexpr uint8_t REG_BAT_IH       = 0x7C;  // 充放電電流上位
constexpr uint8_t REG_BAT_IL       = 0x7D;  // 充放電電流下位
}  // namespace

class PowerAxp2101 final : public IPower {
public:
    bool begin() {
        if (i2cbus::ensureInit() != ESP_OK) return false;
        ready_ = (i2cbus::readReg8(pins::AXP2101_ADDR, 0x03, &chip_id_) == ESP_OK);
        return ready_;
    }

    PowerState read() const override {
        PowerState s{};
        if (!ready_) return s;

        uint8_t status = 0;
        if (i2cbus::readReg8(pins::AXP2101_ADDR, REG_STATUS, &status) == ESP_OK) {
            s.usbConnected = (status & 0x04) != 0;            // bit2 = VBUS
            bool charging  = (status & 0x20) != 0;            // bit5 = 充電中
            bool batExist  = (status & 0x40) != 0;            // bit6 = BAT 存在
            (void)batExist;
            s.charge = charging ? ChargeState::Charging
                                : (s.usbConnected ? ChargeState::Standby
                                                  : ChargeState::Discharging);
        }

        uint8_t pct = 0;
        if (i2cbus::readReg8(pins::AXP2101_ADDR, REG_BAT_PERCENT, &pct) == ESP_OK) {
            s.level = (pct <= 100) ? pct : 100;
            if (s.level == 100) s.charge = ChargeState::Full;
        }

        // 電圧: 1mV/bit 換算（実機で要確認）
        uint8_t vh = 0, vl = 0;
        if (i2cbus::readReg8(pins::AXP2101_ADDR, REG_BAT_VH, &vh) == ESP_OK &&
            i2cbus::readReg8(pins::AXP2101_ADDR, REG_BAT_VL, &vl) == ESP_OK) {
            uint16_t raw = (uint16_t(((vh) & 0x1F) << 8) | vl);
            s.batteryVoltage = raw / 1000.0f;  // mV -> V
        }

        // 電流: 上位符号付き、下位部で補助（簡易。実機で調整）
        uint8_t ih = 0, il = 0;
        if (i2cbus::readReg8(pins::AXP2101_ADDR, REG_BAT_IH, &ih) == ESP_OK &&
            i2cbus::readReg8(pins::AXP2101_ADDR, REG_BAT_IL, &il) == ESP_OK) {
            int16_t raw = (int16_t)((uint16_t(ih) << 8) | il);
            s.batteryCurrent = raw / 100;  // スケール例（要調整）
        }

        uint8_t th = 0, tl = 0;
        if (i2cbus::readReg8(pins::AXP2101_ADDR, REG_PWR_TEMP_H, &th) == ESP_OK &&
            i2cbus::readReg8(pins::AXP2101_ADDR, REG_PWR_TEMP_L, &tl) == ESP_OK) {
            int16_t raw = (int16_t)((uint16_t(th) << 8) | tl);
            s.temperatureC = raw / 100.0f;  // スケール例
        }
        return s;
    }

    void startMonitor(uint32_t intervalMs) override {
        if (task_ != nullptr) return;
        intervalMs_ = intervalMs;
        monitorRunning_ = true;
        xTaskCreate(monitorTrampoline, "stackchu_pwr", 4096, this,
                    5, &task_);
    }

    void stopMonitor() override {
        monitorRunning_ = false;
        if (task_ != nullptr) {
            // タスク側でフラグを見て自発終了。即時不要なら削除しない。
            task_ = nullptr;
        }
    }

    void onLevel(uint8_t lowPercent, PowerCallback cb) override {
        std::lock_guard<std::mutex> lk(mu_);
        lowLevel_ = lowPercent;
        levelCb_ = std::move(cb);
        levelFired_ = false;  // 状態リセット
    }

    void onChargeChange(PowerCallback cb) override {
        std::lock_guard<std::mutex> lk(mu_);
        chargeCb_ = std::move(cb);
        lastCharge_ = ChargeState::Unknown;
    }

private:
    static void monitorTrampoline(void* arg) {
        static_cast<PowerAxp2101*>(arg)->monitorLoop();
    }

    void monitorLoop() {
        ChargeState last = ChargeState::Unknown;
        bool levelActive = false;
        while (monitorRunning_) {
            PowerState s = read();

            // 充電状態変化通知
            if (s.charge != last) {
                last = s.charge;
                std::lock_guard<std::mutex> lk(mu_);
                if (chargeCb_) chargeCb_(s);
            }

            // 残量低下通知
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (levelCb_ && s.level <= lowLevel_) {
                    if (!levelFired_) {
                        levelFired_ = true;
                        levelCb_(s);
                    }
                } else if (s.level > lowLevel_ + 5) {
                    // ヒステリシス: 復帰
                    levelFired_ = false;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(intervalMs_));
        }
        vTaskDelete(nullptr);
    }

    bool        ready_ = false;
    uint8_t     chip_id_ = 0;

    TaskHandle_t task_ = nullptr;
    std::atomic<bool> monitorRunning_{false};
    uint32_t   intervalMs_ = 5000;

    mutable std::mutex mu_;
    PowerCallback levelCb_;
    uint8_t       lowLevel_ = 20;
    bool          levelFired_ = false;
    PowerCallback chargeCb_;
    ChargeState   lastCharge_ = ChargeState::Unknown;
};

// ファサード用ファクトリ
IPower* createPower() { return new PowerAxp2101(); }

}  // namespace stackchu
