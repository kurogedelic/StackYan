// RgbSk6812 — SK6812 (NeoPixel 互換) RGB LED × 12 を RMT 新 API (v5) で駆動。
//
// 設計:
//   - setPixel/setAll/setColumn は内部バッファを更新するだけ（送信しない）。
//   - show() で初めて RMT 送信。ちらつき防止。
//   - 全体輝度 scale は show 時に各チャネルへ乗算し、過電流保護の上限でクランプ。
//
// SK6812 タイミング (T0H 0.3us / T0L 0.9us / T1H 0.6us / T1L 0.6us, RESET >80us)。
#include "hal/IRgb.h"
#include "HalPins.h"

#include "driver/rmt_tx.h"
#include "esp_check.h"

#include <array>
#include <string.h>

namespace stackchu {

namespace {
// GRB 順の 24bit を RMT シンボルへ展開するバイトエンコーダを利用。
// rmt_bytes_encoder で 1 バイト単位のビットパターンを定義する。

// 各ビットの RMT シンボル定義（APB 10MHz クロック => 1 tick = 100ns）。
//   '0': H=300ns(3tick) / L=900ns(9tick)
//   '1': H=600ns(6tick) / L=600ns(6tick)
rmt_symbol_word_t kSk6812Bit0 = {
    .duration0 = 3,
    .level0    = 1,
    .duration1 = 9,
    .level1    = 0,
};
rmt_symbol_word_t kSk6812Bit1 = {
    .duration0 = 6,
    .level0    = 1,
    .duration1 = 6,
    .level1    = 0,
};
}  // namespace

class RgbSk6812 final : public IRgb {
public:
    bool begin() {
        rmt_tx_channel_config_t cfg = {};
        cfg.gpio_num      = pins::RGB_DIN;
        cfg.clk_src       = RMT_CLK_SRC_DEFAULT;
        cfg.resolution_hz = 10 * 1000 * 1000;  // 10MHz => 100ns/tick
        cfg.mem_block_symbols = 64;
        cfg.trans_queue_depth = 4;

        esp_err_t err = rmt_new_tx_channel(&cfg, &tx_chan_);
        if (err != ESP_OK) return false;
        err = rmt_enable(tx_chan_);
        if (err != ESP_OK) return false;

        // バイトエンコーダ: 各バイトの MSB から 8bit を上記シンボルで送信。
        rmt_bytes_encoder_config_t enc = {};
        enc.bit0 = kSk6812Bit0;
        enc.bit1 = kSk6812Bit1;
        enc.flags.msb_first = 1;  // SK6812 は MSB first
        err = rmt_new_bytes_encoder(&enc, &encoder_);
        if (err != ESP_OK) return false;
        return true;
    }

    void setPixel(uint8_t index, Color c) override {
        if (index >= kCount) return;
        buf_[index] = c;
    }

    void setAll(Color c) override {
        buf_.fill(c);
    }

    void setColumn(LedColumn col, Color c) override {
        const uint8_t start = (col == LedColumn::Left) ? 0 : 6;
        for (uint8_t i = 0; i < 6; ++i) buf_[start + i] = c;
    }

    void setBrightness(float scale) override {
        brightness_ = clampVal(scale, 0.0f, 1.0f);
    }
    float getBrightness() const override { return brightness_; }

    void clear() override {
        buf_.fill(kBlack);
        show();
    }

    void show() override {
        // 全体輝度上限でクランプ（過電流保護）。
        const float scale = clampVal(brightness_, 0.0f, pins::RGB_BRIGHTNESS_LIMIT);

        // SK6812 は GRB 順。各 LED 24bit。
        std::array<uint8_t, kCount * 3> grb;
        for (uint8_t i = 0; i < kCount; ++i) {
            Color c = buf_[i];
            grb[i * 3 + 0] = static_cast<uint8_t>(c.g * scale);
            grb[i * 3 + 1] = static_cast<uint8_t>(c.r * scale);
            grb[i * 3 + 2] = static_cast<uint8_t>(c.b * scale);
        }

        rmt_transmit_config_t tc = {};
        tc.loop_count = 0;
        // bytes_encoder は copy を使うので送信完了までバッファ保持は必須でないが、
        // キューの深度に依存するため同期送信で安全に扱う。
        rmt_transmit(tx_chan_, encoder_, grb.data(), grb.size(), &tc);
        rmt_tx_wait_all_done(tx_chan_, 10);
    }

private:
    rmt_channel_handle_t  tx_chan_  = nullptr;
    rmt_encoder_handle_t  encoder_  = nullptr;
    std::array<Color, kCount> buf_  = {};
    float brightness_ = pins::RGB_BRIGHTNESS_LIMIT;
};

// ファサード用ファクトリ（Hal.cpp から利用）
IRgb* createRgb() { return new RgbSk6812(); }

}  // namespace stackchu
