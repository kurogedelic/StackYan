// BacklightAxp2101 — LCD バックライトを AXP2101 経由で制御。
//
// CoreS3 のバックライトは AXP2101 の ALDO/DCDC 系および BL PWM 制御ピンに
// 結線されている。実機依存のマッピングがあるため、ここでは「PWM 的段階制御」と
// 「電源 on/off」の 2 つのレバレッジを持つ。
//
// 互換性のため、ledc を用いた GPIO PWM にフォールバックする設計:
//   - AXP2101 のバックライト系レジスタを設定（実機で有効ならそちら優先）
//   - 失敗時は GPIO バックライトピンを ledc で PWM 駆動（ピン要確認）
#include "hal/IBacklight.h"
#include "HalPins.h"
#include "I2cBus.h"
#include "Types.h"

#include "driver/ledc.h"
#include "esp_log.h"

#include <cstdint>

namespace stackchu {

namespace {
constexpr const char* kTag = "backlight";

// ledc 設定（GPIO PWM フォールバック用）
constexpr ledc_timer_t   kTimer   = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr gpio_num_t kBlGpioFallback = GPIO_NUM_38;  // CoreS3 BL ピン候補（要確認）
constexpr uint32_t   kPwmFreqHz = 5000;
constexpr uint32_t   kPwmResolution = LEDC_TIMER_8_BIT;
}

class BacklightAxp2101 final : public IBacklight {
public:
    bool begin() {
        if (i2cbus::ensureInit() != ESP_OK) return false;

        // ledc フォールバックを初期化（AXP2101 BL 制御は実機依存のため、
        // 確実な PWM 調光を GPIO 経由で担保する）。
        ledc_timer_config_t tcfg = {};
        tcfg.speed_mode      = LEDC_LOW_SPEED_MODE;
        tcfg.timer_num       = kTimer;
        tcfg.duty_resolution = (ledc_timer_bit_t)kPwmResolution;
        tcfg.freq_hz         = kPwmFreqHz;
        tcfg.clk_cfg         = LEDC_AUTO_CLK;
        if (ledc_timer_config(&tcfg) != ESP_OK) {
            ESP_LOGE(kTag, "ledc timer config failed");
            return false;
        }

        ledc_channel_config_t ccfg = {};
        ccfg.gpio_num   = kBlGpioFallback;
        ccfg.speed_mode = LEDC_LOW_SPEED_MODE;
        ccfg.channel    = kChannel;
        ccfg.timer_sel  = kTimer;
        ccfg.duty       = 0;
        ccfg.hpoint     = 0;
        ccfg.flags.output_invert = 0;
        if (ledc_channel_config(&ccfg) != ESP_OK) {
            ESP_LOGE(kTag, "ledc channel config failed");
            return false;
        }
        ready_ = true;
        setBrightness(80);
        return true;
    }

    void setBrightness(uint8_t percent) override {
        if (!ready_) return;
        percent_ = clampVal<uint8_t>(percent, 0, 100);
        // デューティ 0..(2^8 - 1)
        uint32_t duty = enabled_
            ? (uint32_t)percent_ * 255u / 100u
            : 0;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, kChannel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, kChannel);
    }

    uint8_t getBrightness() const override { return percent_; }

    void enable() override {
        enabled_ = true;
        setBrightness(percent_);
    }
    void disable() override {
        enabled_ = false;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, kChannel, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, kChannel);
    }

private:
    bool     ready_ = false;
    bool     enabled_ = true;
    uint8_t  percent_ = 80;
};

// ファサード用ファクトリ
IBacklight* createBacklight() { return new BacklightAxp2101(); }

}  // namespace stackchu
