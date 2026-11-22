#include "../../include/NavKit/util/CrmdDiff.h"
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
    constexpr size_t cellsPerBlock = 128;
    constexpr uint8_t missingCell = 0xff;
    constexpr uint8_t directionMask = 0x7f;
    constexpr uint8_t twoWayBit = 0x80;
    constexpr uint8_t directionCount = 120;
    constexpr int degreesPerDirection = 3;
    constexpr uint16_t unreachableCost = 0xffff;
    constexpr uint8_t restrictedFlags = 0x07;
    constexpr uint8_t walkableFlag = 0x08;
    constexpr uint8_t teleportFlags = 0x30;
    constexpr uint8_t unusedFlag = 0x40;
    constexpr uint8_t panicOnlyFlag = 0x80;

    constexpr std::array<const char*, CrmdDiff::kindCount> kindNames = {"walkable", "panic-only", "restricted",
        "teleport", "other flags", "group", "source", "reachability", "direction", "cost", "on nav grid", "height"};

    constexpr std::array<glm::vec4, CrmdDiff::kindCount> kindColors = {glm::vec4(0.95f, 0.2f, 0.2f, 0.6f),
        glm::vec4(1.0f, 0.55f, 0.1f, 0.6f), glm::vec4(0.85f, 0.3f, 0.85f, 0.6f), glm::vec4(1.0f, 0.45f, 0.75f, 0.6f),
        glm::vec4(0.6f, 0.6f, 0.6f, 0.6f), glm::vec4(0.95f, 0.9f, 0.2f, 0.6f), glm::vec4(0.2f, 0.9f, 0.95f, 0.6f),
        glm::vec4(0.25f, 0.45f, 1.0f, 0.6f), glm::vec4(0.3f, 0.95f, 0.4f, 0.6f), glm::vec4(0.6f, 0.35f, 0.95f, 0.6f),
        glm::vec4(0.5f, 0.8f, 1.0f, 0.8f), glm::vec4(1.0f, 1.0f, 1.0f, 0.8f)};

    uint8_t packedCell(const TArray<SCrowdCells>& blocks, const size_t index) {
        const size_t block = index / cellsPerBlock;
        return block < blocks.size() ? blocks[block].m_cells[index % cellsPerBlock] : missingCell;
    }

    uint8_t directionAt(const SCrowdFlowChannel& channel, const size_t index) {
        return index < channel.m_aFlowVectorIndex.size() ? channel.m_aFlowVectorIndex[index] : directionCount;
    }

    uint16_t costAt(const SCrowdFlowChannel& channel, const size_t index) {
        return index < channel.m_aFlowCost.size() ? channel.m_aFlowCost[index] : unreachableCost;
    }

    std::string format(const uint32_t value) {
        return std::to_string(value);
    }

    std::string format(const int32_t value) {
        return std::to_string(value);
    }

    std::string format(const size_t value) {
        return std::to_string(value);
    }

    std::string format(const float value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.9g", value);
        return text;
    }

    std::string format(const float4& value) {
        return "(" + format(value.x) + ", " + format(value.y) + ", " + format(value.z) + ", " + format(value.w) + ")";
    }

    std::string format(const SVector3& value) {
        return "(" + format(value.x) + ", " + format(value.y) + ", " + format(value.z) + ")";
    }

    std::string format(const SVector4& value) {
        return "(" + format(value.x) + ", " + format(value.y) + ", " + format(value.z) + ", " + format(value.w) + ")";
    }

    template <typename T> void addIfDifferent(
        std::vector<CrmdDiff::HeaderDifference>& header, const std::string& field, const T& a, const T& b) {
        const std::string formattedA = format(a);
        const std::string formattedB = format(b);
        if (formattedA != formattedB) {
            header.push_back({field, formattedA, formattedB});
        }
    }

    std::vector<CrmdDiff::HeaderDifference> compareHeaders(const SCrowdMapData& a, const SCrowdMapData& b) {
        std::vector<CrmdDiff::HeaderDifference> header;
        addIfDifferent(header, "version", a.m_nVersion, b.m_nVersion);
        addIfDifferent(header, "cell consts", a.m_vCellConsts, b.m_vCellConsts);
        addIfDifferent(header, "grid size x", a.m_nGridSizeX, b.m_nGridSizeX);
        addIfDifferent(header, "grid size y", a.m_nGridSizeY, b.m_nGridSizeY);
        addIfDifferent(header, "cell size x", a.m_nCellSizeX, b.m_nCellSizeX);
        addIfDifferent(header, "cell size y", a.m_nCellSizeY, b.m_nCellSizeY);
        addIfDifferent(header, "grid points", a.m_GridMap.size(), b.m_GridMap.size());
        addIfDifferent(header, "height scale", a.m_fGridMapHeightOffsetsScale, b.m_fGridMapHeightOffsetsScale);
        addIfDifferent(header, "height bias", a.m_fGridMapHeightOffsetsBias, b.m_fGridMapHeightOffsetsBias);
        addIfDifferent(header, "cell flag blocks", a.m_CellFlags.size(), b.m_CellFlags.size());
        addIfDifferent(header, "cell group blocks", a.m_CellGroups.size(), b.m_CellGroups.size());
        addIfDifferent(header, "group flow start channel", a.m_nGroupFlowStartChannel, b.m_nGroupFlowStartChannel);
        addIfDifferent(header, "flow channels", a.m_aFlowChannels.size(), b.m_aFlowChannels.size());
        const size_t channels = std::min(a.m_aFlowChannels.size(), b.m_aFlowChannels.size());
        for (size_t c = 0; c < channels; ++c) {
            const std::string prefix = "channel " + std::to_string(c) + " ";
            addIfDifferent(header, prefix + "directions", a.m_aFlowChannels[c].m_aFlowVectorIndex.size(),
                b.m_aFlowChannels[c].m_aFlowVectorIndex.size());
            addIfDifferent(header, prefix + "costs", a.m_aFlowChannels[c].m_aFlowCost.size(),
                b.m_aFlowChannels[c].m_aFlowCost.size());
        }
        addIfDifferent(header, "grid cell size", a.m_nGridCellSize, b.m_nGridCellSize);
        addIfDifferent(header, "object to world x axis", a.m_mObjectToWorld.XAxis, b.m_mObjectToWorld.XAxis);
        addIfDifferent(header, "object to world y axis", a.m_mObjectToWorld.YAxis, b.m_mObjectToWorld.YAxis);
        addIfDifferent(header, "object to world z axis", a.m_mObjectToWorld.ZAxis, b.m_mObjectToWorld.ZAxis);
        addIfDifferent(header, "object to world translation", a.m_mObjectToWorld.Trans, b.m_mObjectToWorld.Trans);
        addIfDifferent(header, "global size", a.m_vGlobalSize, b.m_vGlobalSize);
        addIfDifferent(header, "grid generator offset", a.m_nGridGeneratorOffset, b.m_nGridGeneratorOffset);
        return header;
    }

    uint16_t flagKinds(const uint8_t a, const uint8_t b) {
        const uint8_t changed = a ^ b;
        uint16_t kinds = 0;
        kinds |= (changed & walkableFlag) != 0 ? CrmdDiff::walkable : 0;
        kinds |= (changed & panicOnlyFlag) != 0 ? CrmdDiff::panicOnly : 0;
        kinds |= (changed & restrictedFlags) != 0 ? CrmdDiff::restricted : 0;
        kinds |= (changed & teleportFlags) != 0 ? CrmdDiff::teleport : 0;
        kinds |= (changed & unusedFlag) != 0 ? CrmdDiff::otherFlags : 0;
        return kinds;
    }

    bool directionsDiffer(const uint8_t a, const uint8_t b, const int toleranceDegrees) {
        if ((a & twoWayBit) != (b & twoWayBit)) {
            return true;
        }
        const int directionA = a & directionMask;
        const int directionB = b & directionMask;
        const bool noneA = directionA >= directionCount;
        const bool noneB = directionB >= directionCount;
        if (noneA || noneB) {
            return noneA != noneB;
        }
        const int steps = std::abs(directionA - directionB);
        return std::min(steps, directionCount - steps) * degreesPerDirection > toleranceDegrees;
    }

    uint16_t costKinds(const uint16_t a, const uint16_t b) {
        if ((a == 0) != (b == 0)) {
            return CrmdDiff::source;
        }
        if ((a == unreachableCost) != (b == unreachableCost)) {
            return CrmdDiff::reachability;
        }
        return a != b ? CrmdDiff::cost : 0;
    }

    void countKinds(CrmdDiff::KindCounts& counts, const uint16_t kinds) {
        for (size_t k = 0; k < CrmdDiff::kindCount; ++k) {
            counts[k] += (kinds >> k) & 1;
        }
    }
} // namespace

