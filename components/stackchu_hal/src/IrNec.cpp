// IrNec — RMT で NEC プロトコル送受信 + RAW 波形キャプチャ。
//
// NEC プロトコル（38kHz キャリア、アクティブ High をキャリアバーストとみなす）:
//   リーダ:  9000us High / 4500us Low
//   '0' bit: 562.5us High / 562.5us Low
//   '1' bit: 562.5us High / 1687.5us Low
//   フレーム: addr8 ~addr8 cmd8 ~cmd8
//   リピート: 9000us High / 2250us Low / 562.5us High
//
// RMT は 1MHz (1us/tick) で動かし、キャリアはソフトウェアエンコードではなく
// TX 側はレベル列で表現する（IR LED は 38kHz 変調が本来だが、本 HAL は
// 「NEC/RAW のレベルタイミング生成と解読」に責任を持つ。実機でキャリア変調を
// 行う場合は GPIO マトリクス/RMT キャリア設定を有効化する）。
#include "hal/IIr.h"
#include "HalPins.h"

#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace stackchu {

namespace {
constexpr const char* kTag = "ir";
constexpr uint32_t kRmtResHz = 1 * 1000 * 1000;  // 1MHz => 1us/tick

// NEC タイミング [us]
constexpr uint32_t NEC_HDR_MARK  = 9000;
constexpr uint32_t NEC_HDR_SPACE = 4500;
constexpr uint32_t NEC_BIT_MARK  = 562;
constexpr uint32_t NEC_ZERO      = 562;
constexpr uint32_t NEC_ONE       = 1687;
constexpr uint32_t NEC_RPT_SPACE = 2250;

inline rmt_symbol_word_t mkSym(uint16_t d0, uint8_t l0, uint16_t d1, uint8_t l1) {
    rmt_symbol_word_t s;
    s.duration0 = d0; s.level0 = l0;
    s.duration1 = d1; s.level1 = l1;
    return s;
}
}  // namespace

class IrNec final : public IIr {
public:
    ~IrNec() override {
        if (rx_chan_) rmt_disable(rx_chan_);
        if (tx_chan_) rmt_disable(tx_chan_);
    }

    bool begin() {
        // --- TX ---
        rmt_tx_channel_config_t tcfg = {};
        tcfg.gpio_num   = pins::IR_TX_PIN;
        tcfg.clk_src    = RMT_CLK_SRC_DEFAULT;
        tcfg.resolution_hz = kRmtResHz;
        tcfg.mem_block_symbols = 64;
        tcfg.trans_queue_depth = 4;
        if (rmt_new_tx_channel(&tcfg, &tx_chan_) != ESP_OK) return false;

        // RAW 送信は copy エンコーダでバッファを直接シンボルとして送る。
        rmt_copy_encoder_config_t cec = {};
        if (rmt_new_copy_encoder(&cec, &copy_enc_) != ESP_OK) return false;
        if (rmt_enable(tx_chan_) != ESP_OK) return false;
        return true;
    }

    // ---- 送信 ----
    void sendNec(uint8_t address, uint8_t command) override {
        if (!tx_chan_) return;
        // リーダ + 32bit + トレーラ。全シンボルで level0=1(バースト), level1=0(スペース)
        std::vector<rmt_symbol_word_t> syms;
        syms.reserve(34 + 1);
        syms.push_back(mkSym(NEC_HDR_MARK, 1, NEC_HDR_SPACE, 0));

        uint32_t bits = ((uint32_t)address)
                      | ((uint32_t)(uint8_t)~address << 8)
                      | ((uint32_t)command << 16)
                      | ((uint32_t)(uint8_t)~command << 24);
        for (int i = 0; i < 32; ++i) {
            bool one = (bits >> i) & 1u;
            syms.push_back(mkSym(NEC_BIT_MARK, 1, one ? NEC_ONE : NEC_ZERO, 0));
        }
        syms.push_back(mkSym(NEC_BIT_MARK, 1, 0, 0));  // 終端バースト

        rmt_transmit_config_t tc = {};
        rmt_transmit(tx_chan_, copy_enc_, syms.data(),
                     syms.size() * sizeof(rmt_symbol_word_t), &tc);
        rmt_tx_wait_all_done(tx_chan_, 20);
    }

    void sendRaw(const uint16_t* timings, size_t n, bool startHigh) override {
        if (!tx_chan_ || n == 0) return;
        std::vector<rmt_symbol_word_t> syms;
        syms.reserve(n / 2 + 1);
        // 2 つで 1 シンボル（level0=startHigh の極性）。
        uint8_t l0 = startHigh ? 1 : 0;
        uint8_t l1 = startHigh ? 0 : 1;
        for (size_t i = 0; i + 1 < n; i += 2) {
            syms.push_back(mkSym(timings[i], l0, timings[i + 1], l1));
        }
        if (n & 1u) {
            syms.push_back(mkSym(timings[n - 1], l0, 0, 0));
        }
        rmt_transmit_config_t tc = {};
        rmt_transmit(tx_chan_, copy_enc_, syms.data(),
                     syms.size() * sizeof(rmt_symbol_word_t), &tc);
        rmt_tx_wait_all_done(tx_chan_, 20);
    }

