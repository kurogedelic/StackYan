#pragma once

#include "hal/IMotionSensor.h"
#include "hal/IPower.h"
#include "hal/IRgb.h"
#include "hal/IServo.h"
#include "hal/IStorage.h"

namespace stackyan {

class IHardware {
public:
    virtual ~IHardware() = default;
    virtual stackchu::IServo& servo() = 0;
    virtual stackchu::IRgb& rgb() = 0;
    virtual stackchu::IMotionSensor& motion() = 0;
    virtual stackchu::IPower& power() = 0;
    virtual stackchu::IStorage& storage() = 0;
};

}  // namespace stackyan