const char* CrmdDiff::kindName(const size_t kindIndex) {
    return kindIndex < kindNames.size() ? kindNames[kindIndex] : "unknown";
}

glm::vec4 CrmdDiff::kindColor(const size_t kindIndex) {
    return kindIndex < kindColors.size() ? kindColors[kindIndex] : glm::vec4(1.0f);
}

std::string CrmdDiff::describeKinds(const uint16_t kinds) {
    std::string description;
    for (size_t k = 0; k < kindCount; ++k) {
        if ((kinds >> k) & 1) {
            description += (description.empty() ? "" : ", ") + std::string(kindNames[k]);
        }
    }
    return description;
}

CrmdDiff::Result CrmdDiff::compare(const SCrowdMapData& a, const SCrowdMapData& b, const Tolerances& tolerances) {
    Result result;
    result.header = compareHeaders(a, b);
    result.gridsMatch = a.m_nCellSizeX == b.m_nCellSizeX && a.m_nCellSizeY == b.m_nCellSizeY &&
        a.m_nGridSizeX == b.m_nGridSizeX && a.m_nGridSizeY == b.m_nGridSizeY && a.m_nCellSizeX != 0 &&
        a.m_nCellSizeY != 0 && a.m_nGridSizeX == a.m_nCellSizeX + 1 && a.m_nGridSizeY == a.m_nCellSizeY + 1;
    if (!result.gridsMatch) {
        return result;
    }
    result.cellsX = a.m_nCellSizeX;
    result.cellsY = a.m_nCellSizeY;
    const size_t cellCount = size_t{result.cellsX} * result.cellsY;
    result.cellKinds.assign(cellCount, 0);
    for (size_t i = 0; i < cellCount; ++i) {
        uint16_t kinds = flagKinds(packedCell(a.m_CellFlags, i), packedCell(b.m_CellFlags, i));
        kinds |= packedCell(a.m_CellGroups, i) != packedCell(b.m_CellGroups, i) ? group : 0;
        result.cellKinds[i] = kinds;
    }

    const size_t channels = std::min(a.m_aFlowChannels.size(), b.m_aFlowChannels.size());
    result.channelCounts.assign(channels, {});
    result.channelCellKinds.assign(channels, std::vector<uint16_t>(cellCount, 0));
    for (size_t c = 0; c < channels; ++c) {
        const SCrowdFlowChannel& channelA = a.m_aFlowChannels[c];
        const SCrowdFlowChannel& channelB = b.m_aFlowChannels[c];
        for (size_t i = 0; i < cellCount; ++i) {
            uint16_t kinds = costKinds(costAt(channelA, i), costAt(channelB, i));
            if (directionsDiffer(directionAt(channelA, i), directionAt(channelB, i), tolerances.directionDegrees)) {
                kinds |= direction;
            }
            countKinds(result.channelCounts[c], kinds);
            result.channelCellKinds[c][i] = kinds;
            result.cellKinds[i] |= kinds;
        }
    }

    const size_t pointCount = size_t{a.m_nGridSizeX} * a.m_nGridSizeY;
    result.pointKinds.assign(pointCount, 0);
    const double heightTolerance = tolerances.heightSteps *
        std::max(std::abs(a.m_fGridMapHeightOffsetsScale), std::abs(b.m_fGridMapHeightOffsetsScale));
    const size_t comparablePoints = std::min({pointCount, a.m_GridMap.size(), b.m_GridMap.size()});
    for (size_t p = 0; p < comparablePoints; ++p) {
        const ZCrowdGridPoint& pointA = a.m_GridMap[p];
        const ZCrowdGridPoint& pointB = b.m_GridMap[p];
        uint16_t kinds = pointA.m_nOnNavGrid != pointB.m_nOnNavGrid ? onNavGrid : 0;
        if (pointA.m_nOnNavGrid != 0 && pointB.m_nOnNavGrid != 0) {
            const double heightA = pointA.m_nHeightOffset * static_cast<double>(a.m_fGridMapHeightOffsetsScale) +
                a.m_fGridMapHeightOffsetsBias;
            const double heightB = pointB.m_nHeightOffset * static_cast<double>(b.m_fGridMapHeightOffsetsScale) +
                b.m_fGridMapHeightOffsetsBias;
            kinds |= std::abs(heightA - heightB) > heightTolerance * 1.0001 ? height : 0;
        }
        result.pointKinds[p] = kinds;
    }
    for (size_t p = comparablePoints; p < pointCount; ++p) {
        result.pointKinds[p] = onNavGrid;
    }

    for (const uint16_t kinds : result.cellKinds) {
        countKinds(result.counts, kinds);
    }
    for (const uint16_t kinds : result.pointKinds) {
        countKinds(result.counts, kinds);
    }
    return result;
}

