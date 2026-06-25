#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "IHardware.h"

namespace stackyan::sim {

struct SimState {
    std::array<stackchu::Color, stackchu::IRgb::kCount> leds{};
    float brightness = 0.25f;
    float servoHorizontal = 0.0f;
    float servoVertical = 45.0f;
    stackchu::MotionSample motion{};
    stackchu::PowerState power{};
};

class SimRgb final : public stackchu::IRgb {
public:
    explicit SimRgb(SimState& state) : state_(state) {}
    bool begin() override { return true; }
    void setPixel(uint8_t index, stackchu::Color c) override;
    void setAll(stackchu::Color c) override;
    void setColumn(stackchu::LedColumn col, stackchu::Color c) override;
    void setBrightness(float scale) override;
    float getBrightness() const override;
    void clear() override;
    void show() override;
private:
    SimState& state_;
};

class SimServo final : public stackchu::IServo {
public:
    explicit SimServo(SimState& state) : state_(state) {}
    bool begin() override { return true; }
    void moveTo(stackchu::ServoAxis ax, float degrees, uint16_t speed = 1000) override;
    void spin(stackchu::ServoAxis ax, int16_t speed) override;
    void disableTorque(stackchu::ServoAxis ax) override;
    stackchu::ServoState readState(stackchu::ServoAxis ax) override;
    void setOverheatThreshold(float celsius) override { overheat_ = celsius; }
private:
    SimState& state_;
    float overheat_ = 65.0f;
};

class SimMotion final : public stackchu::IMotionSensor {
public:
    explicit SimMotion(SimState& state) : state_(state) {}
    bool begin(stackchu::AccelRange ar = stackchu::AccelRange::G8,
               stackchu::GyroRange gr = stackchu::GyroRange::DPS2000) override;
    stackchu::MotionSample read() override;
    bool isReady() const override { return ready_; }
private:
    SimState& state_;
    bool ready_ = false;
};

class SimPower final : public stackchu::IPower {
public:
    explicit SimPower(SimState& state) : state_(state) {}
    bool begin() override;
    stackchu::PowerState read() const override;
    void startMonitor(uint32_t intervalMs = 5000) override {}
    void stopMonitor() override {}
    void onLevel(uint8_t lowPercent, stackchu::PowerCallback cb) override {}
    void onChargeChange(stackchu::PowerCallback cb) override {}
private:
    SimState& state_;
};

class SimStorage final : public stackchu::IStorage {
public:
    explicit SimStorage(std::filesystem::path root) : root_(std::move(root)) {}
    ~SimStorage() override;

    bool mount() override;
    void unmount() override;
    bool isMounted() const override { return mounted_; }
    const char* root() const override { return rootString_.c_str(); }

    stackchu::FileHandle open(const char* path, stackchu::OpenMode mode) override;
    bool close(stackchu::FileHandle h) override;
    size_t read(stackchu::FileHandle h, void* dst, size_t len) override;
    size_t write(stackchu::FileHandle h, const void* src, size_t len) override;
    bool rewind(stackchu::FileHandle h) override;
    long tell(stackchu::FileHandle h) override;
    long size(stackchu::FileHandle h) override;
    bool readAll(const char* path, std::vector<uint8_t>& out) override;
    bool writeAll(const char* path, const void* src, size_t len) override;
    bool exists(const char* path) override;
    bool remove(const char* path) override;
    bool mkdir(const char* path) override;
    bool listDir(const char* path, std::vector<stackchu::DirEntry>& out) override;
    long freeSpace() override;

private:
    std::filesystem::path resolve(const char* path) const;
    int allocHandle(FILE* fp);

    std::filesystem::path root_;
    std::string rootString_;
    bool mounted_ = false;
    std::mutex mutex_;
    std::map<int, FILE*> handles_;
    int nextHandle_ = 1;
};

class SimHardware final : public stackyan::IHardware {
public:
    SimHardware();
    bool begin();
    void printState() const;

    stackchu::IServo& servo() override { return servo_; }
    stackchu::IRgb& rgb() override { return rgb_; }
    stackchu::IMotionSensor& motion() override { return motion_; }
    stackchu::IPower& power() override { return power_; }
    stackchu::IStorage& storage() override { return storage_; }

private:
    SimState state_;
    SimRgb rgb_;
    SimServo servo_;
    SimMotion motion_;
    SimPower power_;
    SimStorage storage_;
};

}  // namespace stackyan::sim
