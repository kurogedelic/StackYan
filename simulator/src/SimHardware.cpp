#include "SimHardware.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace stackyan::sim {
namespace {

float clampFloat(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

std::string colorText(stackchu::Color c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

void addColor(cJSON* array, stackchu::Color c) {
    cJSON_AddItemToArray(array, cJSON_CreateString(colorText(c).c_str()));
}

}  // namespace

void SimRgb::setPixel(uint8_t index, stackchu::Color c) {
    if (index < state_.leds.size()) state_.leds[index] = c;
}

void SimRgb::setAll(stackchu::Color c) {
    state_.leds.fill(c);
}

void SimRgb::setColumn(stackchu::LedColumn col, stackchu::Color c) {
    const size_t start = (col == stackchu::LedColumn::Left) ? 0 : 6;
    for (size_t i = start; i < start + 6 && i < state_.leds.size(); ++i) state_.leds[i] = c;
}

void SimRgb::setBrightness(float scale) {
    state_.brightness = clampFloat(scale, 0.0f, 1.0f);
}

float SimRgb::getBrightness() const {
    return state_.brightness;
}

void SimRgb::clear() {
    state_.leds.fill(stackchu::Color{});
    show();
}

void SimRgb::show() {
    std::cout << "[sim] RGB brightness=" << state_.brightness << " leds=";
    for (const auto& led : state_.leds) std::cout << colorText(led) << ' ';
    std::cout << std::endl;
}

void SimServo::moveTo(stackchu::ServoAxis ax, float degrees, uint16_t speed) {
    if (ax == stackchu::ServoAxis::Vertical) {
        state_.servoVertical = clampFloat(degrees, 5.0f, 85.0f);
    } else {
        while (degrees < 0) degrees += 360.0f;
        while (degrees >= 360.0f) degrees -= 360.0f;
        state_.servoHorizontal = degrees;
    }
    std::cout << "[sim] Servo " << (ax == stackchu::ServoAxis::Vertical ? "vertical" : "horizontal")
              << " -> " << (ax == stackchu::ServoAxis::Vertical ? state_.servoVertical : state_.servoHorizontal)
              << " speed=" << speed << std::endl;
}

void SimServo::spin(stackchu::ServoAxis ax, int16_t speed) {
    std::cout << "[sim] Servo spin " << (ax == stackchu::ServoAxis::Vertical ? "vertical" : "horizontal")
              << " speed=" << speed << std::endl;
}

void SimServo::disableTorque(stackchu::ServoAxis ax) {
    std::cout << "[sim] Servo torque off " << (ax == stackchu::ServoAxis::Vertical ? "vertical" : "horizontal") << std::endl;
}

stackchu::ServoState SimServo::readState(stackchu::ServoAxis ax) {
    stackchu::ServoState state;
    state.valid = true;
    state.positionDeg = (ax == stackchu::ServoAxis::Vertical) ? state_.servoVertical : state_.servoHorizontal;
    state.speed = 0;
    state.load = 0;
    state.temperatureC = 32.0f;
    state.voltage = 5.0f;
    return state;
}

bool SimMotion::begin(stackchu::AccelRange, stackchu::GyroRange) {
    ready_ = true;
    state_.motion.valid = true;
    state_.motion.ax = 0.01f;
    state_.motion.ay = -0.02f;
    state_.motion.az = 1.00f;
    state_.motion.gx = 0.2f;
    state_.motion.gy = -0.1f;
    state_.motion.gz = 0.0f;
    state_.motion.temperatureC = 28.0f;
    return true;
}

stackchu::MotionSample SimMotion::read() {
    return state_.motion;
}

bool SimPower::begin() {
    state_.power.level = 87;
    state_.power.charge = stackchu::ChargeState::Discharging;
    state_.power.batteryVoltage = 3.95f;
    state_.power.batteryCurrent = 120;
    state_.power.temperatureC = 30.0f;
    state_.power.usbConnected = true;
    return true;
}

stackchu::PowerState SimPower::read() const {
    return state_.power;
}

SimStorage::~SimStorage() {
    unmount();
}

bool SimStorage::mount() {
    std::lock_guard<std::mutex> lock(mutex_);
    fs::create_directories(root_);
    rootString_ = fs::absolute(root_).string();
    mounted_ = true;
    std::cout << "[sim] Storage mounted at " << rootString_ << std::endl;
    return true;
}

void SimStorage::unmount() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [_, fp] : handles_) {
        if (fp) std::fclose(fp);
    }
    handles_.clear();
    mounted_ = false;
}

fs::path SimStorage::resolve(const char* path) const {
    if (!path || !path[0]) return root_;
    fs::path p(path);
    if (p.is_absolute()) {
        std::string s = p.string();
        if (s.rfind(rootString_, 0) == 0) return p;
        while (!s.empty() && s[0] == '/') s.erase(s.begin());
        return root_ / s;
    }
    return root_ / p;
}

int SimStorage::allocHandle(FILE* fp) {
    const int id = nextHandle_++;
    handles_[id] = fp;
    return id;
}

stackchu::FileHandle SimStorage::open(const char* path, stackchu::OpenMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    stackchu::FileHandle h;
    if (!mounted_) return h;
    const fs::path full = resolve(path);
    fs::create_directories(full.parent_path());
    const char* m = "rb";
    switch (mode) {
        case stackchu::OpenMode::Read: m = "rb"; break;
        case stackchu::OpenMode::Write: m = "wb"; break;
        case stackchu::OpenMode::Append: m = "ab"; break;
        case stackchu::OpenMode::ReadWrite: m = "r+b"; break;
    }
    FILE* fp = std::fopen(full.string().c_str(), m);
    if (!fp) return h;
    h.id = allocHandle(fp);
    return h;
}