std::vector<CrmdDiff::Region> CrmdDiff::findRegions(const Result& result, const uint16_t kindMask) {
    if (!result.gridsMatch) {
        return {};
    }
    const size_t width = result.cellsX;
    const size_t height = result.cellsY;
    const size_t pointsX = width + 1;
    const auto kindsAt = [&](const size_t x, const size_t y) {
        uint16_t kinds = result.cellKinds[y * width + x];
        for (const size_t pointY : {y, y + 1}) {
            for (const size_t pointX : {x, x + 1}) {
                kinds |= result.pointKinds[pointY * pointsX + pointX];
            }
        }
        return static_cast<uint16_t>(kinds & kindMask);
    };

    std::vector<uint16_t> kinds(width * height);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            kinds[y * width + x] = kindsAt(x, y);
        }
    }

    std::vector<Region> regions;
    std::vector<bool> visited(kinds.size(), false);
    std::vector<size_t> stack;
    for (size_t start = 0; start < kinds.size(); ++start) {
        if (visited[start] || kinds[start] == 0) {
            continue;
        }
        Region region;
        region.minX = region.maxX = static_cast<uint32_t>(start % width);
        region.minY = region.maxY = static_cast<uint32_t>(start / width);
        visited[start] = true;
        stack.push_back(start);
        while (!stack.empty()) {
            const size_t index = stack.back();
            stack.pop_back();
            const auto x = static_cast<uint32_t>(index % width);
            const auto y = static_cast<uint32_t>(index / width);
            region.minX = std::min(region.minX, x);
            region.maxX = std::max(region.maxX, x);
            region.minY = std::min(region.minY, y);
            region.maxY = std::max(region.maxY, y);
            region.kinds |= kinds[index];
            ++region.cellCount;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int64_t neighbourX = int64_t{x} + dx;
                    const int64_t neighbourY = int64_t{y} + dy;
                    if (neighbourX < 0 || neighbourY < 0 || neighbourX >= static_cast<int64_t>(width) ||
                        neighbourY >= static_cast<int64_t>(height)) {
                        continue;
                    }
                    const size_t neighbour = static_cast<size_t>(neighbourY) * width + static_cast<size_t>(neighbourX);
                    if (!visited[neighbour] && kinds[neighbour] != 0) {
                        visited[neighbour] = true;
                        stack.push_back(neighbour);
                    }
                }
            }
        }
        regions.push_back(region);
    }
    std::ranges::stable_sort(regions, std::greater{}, &Region::cellCount);
    return regions;
}

