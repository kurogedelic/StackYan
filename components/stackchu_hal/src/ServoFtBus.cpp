// ServoFtBus — FEETech SCS/STS 系バスサーボを UART 半二重で制御。
//
// StackChan 本体は PWM サーボではなく FEETech フィードバック付きバスサーボを
// 採用しており、位置・温度・負荷・電圧のフィードバックが得られる。
// これにより「壊さない」保護を HAL 側で実装する:
//   - 垂直軸は公式推奨 5–85° にクランプ
//   - 温度が上限を超えたらトルク解除（過熱保護）
//   - 移動後の readState で温度監視し、必要に応じて自動脱力
//
// SCS プロトコル概要（半二重 1 線、1Mbps）:
//   フレーム: ヘッダ 0xFF 0xFF, ID, Length, Instruction, Params..., Sum
//   Length = Params数 + 2
//   Instruction: 0x03 WRITE, 0x02 READ
//   主要 RAM アドレス:
//     0x2A TorqueEnable (0/1)
//     0x2D GoalAcc
//     0x2E GoalPosition(2B)  ※0–1000 = 0–360° (SCS) / 0–1023 (STS は仕様差)
//     0x30 GoalTime(2B) / GoalSpeed(2B) ※シリーズ差
//     0x38 PresentPosition(2B) ※シリーズ差アドレス
//     0x3A PresentSpeed(2B)
//     0x3C PresentLoad(2B)
//     0x3E PresentVoltage
//     0x3F PresentTemperature
//
// ※アドレス詳細はシリーズ (SCS0009/SCS1150/STS3215 等) で差があるため、
// 実機で合わせる前提。本実装は SCS 系標準の配置を用いる。
#include "hal/IServo.h"
#include "HalPins.h"
#include "Types.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include <cstdint>
#include <cstring>

namespace stackchu {

namespace {
constexpr const char* kTag = "servo";

// SCS/STS 命令
constexpr uint8_t INST_READ  = 0x02;
constexpr uint8_t INST_WRITE = 0x03;

// RAM アドレス
constexpr uint8_t REG_TORQUE_ENABLE = 0x28;  // SCS 系の多くは 0x28
constexpr uint8_t REG_GOAL_ACC      = 0x29;
constexpr uint8_t REG_GOAL_POS_L    = 0x2A;  // 目標位置
constexpr uint8_t REG_GOAL_POS_H    = 0x2B;
constexpr uint8_t REG_GOAL_SPD_L    = 0x2E;
constexpr uint8_t REG_GOAL_SPD_H    = 0x2F;
constexpr uint8_t REG_PRESENT_POS_L = 0x38;
constexpr uint8_t REG_PRESENT_POS_H = 0x39;
constexpr uint8_t REG_PRESENT_SPD_L = 0x3A;
constexpr uint8_t REG_PRESENT_SPD_H = 0x3B;
constexpr uint8_t REG_PRESENT_LOAD_L= 0x3C;
constexpr uint8_t REG_PRESENT_LOAD_H= 0x3D;
constexpr uint8_t REG_PRESENT_VOLT  = 0x3E;
constexpr uint8_t REG_PRESENT_TEMP  = 0x3F;

// 角度→ステップ換算（SCS: 0–1000 step = 0–360°）
constexpr uint16_t SCS_POS_MAX = 1000;
constexpr float    SCS_DEG_PER_STEP = 360.0f / 1000.0f;

uint8_t idFor(ServoAxis ax) {
    return (ax == ServoAxis::Horizontal) ? pins::SERVO_ID_HORIZONTAL
                                         : pins::SERVO_ID_VERTICAL;
}
}  // namespace

class ServoFtBus final : public IServo {
public:
    bool begin() {
        uart_config_t cfg = {};
        cfg.baud_rate  = pins::SERVO_UART_BAUD;
        cfg.data_bits  = UART_DATA_8_BITS;
        cfg.parity     = UART_PARITY_DISABLE;
        cfg.stop_bits  = UART_STOP_BITS_1;
        cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;
        esp_err_t err = uart_param_config(pins::SERVO_UART_PORT, &cfg);
        if (err != ESP_OK) return false;
        err = uart_set_pin(pins::SERVO_UART_PORT,
                           pins::SERVO_UART_TX,
                           pins::SERVO_UART_RX,
                           UART_PIN_NO_CHANGE,  // RTS は使わない
                           UART_PIN_NO_CHANGE);
        if (err != ESP_OK) return false;
        err = uart_driver_install(pins::SERVO_UART_PORT, 256, 256, 0, nullptr, 0);
        if (err != ESP_ERR_INVALID_STATE && err != ESP_OK) return false;

        // 半二制: TX と RX を同じ信号ラインにしたい。UART の TX は送信時のみ
        // ドライブし、受信時はハイZにしたいが、ESP32 の UART は受信中も TX は
        // アイドル High を維持する。物理的な外部回路（方向制御）を前提としつつ、
        // ソフトウェアでは送信前にバッファフラッシュ、送信後短いウェイトで衝突回避。
        ready_ = true;
        return true;
    }

    // ---- 移動 ----
    void moveTo(ServoAxis ax, float degrees, uint16_t speed) override {
        if (!ready_) return;
        degrees = clampForAxis(ax, degrees);
        uint8_t id = idFor(ax);
        uint16_t step = (uint16_t)(degrees / SCS_DEG_PER_STEP);

        // 速度設定
        writeReg16(id, REG_GOAL_SPD_L, speed);
        // 位置書き込み（アトミックに 2byte）
        uint8_t params[3] = {REG_GOAL_POS_L, (uint8_t)(step & 0xFF), (uint8_t)(step >> 8)};
        sendWrite(id, params, sizeof(params));
        protectCheck(ax);
    }

