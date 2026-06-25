#pragma once

namespace stackyan {

class NetworkManager {
public:
    bool begin();
    const char* hostname() const { return "stackyan"; }
    const char* apSsid() const { return "StackYan-Setup"; }
};

}  // namespace stackyan