    // ---- 受信 ----
    void onReceived(IrCallback cb) override {
        std::lock_guard<std::mutex> lk(mu_);
        cb_ = std::move(cb);
    }

    void enableReceive(bool captureRaw) override {
        if (rx_chan_) { rmt_disable(rx_chan_); rmt_del_channel(rx_chan_); rx_chan_ = nullptr; }
        captureRaw_ = captureRaw;

        rmt_rx_channel_config_t rcfg = {};
        rcfg.gpio_num = pins::IR_RX_PIN;
        rcfg.clk_src  = RMT_CLK_SRC_DEFAULT;
        rcfg.resolution_hz = kRmtResHz;
        rcfg.mem_block_symbols = 64;
        if (rmt_new_rx_channel(&rcfg, &rx_chan_) != ESP_OK) return;

        rmt_rx_event_callbacks_t cbs = {};
        cbs.on_recv_done = &IrNec::onRxDoneCb;
        rmt_rx_register_event_callbacks(rx_chan_, &cbs, this);

        rmt_receive_config_t rc = {};
        rc.signal_range_min_ns = 1000;             // 1us 以上
        rc.signal_range_max_ns = 12000 * 1000;     // 12ms 以下（リーダ許容）
        rmt_enable(rx_chan_);
        rmt_receive(rx_chan_, rx_buf_, sizeof(rx_buf_), &rc);
        receiving_ = true;
    }

    void disableReceive() override {
        receiving_ = false;
        if (rx_chan_) {
            rmt_disable(rx_chan_);
            rmt_del_channel(rx_chan_);
            rx_chan_ = nullptr;
        }
    }

private:
    static bool onRxDoneCb(rmt_channel_handle_t, const rmt_rx_done_event_data_t* e,
                           void* user) {
        static_cast<IrNec*>(user)->handleRx(e->received_symbols, e->num_symbols);
        return false;
    }

    void handleRx(const rmt_symbol_word_t* syms, size_t n) {
        if (n < 2) { rearm(); return; }

        // NEC デコード試行
        IrMessage msg;
        if (captureRaw_) {
            msg.protocol = IrProtocol::Raw;
            msg.startHigh = true;
            for (size_t i = 0; i < n; ++i) {
                msg.timings.push_back(syms[i].duration0);
                msg.timings.push_back(syms[i].duration1);
            }
        }

        // リーダ確認（level0=1 が 9ms, level1=0 が 4.5ms 付近）
        if (syms[0].level0 == 1 && inRange(syms[0].duration0, NEC_HDR_MARK) &&
            inRange(syms[0].duration1, NEC_HDR_SPACE) && n >= 1 + 32 + 1) {
            uint32_t bits = 0;
            bool ok = true;
            for (int i = 0; i < 32; ++i) {
                const auto& s = syms[1 + i];
                if (s.level0 != 1) { ok = false; break; }
                bool one = inRange(s.duration1, NEC_ONE);
                bool zero = inRange(s.duration1, NEC_ZERO);
                if (!one && !zero) { ok = false; break; }
                if (one) bits |= (1u << i);
            }
            if (ok) {
                uint8_t addr  = (bits >> 0)  & 0xFF;
                uint8_t naddr = (bits >> 8)  & 0xFF;
                uint8_t cmd   = (bits >> 16) & 0xFF;
                uint8_t ncmd  = (bits >> 24) & 0xFF;
                if ((uint8_t)~addr == naddr && (uint8_t)~cmd == ncmd) {
                    msg.protocol = IrProtocol::Nec;
                    msg.address  = addr;
                    msg.command  = cmd;
                }
            }
        }

        if (msg.protocol == IrProtocol::Unknown && !captureRaw_) {
            rearm();
            return;
        }

        IrCallback cb;
        { std::lock_guard<std::mutex> lk(mu_); cb = cb_; }
        if (cb) cb(msg);
        rearm();
    }

    void rearm() {
        if (!receiving_ || !rx_chan_) return;
        rmt_receive_config_t rc = {};
        rc.signal_range_min_ns = 1000;
        rc.signal_range_max_ns = 12000 * 1000;
        rmt_receive(rx_chan_, rx_buf_, sizeof(rx_buf_), &rc);
    }

    static bool inRange(uint16_t val, uint32_t ref) {
        const uint32_t tol = ref / 4 + 100;  // 25% + 100us の許容
        return val >= (ref > tol ? ref - tol : 0) && val <= ref + tol;
    }

    rmt_channel_handle_t tx_chan_ = nullptr;
    rmt_channel_handle_t rx_chan_ = nullptr;
    rmt_encoder_handle_t copy_enc_ = nullptr;

    rmt_symbol_word_t rx_buf_[128] = {};
    std::atomic<bool> receiving_{false};
    bool captureRaw_ = false;

    std::mutex mu_;
    IrCallback cb_;
};

// ファサード用ファクトリ
IIr* createIr() { return new IrNec(); }

}  // namespace stackchu
