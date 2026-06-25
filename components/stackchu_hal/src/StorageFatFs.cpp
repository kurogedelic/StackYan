// StorageFatFs — FATFS（wear levelling）で IStorage を実装。
//
// ESP-IDF の FATFS + wear-leveling を使い、partitions.csv の `storage`
// パーティション（FAT, subtype=fat）をマウントする。
//
// パーティションラベルは "storage" とする（partitions.csv と一致させること）。
// マウントポイントは "/storage"。
//
// パスの扱い:
//   - 利用者は "/settings/foo.json"（ルート相対）か
//     "/storage/settings/foo.json"（絶対）のどちらでも渡せる。
//   - 内部で絶対パスへ正規化してファイル API を呼ぶ。
#include "hal/IStorage.h"
#include "HalPins.h"

#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <mutex>
#include <string>

namespace stackchu {

namespace {
constexpr const char* kTag    = "storage";
constexpr const char* kMount  = pins::STORAGE_MOUNT_POINT;
constexpr const char* kPartLabel = pins::STORAGE_PART_LABEL;

// 同時に開けるファイルハンドル上限。
constexpr size_t kMaxHandles = 8;

// FileHandle.id は 0.. を配列添字として扱う。-1 は無効。
struct Slot {
    bool used = false;
    int  fd   = -1;
};
}  // namespace

class StorageFatFs final : public IStorage {
public:
    ~StorageFatFs() override { unmount(); }

    bool mount() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (mounted_) return true;

        // VFS + FAT + WL を一括設定するヘルパを使う。
        esp_vfs_fat_mount_config_t cfg = {};
        cfg.format_if_mount_failed = true;   // マウント失敗時はフォーマット
        cfg.max_files = 8;
        cfg.allocation_unit_size = 16 * 1024;

        esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
            kMount, kPartLabel, &cfg, &wl_handle_);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "mount failed: %s", esp_err_to_name(err));
            return false;
        }
        mounted_ = true;
        ESP_LOGI(kTag, "mounted at %s", kMount);
        return true;
    }

    void unmount() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (!mounted_) return;
        // 開いているハンドルを全て閉じる
        for (auto& s : slots_) {
            if (s.used && s.fd >= 0) { ::close(s.fd); s.fd = -1; s.used = false; }
        }
        esp_vfs_fat_spiflash_unmount_rw_wl(kMount, wl_handle_);
        mounted_ = false;
    }

    bool isMounted() const override { return mounted_; }
    const char* root() const override { return kMount; }

    // ---- 低レベル ----
    FileHandle open(const char* path, OpenMode mode) override {
        FileHandle h;
        std::lock_guard<std::mutex> lk(mu_);
        if (!mounted_) return h;

        int slotIdx = allocSlot();
        if (slotIdx < 0) return h;

        std::string full = normalize(path);
        int flags = 0;
        switch (mode) {
            case OpenMode::Read:      flags = O_RDONLY; break;
            case OpenMode::Write:     flags = O_WRONLY | O_CREAT | O_TRUNC; break;
            case OpenMode::Append:    flags = O_WRONLY | O_CREAT | O_APPEND; break;
            case OpenMode::ReadWrite: flags = O_RDWR; break;
        }
        int fd = ::open(full.c_str(), flags, 0666);
        if (fd < 0) {
            ESP_LOGW(kTag, "open failed: %s", full.c_str());
            return h;  // 無効
        }
        slots_[slotIdx].used = true;
        slots_[slotIdx].fd   = fd;
        h.id = slotIdx;
        return h;
    }

    bool close(FileHandle h) override {
        if (!h.valid()) return false;
        std::lock_guard<std::mutex> lk(mu_);
        if (!slotOk_(h)) return false;
        int r = ::close(slots_[h.id].fd);
        slots_[h.id] = Slot{};
        return r == 0;
    }

    size_t read(FileHandle h, void* dst, size_t len) override {
        if (!h.valid() || dst == nullptr) return 0;
        std::lock_guard<std::mutex> lk(mu_);
        if (!slotOk_(h)) return 0;
        ssize_t n = ::read(slots_[h.id].fd, dst, len);
        return n > 0 ? (size_t)n : 0;
    }

    size_t write(FileHandle h, const void* src, size_t len) override {
        if (!h.valid() || src == nullptr) return 0;
        std::lock_guard<std::mutex> lk(mu_);
        if (!slotOk_(h)) return 0;
        ssize_t n = ::write(slots_[h.id].fd, src, len);
        return n > 0 ? (size_t)n : 0;
    }

    bool rewind(FileHandle h) override {
        if (!h.valid()) return false;
        std::lock_guard<std::mutex> lk(mu_);
        if (!slotOk_(h)) return false;
        return ::lseek(slots_[h.id].fd, 0, SEEK_SET) >= 0;
    }

    long tell(FileHandle h) override {
        if (!h.valid()) return -1;
        std::lock_guard<std::mutex> lk(mu_);
        if (!slotOk_(h)) return -1;
        return ::lseek(slots_[h.id].fd, 0, SEEK_CUR);
    }

    long size(FileHandle h) override {
        if (!h.valid()) return -1;
        std::lock_guard<std::mutex> lk(mu_);
        if (!slotOk_(h)) return -1;
        struct stat st;
        if (::fstat(slots_[h.id].fd, &st) != 0) return -1;
        return (long)st.st_size;
    }

    // ---- 高レベル ----
    bool readAll(const char* path, std::vector<uint8_t>& out) override {
        std::string full = normalize(path);
        std::lock_guard<std::mutex> lk(mu_);
        if (!mounted_) return false;
        FILE* fp = std::fopen(full.c_str(), "rb");
        if (!fp) return false;
        std::fseek(fp, 0, SEEK_END);
        long sz = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (sz < 0) { std::fclose(fp); return false; }
        out.resize((size_t)sz);
        size_t rd = (sz > 0) ? std::fread(out.data(), 1, (size_t)sz, fp) : 0;
        std::fclose(fp);
        return rd == (size_t)sz;
    }

    bool writeAll(const char* path, const void* src, size_t len) override {
        std::string full = normalize(path);
        std::lock_guard<std::mutex> lk(mu_);
        if (!mounted_) return false;
        // 親ディレクトリを確保（簡易: 末尾コンポーネント以外を mkdir）
        ensureParentDir(full);
        FILE* fp = std::fopen(full.c_str(), "wb");
        if (!fp) return false;
        size_t wr = (len > 0) ? std::fwrite(src, 1, len, fp) : 0;
        std::fclose(fp);
        return wr == len;
    }

    // ---- FS 操作 ----
    bool exists(const char* path) override {
        std::string full = normalize(path);
        struct stat st;
        return ::stat(full.c_str(), &st) == 0;
    }

    bool remove(const char* path) override {
        std::string full = normalize(path);
        return ::unlink(full.c_str()) == 0;
    }

    bool mkdir(const char* path) override {
        std::string full = normalize(path);
        // 既存なら成功扱い
        struct stat st;
        if (::stat(full.c_str(), &st) == 0) return true;
        return ::mkdir(full.c_str(), 0777) == 0;
    }

    bool listDir(const char* path, std::vector<DirEntry>& out) override {
        std::string full = normalize(path);
        DIR* d = ::opendir(full.c_str());
        if (!d) return false;
        out.clear();
        struct dirent* e;
        while ((e = ::readdir(d)) != nullptr) {
            // "." と ".." はスキップ
            if (std::strcmp(e->d_name, ".") == 0 ||
                std::strcmp(e->d_name, "..") == 0) continue;
            DirEntry de;
            size_t nl = std::strlen(e->d_name);
            de.name.assign(e->d_name, e->d_name + nl + 1);  // NUL 含む
            de.isDir = (e->d_type == DT_DIR);
            if (e->d_type != DT_DIR) {
                std::string p = full + "/" + e->d_name;
                struct stat st;
                if (::stat(p.c_str(), &st) == 0) de.size = (size_t)st.st_size;
            }
            out.push_back(std::move(de));
        }
        ::closedir(d);
        return true;
    }

    long freeSpace() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (!mounted_) return -1;
        // FATFS のサイズ取得。esp_vfs_fat はマウント時にグローバルへ登録される。
        DWORD fre_clust = 0;
        FATFS* fs = nullptr;
        // パスクッキーはマウントポイントの先頭文字（FATFS の仕様）
        if (f_getfree(kMount, &fre_clust, &fs) != FR_OK) return -1;
        if (!fs) return -1;
