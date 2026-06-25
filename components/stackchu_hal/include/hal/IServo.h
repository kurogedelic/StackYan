// IServo — サーボ抽象（FEETech バスサーボ / 位置・温度・負荷フィードバック付き）
//
// StackChan 本体の 2 基:
//   - Horizontal: 水平 360° 無限回転（連続回転型）
//   - Vertical  : 垂直 90° 可動（公式推奨動作範囲 5–85°）
//
// 「壊さない」ための安全機構を HAL 側で担保する:
//   - 垂直軸は必ず 5–85° にクランプ
//   - 温度が上限を超えたらトルク解除（disableTorque 相当）
#pragma once

#include <cstdint>

namespace stackchu {

enum class ServoAxis : uint8_t {
    Horizontal = 0,  // 水平（360° 連続回転）
    Vertical   = 1,  // 垂直（90° 可動）
};

// サーボ状態のフィードバック。
struct ServoState {
    bool    valid       = false;  // 応答が取れたか
    float   positionDeg = 0.0f;   // 位置 [度]（水平は回転量の目安）
    int16_t speed       = 0;      // 現在速度ステップ
    int16_t load        = 0;      // 負荷（トルク代理値）
    float   temperatureC = 0.0f;  // 温度 [℃]
    float   voltage     = 0.0f;   // 電圧 [V]
};

class IServo {
public:
    virtual ~IServo() = default;

    // 初期化。UART を設定しサーボバスを有効化する。成功で true。
    // ※トルクは解除状態で起動する（安全）。
    virtual bool begin() = 0;

    // 指定角度へ移動（垂直は 5–85° に自動クランプ）。
    // speed は移動速度ステップ（0=最大速度）。
    virtual void moveTo(ServoAxis ax, float degrees, uint16_t speed = 1000) = 0;

    // 連続回転（主に水平軸用）。speed は -最大..+最大。
    virtual void spin(ServoAxis ax, int16_t speed) = 0;

    // トルク解除（フリー）。過熱時の自動保護でも使用。
    virtual void disableTorque(ServoAxis ax) = 0;

    // 現在状態を読出し（フィードバック）。
    virtual ServoState readState(ServoAxis ax) = 0;

    // 過熱保護の閾値 [℃]。既定 65℃。
    virtual void setOverheatThreshold(float celsius) = 0;
};

}  // namespace stackchu