glm::dvec3 CrmdDiff::gridToWorld(const SCrowdMapData& data, const double cellX, const double cellY) {
    const double cellSize = data.m_nGridCellSize;
    const double localX = (cellX - 0.5 * data.m_nCellSizeX) * cellSize;
    const double localY = (cellY - 0.5 * data.m_nCellSizeY) * cellSize;
    double localZ = 0;
    const auto pointX =
        static_cast<size_t>(std::clamp(std::lround(cellX), 0l, static_cast<long>(data.m_nGridSizeX) - 1));
    const auto pointY =
        static_cast<size_t>(std::clamp(std::lround(cellY), 0l, static_cast<long>(data.m_nGridSizeY) - 1));
    if (const size_t point = pointY * data.m_nGridSizeX + pointX;
        point < data.m_GridMap.size() && data.m_GridMap[point].m_nOnNavGrid != 0) {
        localZ = data.m_GridMap[point].m_nHeightOffset * static_cast<double>(data.m_fGridMapHeightOffsetsScale) +
            data.m_fGridMapHeightOffsetsBias;
    }
    const SMatrix& m = data.m_mObjectToWorld;
    const auto axis = [](const float4& v) { return glm::dvec3(v.x, v.y, v.z); };
    return axis(m.XAxis) * localX + axis(m.YAxis) * localY + axis(m.ZAxis) * localZ + axis(m.Trans);
}

