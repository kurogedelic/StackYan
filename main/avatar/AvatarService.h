#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "cJSON.h"

namespace stackyan {

class AvatarService {
public:
    bool setExpression(const char* expression);
    bool setVowel(const char* vowel);
    void setCaption(const char* line1, const char* line2);
    void clearCaption();
    void setPalette(const char* palette);
    void reset();

    cJSON* stateJson() const;

    static bool isValidExpression(const char* expression);
    static bool isValidVowel(const char* vowel);
    static bool isValidPalette(const char* palette);
    static void addExpressionEnum(cJSON* array);
    static void addVowelEnum(cJSON* array);
    static void addPaletteEnum(cJSON* array);

private:
    mutable std::mutex mutex_;
    std::string expression_ = "Neutral";
    std::string vowel_ = "Off";
    std::string captionLine1_;
    std::string captionLine2_;
    std::string palette_ = "default";
    uint32_t revision_ = 0;
};

}  // namespace stackyan
