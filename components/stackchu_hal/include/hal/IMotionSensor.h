// IMotionSensor — 6 軸 IMU 抽象（BMI270）
//
// 「壊さない」ため、read() は公式最大レンジでクランプした値を返す:
//   - 加速度: ±16 g
//   - ジャイロ: ±2000 dps
// レンジは begin() で指定可能（推奨: 加速度 ±8g / ジャイロ ±2000dps）。
#pragma once

#include "Types.h"
#include <cstdint>

namespace stackchu {

enum class AccelRange : uint8_t {
    G2  = 2,
    G4  = 4,
    G8  = 8,
    G16 = 16,
};

enum class GyroRange : uint16_t {
    DPS125  = 125,
    DPS250  = 250,
    DPS500  = 500,
    DPS1000 = 1000,
    DPS2000 = 2000,
};

// 1 サンプル。SI 単位系（g / dps / ℃）。クランプ済み。
struct MotionSample {
    float ax = 0.f, ay = 0.f, az = 0.f;   // [g]   範囲 ±16
    float gx = 0.f, gy = 0.f, gz = 0.f;   // [dps] 範囲 ±2000
    float temperatureC = 0.f;             // [℃]
    bool  valid = false;
};

class IMotionSensor {
public:
    virtual ~IMotionSensor() = default;

    // 指定レンジで初期化。成功で true。
    virtual bool begin(AccelRange ar = AccelRange::G8,
                       GyroRange gr = GyroRange::DPS2000) = 0;

    // クランプ済みサンプルを取得。
    virtual MotionSample read() = 0;

    virtual bool isReady() const = 0;
};

}  // namespace stackchu
