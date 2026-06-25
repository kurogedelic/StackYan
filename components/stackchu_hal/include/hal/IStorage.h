// IStorage — ストレージ抽象（ファイル API ベース）
//
// 設計方針（ユーザ選択: ファイル API のみ）:
//   - 大きなデータ（音声/表情画像/IR 学習波形）も設定値も、すべて「ファイル」として扱う。
//   - FATFS（partitions.csv の `storage` パーティション）をマウントし、ルート以下を操作。
//   - POSIX ライクな open/read/write/close と、便利ヘルパを併用可。
//
// 想定レイアウト（マウントポイント）:
//   /storage/            <- ルート（FAT）
//   /storage/settings/   <- 設定値（JSON/バイナリ）
//   /storage/ir/         <- IR 学習パターン
//   /storage/audio/      <- 音声データ
//   /storage/faces/      <- 表情データ
//
// パスはルート相対（"/settings/foo.json" のように先頭の /storage を省略可、
// または "/storage/settings/foo.json" の絶対パスも許容）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace stackchu {

// 読み書きモード
enum class OpenMode : uint8_t {
    Read,            // 読取専用。存在しない場合は失敗。
    Write,           // 書込。ファイルを切り詰めて先頭から。存在しなければ作成。
    Append,          // 末尾追記。存在しなければ作成。
    ReadWrite,       // 読み書き両用（切り詰めなし）。
};

// ファイルハンドル（実装は不透明ポインタ風の整数 ID）。
// 無効値。ハンドルがこれと等しければ「開いていない」。
struct FileHandle {
    static constexpr int kInvalid = -1;
    int id = kInvalid;
    bool valid() const { return id != kInvalid; }
};

// ディレクトリエントリ
struct DirEntry {
    std::vector<char> name;  // UTF-8 名（NUL 終端含む）
    bool   isDir    = false;
    size_t size     = 0;     // ファイルの場合のバイト数
};

class IStorage {
public:
    virtual ~IStorage() = default;

    // --- ライフサイクル ---
    // ファイルシステムをマウント。成功で true。
    // 既にマウント済みなら何もせず true。
    virtual bool mount() = 0;

    // アンマウント。flush してから解除。
    virtual void unmount() = 0;

    // マウント状態。
    virtual bool isMounted() const = 0;

    // ルートパス（例: "/storage"）。
    virtual const char* root() const = 0;

    // --- ファイル操作（低レベル POSIX ライク）---
    // 成功なら有効な FileHandle、失敗なら無効値。
    virtual FileHandle open(const char* path, OpenMode mode) = 0;

    // フラッシュして閉じる。
    virtual bool close(FileHandle h) = 0;

    // 読込。実際に読めたバイト数を返す（0 = EOF またはエラー）。
    virtual size_t read(FileHandle h, void* dst, size_t len) = 0;

    // 書込。実際に書けたバイト数を返す。
    virtual size_t write(FileHandle h, const void* src, size_t len) = 0;

    // 現在位置を先頭に戻す。
    virtual bool rewind(FileHandle h) = 0;

    // 現在の読み書き位置 [バイト]。失敗時 -1。
    virtual long tell(FileHandle h) = 0;

    // サイズ [バイト]。失敗時 -1。
    virtual long size(FileHandle h) = 0;

    // --- ファイル操作（高レベル便利 API）---
    // 全内容を一括読込。存在しない場合は空を返す。
    virtual bool readAll(const char* path, std::vector<uint8_t>& out) = 0;

    // 全内容を一括書込（Write モード相当）。既存は上書き。
    virtual bool writeAll(const char* path, const void* src, size_t len) = 0;

    // テキスト一括読込（NUL 終端）。
    bool readText(const char* path, std::vector<char>& out) {
        std::vector<uint8_t> raw;
        if (!readAll(path, raw)) return false;
        out.assign(raw.begin(), raw.end());
        out.push_back('\0');
        return true;
    }

    // テキスト一括書込。
    bool writeText(const char* path, const char* text) {
        return writeAll(path, text, std::strlen(text));
    }

    // --- ファイルシステム操作 ---
    // ファイル/ディレクトリの存在確認。
    virtual bool exists(const char* path) = 0;

    // ファイル削除。存在しなければ false。
    virtual bool remove(const char* path) = 0;

    // ディレクトリ作成（再帰なし）。成功/既存で true。
    virtual bool mkdir(const char* path) = 0;

    // ディレクトリ内容を一覧取得。path はディレクトリ。
    virtual bool listDir(const char* path, std::vector<DirEntry>& out) = 0;

    // 空き容量 [バイト]。失敗時 -1。
    virtual long freeSpace() = 0;
};

}  // namespace stackchu