bool SimStorage::close(stackchu::FileHandle h) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(h.id);
    if (it == handles_.end()) return false;
    std::fclose(it->second);
    handles_.erase(it);
    return true;
}

size_t SimStorage::read(stackchu::FileHandle h, void* dst, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(h.id);
    return it == handles_.end() ? 0 : std::fread(dst, 1, len, it->second);
}

size_t SimStorage::write(stackchu::FileHandle h, const void* src, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(h.id);
    return it == handles_.end() ? 0 : std::fwrite(src, 1, len, it->second);
}

bool SimStorage::rewind(stackchu::FileHandle h) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(h.id);
    if (it == handles_.end()) return false;
    std::rewind(it->second);
    return true;
}

long SimStorage::tell(stackchu::FileHandle h) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(h.id);
    return it == handles_.end() ? -1 : std::ftell(it->second);
}

long SimStorage::size(stackchu::FileHandle h) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handles_.find(h.id);
    if (it == handles_.end()) return -1;
    const long pos = std::ftell(it->second);
    std::fseek(it->second, 0, SEEK_END);
    const long sz = std::ftell(it->second);
    std::fseek(it->second, pos, SEEK_SET);
    return sz;
}

bool SimStorage::readAll(const char* path, std::vector<uint8_t>& out) {
    const fs::path full = resolve(path);
    std::ifstream in(full, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool SimStorage::writeAll(const char* path, const void* src, size_t len) {
    const fs::path full = resolve(path);
    fs::create_directories(full.parent_path());
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(static_cast<const char*>(src), static_cast<std::streamsize>(len));
    return static_cast<bool>(out);
}

bool SimStorage::exists(const char* path) {
    return fs::exists(resolve(path));
}

bool SimStorage::remove(const char* path) {
    return fs::remove(resolve(path));
}

bool SimStorage::mkdir(const char* path) {
    return fs::create_directories(resolve(path)) || fs::exists(resolve(path));
}

bool SimStorage::listDir(const char* path, std::vector<stackchu::DirEntry>& out) {
    const fs::path dir = resolve(path);
    if (!fs::is_directory(dir)) return false;
    out.clear();
    for (const auto& entry : fs::directory_iterator(dir)) {
        stackchu::DirEntry de;
        const std::string name = entry.path().filename().string();
        de.name.assign(name.begin(), name.end());
        de.name.push_back('\0');
        de.isDir = entry.is_directory();
        de.size = de.isDir ? 0 : static_cast<size_t>(entry.file_size());
        out.push_back(std::move(de));
    }
    return true;
}

long SimStorage::freeSpace() {
    std::error_code ec;
    const auto info = fs::space(root_, ec);
    return ec ? -1 : static_cast<long>(info.available);
}

SimHardware::SimHardware()
    : rgb_(state_),
      servo_(state_),
      motion_(state_),
      power_(state_),
      storage_("./sd") {}

bool SimHardware::begin() {
    const bool ok = storage_.mount() && motion_.begin() && power_.begin() && rgb_.begin() && servo_.begin();
    printState();
    return ok;
}

void SimHardware::printState() const {
    std::cout << "[sim] State: servo(horizontal=" << state_.servoHorizontal
              << ", vertical=" << state_.servoVertical << ") motion.az="
              << state_.motion.az << " battery=" << static_cast<int>(state_.power.level) << "%"
              << std::endl;
}

cJSON* SimHardware::stateJson() const {
    cJSON* root = cJSON_CreateObject();

    cJSON* rgb = cJSON_AddObjectToObject(root, "rgb");
    cJSON_AddNumberToObject(rgb, "brightness", state_.brightness);
    cJSON* leds = cJSON_AddArrayToObject(rgb, "leds");
    for (const auto& led : state_.leds) addColor(leds, led);

    cJSON* servo = cJSON_AddObjectToObject(root, "servo");
    cJSON_AddNumberToObject(servo, "horizontal", state_.servoHorizontal);
    cJSON_AddNumberToObject(servo, "vertical", state_.servoVertical);

    cJSON* motion = cJSON_AddObjectToObject(root, "motion");
    cJSON_AddBoolToObject(motion, "valid", state_.motion.valid);
    cJSON_AddNumberToObject(motion, "ax", state_.motion.ax);
    cJSON_AddNumberToObject(motion, "ay", state_.motion.ay);
    cJSON_AddNumberToObject(motion, "az", state_.motion.az);
    cJSON_AddNumberToObject(motion, "gx", state_.motion.gx);
    cJSON_AddNumberToObject(motion, "gy", state_.motion.gy);
    cJSON_AddNumberToObject(motion, "gz", state_.motion.gz);
    cJSON_AddNumberToObject(motion, "temperature_c", state_.motion.temperatureC);

    cJSON* power = cJSON_AddObjectToObject(root, "power");
    cJSON_AddNumberToObject(power, "level", state_.power.level);
    cJSON_AddNumberToObject(power, "battery_voltage", state_.power.batteryVoltage);
    cJSON_AddNumberToObject(power, "battery_current_ma", state_.power.batteryCurrent);
    cJSON_AddNumberToObject(power, "temperature_c", state_.power.temperatureC);
    cJSON_AddBoolToObject(power, "usb_connected", state_.power.usbConnected);

    cJSON* storage = cJSON_AddObjectToObject(root, "storage");
    cJSON_AddBoolToObject(storage, "mounted", storage_.isMounted());
    cJSON_AddStringToObject(storage, "root", storage_.root());

    return root;
}

}  // namespace stackyan::sim
