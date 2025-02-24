#pragma once

#include <string>
#include "ReasoningGrid.h"

class Vec4;

enum VisionDataType : uint8_t {
    SIZE_556,
    SIZE_1110,
    SIZE_1664,
    SIZE_2218,
    SIZE_2772,
    SIZE_3326,
    SIZE_3880,
    SIZE_UNKNOWN
};

class VisionData {
public:
    VisionData() = default;

    constexpr VisionData(VisionDataType type) : visionDataType(type) {
    }

    constexpr bool operator==(VisionData a) const { return visionDataType == a.visionDataType; }
    constexpr bool operator!=(VisionData a) const { return visionDataType != a.visionDataType; }

    std::string getName();

    Vec4 getColor();

    static VisionData GetVisionDataType(int size);

private:
    VisionDataType visionDataType;
};
