#pragma once

#include "../../include/NavKit/util/Pathfinding.h"

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/util/GridGenerator.h"
#include "../../include/NavKit/util/Math.h"

namespace Pathfinding {
    NavPower::BBox calculateBBox(const NavPower::Area *area) {
        float s_minFloat = -300000000000;
        float s_maxFloat = 300000000000;
        NavPower::BBox bbox;
        bbox.m_min.X = s_maxFloat;
        bbox.m_min.Y = s_maxFloat;
        bbox.m_min.Z = s_maxFloat;
        bbox.m_max.X = s_minFloat;
        bbox.m_max.Y = s_minFloat;
        bbox.m_max.Z = s_minFloat;
        for (auto &edge: area->m_edges) {
            bbox.m_max.X = std::max(bbox.m_max.X, edge->m_pos.X);
            bbox.m_max.Y = std::max(bbox.m_max.Y, edge->m_pos.Y);
            bbox.m_max.Z = std::max(bbox.m_max.Z, edge->m_pos.Z);
            bbox.m_min.X = std::min(bbox.m_min.X, edge->m_pos.X);
            bbox.m_min.Y = std::min(bbox.m_min.Y, edge->m_pos.Y);
            bbox.m_min.Z = std::min(bbox.m_min.Z, edge->m_pos.Z);
        }
        return bbox;
    }

    Vec3 *GetClosestPosInArea2d_G2_EdgeIndex(Vec3 *navPowerResult,
                                             const dtPolyRef polyRef, const Vec3 *navPowerPos,
                                             int *edgeIndex) {
        *edgeIndex = -1;
        float closestDistance = std::numeric_limits<float>::max();
        int edgeIntersections = 0;
        // Check if point is in polygon
        int numEdges = 0;
        std::vector<Vec3> polyEdges;
        RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
        GridGenerator &gridGenerator = GridGenerator::getInstance();
        polyEdges = recastAirgAdapter.getEdges(polyRef);
        numEdges = polyEdges.size();
        for (int i = 0; i < numEdges; ++i) {
            int j = (i + 1) % numEdges;
            Vec3 edgeNavPowerStart;
            Vec3 edgeNavPowerEnd;
            edgeNavPowerStart = RecastAdapter::convertFromRecastToNavPower(polyEdges[i]);
            edgeNavPowerEnd = RecastAdapter::convertFromRecastToNavPower(polyEdges[j]);
            if (edgeNavPowerStart.Y > navPowerPos->Y != edgeNavPowerEnd.Y > navPowerPos->Y &&
                navPowerPos->X <
                (edgeNavPowerEnd.X - edgeNavPowerStart.X)
                * (navPowerPos->Y - edgeNavPowerStart.Y) / (
                    edgeNavPowerEnd.Y - edgeNavPowerStart.Y)
                + edgeNavPowerStart.X
            ) {
                edgeIntersections++;
            }
        }
        if (edgeIntersections % 2 == 1) {
            Vec3 normalNavPower;
            Vec3 v1NavPower;
            normalNavPower = RecastAdapter::convertFromRecastToNavPower(recastAirgAdapter.calculateNormal(polyRef));
            v1NavPower = RecastAdapter::convertFromRecastToNavPower(polyEdges[0]);
            navPowerResult->X = navPowerPos->X;
            navPowerResult->Y = navPowerPos->Y;
            const float dNavPower = -(v1NavPower.X * normalNavPower.X + v1NavPower.Y * normalNavPower.Y + v1NavPower.Z * normalNavPower.Z);
            navPowerResult->Z = -(navPowerPos->X * normalNavPower.X + navPowerPos->Y * normalNavPower.Y + dNavPower) / normalNavPower.Z;
            return navPowerResult;
        }
        for (int i = 0; i < numEdges; ++i) {
            int j = (i + 1) % numEdges;

            const Vec3 &v1NavPower = RecastAdapter::convertFromRecastToNavPower(polyEdges[i]);
            const Vec3 &v2NavPower = RecastAdapter::convertFromRecastToNavPower(polyEdges[j]);
            const Vec3 vNavPower{v2NavPower.X - v1NavPower.X, v2NavPower.Y - v1NavPower.Y, 0};
            const Vec3 wNavPower{navPowerPos->X - v1NavPower.X, navPowerPos->Y - v1NavPower.Y, 0};
            const float t = (wNavPower.X * vNavPower.X + wNavPower.Y * vNavPower.Y) / (vNavPower.X * vNavPower.X + vNavPower.Y * vNavPower.Y);
            Vec3 projectionNavPower;
            if (t < 0) {
                projectionNavPower = {v1NavPower.X, v1NavPower.Y, 0.0};
            } else if (t > 1) {
                projectionNavPower = {v2NavPower.X, v2NavPower.Y, 0.0};
            } else {
                projectionNavPower.X = v1NavPower.X + t * vNavPower.X;
                projectionNavPower.Y = v1NavPower.Y + t * vNavPower.Y;
                projectionNavPower.Z = 0.0;
            }

            if (const float dist = projectionNavPower.DistanceTo(*navPowerPos); dist < closestDistance) {
                closestDistance = dist;
                *edgeIndex = i;
                navPowerResult->X = projectionNavPower.X;
                navPowerResult->Y = projectionNavPower.Y;
                Vec3 normalNavPower;
                normalNavPower = RecastAdapter::convertFromRecastToNavPower(recastAirgAdapter.calculateNormal(polyRef));
                const float dNavPower = -(v1NavPower.X * normalNavPower.X + v1NavPower.Y * normalNavPower.Y + v1NavPower.Z * normalNavPower.Z);
                navPowerResult->Z = -(projectionNavPower.X * normalNavPower.X + projectionNavPower.Y * normalNavPower.Y + dNavPower) / normalNavPower.Z;
            }
        }
        return navPowerResult;
    }

