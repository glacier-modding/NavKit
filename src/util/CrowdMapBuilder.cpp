#include "../../include/NavKit/util/CrowdMapBuilder.h"
#include "../../include/NavKit/model/Json.h"
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {
    using glm::dmat3;
    using glm::dvec2;
    using glm::dvec3;
    using glm::vec2;

    constexpr float unreachable = std::numeric_limits<float>::max();
    constexpr double uprightTolerance = 1e-3;
    constexpr double groundBucketSize = 2.0;
    constexpr double groundRegionMargin = 1.0;
    constexpr double groundProbeDepth = 2.0;
    constexpr double groundEdgeTolerance = 0.05;
    constexpr double exclusionRetryDrop = 0.1;
    constexpr double barycentricTolerance = 1e-6;
    constexpr double heightSnapAbove = 0.25;
    constexpr double heightSnapBelow = 0.5;
    constexpr float maxCellSlope = 1.25f;
    constexpr float straightStep = 1.0f;
    constexpr float diagonalStep = 1.7f;
    constexpr float blockedNeighbourPenalty = 0.24f;
    constexpr float pinchPenalty = 1.0f;
    constexpr float minHeightSpan = 1.0f;
    constexpr float heightHalfRange = 32767.5f;
    constexpr float costHalfRange = 32767.0f;
    constexpr float twoPi = 6.2831855f;
    constexpr int directionCount = 120;
    constexpr float splineSampleStep = 0.1f;
    constexpr uint32_t initialRandomSeed = 0;
    constexpr uint32_t randomMultiplier = 0x10dcd;
    constexpr uint32_t tieBreakChances = 7;
    constexpr uint32_t tieBreakTakes = 5;

    struct NeighbourOffset {
        int dx;
        int dy;
        bool diagonal;
        vec2 awayFromBlocked;
    };

    constexpr std::array<NeighbourOffset, 8> neighbourOffsets = {{
        {-1, -1, true, {0, 0}},
        {0, -1, false, {0, 1}},
        {1, -1, true, {0, 0}},
        {-1, 0, false, {1, 0}},
        {1, 0, false, {-1, 0}},
        {-1, 1, true, {0, 0}},
        {0, 1, false, {0, -1}},
        {1, 1, true, {0, 0}},
    }};

    dvec3 toDvec3(const Json::Vec3& value) {
        return {value.x, value.y, value.z};
    }

    const std::array<vec2, directionCount + 1>& unitCircleSplits() {
        static const std::array<vec2, directionCount + 1> splits = [] {
            std::array<vec2, directionCount + 1> table{};
            for (int k = 0; k < directionCount; ++k) {
                const double angle = 2.0 * std::numbers::pi * k / directionCount;
                table[k] = vec2(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
            }
            return table;
        }();
        return splits;
    }

    vec2 directionVector(const uint8_t direction) {
        return unitCircleSplits()[std::min<int>(direction & 0x7F, directionCount)];
    }

    uint8_t quantiseDirection(const vec2 vector) {
        if (vector.x == 0.0f && vector.y == 0.0f) {
            return CrowdMapBuilder::noDirection;
        }
        float angle = std::atan2(vector.y, vector.x);
        while (angle < 0.0f) {
            angle += twoPi;
        }
        while (angle >= twoPi) {
            angle -= twoPi;
        }
        return static_cast<uint8_t>(
            static_cast<uint32_t>(static_cast<int>(angle * directionCount / twoPi + 0.5f)) % directionCount);
    }

    class FlowRandom {
    public:
        uint32_t next() {
            const uint32_t first = seed * randomMultiplier + 1;
            seed = first * randomMultiplier + 1;
            return (seed & 0xFFFF0000u) | (first >> 16);
        }

    private:
        uint32_t seed = initialRandomSeed;
    };

    template <typename Point> bool pointInPolygon(const std::vector<Point>& polygon, const double x, const double y) {
        bool inside = false;
        for (size_t i = 0; i < polygon.size(); ++i) {
            const Point& a = polygon[i];
            const Point& b = polygon[(i + 1) % polygon.size()];
            if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) {
                inside = !inside;
            }
        }
        return inside;
    }

    class BucketIndex {
    public:
        explicit BucketIndex(const double bucketSize) : bucketSize(bucketSize) {}

        void add(const int item, const double minX, const double minY, const double maxX, const double maxY) {
            for (auto bx = bucketOf(minX); bx <= bucketOf(maxX); ++bx) {
                for (auto by = bucketOf(minY); by <= bucketOf(maxY); ++by) {
                    buckets[key(bx, by)].push_back(item);
                }
            }
        }

        const std::vector<int>& at(const double x, const double y) const {
            static const std::vector<int> empty;
            const auto found = buckets.find(key(bucketOf(x), bucketOf(y)));
            return found == buckets.end() ? empty : found->second;
        }

    private:
        int64_t bucketOf(const double value) const {
            return static_cast<int64_t>(std::floor(value / bucketSize));
        }

        static int64_t key(const int64_t bx, const int64_t by) {
            return (bx << 32) ^ (by & 0xFFFFFFFF);
        }

        double bucketSize;
        std::unordered_map<int64_t, std::vector<int>> buckets;
    };

    class GroundSurface {
    public:
        explicit GroundSurface(const std::vector<dvec3>& triangles) : triangles(triangles), buckets(groundBucketSize) {
            for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
                const dvec3& a = triangles[i];
                const dvec3& b = triangles[i + 1];
                const dvec3& c = triangles[i + 2];
                buckets.add(static_cast<int>(i), std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}),
                    std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}));
            }
        }

        /// The first surface a ray cast straight down from @p top meets before @p bottom.
        std::optional<double> firstHitBelow(
            const double x, const double y, const double top, const double bottom) const {
            std::optional<double> highest;
            for (const int index : buckets.at(x, y)) {
                const dvec3& a = triangles[index];
                const dvec3& b = triangles[index + 1];
                const dvec3& c = triangles[index + 2];
                const double denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
                if (std::abs(denominator) < 1e-12) {
                    continue;
                }
                const double wa = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / denominator;
                const double wb = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / denominator;
                const double wc = 1.0 - wa - wb;
                if (wa < -barycentricTolerance || wb < -barycentricTolerance || wc < -barycentricTolerance) {
                    continue;
                }
                const double z = wa * a.z + wb * b.z + wc * c.z;
                if (z <= top && z >= bottom && (!highest || z > *highest)) {
                    highest = z;
                }
            }
            return highest;
        }

    private:
        const std::vector<dvec3>& triangles;
        BucketIndex buckets;
    };

    dmat3 quaternionMatrix(const Json::Rotation& q) {
        const double x = q.x, y = q.y, z = q.z, w = q.w;
        dmat3 m;
        m[0][0] = 1 - 2 * (y * y + z * z);
        m[1][0] = 2 * (x * y - z * w);
        m[2][0] = 2 * (x * z + y * w);
        m[0][1] = 2 * (x * y + z * w);
        m[1][1] = 1 - 2 * (x * x + z * z);
        m[2][1] = 2 * (y * z - x * w);
        m[0][2] = 2 * (x * z - y * w);
        m[1][2] = 2 * (y * z + x * w);
        m[2][2] = 1 - 2 * (x * x + y * y);
        return m;
    }

    dmat3 transformRotation(const Json::Transform& transform) {
        return {toDvec3(transform.x), toDvec3(transform.y), toDvec3(transform.z)};
    }

    struct OrientedBox {
        dvec3 position;
        dmat3 axes;
        dvec3 half;

        dvec3 local(const dvec3& point) const {
            return glm::transpose(axes) * (point - position);
        }

        bool contains(const dvec3& point) const {
            const dvec3 offset = local(point);
            return std::abs(offset.x) <= half.x && std::abs(offset.y) <= half.y && std::abs(offset.z) <= half.z;
        }
    };

    OrientedBox boxFromEntity(const Json::Entity& entity) {
        return {toDvec3(entity.position), quaternionMatrix(entity.rotation), toDvec3(entity.scale.data) / 2.0};
    }

    struct Volume {
        std::function<bool(const dvec3&)> contains;
        dvec3 origin;

        bool containsBoth(const dvec3& a, const dvec3& b) const {
            return contains(a) && contains(b);
        }
    };

    Volume boxVolume(const Json::Entity& entity) {
        const OrientedBox box = boxFromEntity(entity);
        return {[box](const dvec3& point) { return box.contains(point); }, box.position};
    }

    Volume sphereVolume(const Json::VolumeSphere& sphere) {
        const dvec3 center = toDvec3(sphere.position);
        return {[center, radius = static_cast<double>(sphere.radius.data)](
                    const dvec3& point) { return glm::distance(point, center) <= radius; },
            center};
    }

    Volume shapeVolume(const Json::VolumeShape& shape) {
        const OrientedBox frame{toDvec3(shape.transform.t), transformRotation(shape.transform), dvec3(0.0)};
        std::vector<dvec2> outline;
        double low = std::numeric_limits<double>::max();
        double high = std::numeric_limits<double>::lowest();
        for (const Json::Vec3& point : shape.points) {
            outline.emplace_back(point.x, point.y);
            low = std::min(low, static_cast<double>(point.z));
            high = std::max(high, static_cast<double>(point.z));
        }
        high += shape.extrusionHeight;
        return {[frame, outline = std::move(outline), low, high](const dvec3& point) {
                    const dvec3 local = frame.local(point);
                    return outline.size() >= 3 && low <= local.z && local.z <= high &&
                        pointInPolygon(outline, local.x, local.y);
                },
            frame.position};
    }

    bool anyContains(const std::vector<Volume>& volumes, const dvec3& point) {
        return std::ranges::any_of(volumes, [&](const Volume& volume) { return volume.contains(point); });
    }

    bool anyContainsBoth(const std::vector<Volume>& volumes, const dvec3& a, const dvec3& b) {
        return std::ranges::any_of(volumes, [&](const Volume& volume) { return volume.containsBoth(a, b); });
    }

    template <typename T> const T& resolveVolume(
        const Json::Crowd& crowd, const std::vector<T>& volumes, const size_t index, const char* listName) {
        if (index >= volumes.size()) {
            throw std::runtime_error("Crowd " + crowd.name + " references " + listName + " index " +
                std::to_string(index) + ", but the scene only has " + std::to_string(volumes.size()) + ".");
        }
        return volumes[index];
    }

    class VolumeResolver {
    public:
        VolumeResolver(const Json::Crowd& crowd, const CrowdMapBuilder::SceneVolumes& scene) :
            crowd(crowd), scene(scene) {}

        std::vector<Volume> boxes(const std::vector<size_t>& indices) const {
            std::vector<Volume> volumes;
            for (const size_t index : indices) {
                volumes.push_back(boxVolume(resolveVolume(crowd, scene.boxes, index, "volumeBoxes")));
            }
            return volumes;
        }

        std::vector<Volume> spheres(const std::vector<size_t>& indices) const {
            std::vector<Volume> volumes;
            for (const size_t index : indices) {
                volumes.push_back(sphereVolume(resolveVolume(crowd, scene.spheres, index, "volumeSpheres")));
            }
            return volumes;
        }

        std::vector<Volume> shapes(const std::vector<size_t>& indices) const {
            std::vector<Volume> volumes;
            for (const size_t index : indices) {
                volumes.push_back(shapeVolume(resolveVolume(crowd, scene.shapes, index, "volumeShapes")));
            }
            return volumes;
        }

    private:
        const Json::Crowd& crowd;
        const CrowdMapBuilder::SceneVolumes& scene;
    };

    struct CrowdFrame {
        dvec3 center;
        dvec2 xAxis;
        dvec2 yAxis;
        dvec3 size;
        double cell;
        int width;
        int height;

        dvec3 world(const double localX, const double localY, const double heightOffset = 0.0) const {
            const dvec2 xy = dvec2(center) + xAxis * (localX - size.x / 2) + yAxis * (localY - size.y / 2);
            return {xy, center.z + heightOffset};
        }

        dvec3 gridPoint(const int px, const int py, const double heightOffset = 0.0) const {
            return world(px * cell, py * cell, heightOffset);
        }

        dvec3 local(const dvec3& point) const {
            const dvec2 offset = dvec2(point) - dvec2(center);
            return {glm::dot(xAxis, offset) + size.x / 2, glm::dot(yAxis, offset) + size.y / 2, point.z - center.z};
        }

        vec2 localDirection(const dvec3& direction) const {
            return vec2(static_cast<float>(glm::dot(xAxis, dvec2(direction))),
                static_cast<float>(glm::dot(yAxis, dvec2(direction))));
        }

        size_t cellIndex(const int x, const int y) const {
            return static_cast<size_t>(y) * width + x;
        }

        size_t pointIndex(const int px, const int py) const {
            return static_cast<size_t>(py) * (width + 1) + px;
        }

        bool inGrid(const int x, const int y) const {
            return x >= 0 && x < width && y >= 0 && y < height;
        }
    };

    CrowdFrame crowdFrame(const Json::Crowd& crowd) {
        if (std::abs(crowd.transform.z.z - 1.0) > uprightTolerance) {
            throw std::runtime_error("Crowd " + crowd.name + " is tilted; only rotation about Z is supported.");
        }
        if (!(crowd.cellSize > 0.0f)) {
            throw std::runtime_error("Crowd " + crowd.name + " has no grid cell size.");
        }
        const dvec2 xAxis(crowd.transform.x.x, crowd.transform.x.y);
        const dvec2 yAxis(crowd.transform.y.x, crowd.transform.y.y);
        if (glm::length(xAxis) == 0.0 || glm::length(yAxis) == 0.0) {
            throw std::runtime_error("Crowd " + crowd.name + " has a degenerate transform.");
        }
        const int width = std::max(0, static_cast<int>(crowd.globalSize.x / crowd.cellSize));
        const int height = std::max(0, static_cast<int>(crowd.globalSize.y / crowd.cellSize));
        if (width == 0 || height == 0) {
            throw std::runtime_error("Crowd " + crowd.name + " is smaller than one grid cell.");
        }
        return {toDvec3(crowd.transform.t), glm::normalize(xAxis), glm::normalize(yAxis), toDvec3(crowd.globalSize),
            crowd.cellSize, width, height};
    }

    struct FlowBox {
        Volume volume;
        vec2 direction;
    };

    std::vector<FlowBox> flowBoxes(const Json::Crowd& crowd, const CrowdMapBuilder::SceneVolumes& scene,
        const CrowdFrame& frame, const std::vector<size_t>& indices) {
        std::vector<FlowBox> boxes;
        for (const size_t index : indices) {
            const Json::Entity& entity = resolveVolume(crowd, scene.boxes, index, "volumeBoxes");
            boxes.push_back({boxVolume(entity), frame.localDirection(quaternionMatrix(entity.rotation)[0])});
        }
        return boxes;
    }

    struct GroupVolumes {
        Volume box;
        std::vector<Volume> exclusion;
        std::vector<Volume> seedPoints;
    };

    struct TeleportVolumes {
        std::optional<Volume> enterZone;
        std::optional<Volume> exitZone;
        bool leavesCrowd;
    };

    /// ZCrowdMap::FindGeomPos: the ground under a grid point, skipping hits inside exclusion volumes.
    class GroundFinder {
    public:
        GroundFinder(const Json::Crowd& crowd, const CrowdFrame& frame, const GroundSurface& ground,
            const std::optional<GroundSurface>& heights, const std::vector<Volume>& exclusion) :
            frame(frame), ground(ground), heights(heights), exclusion(exclusion), probeLift(crowd.generatorOffset) {
            const double offset = (1.0 - crowd.navMeshShrink) * 0.5 * frame.cell;
            sampleOffsets = {dvec2(0, 0)};
            if (crowd.navMeshShrink < 1.0f) {
                sampleOffsets.insert(sampleOffsets.end(),
                    {dvec2(-offset, -offset), dvec2(offset, -offset), dvec2(offset, offset), dvec2(-offset, offset)});
            }
        }

        std::optional<dvec3> find(const int px, const int py) const {
            std::vector<dvec2> starts;
            for (const dvec2& offset : sampleOffsets) {
                starts.emplace_back(frame.world(px * frame.cell + offset.x, py * frame.cell + offset.y));
            }
            double top = frame.center.z + frame.size.z / 2;
            while (true) {
                std::optional<dvec3> firstHit;
                for (const dvec2& start : starts) {
                    if (const std::optional<double> z =
                            ground.firstHitBelow(start.x, start.y, top, top - frame.size.z)) {
                        firstHit = dvec3(start, *z);
                        break;
                    }
                }
                if (!firstHit) {
                    return std::nullopt;
                }
                const double probeTop = firstHit->z + probeLift;
                const std::optional<double> probeZ =
                    ground.firstHitBelow(firstHit->x, firstHit->y, probeTop, probeTop - probeLift - groundProbeDepth);
                if (!probeZ) {
                    return std::nullopt;
                }
                const dvec3 probeHit(firstHit->x, firstHit->y, *probeZ);
                const dvec3 hit(probeHit.x, probeHit.y, snappedHeight(probeHit));
                const dvec3 local = frame.local(hit);
                if (local.x < -groundEdgeTolerance || local.y < -groundEdgeTolerance ||
                    local.x > frame.size.x + groundEdgeTolerance || local.y > frame.size.y + groundEdgeTolerance ||
                    local.z < -frame.size.z / 2 || local.z > frame.size.z / 2) {
                    return std::nullopt;
                }
                if (!anyContains(exclusion, hit)) {
                    return hit;
                }
                top = firstHit->z - exclusionRetryDrop;
            }
        }

    private:
        double snappedHeight(const dvec3& hit) const {
            if (!heights) {
                return hit.z;
            }
            const std::optional<double> above = heights->firstHitBelow(hit.x, hit.y, hit.z + heightSnapAbove, hit.z);
            const std::optional<double> below = heights->firstHitBelow(hit.x, hit.y, hit.z, hit.z - heightSnapBelow);
            if (above && (!below || *above - hit.z < hit.z - *below)) {
                return *above;
            }
            return below.value_or(hit.z);
        }

        const CrowdFrame& frame;
        const GroundSurface& ground;
        const std::optional<GroundSurface>& heights;
        const std::vector<Volume>& exclusion;
        double probeLift;
        std::vector<dvec2> sampleOffsets;
    };

    std::vector<uint8_t> paddedCells(const size_t count, const uint8_t fill) {
        const size_t blocks = (count + CrowdMapBuilder::cellsPerBlock - 1) / CrowdMapBuilder::cellsPerBlock;
        std::vector<uint8_t> cells(blocks * CrowdMapBuilder::cellsPerBlock, 0);
        std::fill_n(cells.begin(), count, fill);
        return cells;
    }

    /// ZCrowdMap::BuildFlow: Dijkstra from @p sources, with each cell pointing at the parent it was reached from.
    class FlowBuilder {
    public:
        FlowBuilder(const CrowdFrame& frame, const std::vector<uint8_t>& cellFlags, const bool smoothen) :
            frame(frame), cellFlags(cellFlags), smoothen(smoothen),
            count(static_cast<size_t>(frame.width) * frame.height) {
            entryPenalty.resize(count);
            for (int y = 0; y < frame.height; ++y) {
                for (int x = 0; x < frame.width; ++x) {
                    entryPenalty[frame.cellIndex(x, y)] = computeEntryPenalty(x, y);
                }
            }
        }

        void build(const std::vector<size_t>& sources, std::vector<float>& costs, std::vector<uint8_t>& directions) {
            directions.assign(count, CrowdMapBuilder::noDirection);
            costs.assign(count, unreachable);
            std::vector<uint8_t> state(count, unvisited);
            std::vector<int64_t> parent(count, -1);
            std::vector<size_t> candidates;

            for (const size_t source : sources) {
                state[source] = finished;
                costs[source] = 0.0f;
            }
            for (const size_t source : sources) {
                for (const NeighbourOffset& offset : neighbourOffsets) {
                    const std::optional<size_t> target = reachableNeighbour(source, offset);
                    if (!target || state[*target] == finished) {
                        continue;
                    }
                    const float cost = costs[source] + stepCost(offset) + entryPenalty[*target];
                    if (cost < costs[*target]) {
                        costs[*target] = cost;
                        parent[*target] = static_cast<int64_t>(source);
                        insertCandidate(candidates, *target, cost, costs, state);
                    }
                }
            }

            while (!candidates.empty()) {
                const size_t current = candidates.back();
                candidates.pop_back();
                state[current] = finished;
                for (const size_t k : shuffledNeighbourOrder()) {
                    const NeighbourOffset& offset = neighbourOffsets[k];
                    const std::optional<size_t> target = reachableNeighbour(current, offset);
                    if (!target) {
                        continue;
                    }
                    const float cost = costs[current] + stepCost(offset) + entryPenalty[*target];
                    if (cost < costs[*target]) {
                        costs[*target] = cost;
                        parent[*target] = static_cast<int64_t>(current);
                        insertCandidate(candidates, *target, cost, costs, state);
                    }
                    if (cost == costs[*target] && random.next() % tieBreakChances < tieBreakTakes) {
                        parent[*target] = static_cast<int64_t>(current);
                    }
                }
            }

            for (size_t i = 0; i < count; ++i) {
                if (costs[i] > 0.0f && costs[i] < unreachable) {
                    const auto from = static_cast<size_t>(parent[i]);
                    const auto cell = static_cast<float>(frame.cell);
                    directions[i] = quantiseDirection(vec2(
                        cell * static_cast<float>(from % frame.width) - cell * static_cast<float>(i % frame.width),
                        cell * static_cast<float>(from / frame.width) - cell * static_cast<float>(i / frame.width)));
                }
            }
            if (smoothen) {
                smoothDirections(costs, parent, directions);
            }
        }

    private:
        static constexpr uint8_t unvisited = 0;
        static constexpr uint8_t candidate = 1;
        static constexpr uint8_t finished = 2;

        bool walkable(const int x, const int y) const {
            return (cellFlags[frame.cellIndex(x, y)] & CrowdMapBuilder::cellFlagWalkable) != 0;
        }

        float computeEntryPenalty(const int x, const int y) const {
            float penalty = 0.0f;
            for (const NeighbourOffset& offset : neighbourOffsets) {
                if (!frame.inGrid(x + offset.dx, y + offset.dy) || walkable(x + offset.dx, y + offset.dy)) {
                    continue;
                }
                penalty += blockedNeighbourPenalty;
                if (frame.inGrid(x - offset.dx, y - offset.dy) && !walkable(x - offset.dx, y - offset.dy)) {
                    penalty += pinchPenalty;
                }
            }
            return penalty;
        }

        float stepCost(const NeighbourOffset& offset) const {
            return offset.diagonal ? diagonalStep : straightStep;
        }

        std::optional<size_t> reachableNeighbour(const size_t from, const NeighbourOffset& offset) const {
            const int x = static_cast<int>(from % frame.width);
            const int y = static_cast<int>(from / frame.width);
            const int nx = x + offset.dx;
            const int ny = y + offset.dy;
            if (!frame.inGrid(nx, ny)) {
                return std::nullopt;
            }
            if (offset.diagonal && !(walkable(x, ny) && walkable(nx, y))) {
                return std::nullopt;
            }
            if (!walkable(nx, ny)) {
                return std::nullopt;
            }
            return frame.cellIndex(nx, ny);
        }

        /// ZCrowdMap::InsertInCandidateList: kept sorted by descending cost, so the cheapest is at the back.
        static void insertCandidate(std::vector<size_t>& candidates, const size_t cell, const float cost,
            const std::vector<float>& costs, std::vector<uint8_t>& state) {
            if (const auto existing = std::ranges::find(candidates, cell); existing != candidates.end()) {
                candidates.erase(existing);
            }
            if (!candidates.empty() && costs[candidates.back()] < cost) {
                const auto position =
                    std::ranges::find_if(candidates, [&](const size_t other) { return !(cost < costs[other]); });
                candidates.insert(position, cell);
            } else {
                candidates.push_back(cell);
            }
            state[cell] = candidate;
        }

        std::array<size_t, 8> shuffledNeighbourOrder() {
            std::array<size_t, 8> remaining = {0, 1, 2, 3, 4, 5, 6, 7};
            std::array<size_t, 8> order{};
            size_t next = 0;
            for (uint32_t last = 7; last >= 1; --last) {
                const uint32_t pick = random.next() % (last + 1);
                order[next++] = remaining[pick];
                remaining[pick] = remaining[last];
            }
            order[next] = remaining[0];
            return order;
        }

        void smoothDirections(const std::vector<float>& costs, const std::vector<int64_t>& parent,
            std::vector<uint8_t>& directions) const {
            for (size_t i = 0; i < count; ++i) {
                if (!(costs[i] > 0.0f && costs[i] < unreachable)) {
                    continue;
                }
                const int x = static_cast<int>(i % frame.width);
                const int y = static_cast<int>(i / frame.width);
                std::vector<vec2> blocked;
                for (const NeighbourOffset& offset : neighbourOffsets) {
                    if (frame.inGrid(x + offset.dx, y + offset.dy) &&
                        costs[frame.cellIndex(x + offset.dx, y + offset.dy)] == unreachable &&
                        offset.awayFromBlocked != vec2(0.0f)) {
                        blocked.push_back(offset.awayFromBlocked);
                    }
                }
                const vec2 own = directionVector(directions[i]);
                vec2 sum = own;
                for (const NeighbourOffset& offset : neighbourOffsets) {
                    if (!frame.inGrid(x + offset.dx, y + offset.dy)) {
                        continue;
                    }
                    const size_t neighbour = frame.cellIndex(x + offset.dx, y + offset.dy);
                    if (!(costs[neighbour] < unreachable)) {
                        continue;
                    }
                    const vec2 flow = directionVector(directions[neighbour]);
                    if (!(glm::dot(own, flow) > 0.0f) ||
                        std::ranges::any_of(blocked, [&](const vec2& away) { return glm::dot(flow, away) < 0.0f; })) {
                        continue;
                    }
                    sum += flow;
                    if (static_cast<int64_t>(neighbour) == parent[i]) {
                        sum += flow;
                    }
                }
                if (const float length = glm::length(sum); length > 0.0f) {
                    directions[i] = quantiseDirection(sum / length);
                }
            }
        }

        const CrowdFrame& frame;
        const std::vector<uint8_t>& cellFlags;
        bool smoothen;
        size_t count;
        std::vector<float> entryPenalty;
        FlowRandom random;
    };

    /// ZCrowdMap::CompressAndSaveFlowCosts: one cost range shared by every channel after channel 0.
    void compressFlowCosts(
        const std::vector<std::vector<float>>& channelCosts, std::vector<CrowdMapBuilder::FlowChannel>& channels) {
        float lowest = unreachable;
        float highest = -unreachable;
        bool anyReachable = false;
        for (size_t c = 1; c < channelCosts.size(); ++c) {
            for (const float cost : channelCosts[c]) {
                if (cost != unreachable) {
                    lowest = std::min(lowest, cost);
                    highest = std::max(highest, cost);
                    anyReachable = true;
                }
            }
        }
        for (size_t c = 1; c < channelCosts.size(); ++c) {
            std::vector<uint16_t>& encoded = channels[c].costs;
            encoded.resize(channelCosts[c].size());
            for (size_t i = 0; i < encoded.size(); ++i) {
                const float cost = channelCosts[c][i];
                if (cost == unreachable || !anyReachable) {
                    encoded[i] = CrowdMapBuilder::unreachableCost;
                } else if (lowest == highest) {
                    encoded[i] = 0;
                } else {
                    const float range = highest - lowest;
                    encoded[i] = static_cast<uint16_t>(
                        static_cast<int>((costHalfRange - ((highest + lowest) / range) * costHalfRange) +
                            (2.0f / range) * costHalfRange * cost));
                }
            }
        }
    }

    vec2 steer(const uint8_t direction, const vec2 flow) {
        return directionVector(direction) + flow;
    }

    struct SplineNeighbours {
        size_t previous;
        size_t next;
    };

    SplineNeighbours splineNeighbours(const size_t index, const size_t count, const bool closed) {
        if (closed) {
            return {(index == 0 ? count : index) - 1, index + 1 == count ? 0 : index + 1};
        }
        return {index == 0 ? 0 : index - 1, index + 1 < count ? index + 1 : index};
    }

    dvec3 normaliseOrZero(const dvec3& vector) {
        const double length = glm::length(vector);
        return length == 0.0 ? dvec3(0.0) : vector / length;
    }

    struct BezierCurve {
        dvec3 start;
        dvec3 startHandle;
        dvec3 endHandle;
        dvec3 end;

        dvec3 position(const double t) const {
            const double u = 1.0 - t;
            return u * u * u * start + 3.0 * u * u * t * startHandle + 3.0 * u * t * t * endHandle + t * t * t * end;
        }

        dvec3 velocity(const double t) const {
            const double u = 1.0 - t;
            return 3.0 * u * u * (startHandle - start) + 6.0 * u * t * (endHandle - startHandle) +
                3.0 * t * t * (end - endHandle);
        }
    };

    /// ZBezierSpline after SetControlPointsWithoutTime: each curve spans one unit of parameter.
    class BezierSpline {
    public:
        BezierSpline(const std::vector<dvec3>& points, const bool closed) {
            const size_t count = points.size();
            std::vector<dvec3> inTangents(count);
            std::vector<dvec3> outTangents(count);
            for (size_t i = 0; i < count; ++i) {
                const auto [previous, next] = splineNeighbours(i, count, closed);
                const dvec3 direction = normaliseOrZero(points[next] - points[previous]);
                inTangents[i] = direction * handleLength(points, i, previous);
                outTangents[i] = direction * handleLength(points, i, next);
            }
            const size_t curveCount = count < 2 ? 0 : count - (closed ? 0 : 1);
            for (size_t i = 0; i < curveCount; ++i) {
                const size_t j = (i + 1) % count;
                curves.push_back(
                    {points[i], points[i] + outTangents[i] / 3.0, points[j] - inTangents[j] / 3.0, points[j]});
            }
        }

        float duration() const {
            return static_cast<float>(curves.size());
        }

        dvec3 position(const float t) const {
            const auto [curve, local] = locate(t);
            return curves[curve].position(local);
        }

        dvec3 velocity(const float t) const {
            const auto [curve, local] = locate(t);
            return curves[curve].velocity(local);
        }

    private:
        std::vector<BezierCurve> curves;

        static double handleLength(const std::vector<dvec3>& points, const size_t index, const size_t neighbour) {
            return neighbour == index ? 1.0 : std::min(glm::distance(points[index], points[neighbour]) * 1.35, 1.0);
        }

        /// ZBezierSpline::ZEvaluator::Move
        std::pair<size_t, float> locate(float t) const {
            size_t curve = 0;
            while (t >= 1.0f) {
                if (curve + 1 >= curves.size()) {
                    return {curve, 1.0f};
                }
                t -= 1.0f;
                ++curve;
            }
            return {curve, t};
        }
    };

    /// ZSplineCrowdFlowEntity::UpdateSplines: lanes across flowWidth, each resampled every lengthTessellation.
    std::vector<BezierSpline> splineFlowLanes(const Json::CrowdSplineFlow& spline) {
        std::vector<BezierSpline> lanes;
        const size_t count = spline.points.size();
        if (!(spline.widthTessellation > 0.0f) || !(spline.lengthTessellation > 0.0f) || count < 2) {
            return lanes;
        }
        const auto laneCount = static_cast<uint32_t>(spline.flowWidth / spline.widthTessellation);
        if (laneCount == 0) {
            return lanes;
        }
        const double laneSpacing = spline.flowWidth / static_cast<float>(laneCount);
        std::vector<std::vector<dvec3>> lanePoints(laneCount);
        for (size_t i = 0; i < count; ++i) {
            const auto [previous, next] = splineNeighbours(i, count, spline.closed);
            const dvec3 along = normaliseOrZero(toDvec3(spline.points[next]) - toDvec3(spline.points[previous]));
            const dvec3 across(along.y, -along.x, 0.0);
            const dvec3 edge = toDvec3(spline.points[i]) - across * (spline.flowWidth * 0.5);
            for (uint32_t lane = 0; lane < laneCount; ++lane) {
                lanePoints[lane].push_back(edge + across * (laneSpacing * lane));
            }
        }
        for (const std::vector<dvec3>& points : lanePoints) {
            const BezierSpline controlCurve(points, spline.closed);
            std::vector<dvec3> samples;
            for (float t = 0.0f;; t += spline.lengthTessellation) {
                samples.push_back(controlCurve.position(std::clamp(t, 0.0f, controlCurve.duration())));
                if (!(t < controlCurve.duration())) {
                    break;
                }
            }
            lanes.emplace_back(samples, spline.closed);
        }
        return lanes;
    }

    /// ZCrowdMap::BuildAmbientFlow: channel 0, steered by flow boxes and spline flows.
    std::vector<uint8_t> ambientFlow(const CrowdFrame& frame, const std::vector<FlowBox>& flowBoxes,
        const std::vector<FlowBox>& flowBoxesTwoWay, const std::vector<Json::CrowdSplineFlow>& splineFlows,
        const std::vector<uint8_t>& cellFlags, const std::vector<float>& pointHeights) {
        std::vector<uint8_t> directions(static_cast<size_t>(frame.width) * frame.height, CrowdMapBuilder::noDirection);
        const auto walkable = [&](const size_t i) { return (cellFlags[i] & CrowdMapBuilder::cellFlagWalkable) != 0; };
        if (flowBoxes.empty() && flowBoxesTwoWay.empty()) {
            return directions;
        }
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width; ++x) {
                const size_t i = frame.cellIndex(x, y);
                if (!walkable(i)) {
                    continue;
                }
                const dvec3 centre =
                    frame.world((x + 0.5) * frame.cell, (y + 0.5) * frame.cell, pointHeights[frame.pointIndex(x, y)]);
                for (const FlowBox& box : flowBoxes) {
                    if (box.volume.contains(centre)) {
                        directions[i] = quantiseDirection(steer(directions[i], box.direction));
                    }
                }
                for (const FlowBox& box : flowBoxesTwoWay) {
                    if (box.volume.contains(centre)) {
                        directions[i] =
                            quantiseDirection(steer(directions[i], box.direction)) | CrowdMapBuilder::directionTwoWay;
                    }
                }
            }
        }
        for (const Json::CrowdSplineFlow& spline : splineFlows) {
            const dmat3 rotation = transformRotation(spline.transform);
            const dvec3 origin = toDvec3(spline.transform.t);
            for (const BezierSpline& lane : splineFlowLanes(spline)) {
                for (float t = 0.0f;; t += splineSampleStep) {
                    const float clamped = std::min(t, lane.duration());
                    const dvec3 local = frame.local(origin + rotation * lane.position(clamped));
                    const int x = static_cast<int>(std::floor(local.x / frame.cell));
                    const int y = static_cast<int>(std::floor(local.y / frame.cell));
                    if (frame.inGrid(x, y) && walkable(frame.cellIndex(x, y))) {
                        const size_t i = frame.cellIndex(x, y);
                        const vec2 flow = frame.localDirection(rotation * lane.velocity(clamped));
                        directions[i] = quantiseDirection(steer(directions[i], flow));
                    }
                    if (!(t < lane.duration())) {
                        break;
                    }
                }
            }
        }
        return directions;
    }
} // namespace

