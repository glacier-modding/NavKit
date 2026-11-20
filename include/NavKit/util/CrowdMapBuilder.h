#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Json {
    class Crowd;
    class Entity;
    class VolumeSphere;
    class VolumeShape;
} // namespace Json

namespace CrowdMapBuilder {
    constexpr uint32_t crowdMapVersion = 0x33445568;
    constexpr uint8_t noDirection = 120;
    constexpr uint8_t directionTwoWay = 0x80;
    constexpr uint16_t unreachableCost = 0xFFFF;
    constexpr size_t cellsPerBlock = 128;
    constexpr uint8_t noGroup = 0xFF;
    constexpr uint8_t cellFlagWalkable = 0x08;
    constexpr uint8_t cellFlagTeleportIn = 0x10;
    constexpr uint8_t cellFlagTeleportOut = 0x20;
    constexpr uint8_t cellFlagPanicOnly = 0x80;
    constexpr uint8_t offNavGrid = 0;
    constexpr uint8_t onNavGrid = 1;
    constexpr uint8_t onNavGridPanicOnly = 3;

    /// The scene's top-level volume lists that a crowd's volume indices point into.
    struct SceneVolumes {
        const std::vector<Json::Entity>& boxes;
        const std::vector<Json::VolumeSphere>& spheres;
        const std::vector<Json::VolumeShape>& shapes;
    };

    struct GridPoint {
        int16_t heightOffset;
        uint8_t onNavGrid;
    };

    struct FlowChannel {
        std::vector<uint8_t> directions;
        std::vector<uint16_t> costs;
    };

    struct CrowdMap {
        uint32_t cellsX = 0;
        uint32_t cellsY = 0;
        float cellSize = 0;
        float heightScale = 0;
        float heightBias = 0;
        std::vector<GridPoint> gridMap;
        std::vector<uint8_t> cellFlags;
        std::vector<uint8_t> cellGroups;
        int32_t groupFlowStartChannel = -1;
        std::vector<FlowChannel> flowChannels;
        std::array<glm::vec4, 4> objectToWorld{};
        glm::vec3 globalSize{};
        float generatorOffset = 0;
    };

    struct Report {
        size_t walkableCells = 0;
        size_t panicOnlyCells = 0;
        std::vector<size_t> exitCells;
        std::vector<std::string> skippedExits;
        std::vector<size_t> groupCells;
        size_t gridPointsOnGeometry = 0;
        size_t gridPointsOnInclusion = 0;
        size_t gridPointsOffGrid = 0;
    };

    /// Game-space (Z up) vertices of the scene-mesh triangles that can affect @p crowd, three per triangle.
    /// @p recastVerts are Y-up, as loaded from the GLB.
    std::vector<glm::dvec3> groundTrianglesNearCrowd(
        const Json::Crowd& crowd, const float* recastVerts, const int* tris, int triCount);

    /// Follows the game's own generator (ZCrowdMap::Create), raycasting @p groundTriangles in place of collision.
    /// When @p heightTriangles is not empty, each ground hit takes the height of the nearest of those surfaces.
    CrowdMap build(const Json::Crowd& crowd, const SceneVolumes& sceneVolumes,
        const std::vector<glm::dvec3>& groundTriangles, const std::vector<glm::dvec3>& heightTriangles, Report& report);
} // namespace CrowdMapBuilder