std::vector<std::string> CrmdDiff::summarize(const Result& result, const std::vector<Region>& regions,
    const SCrowdMapData& a, const uint16_t kindMask, const size_t maxRegions) {
    std::vector<std::string> lines;
    char line[256];
    lines.push_back(
        result.header.empty() ? "Headers match." : std::to_string(result.header.size()) + " header differences:");
    for (const HeaderDifference& difference : result.header) {
        lines.push_back("  " + difference.field + ": " + difference.a + " vs " + difference.b);
    }
    if (!result.gridsMatch) {
        lines.emplace_back("Grid sizes differ, so cells were not compared.");
        return lines;
    }

    const size_t cellCount = size_t{result.cellsX} * result.cellsY;
    const size_t pointCount = result.pointKinds.size();
    std::string counts;
    for (size_t k = 0; k < kindCount; ++k) {
        if (result.counts[k] == 0) {
            continue;
        }
        const bool isPointKind = ((gridPointKinds >> k) & 1) != 0;
        std::snprintf(line, sizeof(line), "%s%s %zu/%zu %s%s", counts.empty() ? "" : ", ", kindNames[k],
            result.counts[k], isPointKind ? pointCount : cellCount, isPointKind ? "points" : "cells",
            ((kindMask >> k) & 1) != 0 ? "" : " (hidden)");
        counts += line;
    }
    lines.push_back(counts.empty() ? "No cell or grid point differences." : "Differing: " + counts);

    for (size_t c = 0; c < result.channelCounts.size(); ++c) {
        std::string channelCounts;
        for (size_t k = 0; k < kindCount; ++k) {
            if (result.channelCounts[c][k] != 0) {
                channelCounts += (channelCounts.empty() ? "" : ", ") + std::string(kindNames[k]) + " " +
                    std::to_string(result.channelCounts[c][k]);
            }
        }
        if (!channelCounts.empty()) {
            lines.push_back("  channel " + std::to_string(c) + ": " + channelCounts);
        }
    }

    lines.push_back(std::to_string(regions.size()) + " regions of " + describeKinds(kindMask) + " differences" +
        (regions.size() > maxRegions ? ", largest " + std::to_string(maxRegions) + ":" : ":"));
    for (size_t r = 0; r < std::min(maxRegions, regions.size()); ++r) {
        const Region& region = regions[r];
        const glm::dvec3 centre =
            gridToWorld(a, (region.minX + region.maxX + 1) * 0.5, (region.minY + region.maxY + 1) * 0.5);
        std::snprintf(line, sizeof(line), "  #%zu: %zu cells, x %u-%u y %u-%u, centre (%.2f, %.2f, %.2f): ", r + 1,
            region.cellCount, region.minX, region.maxX, region.minY, region.maxY, centre.x, centre.y, centre.z);
        lines.push_back(line + describeKinds(region.kinds));
    }
    return lines;
}