std::vector<glm::dvec3> CrowdMapBuilder::groundTrianglesNearCrowd(
    const Json::Crowd& crowd, const float* recastVerts, const int* tris, const int triCount) {
    const CrowdFrame frame = crowdFrame(crowd);
    const dvec2 center(frame.center);
    const dvec2 halfExtent =
        glm::abs(frame.xAxis) * (frame.size.x / 2.0) + glm::abs(frame.yAxis) * (frame.size.y / 2.0);
    const dvec2 regionMin = center - halfExtent - groundRegionMargin;
    const dvec2 regionMax = center + halfExtent + groundRegionMargin;
    const double zMin = frame.center.z - frame.size.z / 2.0 - groundRegionMargin;
    const double zMax = frame.center.z + frame.size.z / 2.0 + groundRegionMargin + crowd.generatorOffset;

    std::vector<dvec3> triangles;
    for (int t = 0; t < triCount; ++t) {
        std::array<dvec3, 3> corners;
        for (int c = 0; c < 3; ++c) {
            const float* v = &recastVerts[tris[t * 3 + c] * 3];
            corners[c] = dvec3(v[0], -v[2], v[1]);
        }
        const dvec3 low = glm::min(corners[0], glm::min(corners[1], corners[2]));
        const dvec3 high = glm::max(corners[0], glm::max(corners[1], corners[2]));
        if (high.x < regionMin.x || low.x > regionMax.x || high.y < regionMin.y || low.y > regionMax.y ||
            high.z < zMin || low.z > zMax) {
            continue;
        }
        triangles.insert(triangles.end(), corners.begin(), corners.end());
    }
    return triangles;
}

