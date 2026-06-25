// IBacklight — バックライト抽象
// CoreS3 の LCD バックライト（AXP2101 経由の電源制御 / PWM）。
#pragma once

#include <cstdint>

namespace stackchu {

class IBacklight {
public:
    virtual ~IBacklight() = default;

    // 初期化。ハードウェアを有効化し、既定の明るさを適用する。
    // 成功で true。
    virtual bool begin() = 0;

    // 明るさ 0–100 [%]。
    virtual void setBrightness(uint8_t percent) = 0;
    virtual uint8_t getBrightness() const = 0;

    // オン/オフ（オフ時は前回値を保持して enable で復元）。
    virtual void enable() = 0;
    virtual void disable() = 0;
};

}  // namespace stackchu
