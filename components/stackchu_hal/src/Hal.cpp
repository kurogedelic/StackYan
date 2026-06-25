// Hal.cpp — 統合ファサード実装。
// 各サブシステムを生成し、begin() で初期化順序を制御する。
//
// 初期化順序の意図:
//   1. Backlight  (画面を見える化、早めに)
//   2. Power      (電源状態把握、以降のログの安全確保)
//   3. Storage    (FATFS マウント、以降の永続化を有効化)
//   4. Motion     (IMU)
//   5. RGB        (LED)
//   6. IR         (赤外線)
//   7. Servo      (サーボ、最後に：起動直後の急動作回避)
//
// 各 begin/mount は失敗しても全体は続行（部分的に動作させる）。失敗はログ出力。
#include "Hal.h"
#include "HalPins.h"

#include "esp_log.h"

#include <memory>

namespace stackchu {

// 各実装のファクトリ（各 cpp で定義）
IBacklight*    createBacklight();
IPower*        createPower();
IMotionSensor* createMotion();
IRgb*          createRgb();
IIr*           createIr();
IServo*        createServo();
IStorage*      createStorage();

struct Hal::Impl {
    std::unique_ptr<IBacklight>    bl;
    std::unique_ptr<IPower>        pw;
    std::unique_ptr<IStorage>      st;
    std::unique_ptr<IMotionSensor> mo;
    std::unique_ptr<IRgb>          rg;
    std::unique_ptr<IIr>           ir;
    std::unique_ptr<IServo>        sv;

    bool okBl = false, okPw = false, okSt = false, okMo = false;
    bool okRg = false, okIr = false, okSv = false;
};

Hal::Hal() : p_(std::make_unique<Impl>()) {}
Hal::~Hal() = default;

bool Hal::begin() {
    bool anyOk = false;
    auto tryBegin = [](auto& up, bool& ok, const char* name) {
        if (up && up->begin()) { ok = true; ESP_LOGI("hal", "%s: ok", name); }
        else { ESP_LOGW("hal", "%s: failed (continuing)", name); }
    };

    p_->bl = std::unique_ptr<IBacklight>(createBacklight());
    p_->pw = std::unique_ptr<IPower>(createPower());
    p_->st = std::unique_ptr<IStorage>(createStorage());
    p_->mo = std::unique_ptr<IMotionSensor>(createMotion());
    p_->rg = std::unique_ptr<IRgb>(createRgb());
    p_->ir = std::unique_ptr<IIr>(createIr());
    p_->sv = std::unique_ptr<IServo>(createServo());

    tryBegin(p_->bl, p_->okBl, "backlight");
    tryBegin(p_->pw, p_->okPw, "power");
    // Storage は begin() ではなく mount() を使う
    if (p_->st && p_->st->mount()) { p_->okSt = true; ESP_LOGI("hal", "storage: ok"); }
    else { ESP_LOGW("hal", "storage: mount failed (continuing)"); }
    tryBegin(p_->mo, p_->okMo, "motion");
    tryBegin(p_->rg, p_->okRg, "rgb");
    tryBegin(p_->ir, p_->okIr, "ir");
    tryBegin(p_->sv, p_->okSv, "servo");

    anyOk = p_->okBl || p_->okPw || p_->okSt || p_->okMo ||
            p_->okRg || p_->okIr || p_->okSv;
    return anyOk;
}

IBacklight&    Hal::backlight() { return *p_->bl; }
IPower&        Hal::power()     { return *p_->pw; }
IStorage&      Hal::storage()   { return *p_->st; }
IMotionSensor& Hal::motion()    { return *p_->mo; }
IRgb&          Hal::rgb()       { return *p_->rg; }
IIr&           Hal::ir()        { return *p_->ir; }
IServo&        Hal::servo()     { return *p_->sv; }

}  // namespace stackchu