CrowdMapBuilder::CrowdMap CrowdMapBuilder::build(const Json::Crowd& crowd, const SceneVolumes& sceneVolumes,
    const std::vector<glm::dvec3>& groundTriangles, const std::vector<glm::dvec3>& heightTriangles, Report& report) {
    const CrowdFrame frame = crowdFrame(crowd);
    if (crowd.groupBoxes.size() >= noGroup) {
        throw std::runtime_error("Crowd " + crowd.name + ": " + std::to_string(crowd.groupBoxes.size()) +
            " group boxes, at most " + std::to_string(noGroup) + " are supported.");
    }
    if (groundTriangles.size() < 3) {
        throw std::runtime_error("Crowd " + crowd.name + ": there is no ground geometry inside the crowd box.");
    }

    const VolumeResolver resolve(crowd, sceneVolumes);
    std::vector<Volume> exclusion = resolve.boxes(crowd.exclusionBoxIndices);
    std::ranges::move(resolve.spheres(crowd.exclusionSphereIndices), std::back_inserter(exclusion));
    std::ranges::move(resolve.shapes(crowd.exclusionShapeIndices), std::back_inserter(exclusion));
    const std::vector<Volume> inclusionBoxes = resolve.boxes(crowd.inclusionBoxIndices);
    const std::vector<Volume> inclusionShapes = resolve.shapes(crowd.inclusionShapeIndices);
    std::vector<Volume> panicOnly = resolve.boxes(crowd.panicOnlyBoxIndices);
    std::ranges::move(resolve.shapes(crowd.panicOnlyShapeIndices), std::back_inserter(panicOnly));

    std::vector<TeleportVolumes> teleports;
    for (const Json::CrowdTeleport& teleport : crowd.teleports) {
        teleports.push_back({teleport.enterZone ? std::optional(boxVolume(*teleport.enterZone)) : std::nullopt,
            teleport.exitZone ? std::optional(boxVolume(*teleport.exitZone)) : std::nullopt,
            teleport.exitActionScared == TeleportActionType::LEAVE_CROWD});
    }
    std::vector<GroupVolumes> groups;
    for (const Json::CrowdGroupBox& group : crowd.groupBoxes) {
        std::vector<Volume> groupExclusion = resolve.boxes(group.exclusionBoxIndices);
        std::ranges::move(resolve.shapes(group.exclusionShapeIndices), std::back_inserter(groupExclusion));
        groups.push_back({boxVolume(group), std::move(groupExclusion), resolve.boxes(group.seedPointIndices)});
    }

    const GroundSurface ground(groundTriangles);
    std::optional<GroundSurface> heights;
    if (!heightTriangles.empty()) {
        heights.emplace(heightTriangles);
    }
    const GroundFinder groundFinder(crowd, frame, ground, heights, exclusion);
    const size_t pointCount = static_cast<size_t>(frame.width + 1) * (frame.height + 1);
    std::vector<float> rawHeights(pointCount, 0.0f);
    std::vector<uint8_t> pointStates(pointCount, offNavGrid);
    for (int py = 0; py <= frame.height; ++py) {
        for (int px = 0; px <= frame.width; ++px) {
            const dvec3 plane = frame.gridPoint(px, py);
            std::optional<dvec3> surface = groundFinder.find(px, py);
            if (surface) {
                ++report.gridPointsOnGeometry;
            } else {
                for (const std::vector<Volume>* inclusion : {&inclusionBoxes, &inclusionShapes}) {
                    const auto containing =
                        std::ranges::find_if(*inclusion, [&](const Volume& volume) { return volume.contains(plane); });
                    if (containing != inclusion->end()) {
                        surface = dvec3(dvec2(plane), containing->origin.z);
                    }
                }
                if (!surface) {
                    ++report.gridPointsOffGrid;
                    continue;
                }
                ++report.gridPointsOnInclusion;
            }
            const size_t p = frame.pointIndex(px, py);
            pointStates[p] = anyContains(panicOnly, *surface) ? onNavGridPanicOnly : onNavGrid;
            rawHeights[p] = static_cast<float>(surface->z - plane.z);
        }
    }

    CrowdMap map;
    float lowest = unreachable;
    float highest = -unreachable;
    for (const float height : rawHeights) {
        lowest = std::min(lowest, height);
        highest = std::max(highest, height);
    }
    if (highest <= lowest + minHeightSpan) {
        highest = lowest + minHeightSpan;
    }
    const float heightRange = highest - lowest;
    map.heightScale = heightRange * 0.5f / heightHalfRange;
    map.heightBias = (lowest + highest) * 0.5f + map.heightScale * 0.5f;
    std::vector<float> pointHeights(pointCount);
    map.gridMap.reserve(pointCount);
    for (size_t p = 0; p < pointCount; ++p) {
        const auto encoded =
            static_cast<int16_t>(static_cast<int>((-0.5f - ((lowest + highest) / heightRange) * heightHalfRange) +
                (2.0f / heightRange) * heightHalfRange * rawHeights[p]));
        map.gridMap.push_back({encoded, pointStates[p]});
        pointHeights[p] = map.heightScale * static_cast<float>(encoded) + map.heightBias;
    }

    const size_t cellCount = static_cast<size_t>(frame.width) * frame.height;
    map.cellFlags = paddedCells(cellCount, 0);
    map.cellGroups = paddedCells(cellCount, noGroup);
    std::vector<std::vector<size_t>> exitCells(teleports.size());
    std::vector<std::vector<size_t>> groupSources(groups.size());
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            uint8_t cornerStates = onNavGridPanicOnly;
            float cornerLow = unreachable;
            float cornerHigh = -unreachable;
            for (const auto& [cx, cy] : {std::pair{x, y}, {x + 1, y}, {x, y + 1}, {x + 1, y + 1}}) {
                const size_t p = frame.pointIndex(cx, cy);
                cornerStates &= pointStates[p];
                cornerLow = std::min(cornerLow, pointHeights[p]);
                cornerHigh = std::max(cornerHigh, pointHeights[p]);
            }
            if ((cornerHigh - cornerLow) / static_cast<float>(frame.cell) > maxCellSlope ||
                cornerStates == offNavGrid) {
                continue;
            }
            const size_t i = frame.cellIndex(x, y);
            map.cellFlags[i] = cellFlagWalkable;
            ++report.walkableCells;
            if (cornerStates == onNavGridPanicOnly) {
                map.cellFlags[i] |= cellFlagPanicOnly;
                ++report.panicOnlyCells;
            }
            const dvec3 nearCorner = frame.gridPoint(x, y, pointHeights[frame.pointIndex(x, y)]);
            const dvec3 farCorner = frame.gridPoint(x + 1, y + 1, pointHeights[frame.pointIndex(x + 1, y + 1)]);
            for (size_t t = 0; t < teleports.size(); ++t) {
                if (teleports[t].enterZone && teleports[t].enterZone->containsBoth(nearCorner, farCorner)) {
                    map.cellFlags[i] |= cellFlagTeleportIn;
                }
                if (teleports[t].exitZone && teleports[t].exitZone->containsBoth(nearCorner, farCorner)) {
                    map.cellFlags[i] |= cellFlagTeleportOut;
                    exitCells[t].push_back(i);
                }
            }
            if (cornerStates != onNavGrid) {
                continue;
            }
            for (size_t g = 0; g < groups.size(); ++g) {
                const GroupVolumes& group = groups[g];
                if (!group.box.containsBoth(nearCorner, farCorner) ||
                    anyContainsBoth(group.exclusion, nearCorner, farCorner)) {
                    continue;
                }
                map.cellGroups[i] = static_cast<uint8_t>(g);
                if (group.seedPoints.empty() || anyContainsBoth(group.seedPoints, nearCorner, farCorner)) {
                    groupSources[g].push_back(i);
                }
            }
        }
    }

    const size_t leavingTeleports = std::ranges::count_if(
        teleports, [](const TeleportVolumes& teleport) { return teleport.exitZone && teleport.leavesCrowd; });
    const size_t channelCount = 1 + leavingTeleports + groups.size();
    map.flowChannels.resize(channelCount);
    std::vector<std::vector<float>> channelCosts(channelCount);
    for (size_t c = 1; c < channelCount; ++c) {
        map.flowChannels[c].directions.assign(cellCount, 0);
        channelCosts[c].assign(cellCount, 0.0f);
    }
    FlowBuilder flowBuilder(frame, map.cellFlags, crowd.smoothenExitFlows);
    size_t nextChannel = 1;
    for (size_t t = 0; t < teleports.size(); ++t) {
        if (!teleports[t].exitZone || !teleports[t].leavesCrowd) {
            continue;
        }
        if (exitCells[t].empty()) {
            report.skippedExits.push_back(crowd.teleports[t].name);
            continue;
        }
        flowBuilder.build(exitCells[t], channelCosts[nextChannel], map.flowChannels[nextChannel].directions);
        report.exitCells.push_back(exitCells[t].size());
        ++nextChannel;
    }
    if (!groups.empty()) {
        map.groupFlowStartChannel = static_cast<int32_t>(nextChannel);
        for (size_t g = 0; g < groups.size(); ++g) {
            flowBuilder.build(
                groupSources[g], channelCosts[nextChannel + g], map.flowChannels[nextChannel + g].directions);
            report.groupCells.push_back(groupSources[g].size());
        }
    }
    compressFlowCosts(channelCosts, map.flowChannels);
    map.flowChannels[0].directions = ambientFlow(frame, flowBoxes(crowd, sceneVolumes, frame, crowd.flowBoxIndices),
        flowBoxes(crowd, sceneVolumes, frame, crowd.flowBoxTwoWayIndices), crowd.splineFlows, map.cellFlags,
        pointHeights);

    map.cellsX = static_cast<uint32_t>(frame.width);
    map.cellsY = static_cast<uint32_t>(frame.height);
    map.cellSize = crowd.cellSize;
    map.globalSize = {crowd.globalSize.x, crowd.globalSize.y, crowd.globalSize.z};
    map.generatorOffset = crowd.generatorOffset;
    map.objectToWorld = {glm::vec4(frame.xAxis, 0, 0), glm::vec4(frame.yAxis, 0, 0), glm::vec4(0, 0, 1, 0),
        glm::vec4(glm::vec3(frame.center), 1)};
    return map;
}
