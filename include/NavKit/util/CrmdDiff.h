#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class SCrowdMapData;

namespace CrmdDiff {
    enum Kind : uint16_t {
        walkable = 1 << 0,
        panicOnly = 1 << 1,
        restricted = 1 << 2,
        teleport = 1 << 3,
        otherFlags = 1 << 4,
        group = 1 << 5,
        source = 1 << 6,
        reachability = 1 << 7,
        direction = 1 << 8,
        cost = 1 << 9,
        onNavGrid = 1 << 10,
        height = 1 << 11,
    };
    constexpr size_t kindCount = 12;
    constexpr uint16_t allKinds = (1 << kindCount) - 1;
    constexpr uint16_t gridPointKinds = onNavGrid | height;
    constexpr uint16_t defaultKinds = allKinds & ~cost;

    const char* kindName(size_t kindIndex);
    glm::vec4 kindColor(size_t kindIndex);
    std::string describeKinds(uint16_t kinds);

    struct Tolerances {
        int heightSteps = 1;
        int directionDegrees = 6;
    };

    struct HeaderDifference {
        std::string field;
        std::string a;
        std::string b;
    };

    using KindCounts = std::array<size_t, kindCount>;

    struct Result {
        std::vector<HeaderDifference> header;
        bool gridsMatch = false;
        uint32_t cellsX = 0;
        uint32_t cellsY = 0;
        std::vector<uint16_t> cellKinds;
        /// One per grid point, (cellsX + 1) * (cellsY + 1): the corners of the cells.
        std::vector<uint16_t> pointKinds;
        std::vector<std::vector<uint16_t>> channelCellKinds;
        KindCounts counts{};
        std::vector<KindCounts> channelCounts;
    };

    struct Region {
        uint32_t minX = 0;
        uint32_t minY = 0;
        uint32_t maxX = 0;
        uint32_t maxY = 0;
        size_t cellCount = 0;
        uint16_t kinds = 0;
    };

    /// Compares @p a and @p b cell by cell. Grids of different sizes only get header differences.
    Result compare(const SCrowdMapData& a, const SCrowdMapData& b, const Tolerances& tolerances);

    /// Connected (8-neighbour) groups of cells that differ in any of @p kindMask, largest first.
    /// A grid point difference marks the cells around it.
    std::vector<Region> findRegions(const Result& result, uint16_t kindMask);

    /// Game-space (Z up) position of a point on @p data's cell grid, in cell units, at the nearest grid point's height.
    glm::dvec3 gridToWorld(const SCrowdMapData& data, double cellX, double cellY);

    /// Report lines: header differences, counts per kind and channel, and up to @p maxRegions regions placed on @p a.
    std::vector<std::string> summarize(const Result& result, const std::vector<Region>& regions, const SCrowdMapData& a,
        uint16_t kindMask, size_t maxRegions);
} // namespace CrmdDiff
