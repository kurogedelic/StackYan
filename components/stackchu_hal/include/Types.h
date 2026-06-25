// Stackchu HAL — 共通型
// HAL 間で共有される値型・限界値・ユーティリティ。
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace stackchu {

// ---------------------------------------------------------------------------
// 色型（RGB 24bit）。Hex からの変換をサポート。
// ---------------------------------------------------------------------------
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    constexpr Color() = default;
    constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}

    // 0xRRGGBB 形式（例: 0xFF8800）
    static constexpr Color fromHex(uint32_t rgb) {
        return Color{
            static_cast<uint8_t>((rgb >> 16) & 0xFF),
            static_cast<uint8_t>((rgb >> 8) & 0xFF),
            static_cast<uint8_t>(rgb & 0xFF)};
    }

    // "#RRGGBB" 文字列形式（例: "#FF8800"）。不正時は黒。
    static Color fromHex(const char* s) {
        if (s == nullptr) return Color{};
        if (*s == '#') ++s;
        // 厳密には strtoul だが 16 進 6 桁前提
        char* end = nullptr;
        unsigned long v = std::strtoul(s, &end, 16);
        if (end == s) return Color{};
        return fromHex(static_cast<uint32_t>(v & 0xFFFFFFu));
    }
};

// よく使う定数色
inline constexpr Color kBlack{0, 0, 0};
inline constexpr Color kWhite{255, 255, 255};
inline constexpr Color kRed{255, 0, 0};
inline constexpr Color kGreen{0, 255, 0};
inline constexpr Color kBlue{0, 0, 255};

// ---------------------------------------------------------------------------
// 限界値（公式データシートに基づく「壊さない」ための上限）
// ---------------------------------------------------------------------------

// BMI270 加速度: ±2/4/8/16 g（公式最大 ±16g）
inline constexpr float kAccelMaxG     = 16.0f;   // [g]
inline constexpr float kAccelMinG     = -16.0f;

// BMI270 ジャイロ: ±125/250/500/1000/2000 dps（公式最大 ±2000 dps）
inline constexpr float kGyroMaxDps    = 2000.0f; // [dps]
inline constexpr float kGyroMinDps    = -2000.0f;

// ---------------------------------------------------------------------------
// 数値ユーティリティ
// ---------------------------------------------------------------------------

template <typename T>
constexpr T clampVal(T v, T lo, T hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

// 角度 [度] を指定範囲へクランプ
inline float clampAngle(float deg, float lo, float hi) {
    return clampVal(deg, lo, hi);
}

}  // namespace stackchu
