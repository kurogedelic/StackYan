// IRgb — RGB LED 抽象（SK6812 / NeoPixel 互換 × 12 個・2 列）
//
// 設計意図（ユーザ質問への回答）:
//   - 「RGB は別々に操作できるのか？」 → はい。各 LED は index で個別指定可。
//   - 「RGB hex でいいのか？」         → はい。Color::fromHex("#RRGGBB") をサポート。
//   - 全体輝度は過電流保護のため上限でクランプ（HalPins.h の RGB_BRIGHTNESS_LIMIT）。
#pragma once

#include "Types.h"
#include <cstdint>

namespace stackchu {

// 2 列構成の抽象。物理的な index 割り当ては実装依存。
// 便宜的に 0–5 を Left、6–11 を Right とみなす。
enum class LedColumn : uint8_t {
    Left  = 0,
    Right = 1,
};

class IRgb {
public:
    static constexpr uint8_t kCount = 12;

    virtual ~IRgb() = default;

    // 初期化。RMT チャネルとエンコーダを設定する。成功で true。
    virtual bool begin() = 0;

    // 個別 LED 指定（index 0..kCount-1）。show() を呼ぶまで確定しない。
    virtual void setPixel(uint8_t index, Color c) = 0;

    // 全 LED 同一色。
    virtual void setAll(Color c) = 0;

    // 左/右 列まとめて指定。
    virtual void setColumn(LedColumn col, Color c) = 0;

    // 全体輝度スケール 0.0–1.0（show 適用前に乗算される）。
    virtual void setBrightness(float scale) = 0;
    virtual float getBrightness() const = 0;

    // 全消灯（バッファを 0 にして即時送信）。
    virtual void clear() = 0;

    // バッファを実際の LED へ反映。明示呼び出し式でちらつきを抑える。
    virtual void show() = 0;
};

}  // namespace stackchu
