#pragma once

#include "../../ResourceLib_HM3/Generated/HM3/ZHMGen.h"
#include "../model/ReasoningGrid.h"
#include <DetourNavMesh.h>

namespace Pathfinding {
    class ClosestPositionData {
    public:
        ClosestPositionData() : isEdgePos(false), edgeIndex(-1) {
        }

        bool isEdgePos;
        int edgeIndex;
    };

    class SGCell {
    public:
        SGCell() : fZ(0), m_Points({}) {
        };
        float fZ;
        std::vector<float4> m_Points;
    };

    class ZPFLocation {
    public:
        ZPFLocation() : polyRef(0), mapped(false) {
        }

        SVector3 pos;
        dtPolyRef polyRef;
        bool mapped;
    };

    NavPower::BBox calculateBBox(const NavPower::Area *area);

    Vec3 *GetClosestPosInArea2d_G2_EdgeIndex(Vec3 *navPowerResult,
                                             dtPolyRef poly, const Vec3 *navPowerPos,
                                             int *edgeIndex);

    Vec3 *GetClosestPosInArea2d_G2_ClosestPos(
        Vec3 *resultNavPower,
        dtPolyRef polyRef,
        const Vec3 *posWCoordNavPower,
        ClosestPositionData *pDataOut);

    float4 *ClosestPointOnSegment(float4 *result, const float4 *vV0, const float4 *vV1, const float4 *vP);

    int GetNeighborCellIndex(int cellIndex, int direction, int gridWidth);

    char LineLineIntersect2D_G2(
        const Vec3 *p0,
        const Vec3 *p1,
        const Vec3 *p2,
        const Vec3 *p3,
        Vec3 *i);
}
