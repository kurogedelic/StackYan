// IIr — 赤外線 送受信抽象
//
// ユーザ要件（複数選択）:
//   1) 汎用 NEC リモコン      → sendNec / onReceived(NEC デコード)
//   2) StackChan 間通信        → NEC ベースの address/command で相互送受信
//   3) 学習リモコン            → sendRaw / enableReceive(captureRaw=true)
//
// 送受信とも ESP-IDF RMT で実装し、CPU 負荷を最小化する。
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace stackchu {

enum class IrProtocol : uint8_t {
    Unknown = 0,
    Nec,    // NEC 標準（家電の約 80%）
    Raw,    // 任意波形（学習/未知プロトコル）
};

// 受信メッセージ。
struct IrMessage {
    IrProtocol protocol = IrProtocol::Unknown;

    // NEC の場合: 8bit address / 8bit command（拡張アドレス時は 16bit 扱い）
    uint16_t address = 0;
    uint16_t command = 0;

    // Raw の場合: 交互の電圧レベルと持続時間 [us]。
    //   timings[0] = startHigh ? High : Low の時間, 以降交互。
    std::vector<uint16_t> timings;
    bool startHigh = true;
};

using IrCallback = std::function<void(const IrMessage&)>;

class IIr {
public:
    virtual ~IIr() = default;

    // 初期化。RMT TX/RX チャネルを設定する。成功で true。
    virtual bool begin() = 0;

    // NEC 1 フレーム送信。
    virtual void sendNec(uint8_t address, uint8_t command) = 0;

    // 任意波形送信（学習データの再生など）。
    //   timings: 持続時間 [us] の配列。startHigh で先頭電圧を指定。
    virtual void sendRaw(const uint16_t* timings, size_t n, bool startHigh) = 0;
    inline void sendRaw(const std::vector<uint16_t>& t, bool startHigh) {
        sendRaw(t.data(), t.size(), startHigh);
    }

    // 受信コールバック登録。
    virtual void onReceived(IrCallback cb) = 0;

    // 受信有効化。captureRaw=true なら生波形も IrMessage.timings に格納。
    virtual void enableReceive(bool captureRaw = false) = 0;
    virtual void disableReceive() = 0;
};

}  // namespace stackchu