#if FF_MAX_SS != FF_MIN_SS
        return (long)(fre_clust * fs->csize * (fs->ssize));
#else
        return (long)(fre_clust * fs->csize * FF_MAX_SS);
#endif
    }

private:
    // 利用者パス -> 絶対パスへ正規化
    std::string normalize(const char* path) const {
        if (path == nullptr) return kMount;
        // 既にマウントポイントで始まっていればそのまま
        std::string p(path);
        if (p.rfind(kMount, 0) == 0) return p;
        // 先頭の / は除いて結合
        while (!p.empty() && p[0] == '/') p.erase(p.begin());
        std::string full = kMount;
        full += "/";
        full += p;
        return full;
    }

    int allocSlot() {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].used) return (int)i;
        }
        return -1;
    }
    bool slotOk_(FileHandle h) const {
        return (size_t)h.id < slots_.size() && slots_[h.id].used;
    }

    // 書込前に親ディレクトリを段階的に作成（簡易版）。
    void ensureParentDir(const std::string& full) {
        // full は "/storage/.../file"
        size_t slash = full.find_last_of('/');
        if (slash == std::string::npos || slash == 0) return;
        std::string dir = full.substr(0, slash);
        // /storage 以下を '/' ごとに mkdir
        size_t pos = std::string(kMount).size();  // "/storage" の長さから開始
        while (pos != std::string::npos && pos < dir.size()) {
            pos = dir.find('/', pos + 1);
            std::string sub = (pos == std::string::npos) ? dir : dir.substr(0, pos);
            ::mkdir(sub.c_str(), 0777);  // 既存でも無視
        }
    }

    std::array<Slot, kMaxHandles> slots_;
    wl_handle_t wl_handle_ = WL_INVALID_HANDLE;
    bool mounted_ = false;
    mutable std::mutex mu_;
};

// ファサード用ファクトリ
IStorage* createStorage() { return new StorageFatFs(); }

}  // namespace stackchu
