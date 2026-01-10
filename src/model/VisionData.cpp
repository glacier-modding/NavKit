#include "../../include/NavKit/model/VisionData.h"

std::string VisionData::getName() const {
    switch (visionDataType) {
        case SIZE_556:
            return "blue";
        case SIZE_1110:
            return "red";
        case SIZE_1664:
            return "indigo";
        case SIZE_2218:
            return "yellow";
        case SIZE_2772:
            return "purple";
        case SIZE_3326:
            return "teal";
        case SIZE_3880:
            return "white";
        default:
            return "black";
    }
}

VisionData VisionData::GetVisionDataType(int size) {
    switch (size) {
        case 556:
            return VisionData(SIZE_556);
        case 1110:
            return VisionData(SIZE_1110);
        case 1664:
            return VisionData(SIZE_1664);
        case 2218:
            return VisionData(SIZE_2218);
        case 2772:
            return VisionData(SIZE_2772);
        case 3326:
            return VisionData(SIZE_3326);
        case 3880:
            return VisionData(SIZE_3880);
        default:
            return VisionData(SIZE_UNKNOWN);
    }
}

Vec4 VisionData::getColor() const {
    //return Vec4(0.0, 0.0, 1.0, 0.6);
    switch (visionDataType) {
        case SIZE_556:
            return Vec4(0.0, 0.0, 1.0, 0.6);
        case SIZE_1110:
            return Vec4(1.0, 0.0, 0.0, 0.6);
        case SIZE_1664:
            return Vec4(0.5, 0.0, 1.0, 0.6);
        case SIZE_2218:
            return Vec4(1.0, 1.0, 0.0, 0.6);
        case SIZE_2772:
            return Vec4(1.0, 0.0, 1.0, 0.6);
        case SIZE_3326:
            return Vec4(0.0, 1.0, 1.0, 0.6);
        case SIZE_3880:
            return Vec4(1.0, 1.0, 1.0, 0.6);
        default:
            return Vec4(0.0, 0.0, 0.0, 0.6);
    }
}
