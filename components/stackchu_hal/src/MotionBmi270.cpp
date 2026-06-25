// MotionBmi270 — BMI270 6 軸 IMU を I2C で読み、公式最大レンジでクランプして返す。
//
// 「壊さない」ためのクランプ:
//   - 加速度: ±kAccelMaxG (16g)
//   - ジャイロ: ±kGyroMaxDps (2000dps)
// センサ自体は設定レンジ以上を出力しないが、換算/オーバーフロー時の安全策。
//
// 注意: 本実装は「直接レジスタ読み出し」の最小構成。BMI270 は内部 FIFO を使うのが
// 本来望ましいが、HAL の読み取り API としては burst read で十分。初期化は
// レンジ/ODR のみ設定し、必要に応じて拡張する。
#include "hal/IMotionSensor.h"
#include "HalPins.h"
#include "I2cBus.h"

#include "esp_log.h"

#include <cmath>

namespace stackchu {

namespace {
constexpr const char* kTag = "motion";

// BMI270 レジスタ
constexpr uint8_t REG_CHIPID   = 0x00;  // 0x24
constexpr uint8_t REG_DATA_8   = 0x03;  // acc/gyr/temp 連続（ここから開始）
constexpr uint8_t REG_PWR_CTRL = 0x7D;  // bit2 aux | bit1 gyr | bit0 acc
constexpr uint8_t REG_ACC_RANGE= 0x41;  // acc range
constexpr uint8_t REG_GYR_RANGE= 0x43;  // gyr range

float accelLsb(AccelRange r) {
    switch (r) {
        case AccelRange::G2:  return 1.0f / 16384.0f;
        case AccelRange::G4:  return 1.0f / 8192.0f;
        case AccelRange::G8:  return 1.0f / 4096.0f;
        case AccelRange::G16: return 1.0f / 2048.0f;
    }
    return 1.0f / 4096.0f;
}
float gyroLsb(GyroRange r) {
    switch (r) {
        case GyroRange::DPS125:  return 125.0f / 32768.0f;
        case GyroRange::DPS250:  return 250.0f / 32768.0f;
        case GyroRange::DPS500:  return 500.0f / 32768.0f;
        case GyroRange::DPS1000: return 1000.0f / 32768.0f;
        case GyroRange::DPS2000: return 2000.0f / 32768.0f;
    }
    return 2000.0f / 32768.0f;
}
int accelRangeBits(AccelRange r) {
    switch (r) {
        case AccelRange::G2:  return 0x00;
        case AccelRange::G4:  return 0x01;
        case AccelRange::G8:  return 0x02;
        case AccelRange::G16: return 0x03;
    }
    return 0x02;
}
int gyroRangeBits(GyroRange r) {
    switch (r) {
        case GyroRange::DPS2000: return 0x00;
        case GyroRange::DPS1000: return 0x01;
        case GyroRange::DPS500:  return 0x02;
        case GyroRange::DPS250:  return 0x03;
        case GyroRange::DPS125:  return 0x04;
    }
    return 0x00;
}
}  // namespace

class MotionBmi270 final : public IMotionSensor {
public:
    bool begin(AccelRange ar, GyroRange gr) override {
        if (i2cbus::ensureInit() != ESP_OK) return false;

        uint8_t id = 0;
        if (i2cbus::readReg8(pins::BMI270_ADDR, REG_CHIPID, &id) != ESP_OK) {
            ESP_LOGE(kTag, "BMI270 chip id read failed");
            return false;
        }
        if (id != 0x24) {
            ESP_LOGW(kTag, "unexpected chip id 0x%02x (expect 0x24)", id);
        }
        accelRange_ = ar;
        gyroRange_  = gr;
        accelLsb_   = accelLsb(ar);
        gyroLsb_    = gyroLsb(gr);

        // ソフトリセット後、電源オン。BMI270 初期化シーケンスは本番では
        // config ファイル書き込みが推奨されるが、HAL の最小実装としては
        // レンジ設定 + PWR_CTRL 有効化のみ行う。
        i2cbus::writeReg8(pins::BMI270_ADDR, REG_ACC_RANGE, (uint8_t)accelRangeBits(ar));
        i2cbus::writeReg8(pins::BMI270_ADDR, REG_GYR_RANGE, (uint8_t)gyroRangeBits(gr));
        // acc(bit0)=1, gyr(bit1)=1
        i2cbus::writeReg8(pins::BMI270_ADDR, REG_PWR_CTRL, 0x03);

        ready_ = true;
        return true;
    }

    MotionSample read() override {
        MotionSample s;
        if (!ready_) return s;

        // REG_DATA_8 から 8 バイト: gyr(X,Y,Z) 6B + は順序注意。
        // BMI270 の通常データ順: gyrX(L,H), gyrY, gyrZ, accX, accY, accZ = 12B at 0x0C。
        // ここでは 0x0C の 12 バイトを burst read する。
        uint8_t buf[12] = {0};
        if (i2cbus::readBurst(pins::BMI270_ADDR, 0x0C, buf, sizeof(buf)) != ESP_OK) {
            return s;  // valid=false のまま
        }

        auto toInt16 = [](uint8_t lo, uint8_t hi) {
            return (int16_t)((uint16_t)hi << 8 | lo);
        };
        int16_t gx = toInt16(buf[0], buf[1]);
        int16_t gy = toInt16(buf[2], buf[3]);
        int16_t gz = toInt16(buf[4], buf[5]);
        int16_t ax = toInt16(buf[6], buf[7]);
        int16_t ay = toInt16(buf[8], buf[9]);
        int16_t az = toInt16(buf[10], buf[11]);

        // 換算 + クランプ（公式最大レンジ）
        s.ax = clampVal(ax * accelLsb_, kAccelMinG, kAccelMaxG);
        s.ay = clampVal(ay * accelLsb_, kAccelMinG, kAccelMaxG);
        s.az = clampVal(az * accelLsb_, kAccelMinG, kAccelMaxG);
        s.gx = clampVal(gx * gyroLsb_, kGyroMinDps, kGyroMaxDps);
        s.gy = clampVal(gy * gyroLsb_, kGyroMinDps, kGyroMaxDps);
        s.gz = clampVal(gz * gyroLsb_, kGyroMinDps, kGyroMaxDps);
        // 温度は別レジスタ (0x22) ; 簡易で 0 とし、必要に応じて拡張。
        s.temperatureC = 0.0f;
        s.valid = true;
        return s;
    }

    bool isReady() const override { return ready_; }

private:
    bool       ready_ = false;
    AccelRange accelRange_ = AccelRange::G8;
    GyroRange  gyroRange_  = GyroRange::DPS2000;
    float      accelLsb_   = 1.0f / 4096.0f;
    float      gyroLsb_    = 2000.0f / 32768.0f;
};

// ファサード用ファクトリ
IMotionSensor* createMotion() { return new MotionBmi270(); }

}  // namespace stackchu
