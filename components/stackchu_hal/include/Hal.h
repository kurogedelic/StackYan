// Hal — 統合ファサード
// 全サブシステムの初期化と、各インターフェース参照の取得口。
//
// 使い方:
//   stackchu::Hal hal;
//   if (!hal.begin()) { /* エラー処理 */ }
//   hal.rgb().setAll(stackchu::Color::fromHex("#00FF00"));
//   hal.rgb().show();
#pragma once

#include "hal/IBacklight.h"
#include "hal/IIr.h"
#include "hal/IMotionSensor.h"
#include "hal/IPower.h"
#include "hal/IRgb.h"
#include "hal/IServo.h"
#include "hal/IStorage.h"

#include <memory>

namespace stackchu {

class Hal {
public:
    Hal();
    ~Hal();

    Hal(const Hal&) = delete;
    Hal& operator=(const Hal&) = delete;

    // 全サブシステムを初期化。I2C はここで共用バスとして設定される。
    // いずれか致命的でなければ true。
    bool begin();

    // 各サブシステムへの参照（begin 後に有効）。
    IBacklight&    backlight();
    IServo&        servo();
    IRgb&          rgb();
    IMotionSensor& motion();
    IIr&           ir();
    IPower&        power();
    IStorage&      storage();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace stackchu