    Vec3 *GetClosestPosInArea2d_G2_ClosestPos(
        Vec3 *resultNavPower,
        const dtPolyRef polyRef,
        const Vec3 *posWCoordNavPower,
        ClosestPositionData *pDataOut) {
        bool valid = false;
        GridGenerator& gridGenerator = GridGenerator::getInstance();
        valid = polyRef != 0;
        if (valid) {
            Vec3 closestNavPowerPos;
            Vec3 closestRecastPos = RecastAdapter::getAirgInstance().calculateCentroid(polyRef);
            closestNavPowerPos = RecastAdapter::convertFromRecastToNavPower(closestRecastPos);

            int edgeIndex = -1;
            GetClosestPosInArea2d_G2_EdgeIndex(&closestNavPowerPos, polyRef, posWCoordNavPower, &edgeIndex);

            if (pDataOut) {
                pDataOut->edgeIndex = edgeIndex;
                pDataOut->isEdgePos = (edgeIndex != -1);
            }

            *resultNavPower = closestNavPowerPos;
        } else {
            *resultNavPower = {0, 0, 0};
        }

        return resultNavPower;
    }

    float4 *ClosestPointOnSegment(float4 *result, const float4 *vV0, const float4 *vV1, const float4 *vP) {
        float4 v = *vV1 - *vV0;
        float dot = Math::DotProduct(*vP - *vV0, v);
        float vSqrLength = Math::DotProduct(v, v);
        float t = std::clamp(dot / vSqrLength, 0.0f, 1.0f);
        *result = *vV0 + v * t;
        return result;
    }

    int GetNeighborCellIndex(int cellIndex, int direction, int gridWidth) {
        int dx = (direction == 1 || direction == 2 || direction == 3)
                     ? 1
                     : (direction == 5 || direction == 6 || direction == 7)
                           ? -1
                           : 0;
        int dy = (direction == 7 || direction == 0 || direction == 1)
                     ? -1
                     : (direction == 3 || direction == 4 || direction == 5)
                           ? 1
                           : 0;
        int neighborX = cellIndex % gridWidth + dx;
        int neighborY = cellIndex / gridWidth + dy;

        if (neighborX < 0 || neighborX >= gridWidth || neighborY < 0) {
            return -1;
        }

        return neighborY * gridWidth + neighborX;
    }

    char LineLineIntersect2D_G2(
        const Vec3 *p0,
        const Vec3 *p1,
        const Vec3 *p2,
        const Vec3 *p3,
        Vec3 *i) {
        Vec3 p0_2d(p0->X, p0->Y, 0.0f);
        Vec3 p1_2d(p1->X, p1->Y, 0.0f);
        Vec3 p2_2d(p2->X, p2->Y, 0.0f);
        Vec3 p3_2d(p3->X, p3->Y, 0.0f);

        Vec3 lineA = p1_2d - p0_2d;
        Vec3 lineB = p3_2d - p2_2d;

        float denominator = lineB.Y * lineA.X - lineB.X * lineA.Y;

        if (denominator == 0.0f) {
            return 0;
        }

        float uA = ((p0_2d.Y - p2_2d.Y) * lineA.X - (p0_2d.X - p2_2d.X) * lineA.Y) / denominator;
        float uB = ((p0_2d.Y - p2_2d.Y) * lineB.X - (p0_2d.X - p2_2d.X) * lineB.Y) / denominator;

        if (uA < 0.0f || uA > 1.0f || uB < 0.0f || uB > 1.0f) {
            return 0;
        }

        i->X = p0_2d.X + lineA.X * uA;
        i->Y = p0_2d.Y + lineA.Y * uA;
        i->Z = p0->Z + (p1->Z - p0->Z) * uA;

        return 1;
    }
}
