#include "AvatarService.h"

#include <cstring>

namespace stackyan {
namespace {

constexpr const char* kExpressions[] = {
    "Neutral", "Happy", "Joy", "Angry", "Sad", "Curious", "Surprised", "Shy",
    "Thinking", "Wink", "Talking", "Love", "Panic", "Proud", "Sigh", "Mischief",
    "Cold", "Sleepy"
};

constexpr const char* kVowels[] = {"Off", "Closed", "A", "I", "U", "E", "O"};
constexpr const char* kPalettes[] = {"default", "warm", "cool", "mono", "night"};

bool contains(const char* value, const char* const* list, size_t count) {
    if (!value) return false;
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(value, list[i]) == 0) return true;
    }
    return false;
}

void addEnumValues(cJSON* array, const char* const* list, size_t count) {
    if (!array) return;
    for (size_t i = 0; i < count; ++i) {
        cJSON_AddItemToArray(array, cJSON_CreateString(list[i]));
    }
}

}  // namespace

bool AvatarService::setExpression(const char* expression) {
    if (!isValidExpression(expression)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    expression_ = expression;
    ++revision_;
    return true;
}

bool AvatarService::setVowel(const char* vowel) {
    if (!isValidVowel(vowel)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    vowel_ = vowel;
    ++revision_;
    return true;
}

void AvatarService::setCaption(const char* line1, const char* line2) {
    std::lock_guard<std::mutex> lock(mutex_);
    captionLine1_ = line1 ? line1 : "";
    captionLine2_ = line2 ? line2 : "";
    ++revision_;
}

void AvatarService::clearCaption() {
    setCaption("", "");
}

void AvatarService::setPalette(const char* palette) {
    std::lock_guard<std::mutex> lock(mutex_);
    palette_ = palette ? palette : "default";
    ++revision_;
}

void AvatarService::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    expression_ = "Neutral";
    vowel_ = "Off";
    captionLine1_.clear();
    captionLine2_.clear();
    palette_ = "default";
    ++revision_;
}

cJSON* AvatarService::stateJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "expression", expression_.c_str());
    cJSON_AddStringToObject(root, "vowel", vowel_.c_str());
    cJSON* caption = cJSON_AddObjectToObject(root, "caption");
    cJSON_AddStringToObject(caption, "line1", captionLine1_.c_str());
    cJSON_AddStringToObject(caption, "line2", captionLine2_.c_str());
    cJSON_AddStringToObject(root, "palette", palette_.c_str());
    cJSON_AddNumberToObject(root, "revision", revision_);
    cJSON_AddBoolToObject(root, "renderer_attached", false);
    return root;
}

bool AvatarService::isValidExpression(const char* expression) {
    return contains(expression, kExpressions, sizeof(kExpressions) / sizeof(kExpressions[0]));
}

bool AvatarService::isValidVowel(const char* vowel) {
    return contains(vowel, kVowels, sizeof(kVowels) / sizeof(kVowels[0]));
}

bool AvatarService::isValidPalette(const char* palette) {
    return contains(palette, kPalettes, sizeof(kPalettes) / sizeof(kPalettes[0]));
}

void AvatarService::addExpressionEnum(cJSON* array) {
    addEnumValues(array, kExpressions, sizeof(kExpressions) / sizeof(kExpressions[0]));
}

void AvatarService::addVowelEnum(cJSON* array) {
    addEnumValues(array, kVowels, sizeof(kVowels) / sizeof(kVowels[0]));
}

void AvatarService::addPaletteEnum(cJSON* array) {
    addEnumValues(array, kPalettes, sizeof(kPalettes) / sizeof(kPalettes[0]));
}

}  // namespace stackyan
