#include "../../include/NavKit/util/CrowdMapBuilder.h"
#include "../../include/NavKit/model/Json.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
    using glm::dmat3;
    using glm::dvec2;
    using glm::dvec3;

    constexpr double infinity = std::numeric_limits<double>::infinity();
    constexpr double uprightTolerance = 1e-3;
    constexpr double layerMatch = 1.0;
    constexpr double navmeshBucketSize = 4.0;
    constexpr double groundBucketSize = 2.0;
    constexpr double groundRegionMargin = 1.0;
    constexpr double groundSearchBelow = 0.5;
    constexpr double groundSearchAbove = 0.25;
    constexpr double barycentricTolerance = 1e-6;
    constexpr double straightStep = 1.0;
    constexpr double maxNormalisedCost = 65534.0;
    constexpr double minHeightScale = 1e-5;
    constexpr int maxHeightOffset = 32767;
    constexpr std::array<std::array<int, 2>, 4> cellCorners = {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    constexpr std::array<std::array<int, 2>, 8> neighbours = {
        {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}}};
    constexpr std::array<std::array<std::array<int, 2>, 2>, 4> pinchAxes = {
        {{{{1, 0}, {-1, 0}}}, {{{0, 1}, {0, -1}}}, {{{1, 1}, {-1, -1}}}, {{{-1, 1}, {1, -1}}}}};

    double roundToFloat(const double value) {
        return static_cast<float>(value);
    }

    dvec3 toDvec3(const Json::Vec3& value) {
        return {value.x, value.y, value.z};
    }

    std::vector<std::array<int, 2>> directionStencil() {
        std::vector<std::array<int, 2>> stencil;
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -2; dy <= 2; ++dy) {
                if ((dx != 0 || dy != 0) && std::gcd(dx, dy) == 1) {
                    stencil.push_back({dx, dy});
                }
            }
        }
        return stencil;
    }

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

    double planeHeight(const CrowdMapBuilder::NavmeshPolygon& polygon, const double x, const double y) {
        const dvec3& a = polygon[0];
        for (size_t i = 1; i + 1 < polygon.size(); ++i) {
            const dvec3 normal = glm::cross(polygon[i] - a, polygon[i + 1] - a);
            if (std::abs(normal.z) > 1e-6) {
                return a.z - (normal.x * (x - a.x) + normal.y * (y - a.y)) / normal.z;
            }
        }
        return a.z;
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

    class NavmeshSurface {
    public:
        explicit NavmeshSurface(const std::vector<CrowdMapBuilder::NavmeshPolygon>& allPolygons) :
            buckets(navmeshBucketSize) {
            for (const auto& polygon : allPolygons) {
                if (polygon.size() < 3) {
                    continue;
                }
                double minX = infinity, minY = infinity, maxX = -infinity, maxY = -infinity;
                for (const dvec3& point : polygon) {
                    minX = std::min(minX, point.x);
                    minY = std::min(minY, point.y);
                    maxX = std::max(maxX, point.x);
                    maxY = std::max(maxY, point.y);
                }
                buckets.add(static_cast<int>(polygons.size()), minX, minY, maxX, maxY);
                polygons.push_back(&polygon);
            }
        }

        std::vector<double> heights(const double x, const double y) const {
            std::vector<double> result;
            for (const int index : buckets.at(x, y)) {
                if (pointInPolygon(*polygons[index], x, y)) {
                    result.push_back(planeHeight(*polygons[index], x, y));
                }
            }
            return result;
        }

    private:
        std::vector<const CrowdMapBuilder::NavmeshPolygon*> polygons;
        BucketIndex buckets;
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

        std::vector<double> heights(const double x, const double y) const {
            std::vector<double> result;
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
                if (wa >= -barycentricTolerance && wb >= -barycentricTolerance && wc >= -barycentricTolerance) {
                    result.push_back(wa * a.z + wb * b.z + wc * c.z);
                }
            }
            return result;
        }

    private:
        const std::vector<dvec3>& triangles;
        BucketIndex buckets;
    };

    using Volume = std::function<bool(const dvec3&)>;

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

    Volume boxVolume(const Json::Entity& entity) {
        return [box = boxFromEntity(entity)](const dvec3& point) { return box.contains(point); };
    }

    Volume sphereVolume(const Json::VolumeSphere& sphere) {
        return [center = toDvec3(sphere.position), radius = static_cast<double>(sphere.radius.data)](
                   const dvec3& point) { return glm::distance(point, center) <= radius; };
    }

    Volume shapeVolume(const Json::VolumeShape& shape) {
        const OrientedBox frame{toDvec3(shape.transform.t), transformRotation(shape.transform), dvec3(0.0)};
        std::vector<dvec2> outline;
        for (const Json::Vec3& point : shape.points) {
            outline.emplace_back(point.x, point.y);
        }
        const double extrusion = shape.flipSurfaceNormal ? -shape.extrusionHeight : shape.extrusionHeight;
        const double low = std::min(0.0, extrusion);
        const double high = std::max(0.0, extrusion);
        return [frame, outline = std::move(outline), low, high](const dvec3& point) {
            const dvec3 local = frame.local(point);
            return outline.size() >= 3 && low <= local.z && local.z <= high &&
                pointInPolygon(outline, local.x, local.y);
        };
    }

    struct Volumes {
        std::vector<Volume> exclusion;
        std::vector<Volume> inclusion;
        std::vector<Volume> panicOnly;
    };

    template <typename T> const T& resolveVolume(
        const Json::Crowd& crowd, const std::vector<T>& volumes, const size_t index, const char* listName) {
        if (index >= volumes.size()) {
            throw std::runtime_error("Crowd " + crowd.name + " references " + listName + " index " +
                std::to_string(index) + ", but the scene only has " + std::to_string(volumes.size()) + ".");
        }
        return volumes[index];
    }

    Volumes crowdVolumes(const Json::Crowd& crowd, const CrowdMapBuilder::SceneVolumes& scene) {
        Volumes volumes;
        const auto addBoxes = [&](std::vector<Volume>& target, const std::vector<size_t>& indices) {
            for (const size_t index : indices) {
                target.push_back(boxVolume(resolveVolume(crowd, scene.boxes, index, "volumeBoxes")));
            }
        };
        const auto addShapes = [&](std::vector<Volume>& target, const std::vector<size_t>& indices) {
            for (const size_t index : indices) {
                target.push_back(shapeVolume(resolveVolume(crowd, scene.shapes, index, "volumeShapes")));
            }
        };
        addBoxes(volumes.exclusion, crowd.exclusionBoxIndices);
        for (const size_t index : crowd.exclusionSphereIndices) {
            volumes.exclusion.push_back(sphereVolume(resolveVolume(crowd, scene.spheres, index, "volumeSpheres")));
        }
        addShapes(volumes.exclusion, crowd.exclusionShapeIndices);
        addBoxes(volumes.inclusion, crowd.inclusionBoxIndices);
        addShapes(volumes.inclusion, crowd.inclusionShapeIndices);
        addBoxes(volumes.panicOnly, crowd.panicOnlyBoxIndices);
        addShapes(volumes.panicOnly, crowd.panicOnlyShapeIndices);
        return volumes;
    }

    struct CrowdFrame {
        dvec3 center;
        dvec2 xAxis;
        dvec2 yAxis;
        dvec3 size;
        double cell;
        uint32_t width;
        uint32_t height;
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
        const auto width = static_cast<uint32_t>(std::max(0.0f, std::floor(crowd.globalSize.x / crowd.cellSize)));
        const auto height = static_cast<uint32_t>(std::max(0.0f, std::floor(crowd.globalSize.y / crowd.cellSize)));
        if (width == 0 || height == 0) {
            throw std::runtime_error("Crowd " + crowd.name + " is smaller than one grid cell.");
        }
        return {toDvec3(crowd.transform.t), glm::normalize(xAxis), glm::normalize(yAxis), toDvec3(crowd.globalSize),
            crowd.cellSize, width, height};
    }

    std::optional<double> nearestLayer(const std::vector<double>& layers, const double z) {
        std::optional<double> nearest;
        for (const double layer : layers) {
            if (std::abs(layer - z) <= layerMatch && (!nearest || std::abs(layer - z) < std::abs(*nearest - z))) {
                nearest = layer;
            }
        }
        return nearest;
    }

    class Grid {
    public:
        Grid(const CrowdFrame& frame, const NavmeshSurface& navmesh, const GroundSurface& ground,
            const Volumes& volumes, const double generatorOffset, const CrowdMapBuilder::CostSettings& costSettings,
            CrowdMapBuilder::Report& report) :
            frame(frame), width(frame.width), height(frame.height), count(size_t{frame.width} * frame.height),
            navmesh(navmesh), volumes(volumes), costSettings(costSettings) {
            samples = {dvec2(generatorOffset, generatorOffset), dvec2(1 - generatorOffset, generatorOffset),
                dvec2(generatorOffset, 1 - generatorOffset), dvec2(1 - generatorOffset, 1 - generatorOffset)};
            pointLayers.reserve(size_t{width + 1} * (height + 1));
            for (int py = 0; py <= height; ++py) {
                for (int px = 0; px <= width; ++px) {
                    pointLayers.push_back(layers(worldXy(px, py)));
                }
            }
            cellGround.resize(count);
            walkable.resize(count);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const auto [ground, isWalkable] = cellSurface(x, y);
                    cellGround[index(x, y)] = ground;
                    walkable[index(x, y)] = isWalkable;
                }
            }
            entryPenalty.resize(count);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    entryPenalty[index(x, y)] = roundToFloat(computeEntryPenalty(x, y));
                }
            }
            computePointHeights(ground, report);
        }

        std::vector<size_t> cellsIn(const Volume& volume) const {
            std::vector<size_t> cells;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t i = index(x, y);
                    if (!walkable[i]) {
                        continue;
                    }
                    const bool allCornersInside = std::ranges::all_of(cellCorners, [&](const auto& corner) {
                        const dvec2 point = worldXy(x + corner[0], y + corner[1]);
                        return volume(dvec3(point, *cellGround[i]));
                    });
                    if (allCornersInside) {
                        cells.push_back(i);
                    }
                }
            }
            return cells;
        }

        std::vector<double> costField(const std::vector<size_t>& targets) const {
            return dijkstra(
                targets,
                [&](const int x, const int y, const int dx, const int dy,
                    const double distance) -> std::optional<double> {
                    if (std::abs(dx) + std::abs(dy) == 2 &&
                        !(walkable[index(x + dx, y)] && walkable[index(x, y + dy)])) {
                        return std::nullopt;
                    }
                    const double step = dx != 0 && dy != 0 ? costSettings.diagonalStep : straightStep;
                    return roundToFloat(roundToFloat(distance + step) + entryPenalty[index(x + dx, y + dy)]);
                },
                neighbourOffsets());
        }

        std::vector<double> directionField(const std::vector<size_t>& targets) const {
            static const std::vector<std::array<int, 2>> stencil = directionStencil();
            return dijkstra(
                targets,
                [&](const int x, const int y, const int dx, const int dy,
                    const double distance) -> std::optional<double> {
                    if ((dx != 0 && dy != 0 || std::abs(dx) > 1 || std::abs(dy) > 1) && !clearLine(x, y, dx, dy)) {
                        return std::nullopt;
                    }
                    return distance + std::hypot(dx, dy) +
                        costSettings.directionWallPenalty * entryPenalty[index(x + dx, y + dy)];
                },
                stencil);
        }

        uint8_t downhillDirection(const std::vector<double>& distance, const size_t i) const {
            if (distance[i] == 0.0 || distance[i] == infinity) {
                return CrowdMapBuilder::noDirection;
            }
            const int x = static_cast<int>(i % width);
            const int y = static_cast<int>(i / width);
            const auto slope = [&](const int dx, const int dy) {
                const std::optional<double> ahead = distanceAt(distance, x + dx, y + dy);
                const std::optional<double> behind = distanceAt(distance, x - dx, y - dy);
                if (ahead && behind) {
                    return (*ahead - *behind) / 2.0;
                }
                if (ahead) {
                    return *ahead - distance[i];
                }
                if (behind) {
                    return distance[i] - *behind;
                }
                return 0.0;
            };
            const double gradientX = slope(1, 0);
            const double gradientY = slope(0, 1);
            if (gradientX == 0.0 && gradientY == 0.0) {
                return CrowdMapBuilder::noDirection;
            }
            double degrees = std::atan2(-gradientY, -gradientX) * 180.0 / std::numbers::pi;
            degrees = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
            return static_cast<uint8_t>(static_cast<int>(std::nearbyint(degrees / 3.0)) % 120);
        }

        dvec2 worldXy(const double gridX, const double gridY) const {
            const double localX = gridX * frame.cell - frame.size.x / 2;
            const double localY = gridY * frame.cell - frame.size.y / 2;
            return dvec2(frame.center) + frame.xAxis * localX + frame.yAxis * localY;
        }

        size_t index(const int x, const int y) const {
            return static_cast<size_t>(y) * width + x;
        }

        bool touchesWalkable(const int px, const int py) const {
            for (int cx = px - 1; cx <= px; ++cx) {
                for (int cy = py - 1; cy <= py; ++cy) {
                    if (inGrid(cx, cy) && walkable[index(cx, cy)]) {
                        return true;
                    }
                }
            }
            return false;
        }

        const CrowdFrame& frame;
        int width;
        int height;
        size_t count;
        std::vector<bool> walkable;
        std::vector<double> pointHeights;

    private:
        using StepCost = std::function<std::optional<double>(int, int, int, int, double)>;

        static const std::vector<std::array<int, 2>>& neighbourOffsets() {
            static const std::vector<std::array<int, 2>> offsets(neighbours.begin(), neighbours.end());
            return offsets;
        }

        bool inGrid(const int x, const int y) const {
            return x >= 0 && x < width && y >= 0 && y < height;
        }

        std::vector<double> layers(const dvec2& point) const {
            std::vector<double> result;
            for (const double z : navmesh.heights(point.x, point.y)) {
                if (std::abs(z - frame.center.z) <= frame.size.z / 2) {
                    result.push_back(z);
                }
            }
            std::ranges::sort(result);
            return result;
        }

        const std::vector<double>& pointLayersAt(const int px, const int py) const {
            return pointLayers[static_cast<size_t>(py) * (width + 1) + px];
        }

        std::pair<std::optional<double>, bool> cellSurface(const int x, const int y) const {
            std::array<std::vector<double>, 4> sampleLayers;
            std::array<dvec2, 4> samplePoints;
            for (size_t s = 0; s < samples.size(); ++s) {
                samplePoints[s] = worldXy(x + samples[s].x, y + samples[s].y);
                sampleLayers[s] = layers(samplePoints[s]);
            }
            std::vector<double> candidates;
            for (const double first : sampleLayers[0]) {
                double sum = 0;
                bool allMatched = true;
                for (const auto& layer : sampleLayers) {
                    const std::optional<double> matched = nearestLayer(layer, first);
                    allMatched = allMatched && matched.has_value();
                    sum += matched.value_or(0.0);
                }
                if (allMatched) {
                    candidates.push_back(sum / static_cast<double>(sampleLayers.size()));
                }
            }
            if (candidates.empty()) {
                return {std::nullopt, false};
            }
            std::ranges::stable_sort(candidates, {}, [&](const double z) { return std::abs(z - frame.center.z); });

            using LayeredPoint = std::pair<dvec2, const std::vector<double>*>;
            std::vector<LayeredPoint> corners;
            for (const auto& corner : cellCorners) {
                const int px = x + corner[0];
                const int py = y + corner[1];
                corners.emplace_back(worldXy(px, py), &pointLayersAt(px, py));
            }
            std::vector<LayeredPoint> cornersAndSamples = corners;
            for (size_t s = 0; s < samples.size(); ++s) {
                cornersAndSamples.emplace_back(samplePoints[s], &sampleLayers[s]);
            }
            const auto inside = [](const std::vector<Volume>& volumeList, const std::vector<LayeredPoint>& points,
                                    const double z) {
                return std::ranges::any_of(points, [&](const LayeredPoint& point) {
                    const double pointZ = nearestLayer(*point.second, z).value_or(z);
                    return std::ranges::any_of(
                        volumeList, [&](const Volume& volume) { return volume(dvec3(point.first, pointZ)); });
                });
            };
            const auto excluded = [&](const double z) { return inside(volumes.exclusion, corners, z); };
            const auto included = [&](const double z) { return inside(volumes.inclusion, cornersAndSamples, z); };
            const auto chosen =
                std::ranges::find_if(candidates, [&](const double z) { return included(z) && !excluded(z); });
            const double ground = chosen != candidates.end() ? *chosen : candidates.front();
            return {ground, !excluded(ground)};
        }

        bool blockedInGrid(const int x, const int y) const {
            return inGrid(x, y) && !walkable[index(x, y)];
        }

        double computeEntryPenalty(const int x, const int y) const {
            int blocked = 0;
            for (const auto& [dx, dy] : neighbours) {
                blocked += blockedInGrid(x + dx, y + dy);
            }
            int pinches = 0;
            for (const auto& [a, b] : pinchAxes) {
                pinches += blockedInGrid(x + a[0], y + a[1]) && blockedInGrid(x + b[0], y + b[1]);
            }
            return costSettings.blockedNeighbourPenalty * blocked + costSettings.pinchPenalty * pinches;
        }

        void computePointHeights(const GroundSurface& ground, CrowdMapBuilder::Report& report) {
            double totalGeometryOffset = 0;
            pointHeights.reserve(size_t{width + 1} * (height + 1));
            for (int py = 0; py <= height; ++py) {
                for (int px = 0; px <= width; ++px) {
                    std::vector<double> walkableGround;
                    std::vector<double> anyGround;
                    for (int cx = px - 1; cx <= px; ++cx) {
                        for (int cy = py - 1; cy <= py; ++cy) {
                            if (!inGrid(cx, cy) || !cellGround[index(cx, cy)]) {
                                continue;
                            }
                            anyGround.push_back(*cellGround[index(cx, cy)]);
                            if (walkable[index(cx, cy)]) {
                                walkableGround.push_back(*cellGround[index(cx, cy)]);
                            }
                        }
                    }
                    const std::vector<double>& preferred = walkableGround.empty() ? anyGround : walkableGround;
                    std::optional<double> reference;
                    if (!preferred.empty()) {
                        reference = std::accumulate(preferred.begin(), preferred.end(), 0.0) /
                            static_cast<double>(preferred.size());
                    }
                    const std::vector<double>& pointLayer = pointLayersAt(px, py);
                    double groundZ;
                    if (!pointLayer.empty() && reference) {
                        const double nearest = *std::ranges::min_element(
                            pointLayer, {}, [&](const double z) { return std::abs(z - *reference); });
                        groundZ = std::abs(nearest - *reference) <= layerMatch ? nearest : *reference;
                    } else if (reference) {
                        groundZ = *reference;
                    } else if (!pointLayer.empty()) {
                        groundZ = *std::ranges::min_element(
                            pointLayer, {}, [&](const double z) { return std::abs(z - frame.center.z); });
                    } else {
                        groundZ = frame.center.z;
                    }

                    if (const std::optional<double> geometryZ = groundBelow(ground, worldXy(px, py), groundZ)) {
                        totalGeometryOffset += *geometryZ - groundZ;
                        groundZ = *geometryZ;
                        ++report.gridPointsOnGeometry;
                    } else {
                        ++report.gridPointsOnNavmesh;
                    }
                    pointHeights.push_back(groundZ - frame.center.z);
                }
            }
            if (report.gridPointsOnGeometry != 0) {
                report.meanGeometryOffset = totalGeometryOffset / static_cast<double>(report.gridPointsOnGeometry);
            }
        }

        static std::optional<double> groundBelow(
            const GroundSurface& ground, const dvec2& point, const double navmeshZ) {
            std::optional<double> nearest;
            for (const double z : ground.heights(point.x, point.y)) {
                if (z < navmeshZ - groundSearchBelow || z > navmeshZ + groundSearchAbove) {
                    continue;
                }
                if (!nearest || std::abs(z - navmeshZ) < std::abs(*nearest - navmeshZ)) {
                    nearest = z;
                }
            }
            return nearest;
        }

        std::vector<double> dijkstra(const std::vector<size_t>& targets, const StepCost& stepCost,
            const std::vector<std::array<int, 2>>& offsets) const {
            std::vector<double> distance(count, infinity);
            using Entry = std::pair<double, size_t>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
            for (const size_t target : targets) {
                distance[target] = 0.0;
                queue.emplace(0.0, target);
            }
            while (!queue.empty()) {
                const auto [current, i] = queue.top();
                queue.pop();
                if (current > distance[i]) {
                    continue;
                }
                const int x = static_cast<int>(i % width);
                const int y = static_cast<int>(i / width);
                for (const auto& [dx, dy] : offsets) {
                    if (!inGrid(x + dx, y + dy) || !walkable[index(x + dx, y + dy)]) {
                        continue;
                    }
                    const std::optional<double> candidate = stepCost(x, y, dx, dy, current);
                    if (const size_t neighbour = index(x + dx, y + dy); candidate && *candidate < distance[neighbour]) {
                        distance[neighbour] = *candidate;
                        queue.emplace(*candidate, neighbour);
                    }
                }
            }
            return distance;
        }

        bool clearLine(const int x, const int y, const int dx, const int dy) const {
            const int steps = 2 * std::max(std::abs(dx), std::abs(dy));
            for (int step = 1; step <= steps; ++step) {
                const auto gx = static_cast<int>(x + 0.5 + dx * static_cast<double>(step) / steps);
                const auto gy = static_cast<int>(y + 0.5 + dy * static_cast<double>(step) / steps);
                if (!inGrid(gx, gy) || !walkable[index(gx, gy)]) {
                    return false;
                }
            }
            return true;
        }

        std::optional<double> distanceAt(const std::vector<double>& distance, const int x, const int y) const {
            if (!inGrid(x, y) || distance[index(x, y)] == infinity) {
                return std::nullopt;
            }
            return distance[index(x, y)];
        }

        const NavmeshSurface& navmesh;
        const Volumes& volumes;
        const CrowdMapBuilder::CostSettings costSettings;
        std::array<dvec2, 4> samples{};
        std::vector<std::vector<double>> pointLayers;
        std::vector<std::optional<double>> cellGround;
        std::vector<double> entryPenalty;
    };

    std::vector<uint8_t> paddedCells(const size_t count, const uint8_t fill) {
        const size_t blocks = (count + CrowdMapBuilder::cellsPerBlock - 1) / CrowdMapBuilder::cellsPerBlock;
        std::vector<uint8_t> cells(blocks * CrowdMapBuilder::cellsPerBlock, 0);
        std::fill_n(cells.begin(), count, fill);
        return cells;
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
    const double zMax = frame.center.z + frame.size.z / 2.0 + groundRegionMargin;

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
    const std::vector<NavmeshPolygon>& navmeshPolygons, const std::vector<glm::dvec3>& groundTriangles,
    const CostSettings& costSettings, Report& report) {
    const CrowdFrame frame = crowdFrame(crowd);
    const Volumes volumes = crowdVolumes(crowd, sceneVolumes);
    std::vector<std::pair<std::string, Volume>> exits;
    for (const auto& teleport : crowd.teleports) {
        if (!teleport.exitZone) {
            throw std::runtime_error("Crowd " + crowd.name + ": teleport " + teleport.name + " has no exit zone.");
        }
        exits.emplace_back(teleport.name, boxVolume(*teleport.exitZone));
    }
    std::vector<std::pair<std::string, Volume>> groups;
    for (const auto& group : crowd.groupBoxes) {
        groups.emplace_back(group.name, boxVolume(group));
    }
    if (groups.size() >= noGroup) {
        throw std::runtime_error("Crowd " + crowd.name + ": " + std::to_string(groups.size()) +
            " group boxes, at most " + std::to_string(noGroup) + " are supported.");
    }

    if (groundTriangles.size() < 3) {
        throw std::runtime_error("Crowd " + crowd.name + ": the GLB has no geometry inside the crowd box.");
    }
    const NavmeshSurface navmesh(navmeshPolygons);
    const GroundSurface ground(groundTriangles);
    const Grid grid(frame, navmesh, ground, volumes, crowd.generatorOffset, costSettings, report);

    std::vector<std::vector<size_t>> sources;
    const auto addSources = [&](const std::vector<std::pair<std::string, Volume>>& items, const std::string& kind,
                                std::vector<size_t>& reportedCounts) {
        for (const auto& [name, volume] : items) {
            sources.push_back(grid.cellsIn(volume));
            if (sources.back().empty()) {
                throw std::runtime_error(
                    "Crowd " + crowd.name + ": " + kind + " " + name + " covers no walkable cells.");
            }
            reportedCounts.push_back(sources.back().size());
        }
    };
    addSources(exits, "exit", report.exitCells);
    addSources(groups, "group", report.groupCells);

    CrowdMap map;
    map.cellsX = frame.width;
    map.cellsY = frame.height;
    map.cellSize = crowd.cellSize;
    map.globalSize = {crowd.globalSize.x, crowd.globalSize.y, crowd.globalSize.z};
    map.generatorOffset = crowd.generatorOffset;
    map.objectToWorld = {glm::vec4(frame.xAxis, 0, 0), glm::vec4(frame.yAxis, 0, 0), glm::vec4(0, 0, 1, 0),
        glm::vec4(glm::vec3(frame.center), 1)};
    map.groupFlowStartChannel = groups.empty() ? -1 : static_cast<int32_t>(1 + exits.size());

    map.cellFlags = paddedCells(grid.count, 0);
    map.cellGroups = paddedCells(grid.count, noGroup);
    for (size_t i = 0; i < grid.count; ++i) {
        if (grid.walkable[i]) {
            map.cellFlags[i] = cellFlagWalkable;
            ++report.walkableCells;
        }
    }
    std::vector<bool> panicCells(grid.count, false);
    for (const Volume& volume : volumes.panicOnly) {
        for (const size_t i : grid.cellsIn(volume)) {
            panicCells[i] = true;
        }
    }
    for (size_t i = 0; i < grid.count; ++i) {
        if (panicCells[i]) {
            map.cellFlags[i] |= cellFlagPanicOnly;
            ++report.panicOnlyCells;
        }
    }
    for (size_t e = 0; e < exits.size(); ++e) {
        for (const size_t i : sources[e]) {
            map.cellFlags[i] |= cellFlagTeleportOut;
        }
    }
    for (size_t g = 0; g < groups.size(); ++g) {
        for (const size_t i : sources[exits.size() + g]) {
            map.cellGroups[i] = static_cast<uint8_t>(g);
        }
    }

    std::vector<std::vector<double>> costFields;
    double longest = 0.0;
    for (const auto& cells : sources) {
        costFields.push_back(grid.costField(cells));
        for (const double cost : costFields.back()) {
            if (cost != infinity) {
                longest = std::max(longest, cost);
            }
        }
    }
    if (longest == 0.0) {
        longest = 1.0;
    }
    const double costScale = roundToFloat(maxNormalisedCost / longest);

    map.flowChannels.push_back({std::vector<uint8_t>(grid.count, noDirection), {}});
    for (size_t s = 0; s < sources.size(); ++s) {
        const std::vector<double> directionDistances = grid.directionField(sources[s]);
        FlowChannel channel;
        channel.directions.reserve(grid.count);
        channel.costs.reserve(grid.count);
        for (size_t i = 0; i < grid.count; ++i) {
            channel.directions.push_back(grid.downhillDirection(directionDistances, i));
            const double cost = costFields[s][i];
            channel.costs.push_back(
                cost == infinity ? unreachableCost : static_cast<uint16_t>(roundToFloat(cost * costScale)));
        }
        map.flowChannels.push_back(std::move(channel));
    }

    double lowest = infinity;
    double highest = -infinity;
    for (int py = 0; py <= grid.height; ++py) {
        for (int px = 0; px <= grid.width; ++px) {
            if (grid.touchesWalkable(px, py)) {
                const double heightOffset = grid.pointHeights[static_cast<size_t>(py) * (grid.width + 1) + px];
                lowest = std::min(lowest, heightOffset);
                highest = std::max(highest, heightOffset);
            }
        }
    }
    if (lowest > highest) {
        lowest = highest = 0.0;
    }
    map.heightBias = static_cast<float>((lowest + highest) / 2);
    map.heightScale = static_cast<float>(std::max((highest - lowest) / 2 / maxHeightOffset, minHeightScale));
    map.gridMap.reserve(grid.pointHeights.size());
    for (int py = 0; py <= grid.height; ++py) {
        for (int px = 0; px <= grid.width; ++px) {
            if (!grid.touchesWalkable(px, py)) {
                map.gridMap.push_back({0, 0});
                continue;
            }
            const double heightOffset = grid.pointHeights[static_cast<size_t>(py) * (grid.width + 1) + px];
            const double encoded = std::nearbyint((heightOffset - map.heightBias) / map.heightScale);
            const double limit = maxHeightOffset;
            map.gridMap.push_back({static_cast<int16_t>(std::clamp(encoded, -limit, limit)), 1});
        }
    }
    return map;
}
