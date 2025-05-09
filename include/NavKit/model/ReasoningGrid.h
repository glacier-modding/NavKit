#pragma once

#include <vector>
#include "../../../extern/simdjson/simdjson.h"
#include "../../NavWeakness/NavPower.h"

class Vec4 {
public:
    float x;
    float y;
    float z;
    float w;

    const void writeJson(std::ostream &f);

    void readJson(simdjson::ondemand::object p_Json);
};

class Properties {
public:
    Vec4 vMin;
    Vec4 vMax;
    uint32_t nGridWidth;
    float fGridSpacing;
    uint32_t nVisibilityRange;

    const void writeJson(std::ostream &f);

    void readJson(simdjson::ondemand::object p_Json);
};

class SizedArray {
public:
    std::vector<uint8_t> m_aBytes;
    uint32_t m_nSize;

    const void writeJson(std::ostream &f);

    void readJson(simdjson::ondemand::object p_Json);
};

class Waypoint {
public:
    Waypoint() : nNeighbors{65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535}, vPos({}), nVisionDataOffset(0),
                 nLayerIndex(0), cellBitmap{false}, xi(-1), yi(-1),
                 zi(-1) {
    }

    std::vector<uint16_t> nNeighbors;
    Vec4 vPos;
    uint32_t nVisionDataOffset;
    int32_t nLayerIndex;
    bool cellBitmap[25];

    int xi;
    int yi;
    int zi;

    const void writeJson(std::ostream &f);

    void readJson(simdjson::ondemand::object p_Json);
};

class ReasoningGridBuilderHelper {
public:
    float spacing;
    float zSpacing;
    Vec3 *min;
    int gridXSize;
    int gridYSize;
    int gridZSize;
    float tolerance;
    float zTolerance;
    std::vector<std::vector<int> > *areasByZLevel;
    std::vector<int> *waypointZLevels;
    std::vector<NavPower::Area *> waypointAreas;
    std::vector<std::vector<std::vector<int> *> *> *grid;
    int result = 0;
};

class ReasoningGrid {
public:
    ReasoningGrid() : m_Properties(), m_HighVisibilityBits(), m_LowVisibilityBits(), m_deadEndData(), m_nNodeCount() {
    }

    Properties m_Properties;
    SizedArray m_HighVisibilityBits;
    SizedArray m_LowVisibilityBits;
    SizedArray m_deadEndData;
    uint32_t m_nNodeCount;
    std::vector<Waypoint> m_WaypointList;
    std::vector<uint8_t> m_pVisibilityData;

    const void writeJson(std::ostream &f);

    void readJson(const char *p_AirgPath);

    std::vector<uint8_t> getWaypointVisionData(int waypointIndex);

    static void build(ReasoningGrid *airg, NavPower::NavMesh *navMesh, float spacing, float zSpacing,
                      float tolerance, float zTolerance);
};
