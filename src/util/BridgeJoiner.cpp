#include "../../include/NavKit/util/BridgeJoiner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <sstream>

#include "../../include/NavKit/model/Json.h"
#include "../../include/NavKit/util/Math.h"
#include "../../include/NavWeakness/NavPower.h"

namespace {
    constexpr float MAX_FACING_ALIGNMENT = -0.7f;
    constexpr float MIN_EDGE_LENGTH_INSIDE_BOX = 0.2f;

    struct BoundaryEdge {
        size_t areaIndex;
        size_t edgeIndex;
        Vec3 start;
        Vec3 end;
        Vec3 outwardNormal;
        Vec3 insideBoxMidpoint;
    };

    Vec3 toBoxLocal(const Vec3& point, const Json::PfBox& box) {
        const Vec3 offset{point.X - box.pos.x, point.Y - box.pos.y, point.Z - box.pos.z};
        return Math::rotatePoint(offset, {-box.rotation.x, -box.rotation.y, -box.rotation.z, box.rotation.w});
    }

    std::optional<Vec3> midpointOfPartInsideBox(const Vec3& start, const Vec3& end, const Json::PfBox& box) {
        Vec3 localStart = toBoxLocal(start, box);
        Vec3 localDirection = toBoxLocal(end, box) - localStart;
        const float halfExtents[3]{box.scale.x / 2, box.scale.y / 2, box.scale.z / 2};
        float entry = 0.0f;
        float exit = 1.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float origin = localStart[axis];
            const float direction = localDirection[axis];
            if (std::abs(direction) < 1e-9f) {
                if (std::abs(origin) > halfExtents[axis]) {
                    return std::nullopt;
                }
                continue;
            }
            float near = (-halfExtents[axis] - origin) / direction;
            float far = (halfExtents[axis] - origin) / direction;
            if (near > far) {
                std::swap(near, far);
            }
            entry = std::max(entry, near);
            exit = std::min(exit, far);
            if (entry > exit) {
                return std::nullopt;
            }
        }
        if ((exit - entry) * start.DistanceTo(end) < MIN_EDGE_LENGTH_INSIDE_BOX) {
            return std::nullopt;
        }
        return start + (end - start) * ((entry + exit) / 2);
    }

    Vec3 horizontalOutwardNormal(const Vec3& start, const Vec3& end, const Vec3& areaCentre) {
        Vec3 normal{end.Y - start.Y, start.X - end.X, 0.0f};
        const Vec3 towardCentre = areaCentre - start.MidpointTo(end);
        if (normal.X * towardCentre.X + normal.Y * towardCentre.Y > 0) {
            normal = normal * -1.0f;
        }
        return normal;
    }

    Vec3 averageVertex(const NavPower::Area& area) {
        Vec3 sum;
        for (const NavPower::Binary::Edge* edge : area.m_edges) {
            sum = sum + edge->m_pos;
        }
        return sum / static_cast<float>(area.m_edges.size());
    }

    std::vector<BoundaryEdge> boundaryEdgesInBox(const NavPower::NavGraph& graph, const Json::PfBox& box) {
        std::vector<BoundaryEdge> edges;
        for (size_t areaIndex = 0; areaIndex < graph.m_areas.size(); ++areaIndex) {
            const NavPower::Area& area = graph.m_areas[areaIndex];
            if (area.m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_PLANNER_EXCLUDED) {
                continue;
            }
            const size_t edgeCount = area.m_edges.size();
            std::optional<Vec3> centre;
            for (size_t edgeIndex = 0; edgeIndex < edgeCount; ++edgeIndex) {
                if (area.m_edges[edgeIndex]->m_pAdjArea != nullptr) {
                    continue;
                }
                const Vec3& start = area.m_edges[edgeIndex]->m_pos;
                const Vec3& end = area.m_edges[(edgeIndex + 1) % edgeCount]->m_pos;
                const std::optional<Vec3> insideBoxMidpoint = midpointOfPartInsideBox(start, end, box);
                if (!insideBoxMidpoint) {
                    continue;
                }
                if (!centre) {
                    centre = averageVertex(area);
                }
                edges.push_back({areaIndex, edgeIndex, start, end, horizontalOutwardNormal(start, end, *centre),
                    *insideBoxMidpoint});
            }
        }
        return edges;
    }

    float horizontalDot(const Vec3& a, const Vec3& b) {
        return a.X * b.X + a.Y * b.Y;
    }

    float horizontalLength(const Vec3& v) {
        return std::sqrt(v.X * v.X + v.Y * v.Y);
    }

    bool facesAcrossGap(const BoundaryEdge& a, const BoundaryEdge& b) {
        const Vec3 directionA = a.end - a.start;
        const Vec3 directionB = b.end - b.start;
        const float lengths = horizontalLength(directionA) * horizontalLength(directionB);
        if (lengths <= 0 || horizontalDot(directionA, directionB) / lengths > MAX_FACING_ALIGNMENT) {
            return false;
        }
        return horizontalDot(a.outwardNormal, b.insideBoxMidpoint - a.insideBoxMidpoint) > 0 &&
            horizontalDot(b.outwardNormal, a.insideBoxMidpoint - b.insideBoxMidpoint) > 0;
    }

    bool isStrictlyConvex(const std::vector<Vec3>& polygon) {
        int sign = 0;
        for (size_t i = 0; i < polygon.size(); ++i) {
            const Vec3& a = polygon[i];
            const Vec3& b = polygon[(i + 1) % polygon.size()];
            const Vec3& c = polygon[(i + 2) % polygon.size()];
            const float cross = (b.X - a.X) * (c.Y - b.Y) - (b.Y - a.Y) * (c.X - b.X);
            if (std::abs(cross) < 1e-6f) {
                return false;
            }
            const int crossSign = cross > 0 ? 1 : -1;
            if (sign != 0 && crossSign != sign) {
                return false;
            }
            sign = crossSign;
        }
        return true;
    }

    NavPower::Binary::Edge* newEdge(const Vec3& position, NavPower::Binary::Area* adjacentArea) {
        auto* edge = new NavPower::Binary::Edge();
        edge->m_pAdjArea = adjacentArea;
        edge->m_pos = position;
        return edge;
    }

    BridgeJoinResult joinBridge(NavPower::NavGraph& graph, const Json::PfBox& box) {
        BridgeJoinResult result{box.name};
        const std::vector<BoundaryEdge> candidates = boundaryEdgesInBox(graph, box);
        std::optional<std::pair<BoundaryEdge, BoundaryEdge>> closestPair;
        float closestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < candidates.size(); ++i) {
            for (size_t j = i + 1; j < candidates.size(); ++j) {
                const BoundaryEdge& a = candidates[i];
                const BoundaryEdge& b = candidates[j];
                if (a.areaIndex == b.areaIndex || !facesAcrossGap(a, b)) {
                    continue;
                }
                const float distance = a.insideBoxMidpoint.DistanceTo(b.insideBoxMidpoint);
                if (distance < closestDistance) {
                    closestDistance = distance;
                    closestPair = {a, b};
                }
            }
        }
        std::ostringstream detail;
        detail << candidates.size() << " boundary edges in box";
        if (!closestPair) {
            detail << ", no facing pair";
            result.detail = detail.str();
            return result;
        }
        const auto& [a, b] = *closestPair;
        const std::vector<Vec3> polygon{a.end, a.start, b.end, b.start};
        if (!isStrictlyConvex(polygon)) {
            detail << ", closest facing pair would make a non-convex area";
            result.detail = detail.str();
            return result;
        }
        NavPower::Area& areaA = graph.m_areas[a.areaIndex];
        NavPower::Area& areaB = graph.m_areas[b.areaIndex];
        auto* bridgeArea = new NavPower::Binary::Area();
        bridgeArea->m_usageFlags = NavPower::AreaUsageFlags::AREA_PLANNER_EXCLUDED;
        NavPower::Area bridge{bridgeArea,
            {newEdge(a.end, areaA.m_area), newEdge(a.start, nullptr), newEdge(b.end, areaB.m_area),
                newEdge(b.start, nullptr)}};
        areaA.m_edges[a.edgeIndex]->m_pAdjArea = bridgeArea;
        areaB.m_edges[b.edgeIndex]->m_pAdjArea = bridgeArea;
        graph.m_areas.push_back(bridge);
        detail << ", joined edges " << horizontalLength(a.end - a.start) << " m and "
               << horizontalLength(b.end - b.start) << " m long, " << closestDistance << " m apart";
        result.joined = true;
        result.detail = detail.str();
        return result;
    }
} // namespace

std::vector<BridgeJoinResult> joinPlannerExcludedBridges(
    NavPower::NavMesh& navMesh, const std::vector<Json::PfBox>& bridgeBoxes) {
    std::vector<BridgeJoinResult> results;
    for (const Json::PfBox& box : bridgeBoxes) {
        BridgeJoinResult best{box.name, false, "no nav graph"};
        for (NavPower::Section& section : navMesh.m_aSections) {
            for (NavPower::NavGraph& graph : section.m_aNavGraphs) {
                BridgeJoinResult graphResult = joinBridge(graph, box);
                if (graphResult.joined || !best.joined) {
                    best = graphResult;
                }
                if (best.joined) {
                    break;
                }
            }
            if (best.joined) {
                break;
            }
        }
        results.push_back(best);
    }
    return results;
}