    void spin(ServoAxis ax, int16_t speed) override {
        if (!ready_) return;
        // 連続回転モード相当: 速度のみ書き込み、位置は触らない。
        // 水平軸（無限回転）で特に有効。
        uint8_t id = idFor(ax);
        uint16_t s = (uint16_t)(speed < 0 ? -speed : speed);
        writeReg16(id, REG_GOAL_SPD_L, s);
        protectCheck(ax);
    }

    void disableTorque(ServoAxis ax) override {
        if (!ready_) return;
        uint8_t id = idFor(ax);
        uint8_t params[2] = {REG_TORQUE_ENABLE, 0};
        sendWrite(id, params, sizeof(params));
    }

    ServoState readState(ServoAxis ax) override {
        ServoState st;
        if (!ready_) return st;
        uint8_t id = idFor(ax);
        // 位置〜温度を 8 バイト連続読み
        uint8_t buf[8] = {0};
        if (readReg(id, REG_PRESENT_POS_L, buf, 8)) {
            st.valid = true;
            uint16_t posStep = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
            st.positionDeg = posStep * SCS_DEG_PER_STEP;
            uint16_t spd    = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
            st.speed = (int16_t)spd;
            uint16_t load   = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
            st.load = (int16_t)(load & 0x7FF) * ((load & 0x800) ? -1 : 1);
            st.voltage = buf[6] / 10.0f;   // 0.1V/bit 想定
            st.temperatureC = buf[7];
        }
        return st;
    }

    void setOverheatThreshold(float celsius) override {
        overtempC_ = celsius;
    }

private:
    static float clampForAxis(ServoAxis ax, float deg) {
        if (ax == ServoAxis::Vertical) {
            return clampAngle(deg, pins::SERVO_VERTICAL_MIN_DEG,
                              pins::SERVO_VERTICAL_MAX_DEG);
        }
        // 水平は 0–360
        while (deg < 0) deg += 360.0f;
        while (deg >= 360.0f) deg -= 360.0f;
        return deg;
    }

    // 温度監視による自動保護
    void protectCheck(ServoAxis ax) {
        ServoState st = readState(ax);
        if (st.valid && st.temperatureC >= overtempC_) {
            ESP_LOGW(kTag, "servo%d overheat %.1fC -> torque off",
                     (int)ax, st.temperatureC);
            disableTorque(ax);
        }
    }

    // --- SCS プロトコル下位 ---
    void sendWrite(uint8_t id, const uint8_t* params, size_t n) {
        // Length = n + 2 (Instruction + Sum 部は含まない)
        uint8_t pkt[16];
        size_t i = 0;
        pkt[i++] = 0xFF;
        pkt[i++] = 0xFF;
        pkt[i++] = id;
        pkt[i++] = (uint8_t)(n + 2);
        pkt[i++] = INST_WRITE;
        for (size_t k = 0; k < n; ++k) pkt[i++] = params[k];
        uint8_t sum = id + (n + 2) + INST_WRITE;
        for (size_t k = 0; k < n; ++k) sum += params[k];
        sum = ~sum;
        pkt[i++] = sum;
        tx(pkt, i);
        drainRx();  // 応答は読み捨て（必要なら拡張）
    }

    void writeReg16(uint8_t id, uint8_t regL, uint16_t val) {
        uint8_t p[3] = {regL, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
        sendWrite(id, p, sizeof(p));
    }

    bool readReg(uint8_t id, uint8_t reg, uint8_t* dst, size_t n) {
        // READ 命令: inst=0x02, start=reg, len=n
        uint8_t pkt[8];
        size_t i = 0;
        pkt[i++] = 0xFF;
        pkt[i++] = 0xFF;
        pkt[i++] = id;
        pkt[i++] = 4;           // 2 params + inst + ... => n+2
        pkt[i++] = INST_READ;
        pkt[i++] = reg;
        pkt[i++] = (uint8_t)n;
        uint8_t sum = id + 4 + INST_READ + reg + (uint8_t)n;
        sum = ~sum;
        pkt[i++] = sum;
        tx(pkt, i);

        // 応答待ち: ヘッダ(2) ID(1) Len(1) Inst(1) data(n) Sum(1)
        uint8_t resp[16];
        size_t want = 2 + 1 + 1 + 1 + n + 1;
        size_t got = 0;
        int64_t deadline = esp_timer_get_time() + 50 * 1000;  // 50ms
        while (got < want && esp_timer_get_time() < deadline) {
            int r = uart_read_bytes(pins::SERVO_UART_PORT, resp + got,
                                    want - got, pdMS_TO_TICKS(5));
            if (r > 0) got += (size_t)r;
        }
        if (got < want) return false;
        // ヘッダ確認 & データコピー
        if (resp[0] != 0xFF || resp[1] != 0xFF || resp[2] != id) return false;
        memcpy(dst, resp + 5, n);
        return true;
    }

    void tx(const uint8_t* data, size_t n) {
        uart_wait_tx_done(pins::SERVO_UART_PORT, pdMS_TO_TICKS(10));
        // 半二重: 送信前に RX バッファを空にする
        uart_flush_input(pins::SERVO_UART_PORT);
        uart_write_bytes(pins::SERVO_UART_PORT, data, n);
        uart_wait_tx_done(pins::SERVO_UART_PORT, pdMS_TO_TICKS(20));
        // 回線が反転するまで少し待つ（外部トランシーバ想定）
        esp_rom_delay_us(100);
    }

    void drainRx() {
        uart_flush_input(pins::SERVO_UART_PORT);
    }

    bool  ready_ = false;
    float overtempC_ = pins::SERVO_OVERTEMP_C;
};

// ファサード用ファクトリ
IServo* createServo() { return new ServoFtBus(); }

}  // namespace stackchu
