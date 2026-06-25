#pragma once

#include "Hal.h"
#include "IHardware.h"

namespace stackyan {

class EspHardware final : public IHardware {
public:
    explicit EspHardware(stackchu::Hal& hal) : hal_(hal) {}

    stackchu::IServo& servo() override { return hal_.servo(); }
    stackchu::IRgb& rgb() override { return hal_.rgb(); }
    stackchu::IMotionSensor& motion() override { return hal_.motion(); }
    stackchu::IPower& power() override { return hal_.power(); }
    stackchu::IStorage& storage() override { return hal_.storage(); }

private:
    stackchu::Hal& hal_;
};

}  // namespace stackyan
