#include "../include/NavKit/adapter/RecastAdapter.h"
#include "../include/NavKit/model/Json.h"
#include "../include/NavKit/model/ReasoningGrid.h"
#include "../include/NavKit/model/VisionData.h"
#include "../include/NavKit/module/Airg.h"
#include "../include/NavKit/module/Grid.h"
#include "../include/NavKit/module/NavKitSettings.h"
#include "../include/NavKit/module/Navp.h"
#include "../include/NavKit/module/Rpkg.h"
#include "../include/NavKit/module/Renderer.h"
#include "../include/NavKit/module/Scene.h"
#include "../include/NavKit/module/SceneMesh.h"
#include "../include/NavKit/render/Mesh.h"
#include "../include/NavKit/render/Model.h"
#include "../include/NavKit/util/UpdateChecker.h"
#include "../include/NavKit/util/GridGenerator.h"
#include "../include/NavKit/util/Math.h"
#include "../include/NavKit/util/Pathfinding.h"
#include "../include/RecastDemo/InputGeom.h"
#include <GL/glew.h>
#include <assimp/Exporter.hpp>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
    void check(const bool condition, const std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    void checkNear(const float actual, const float expected, const float epsilon = 0.0001f) {
        check(std::abs(actual - expected) <= epsilon, "floating-point values differ");
    }

    template <typename T, typename Read> T readJsonObject(const std::string& json, Read&& read) {
        const simdjson::padded_string input(json);
        simdjson::ondemand::parser parser;
        auto document = parser.iterate(input);
        T value;
        read(value, document.get_object().value());
        return value;
    }

    void testMathFunctions() {
        const float4 a{1.0f, 2.0f, 3.0f, 4.0f};
        const float4 b{4.0f, 6.0f, 3.0f, 0.0f};
        checkNear(Math::distanceSquared(a, b), 25.0f);
        checkNear(Math::distanceSquared(a), 14.0f);
        checkNear(Math::dotProduct(a, b), 25.0f);

        const float4 crossed = Math::crossProduct(a, b);
        checkNear(crossed.x, -12.0f);
        checkNear(crossed.y, 9.0f);
        checkNear(crossed.z, -2.0f);
        checkNear(crossed.w, 0.0f);

        const Vec3 normalized = Math::normalize(Vec3{0.0f, 3.0f, 4.0f});
        checkNear(normalized.X, 0.0f);
        checkNear(normalized.Y, 0.6f);
        checkNear(normalized.Z, 0.8f);
        const Vec3 zeroNormalized = Math::normalize(Vec3{});
        checkNear(zeroNormalized.GetMagnitude(), 0.0f);

        const Math::Plane plane = Math::buildPlane({0.0f, 2.0f, 0.0f}, {1.0f, 2.0f, 0.0f}, {0.0f, 2.0f, 1.0f});
        checkNear(plane.mNormal.X, 0.0f);
        checkNear(plane.mNormal.Y, -1.0f);
        checkNear(plane.mNormal.Z, 0.0f);
        checkNear(plane.mD, 2.0f);

        constexpr float halfRootTwo = 0.70710678118f;
        const Vec3 rotated = Math::rotatePoint({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, halfRootTwo, halfRootTwo});
        checkNear(rotated.X, 0.0f);
        checkNear(rotated.Y, 1.0f);
        checkNear(rotated.Z, 0.0f);
    }

    void testMathRayAabbIntersection() {
        const float4 minimum{-1.0f, -1.0f, -1.0f, 0.0f};
        const float4 maximum{1.0f, 1.0f, 1.0f, 0.0f};
        const float4 start{-2.0f, 0.0f, 0.0f, 0.0f};
        const float4 direction{1.0f, 0.0f, 0.0f, 0.0f};
        float4 hit{};
        check(Math::rayAabbIntersect(&hit, &minimum, &maximum, &start, &direction), "ray should hit AABB");
        checkNear(hit.x, -1.0f);
        checkNear(hit.y, 0.0f);

        const float4 inside{0.0f, 0.5f, 0.0f, 0.0f};
        check(Math::rayAabbIntersect(&hit, &minimum, &maximum, &inside, &direction),
            "ray beginning in the AABB should hit");
        checkNear(hit.x, inside.x);
        checkNear(hit.y, inside.y);

        const float4 parallelOutside{-2.0f, 2.0f, 0.0f, 0.0f};
        check(!Math::rayAabbIntersect(&hit, &minimum, &maximum, &parallelOutside, &direction),
            "parallel ray outside AABB should miss");
        const float4 invalidMaximum{-2.0f, 1.0f, 1.0f, 0.0f};
        check(!Math::rayAabbIntersect(&hit, &minimum, &invalidMaximum, &start, &direction),
            "invalid AABB should be rejected");
    }

    void testPathfindingHelpers() {
        const float4 start{0.0f, 0.0f, 0.0f, 0.0f};
        const float4 end{2.0f, 0.0f, 0.0f, 0.0f};
        float4 result{};
        const float4 before{-1.0f, 1.0f, 0.0f, 0.0f};
        Pathfinding::ClosestPointOnSegment(&result, &start, &end, &before);
        checkNear(result.x, 0.0f);
        checkNear(result.y, 0.0f);

        const float4 beyond{4.0f, 1.0f, 0.0f, 0.0f};
        Pathfinding::ClosestPointOnSegment(&result, &start, &end, &beyond);
        checkNear(result.x, 2.0f);

        constexpr std::array<std::tuple<int, int, int>, 8> neighbors = {
            {{4, 0, 1}, {4, 1, 2}, {4, 2, 5}, {4, 3, 8}, {4, 4, 7}, {4, 5, 6}, {4, 6, 3}, {4, 7, 0}}};
        for (const auto& [cell, direction, expected] : neighbors) {
            check(Pathfinding::GetNeighborCellIndex(cell, direction, 3) == expected,
                "grid-neighbor mapping returned an unexpected index");
        }
        check(Pathfinding::GetNeighborCellIndex(0, 0, 3) == -1, "north-west neighbor should be out of bounds");
        check(Pathfinding::GetNeighborCellIndex(8, 4, 3) == 11, "neighbor mapping should preserve the next row index");
        check(Pathfinding::GetNeighborCellIndex(2, 2, 3) == -1, "east neighbor should be out of bounds");

        const Vec3 p0{0.0f, 0.0f, 1.0f};
        const Vec3 p1{2.0f, 2.0f, 3.0f};
        const Vec3 p2{0.0f, 2.0f, 10.0f};
        const Vec3 p3{2.0f, 0.0f, 20.0f};
        Vec3 intersection{};
        check(Pathfinding::LineLineIntersect2D_G2(&p0, &p1, &p2, &p3, &intersection) == 1,
            "crossing finite segments should intersect");
        checkNear(intersection.X, 1.0f);
        checkNear(intersection.Y, 1.0f);
        checkNear(intersection.Z, 2.0f);

        const Vec3 p4{3.0f, 0.0f, 0.0f};
        const Vec3 p5{4.0f, 1.0f, 0.0f};
        check(Pathfinding::LineLineIntersect2D_G2(&p0, &p1, &p4, &p5, &intersection) == 0,
            "disjoint segments should not intersect");
    }

    void testJsonSerializationAndParsing() {
        Json::Entity expected;
        expected.id = "entity-17";
        expected.name = "stair";
        expected.position = {1.25f, -2.5f, 3.75f};
        expected.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        expected.scale = {"SVector3", {2.0f, 3.0f, 4.0f}};

        std::ostringstream output;
        expected.writeJson(output);
        const simdjson::padded_string input(output.str());
        simdjson::ondemand::parser parser;
        auto document = parser.iterate(input);
        Json::Entity actual;
        actual.readJson(document.get_object().value());

        check(actual.id == expected.id, "JSON Entity id did not round trip");
        check(actual.name == expected.name, "JSON Entity name did not round trip");
        checkNear(actual.position.x, expected.position.x);
        checkNear(actual.position.y, expected.position.y);
        checkNear(actual.position.z, expected.position.z);
        checkNear(actual.rotation.w, expected.rotation.w);
        check(actual.scale.type == "SVector3", "JSON Entity scale type did not round trip");
        checkNear(actual.scale.data.z, 4.0f);

        check(Json::toString(true) == "true", "JSON boolean formatting is incorrect");
        check(Json::toString(std::string_view("json")) == "json", "JSON string formatting is incorrect");
        check(Json::toString(int64_t{42}) == "42", "JSON integer formatting is incorrect");
    }

    void testJsonSceneRecordRoundTrips() {
        Json::Gate gate{"gate-id", "gate-name", {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {4.0f, 5.0f, 6.0f},
            {7.0f, 8.0f, 9.0f}};
        std::ostringstream gateJson;
        gate.writeJson(gateJson);
        const Json::Gate parsedGate = readJsonObject<Json::Gate>(
            gateJson.str(), [](Json::Gate& value, simdjson::ondemand::object object) { value.readJson(object); });
        check(parsedGate.id == gate.id && parsedGate.name == gate.name, "gate identity did not round trip");
        checkNear(parsedGate.position.z, 3.0f);
        checkNear(parsedGate.bboxCenter.y, 5.0f);
        checkNear(parsedGate.bboxHalfSize.z, 9.0f);

        Json::Room room{"room-id", "room-name", {10.0f, 11.0f, 12.0f}, {0.0f, 0.0f, 0.0f, 1.0f},
            {"SVector3", {-1.0f, -2.0f, -3.0f}}, {"SVector3", {4.0f, 5.0f, 6.0f}}};
        std::ostringstream roomJson;
        room.writeJson(roomJson);
        const Json::Room parsedRoom = readJsonObject<Json::Room>(
            roomJson.str(), [](Json::Room& value, simdjson::ondemand::object object) { value.readJson(object); });
        check(parsedRoom.id == room.id && parsedRoom.name == room.name, "room identity did not round trip");
        checkNear(parsedRoom.roomExtentMin.data.x, -1.0f);
        checkNear(parsedRoom.roomExtentMax.data.z, 6.0f);

        Json::AiArea area{"area-id", "area-name", {0.0f, 0.0f, 0.0f, 1.0f},
            Json::Parent{"parent-type", {"parent-id", "parent-name", "source", "type"}}};
        area.logicalParents = {"logical-a", "logical-b"};
        area.areaVolumeNames = {"volume-a"};
        std::ostringstream areaJson;
        area.writeJson(areaJson);
        const Json::AiArea parsedArea = readJsonObject<Json::AiArea>(
            areaJson.str(), [](Json::AiArea& value, simdjson::ondemand::object object) { value.readJson(object); });
        check(parsedArea.id == area.id && parsedArea.name == area.name, "AI area identity did not round trip");
        check(parsedArea.logicalParents == area.logicalParents, "AI area logical parents did not round trip");
        check(parsedArea.areaVolumeNames == area.areaVolumeNames, "AI area volume names did not round trip");
        check(parsedArea.parent.type == "parent-type" && parsedArea.parent.data.source == "source",
            "AI area parent metadata did not round trip");

        Json::VolumeSphere sphere{
            "sphere-id", "sphere-name", {3.0f, 2.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {"SRadius", 6.5f}};
        std::ostringstream sphereJson;
        sphere.writeJson(sphereJson);
        const Json::VolumeSphere parsedSphere = readJsonObject<Json::VolumeSphere>(sphereJson.str(),
            [](Json::VolumeSphere& value, simdjson::ondemand::object object) { value.readJson(object); });
        check(parsedSphere.id == sphere.id && parsedSphere.name == sphere.name,
            "volume sphere identity did not round trip");
        check(parsedSphere.radius.type == "SRadius", "volume sphere radius type did not round trip");
        checkNear(parsedSphere.radius.data, 6.5f);
    }

    void testJsonMaterialRecords() {
        Json::Mati material;
        material.hash = "mati-hash";
        material.diffuse = "diffuse-hash";
        material.normal = "normal-hash";
        material.specular = "specular-hash";
        std::ostringstream materialJson;
        material.writeJson(materialJson);
        const Json::Mati parsedMaterial = readJsonObject<Json::Mati>(materialJson.str(),
            [](Json::Mati& value, simdjson::ondemand::object object) { value.readJsonFromScene(object); });
        check(parsedMaterial.hash == material.hash && parsedMaterial.diffuse == material.diffuse,
            "material hashes did not round trip");
        check(parsedMaterial.normal == material.normal && parsedMaterial.specular == material.specular,
            "material texture records did not round trip");

        Json::PrimMati primitiveMaterial;
        primitiveMaterial.primHash = "primitive-hash";
        primitiveMaterial.matiHashes = {"mati-a", "mati-b"};
        std::ostringstream primitiveJson;
        primitiveMaterial.writeJson(primitiveJson);
        const Json::PrimMati parsedPrimitive = readJsonObject<Json::PrimMati>(primitiveJson.str(),
            [](Json::PrimMati& value, simdjson::ondemand::object object) { value.readJson(object); });
        check(parsedPrimitive.primHash == primitiveMaterial.primHash, "primitive material hash did not round trip");
        check(parsedPrimitive.matiHashes == primitiveMaterial.matiHashes,
            "primitive material references did not round trip");
    }

    void testPathfindingBoxAggregation() {
        Scene& scene = Scene::getInstance();
        const Json::PfBox originalInclude = scene.includeBox;
        const std::vector<Json::PfBox> originalExclusions = scene.exclusionBoxes;
        Json::Entity firstInclude;
        firstInclude.id = "include-a";
        firstInclude.name = "first";
        firstInclude.position = {0.0f, 0.0f, 0.0f};
        firstInclude.scale = {"SVector3", {2.0f, 4.0f, 6.0f}};
        firstInclude.type.data = Json::PfBoxes::INCLUDE_TYPE;

        Json::Entity secondInclude = firstInclude;
        secondInclude.id = "include-b";
        secondInclude.position = {4.0f, 2.0f, -2.0f};
        secondInclude.scale.data = {2.0f, 2.0f, 2.0f};

        Json::Entity exclusion = firstInclude;
        exclusion.id = "exclude-a";
        exclusion.name = "exclude";
        exclusion.type.data = Json::PfBoxes::EXCLUDE_TYPE;

        const simdjson::padded_string emptyArray(std::string_view("[]"));
        simdjson::ondemand::parser parser;
        auto document = parser.iterate(emptyArray);
        Json::PfBoxes boxes{document.get_array().value()};
        boxes.entities = {firstInclude, secondInclude, exclusion};
        boxes.readPathfindingBBoxes();
        check(scene.includeBox.id == "include-a", "include aggregation should retain the first include identifier");
        checkNear(scene.includeBox.pos.x, 2.0f);
        checkNear(scene.includeBox.pos.y, 0.5f);
        checkNear(scene.includeBox.pos.z, 0.0f);
        checkNear(scene.includeBox.scale.x, 6.0f);
        checkNear(scene.includeBox.scale.y, 5.0f);
        checkNear(scene.includeBox.scale.z, 6.0f);
        check(scene.exclusionBoxes.size() == 1 && scene.exclusionBoxes.front().id == "exclude-a",
            "exclude boxes should be transferred to the scene");

        boxes.entities.clear();
        boxes.readPathfindingBBoxes();
        check(scene.includeBox.id == Json::PfBoxes::NO_INCLUDE_BOX_FOUND,
            "missing include box should create the documented fallback");
        checkNear(scene.includeBox.scale.x, 1000.0f);
        check(scene.exclusionBoxes.empty(), "reading an empty box list should clear old exclusions");
        scene.includeBox = originalInclude;
        scene.exclusionBoxes = originalExclusions;
    }

    void testUpdateVersionComparison() {
        check(UpdateChecker::isVersionGreaterThan("2.0.0", "1.99.99"), "major version increase was not detected");
        check(UpdateChecker::isVersionGreaterThan("1.3.0", "1.2.99"), "minor version increase was not detected");
        check(UpdateChecker::isVersionGreaterThan("1.2.4", "1.2.3"), "patch version increase was not detected");
        check(!UpdateChecker::isVersionGreaterThan("1.2.3", "1.2.3"), "equal versions should not be newer");
        check(!UpdateChecker::isVersionGreaterThan("1.2.3", "1.2.4"), "older version was reported as newer");
        check(!UpdateChecker::isVersionGreaterThan("1.2", "1.2.0"),
            "missing trailing version components should compare as zero");
    }

    void testSceneMeshLookup() {
        Scene& scene = Scene::getInstance();
        const std::vector<Json::Mesh> originalMeshes = scene.meshes;
        Json::Mesh farther;
        farther.alocHash = "asset-hash";
        farther.entity.id = "entity-id";
        farther.pos = {10.0f, 0.0f, 0.0f};
        Json::Mesh nearest;
        nearest.primHash = "asset-hash";
        nearest.entity.id = "entity-id";
        nearest.pos = {1.0f, 0.0f, 0.0f};
        Json::Mesh wrongEntity = nearest;
        wrongEntity.entity.id = "other-entity";
        wrongEntity.pos = {0.0f, 0.0f, 0.0f};
        scene.meshes = {farther, wrongEntity, nearest};
        const float position[3] = {0.0f, 0.0f, 0.0f};

        check(scene.findMeshByHashAndIdAndPos("asset-hash", "entity-id", position) == &scene.meshes[2],
            "scene lookup should select the nearest matching mesh");
        check(scene.findMeshByHashAndIdAndPos("missing-hash", "entity-id", position) == nullptr,
            "scene lookup should return null when no hash matches");
        check(scene.findMeshByHashAndIdAndPos("asset-hash", "missing-id", position) == nullptr,
            "scene lookup should return null when no entity matches");
        scene.meshes = originalMeshes;
    }

    void testGridBoundsFromAirg() {
        Airg& airg = Airg::getInstance();
        Grid& grid = Grid::getInstance();
        ReasoningGrid* const originalReasoningGrid = airg.reasoningGrid;
        const float originalXMin = grid.xMin;
        const float originalYMin = grid.yMin;
        const float originalXMax = grid.xMax;
        const float originalYMax = grid.yMax;
        const float originalSpacing = grid.spacing;
        const int originalWidth = grid.gridWidth;
        auto reasoningGrid = std::make_unique<ReasoningGrid>();
        reasoningGrid->m_Properties.vMin = {-4.0f, -3.0f, -2.0f, 0.0f};
        reasoningGrid->m_Properties.vMax = {5.0f, 6.0f, 2.0f, 0.0f};
        reasoningGrid->m_Properties.fGridSpacing = 2.5f;
        airg.reasoningGrid = reasoningGrid.get();

        grid.loadBoundsFromAirg();
        checkNear(grid.xMin, -4.0f);
        checkNear(grid.yMin, -3.0f);
        checkNear(grid.xMax, 5.0f);
        checkNear(grid.yMax, 6.0f);
        checkNear(grid.spacing, 2.5f);
        check(grid.gridWidth == 4, "grid width should ceil the bounds-to-spacing ratio");

        airg.reasoningGrid = originalReasoningGrid;
        grid.xMin = originalXMin;
        grid.yMin = originalYMin;
        grid.xMax = originalXMax;
        grid.yMax = originalYMax;
        grid.spacing = originalSpacing;
        grid.gridWidth = originalWidth;
    }

    void testReasoningGridAirgRoundTrip() {
        ReasoningGrid original;
        original.m_Properties.vMin = {-10.0f, -20.0f, -30.0f, 1.0f};
        original.m_Properties.vMax = {10.0f, 20.0f, 30.0f, 1.0f};
        original.m_Properties.nGridWidth = 8;
        original.m_Properties.fGridSpacing = 2.25f;
        original.m_Properties.nVisibilityRange = 23;
        original.m_nNodeCount = 2;
        original.m_HighVisibilityBits = {{1, 2, 3}, 7};
        original.m_LowVisibilityBits = {{4, 5}, 9};
        original.m_deadEndData = {{6, 7}, 2};
        original.m_pVisibilityData = {10, 11, 12, 13, 14, 15};

        Waypoint first;
        first.vPos = {-3.0f, -4.0f, 5.0f, 1.0f};
        first.nVisionDataOffset = 0;
        first.nLayerIndex = 2;
        first.nNeighbors[0] = 1;
        Waypoint second;
        second.vPos = {6.0f, 7.0f, -8.0f, 1.0f};
        second.nVisionDataOffset = 3;
        second.nLayerIndex = 4;
        second.nNeighbors[4] = 0;
        original.m_WaypointList = {first, second};

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("navkit-airg-roundtrip-" + std::to_string(stamp) + ".airg");
        original.writeAirg(path);

        ReasoningGrid loaded;
        loaded.readAirg(path);
        check(loaded.m_nNodeCount == 2 && loaded.m_WaypointList.size() == 2, "AIRG round trip lost waypoint count");
        checkNear(loaded.m_Properties.vMin.y, -20.0f);
        checkNear(loaded.m_Properties.vMax.z, 30.0f);
        check(loaded.m_Properties.nGridWidth == 8 && loaded.m_Properties.nVisibilityRange == 23,
            "AIRG round trip lost grid properties");
        checkNear(loaded.m_Properties.fGridSpacing, 2.25f);
        check(loaded.m_WaypointList[0].nNeighbors[0] == 1 && loaded.m_WaypointList[1].nNeighbors[4] == 0,
            "AIRG round trip lost waypoint connections");
        checkNear(loaded.m_WaypointList[1].vPos.z, -8.0f);
        check(loaded.m_WaypointList[0].nVisionDataOffset == 0 && loaded.m_WaypointList[1].nVisionDataOffset == 3 &&
                loaded.m_WaypointList[1].nLayerIndex == 4,
            "AIRG round trip lost waypoint metadata");
        check(loaded.m_HighVisibilityBits.m_aBytes == original.m_HighVisibilityBits.m_aBytes &&
                loaded.m_HighVisibilityBits.m_nSize == 7,
            "AIRG round trip lost high visibility data");
        check(loaded.m_LowVisibilityBits.m_aBytes == original.m_LowVisibilityBits.m_aBytes &&
                loaded.m_LowVisibilityBits.m_nSize == 9,
            "AIRG round trip lost low visibility data");
        check(loaded.m_deadEndData.m_aBytes == original.m_deadEndData.m_aBytes && loaded.m_deadEndData.m_nSize == 2 &&
                loaded.m_pVisibilityData == original.m_pVisibilityData,
            "AIRG round trip lost dead-end or waypoint visibility data");
        check(loaded.getWaypointVisionData(0) == std::vector<uint8_t>{10, 11, 12},
            "first waypoint visibility range should end at the next waypoint offset");
        check(loaded.getWaypointVisionData(1) == std::vector<uint8_t>{13, 14, 15},
            "last waypoint visibility range should extend to the data end");
        std::filesystem::remove(path);
    }

    void testReasoningGridRejectsInvalidAirg() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("navkit-airg-invalid-" + std::to_string(stamp) + ".airg");
        {
            std::ofstream invalid(path, std::ios::binary);
            invalid << "not an airg file";
        }

        ReasoningGrid target;
        target.m_nNodeCount = 42;
        bool threw = false;
        try {
            target.readAirg(path);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "invalid AIRG input should throw a clear parse error");
        check(target.m_nNodeCount == 42, "failed AIRG load should preserve the existing grid");
        std::filesystem::remove(path);
    }

    void testWaypointConnectivityMapGeneration() {
        Airg& airg = Airg::getInstance();
        GridGenerator& generator = GridGenerator::getInstance();
        ReasoningGrid* const originalGrid = airg.reasoningGrid;
        const auto originalCells = generator.waypointCells;
        const auto originalWaypointMap = generator.m_WaypointMap;
        auto grid = std::make_unique<ReasoningGrid>();
        grid->m_Properties.vMin = {0.0f, 0.0f, 0.0f, 1.0f};
        grid->m_Properties.nGridWidth = 3;
        grid->m_Properties.fGridSpacing = 2.0f;
        Pathfinding::SGCell firstCell;
        firstCell.fZ = 0.0f;
        firstCell.m_Points.push_back({0.5f, 1.5f, 2.0f, 0.0f});
        Pathfinding::SGCell secondCell;
        secondCell.fZ = 3.0f;
        secondCell.m_Points.push_back({2.5f, 1.5f, 4.0f, 0.0f});
        Pathfinding::SGCell outOfBoundsCell;
        outOfBoundsCell.fZ = 0.0f;
        outOfBoundsCell.m_Points.push_back({6.5f, 1.5f, 1.0f, 0.0f});
        generator.waypointCells = {{0, {firstCell}}, {1, {secondCell}}, {2, {outOfBoundsCell}}};
        airg.reasoningGrid = grid.get();

        generator.GenerateWaypointConnectivityMap();
        check(grid->m_WaypointList.size() == 2 && grid->m_nNodeCount == 2,
            "connectivity-map generation should skip out-of-bounds candidate cells");
        check(grid->m_WaypointList[0].xi == 0 && grid->m_WaypointList[0].yi == 0 && grid->m_WaypointList[0].zi == 2,
            "first generated waypoint has incorrect grid coordinates");
        checkNear(grid->m_WaypointList[0].vPos.z, 2.001f);
        check(grid->m_WaypointList[0].nLayerIndex == -1,
            "generated waypoint should remain unlayered until layer generation");
        check(generator.m_WaypointMap.at(0) == std::vector<int>{0} &&
                generator.m_WaypointMap.at(1) == std::vector<int>{1},
            "generated waypoint-to-cell mapping is incorrect");

        airg.reasoningGrid = originalGrid;
        generator.waypointCells = originalCells;
        generator.m_WaypointMap = originalWaypointMap;
    }

    void testWaypointLayerGeneration() {
        Airg& airg = Airg::getInstance();
        GridGenerator& generator = GridGenerator::getInstance();
        ReasoningGrid* const originalGrid = airg.reasoningGrid;
        const auto originalWaypointMap = generator.m_WaypointMap;
        auto grid = std::make_unique<ReasoningGrid>();
        grid->m_Properties.vMin = {0.0f, 0.0f, 0.0f, 1.0f};
        grid->m_Properties.nGridWidth = 2;
        grid->m_Properties.fGridSpacing = 2.0f;
        Waypoint lower;
        lower.vPos = {0.5f, 0.5f, 0.0f, 1.0f};
        lower.nLayerIndex = -1;
        Waypoint upper;
        upper.vPos = {0.5f, 0.5f, 3.0f, 1.0f};
        upper.nLayerIndex = -1;
        grid->m_WaypointList = {lower, upper};
        airg.reasoningGrid = grid.get();
        generator.m_WaypointMap = {{0, {0, 1}}};

        generator.GenerateLayerIndices();
        check(grid->m_WaypointList[0].nLayerIndex == 0, "first waypoint in a cell should be assigned the first layer");
        check(grid->m_WaypointList[1].nLayerIndex == 1,
            "stacked waypoint in the same cell should receive a separate layer");

        airg.reasoningGrid = originalGrid;
        generator.m_WaypointMap = originalWaypointMap;
    }

    void testVisionDataTypes() {
        constexpr std::array<std::pair<int, std::string_view>, 8> types = {{{556, "blue"}, {1110, "red"},
            {1664, "indigo"}, {2218, "yellow"}, {2772, "purple"}, {3326, "teal"}, {3880, "white"}, {0, "black"}}};
        for (const auto& [size, expectedName] : types) {
            const VisionData data = VisionData::GetVisionDataType(size);
            check(data.getName() == expectedName, "visibility size mapped to the wrong type");
            checkNear(data.getColor().w, 0.6f);
        }
    }

    void testPackedWaypointVisibilityLookup() {
        ReasoningGrid grid;
        grid.m_Properties.vMin = {0.0f, 0.0f, 0.0f, 1.0f};
        grid.m_Properties.vMax = {4.0f, 4.0f, 1.0f, 1.0f};
        grid.m_Properties.nGridWidth = 4;
        grid.m_Properties.fGridSpacing = 1.0f;
        grid.m_Properties.nVisibilityRange = 1;

        for (const auto& [x, y, layer] : {std::tuple{1.5f, 1.5f, 1}, std::tuple{2.5f, 1.5f, 7},
                 std::tuple{0.5f, 1.5f, 7}, std::tuple{1.5f, 2.5f, 7}, std::tuple{3.5f, 3.5f, 7}}) {
            Waypoint waypoint;
            waypoint.vPos = {x, y, 0.0f, 1.0f};
            waypoint.nLayerIndex = layer;
            waypoint.nVisionDataOffset = static_cast<uint32_t>(grid.m_pVisibilityData.size());
            grid.m_WaypointList.push_back(waypoint);
            grid.m_pVisibilityData.insert(grid.m_pVisibilityData.end(), 9, 0);
        }
        grid.m_pVisibilityData[0] = 1;
        grid.m_pVisibilityData[2] = 7;
        const auto setBit = [&grid](const size_t recordOffset, const size_t bitIndex) {
            grid.m_pVisibilityData[recordOffset + 4 + bitIndex / 8] |= static_cast<uint8_t>(1u << (bitIndex % 8));
        };
        setBit(0, 4);
        setBit(0, 13);
        setBit(0, 21);
        setBit(0, 32);

        const auto selfVisibility = grid.getVisibility(0, 0);
        check(selfVisibility.has_value() && selfVisibility->low && selfVisibility->high,
            "source-layer visibility should read the first packed layer in low/high channel order");
        const auto otherLayerVisibility = grid.getVisibility(0, 1);
        check(otherLayerVisibility.has_value() && otherLayerVisibility->low && !otherLayerVisibility->high,
            "other-layer low visibility should decode from its layer offset and LSB-first bit position");
        const auto highOnlyVisibility = grid.getVisibility(0, 2);
        check(highOnlyVisibility.has_value() && !highOnlyVisibility->low && highOnlyVisibility->high,
            "other-layer high visibility should decode independently from the low channel");
        check(!grid.getVisibility(0, 4).has_value(),
            "visibility lookup should reject targets outside the source waypoint range");
    }

    void testSceneSaveSerializesCollections() {
        Scene& scene = Scene::getInstance();
        scene.version = 17;
        scene.meshes.clear();
        Json::Mesh mesh;
        mesh.alocHash = "aloc";
        mesh.primHash = "prim";
        mesh.roomName = "room";
        mesh.roomFolderName = "folder";
        mesh.entity.id = "mesh-id";
        mesh.entity.name = "mesh-name";
        mesh.entity.position = {1.0f, 2.0f, 3.0f};
        mesh.entity.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        mesh.entity.scale = {"SVector3", {1.0f, 1.0f, 1.0f}};
        scene.meshes.push_back(mesh);

        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("navkit-scene-serialization-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        scene.saveScene(path.string());
        {
            std::ifstream savedFile(path);
            const std::string serialized((std::istreambuf_iterator<char>(savedFile)), std::istreambuf_iterator<char>());
            check(serialized.find(R"("version":17)") != std::string::npos, "saved scene has the wrong version");
            check(serialized.find(R"("alocHash":"aloc")") != std::string::npos,
                "saved scene did not serialize its mesh list");
            check(serialized.find(R"("pfSeedPoints":[])") != std::string::npos,
                "empty scene collections should serialize as empty arrays");
        }
        scene.meshes.clear();
        scene.version = 2;
        std::filesystem::remove(path);
    }

    void testMeshCpuProperties() {
        const std::vector<Vertex> vertices = {{{-1.0f, 2.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
            {{3.0f, -2.0f, 5.0f}, {}, {1.0f, 1.0f, 1.0f, 0.5f}, {}}};
        Mesh blended(vertices, {0, 1}, {});
        checkNear(blended.aabbMin.x, -1.0f);
        checkNear(blended.aabbMin.y, -2.0f);
        checkNear(blended.aabbMax.x, 3.0f);
        checkNear(blended.aabbMax.z, 5.0f);
        check(blended.isTransparent, "partially transparent vertex should mark mesh transparent");
        check(blended.isBlended, "partially transparent vertex should mark mesh blended");
        check(blended.VAO == 0 && blended.VBO == 0 && blended.EBO == 0,
            "CPU mesh construction should not allocate OpenGL resources");

        Texture rgba{};
        rgba.uploadFormat = GL_RGBA;
        Mesh alphaTexture({}, {}, {rgba});
        check(alphaTexture.isTransparent, "RGBA texture should mark mesh transparent");
        check(!alphaTexture.isBlended, "RGBA texture alone should not classify vertex blending");

        rgba.data = {255, 255, 255, 255, 255, 255, 255, 128};
        rgba.hasBlendedAlpha = true;
        Mesh blendedTexture({}, {}, {rgba});
        check(blendedTexture.isTransparent, "RGBA texture should mark mesh transparent");
        check(blendedTexture.isBlended, "texture alpha should mark mesh blended");

        rgba.data = {255, 255, 255, 0, 255, 255, 255, 25};
        rgba.hasBlendedAlpha = false;
        Mesh cutoutTexture({}, {}, {rgba});
        check(cutoutTexture.isTransparent, "cutout texture should mark mesh transparent");
        check(!cutoutTexture.isBlended, "fully transparent texels should not mark mesh blended");

        Mesh opaque({{{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f, 1.0f}, {}}}, {}, {});
        check(!opaque.isTransparent && !opaque.isBlended, "opaque vertex should remain opaque");
    }

    void testModelMeshBatching() {
        aiMesh source;
        source.mNumVertices = 3;
        source.mVertices = new aiVector3D[3]{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        source.mNumFaces = 1;
        source.mFaces = new aiFace[1];
        source.mFaces[0].mNumIndices = 3;
        source.mFaces[0].mIndices = new unsigned int[3]{0, 1, 2};
        std::vector<Texture> loadedTextures;

        Mesh batch = Model::processBatchedMeshes({&source}, nullptr, {}, loadedTextures);
        check(batch.vertices.size() == 3 && batch.indices == std::vector<unsigned int>{0, 1, 2},
            "model batching should copy vertices and face indices");
        checkNear(batch.vertices[1].position.x, 1.0f);
        checkNear(batch.vertices[2].position.y, 1.0f);
        checkNear(batch.vertices[0].normal.x, 0.0f);
        checkNear(batch.vertices[0].color.w, 1.0f);
        check(loadedTextures.empty(), "batching without a scene should not attempt material texture loads");
    }

    void testRecastAdapterGeometrySurface() {
        const Vec3 recast = RecastAdapter::convertFromNavPowerToRecast({2.0f, 3.0f, 4.0f});
        checkNear(recast.X, 2.0f);
        checkNear(recast.Y, 4.0f);
        checkNear(recast.Z, -3.0f);
        const Vec3 navPower = RecastAdapter::convertFromRecastToNavPower(recast);
        checkNear(navPower.X, 2.0f);
        checkNear(navPower.Y, 3.0f);
        checkNear(navPower.Z, 4.0f);

        check(RecastAdapter::getEdges(nullptr, 1).empty(), "edge lookup without a query should be empty");
        RecastAdapter& adapter = RecastAdapter::getInstance();
        const Vec3 fallbackNormal = adapter.calculateNormal(nullptr, 1);
        const Vec3 fallbackCentroid = adapter.calculateCentroid(nullptr, 1);
        checkNear(fallbackNormal.X, 0.0f);
        checkNear(fallbackNormal.Y, 1.0f);
        checkNear(fallbackNormal.Z, 0.0f);
        checkNear(fallbackCentroid.GetMagnitude(), 0.0f);
        check(adapter.getPoly(-1, 0) == 0, "invalid Recast tile should not produce a polygon reference");
        check(adapter.getClosestPolys(nullptr, {}, 5).empty(), "polygon query without a query object should be empty");
        check(adapter.getClosestReachablePolys(nullptr, {}, 1, 5).empty(),
            "reachable polygon query without a query object should be empty");
        auto scene = std::make_unique<aiScene>();
        scene->mRootNode = new aiNode();
        scene->mRootNode->mTransformation.a4 = 100.0f;
        scene->mRootNode->mTransformation.b4 = 5.0f;
        scene->mRootNode->mTransformation.c4 = 7.0f;
        scene->mRootNode->mNumMeshes = 1;
        scene->mRootNode->mMeshes = new unsigned int[1]{0};
        scene->mNumMaterials = 1;
        scene->mMaterials = new aiMaterial*[1]{new aiMaterial()};
        scene->mNumTextures = 1;
        scene->mTextures = new aiTexture*[1]{new aiTexture()};
        constexpr std::array<unsigned char, 70> pngData = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00,
            0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
            0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf,
            0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
            0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        scene->mTextures[0]->mWidth = static_cast<unsigned int>(pngData.size());
        scene->mTextures[0]->mHeight = 0;
        scene->mTextures[0]->pcData = new aiTexel[(pngData.size() + sizeof(aiTexel) - 1) / sizeof(aiTexel)];
        std::copy(pngData.begin(), pngData.end(), reinterpret_cast<unsigned char*>(scene->mTextures[0]->pcData));
        std::copy_n("png", 3, scene->mTextures[0]->achFormatHint);
        aiString embeddedTexturePath;
        embeddedTexturePath.Set("*0");
        scene->mMaterials[0]->AddProperty(&embeddedTexturePath, AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0));
        scene->mNumMeshes = 1;
        scene->mMeshes = new aiMesh*[1]{new aiMesh()};
        aiMesh* mesh = scene->mMeshes[0];
        mesh->mMaterialIndex = 0;
        mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
        const std::array<aiVector3D, 10> vertices = {{{0, 0, 0}, {3, 0, 0}, {3, 0, -3}, {0, 0, -3}, {4, 0, -3},
            {4, 0, 0}, {7, 0, 0}, {7, 0, -3}, {10, 0, 0}, {10, 0, -3}}};
        mesh->mNumVertices = static_cast<unsigned int>(vertices.size());
        mesh->mVertices = new aiVector3D[vertices.size()];
        std::copy(vertices.begin(), vertices.end(), mesh->mVertices);
        mesh->mTextureCoords[0] = new aiVector3D[vertices.size()]{};
        constexpr std::array<std::array<unsigned int, 3>, 6> faces = {
            {{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}, {7, 6, 8}, {7, 8, 9}}};
        mesh->mNumFaces = static_cast<unsigned int>(faces.size());
        mesh->mFaces = new aiFace[faces.size()];
        for (size_t i = 0; i < faces.size(); ++i) {
            mesh->mFaces[i].mNumIndices = 3;
            mesh->mFaces[i].mIndices = new unsigned int[3]{faces[i][0], faces[i][1], faces[i][2]};
        }
        const std::filesystem::path geometryPath = std::filesystem::temp_directory_path() /
            ("NavKitTestGeometry_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                ".glb");
        Assimp::Exporter exporter;
        check(exporter.Export(scene.get(), "glb2", geometryPath.string()) == AI_SUCCESS,
            "Assimp should export test geometry as GLB");
        Model loadedModel;
        loadedModel.loadModelData(geometryPath.string());
        check(!loadedModel.texturesLoaded.empty(), "GLB loader should discover its embedded material texture");
        check(loadedModel.texturesLoaded[0].loaded, "GLB loader should load its embedded material texture");
        check(!loadedModel.meshes.empty() && !loadedModel.meshes[0].vertices.empty(),
            "GLB loader should load mesh vertices");
        checkNear(loadedModel.meshes[0].vertices[0].position.x, 100.0f);
        checkNear(loadedModel.meshes[0].vertices[0].position.y, 5.0f);
        checkNear(loadedModel.meshes[0].vertices[0].position.z, 7.0f);
        check(adapter.loadInputGeom(geometryPath.string()), "Recast adapter should load test GLB geometry");
        check(adapter.getVertCount() == 10, "Recast adapter loaded the wrong vertex count");
        check(adapter.getTriCount() == 6, "Recast adapter loaded the wrong triangle count");
        checkNear(adapter.inputGeom->getMesh()->getVerts()[0], 100.0f);
        checkNear(adapter.inputGeom->getMesh()->getVerts()[1], 5.0f);
        checkNear(adapter.inputGeom->getMesh()->getVerts()[2], 7.0f);
        ReasoningGrid vision;
        vision.m_Properties.fGridSpacing = 1;
        vision.m_Properties.nVisibilityRange = 1;
        vision.m_Properties.vMin = {100, -7, 0, 1};
        Waypoint below, above;
        below.vPos = {100.5f, -6.5f, 4, 1};
        above.vPos = {101.0f, -6.5f, 6, 1};
        above.nLayerIndex = 1;
        vision.m_WaypointList = {below, above};
        GridGenerator::addVisibilityData(&vision);
        const auto forward = vision.getVisibility(0, 1), reverse = vision.getVisibility(1, 0);
        check(forward && !forward->low && forward->high, "collision floor should block only the low ray");
        check(reverse && !reverse->low && reverse->high, "collision must block from either triangle side");
        float rayStart[3] = {100.5f, 4.6f, 6.5f}, rayEnd[3] = {101.0f, 6.6f, 6.5f};
        float hitTime = 1;
        const bool directHit = adapter.inputGeom->raycastMesh(rayStart, rayEnd, hitTime) != -1;
        std::swap_ranges(rayStart, rayStart + 3, rayEnd);
        const bool reverseHit = adapter.inputGeom->raycastMesh(rayStart, rayEnd, hitTime) != -1;
        check(directHit || reverseHit, "generated blocked bit must agree with direct collision raycast");
        std::error_code removeError;
        std::filesystem::remove(geometryPath, removeError);

        const float minimum[3] = {-2.0f, -1.0f, -4.0f};
        const float maximum[3] = {12.0f, 3.0f, 2.0f};
        adapter.setMeshBBox(minimum, maximum);
        check(adapter.sample->m_maxTiles > 0 && adapter.sample->m_maxPolysPerTile > 0,
            "tile settings should remain valid after setting a mesh bound");
    }

    void testMarkerObjectNames() {
        auto& adapter = RecastAdapter::getInstance();
        auto& scene = Scene::getInstance();
        auto& ranges = SceneMesh::getInstance().objectTriangleRanges;
        const auto savedRanges = ranges;
        const bool loaded = scene.sceneLoaded;
        const auto selected = adapter.selectedObject;
        const bool markerSet = adapter.markerPositionSet;
        const std::array<float, 3> marker = {
            adapter.markerPosition[0], adapter.markerPosition[1], adapter.markerPosition[2]};
        scene.sceneLoaded = true;
        SceneMeshHitTestResult hit;
        hit.hitIndex = 0;
        hit.hitTime = 0.5f;
        hit.rayEnd[0] = 2;
        for (const std::string name : {"", "Mesh 1", "0123456789ABCDEF", "0123456789ABCDEF_",
                 "0123456789ABCDEF_0123456789ABCDEF", "0123456789ABCDEF_0123456789ABCDEF.001"}) {
            ranges = {{name, {0, 1}}};
            adapter.setMarker(hit);
            check(adapter.selectedObject == name, "marker should retain arbitrary mesh names safely");
            checkNear(adapter.markerPosition[0], 1);
        }
        ranges.clear();
        adapter.setMarker(hit);
        check(adapter.selectedObject.empty(), "unmatched triangle should clear stale selected object");
        hit.hitIndex = -1;
        hit.rayEnd[0] = 10;
        adapter.setMarker(hit);
        checkNear(adapter.markerPosition[0], 1);
        ranges = savedRanges;
        scene.sceneLoaded = loaded;
        adapter.selectedObject = selected;
        adapter.markerPositionSet = markerSet;
        std::copy(marker.begin(), marker.end(), adapter.markerPosition);
    }

    void testNavpBinaryLoading() {
        Navp& navp = Navp::getInstance();
        navp.loadNavMesh("tests/resources/triangles.navp", false, false, false);
        check(navp.navpLoaded, "valid test NAVP should mark the module loaded");
        check(Navp::getTotalAreaCount(navp.navMesh) > 0, "binary NAVP should expose test areas");
        check(!navp.canSave() || (!navp.loading && !navp.building), "Navp load should finish its loading state");

        NavKitSettings& settings = NavKitSettings::getInstance();
        const std::string originalOutputFolder = settings.outputFolder;
        const std::filesystem::path outputDirectory = std::filesystem::temp_directory_path() /
            ("NavKitNavpGlb_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(outputDirectory);
        settings.outputFolder = outputDirectory.string();
        SceneMesh::getInstance().buildGlbFromNavp(false);
        const std::filesystem::path generatedGlb = outputDirectory / "outputNavp.glb";
        check(std::filesystem::exists(generatedGlb), "building a GLB from Navp should create the output file");
        RecastAdapter& adapter = RecastAdapter::getInstance();
        check(adapter.loadInputGeom(generatedGlb.string()), "Recast should load a GLB exported from Navp");
        check(adapter.getTriCount() > 0, "Navp GLB export should contain triangles");
        std::filesystem::remove_all(outputDirectory);
        settings.outputFolder = originalOutputFolder;
        navp.navpLoaded = false;
    }

    void testModuleDefaultsAndRpkgGating() {
        Grid& grid = Grid::getInstance();
        const float originalSpacing = grid.spacing;
        const float originalXOffset = grid.xOffset;
        const float originalYOffset = grid.yOffset;
        Airg::resetDefaults();
        checkNear(grid.spacing, 2.25f);
        checkNear(grid.xOffset, 0.0f);
        checkNear(grid.yOffset, 0.0f);
        grid.spacing = originalSpacing;
        grid.xOffset = originalXOffset;
        grid.yOffset = originalYOffset;

        const bool originalReady = Rpkg::extractionDataInitComplete;
        const bool originalUnknownVersion = Rpkg::unknownGameVersion;
        Rpkg::extractionDataInitComplete = false;
        Rpkg::unknownGameVersion = false;
        check(!Rpkg::canExtract(), "Rpkg extraction should wait for initialization");
        Rpkg::extractionDataInitComplete = true;
        check(Rpkg::canExtract(), "Rpkg extraction should be enabled when initialization completes");
        Rpkg::unknownGameVersion = true;
        check(!Rpkg::canExtract(), "Rpkg extraction should reject unknown game versions");
        Rpkg::extractionDataInitComplete = originalReady;
        Rpkg::unknownGameVersion = originalUnknownVersion;

        SceneMesh& sceneMesh = SceneMesh::getInstance();
        std::array<bool, 8> lods{};
        std::copy_n(sceneMesh.primLods, lods.size(), lods.begin());
        const MeshType originalMeshType = sceneMesh.meshTypeForBuild;
        const SceneMeshBuildType originalBuildType = sceneMesh.sceneMeshBuildType;
        const bool originalSkipExtraction = sceneMesh.skipExtractingAlocsOrPrims;
        const bool originalFilter = sceneMesh.filterToIncludeBox;
        const bool originalCollidable = sceneMesh.onlyCollidable;
        sceneMesh.resetDefaults();
        check(sceneMesh.buildPrimLodsString() == "11111111", "scene mesh defaults should enable all Prim LODs");
        std::copy(lods.begin(), lods.end(), sceneMesh.primLods);
        sceneMesh.meshTypeForBuild = originalMeshType;
        sceneMesh.sceneMeshBuildType = originalBuildType;
        sceneMesh.skipExtractingAlocsOrPrims = originalSkipExtraction;
        sceneMesh.filterToIncludeBox = originalFilter;
        sceneMesh.onlyCollidable = originalCollidable;
    }

    void testModuleStateAndCapabilities() {
        Airg& airg = Airg::getInstance();
        Navp& navp = Navp::getInstance();
        Scene& scene = Scene::getInstance();
        SceneMesh& sceneMesh = SceneMesh::getInstance();
        NavKitSettings& settings = NavKitSettings::getInstance();

        check(sceneMesh.canLoad(), "scene mesh should load when no path is pending");
        sceneMesh.glbToLoad = "pending.glb";
        check(!sceneMesh.canLoad(), "scene mesh should not load while another path is pending");
        sceneMesh.glbToLoad.clear();

        for (int i = 0; i < 8; ++i) {
            sceneMesh.primLods[i] = (i % 2) == 0;
        }
        check(sceneMesh.buildPrimLodsString() == "10101010", "Prim LOD string should preserve all LOD toggles");
        sceneMesh.blendFileBuilt = true;
        check(sceneMesh.canSaveBlend(), "built Blender scene should be saveable");
        sceneMesh.blendFileBuilt = false;
        check(!sceneMesh.canSaveBlend(), "unbuilt Blender scene should not be saveable");

        airg.airgLoaded = false;
        airg.airgLoading = false;
        airg.airgSaveState.clear();
        navp.navpLoaded = false;
        check(airg.canLoad(), "Airg load should be available when idle");
        check(!airg.canSave(), "Airg save should be unavailable without loaded data");
        check(!airg.canBuildAirg(), "Airg build should require a loaded Navp");
        navp.navpLoaded = true;
        sceneMesh.glbLoaded = false;
        check(!airg.canBuildAirg(), "Airg build should require loaded collision geometry");
        sceneMesh.glbLoaded = true;
        check(airg.canBuildAirg(), "Airg build should be enabled with Navp and collision geometry");
        airg.airgBuilding = true;
        check(!airg.canBuildAirg(), "Airg build should be disabled while already building");
        airg.airgBuilding = false;
        airg.airgLoaded = true;
        check(airg.canSave(), "loaded Airg should be saveable");
        airg.airgLoading = true;
        check(!airg.canLoad(), "Airg load should be disabled while loading");
        airg.airgLoading = false;
        airg.airgSaveState.push_back(true);
        check(!airg.canLoad() && !airg.canSave() && !airg.canBuildAirg(),
            "Airg save transaction should lock load, save, and build actions");
        airg.airgSaveState.clear();

        navp.navpLoaded = true;
        navp.loading = false;
        navp.building = false;
        navp.navpBuildDone.store(false);
        check(navp.canSave(), "loaded idle Navp should be saveable");
        navp.loading = true;
        check(!navp.canSave(), "Navp should not be saveable while loading");
        navp.loading = false;
        navp.building = true;
        check(!navp.canSave(), "Navp should not be saveable while building");
        navp.building = false;
        navp.navpLoaded = false;

        const bool oldHitmanSet = settings.hitmanSet;
        const bool oldOutputSet = settings.outputSet;
        const bool oldBlenderSet = settings.blenderSet;
        const bool oldExtracting = sceneMesh.extractingResources;
        const bool oldSceneLoaded = scene.sceneLoaded;
        const bool oldBuildStarted = sceneMesh.blenderSceneMeshBuildStarted;
        const bool oldBuildDone = sceneMesh.blenderSceneMeshGenerationDone;
        const bool oldExtractionReady = Rpkg::extractionDataInitComplete;
        settings.hitmanSet = true;
        settings.outputSet = true;
        settings.blenderSet = true;
        sceneMesh.extractingResources = false;
        scene.sceneLoaded = true;
        sceneMesh.blenderSceneMeshBuildStarted = false;
        sceneMesh.blenderSceneMeshGenerationDone = false;
        Rpkg::extractionDataInitComplete = true;
        check(sceneMesh.canBuildGlbFromScene(), "scene mesh build should be enabled when prerequisites are ready");
        sceneMesh.extractingResources = true;
        check(!sceneMesh.canBuildGlbFromScene(), "scene mesh build should wait for resource extraction");
        sceneMesh.extractingResources = false;
        sceneMesh.blenderSceneMeshBuildStarted = true;
        check(!sceneMesh.canBuildGlbFromScene(), "scene mesh build should not start twice");

        settings.hitmanSet = oldHitmanSet;
        settings.outputSet = oldOutputSet;
        settings.blenderSet = oldBlenderSet;
        sceneMesh.extractingResources = oldExtracting;
        scene.sceneLoaded = oldSceneLoaded;
        sceneMesh.blenderSceneMeshBuildStarted = oldBuildStarted;
        sceneMesh.blenderSceneMeshGenerationDone = oldBuildDone;
        Rpkg::extractionDataInitComplete = oldExtractionReady;
    }

    void run(const std::string_view name, const auto& test) {
        test();
        std::cout << "[pass] " << name << std::endl;
    }
} // namespace

void runDomainTests() {
    run("Math vector and plane operations", testMathFunctions);
    run("Math ray/AABB intersections", testMathRayAabbIntersection);
    run("Pathfinding segment, neighbor, and intersection helpers", testPathfindingHelpers);
    run("JSON entity serialization and parsing", testJsonSerializationAndParsing);
    run("JSON scene-record round trips", testJsonSceneRecordRoundTrips);
    run("JSON material-record round trips", testJsonMaterialRecords);
    run("Pathfinding box aggregation and fallback", testPathfindingBoxAggregation);
    run("Update version comparison", testUpdateVersionComparison);
    run("Scene mesh lookup", testSceneMeshLookup);
    run("Grid bounds from AIRG", testGridBoundsFromAirg);
    run("Reasoning grid AIRG persistence", testReasoningGridAirgRoundTrip);
    run("Reasoning grid rejects invalid AIRG", testReasoningGridRejectsInvalidAirg);
    run("Waypoint connectivity-map generation", testWaypointConnectivityMapGeneration);
    run("Waypoint layer generation", testWaypointLayerGeneration);
    run("AIRG visibility type mapping", testVisionDataTypes);
    run("Packed waypoint visibility lookup", testPackedWaypointVisibilityLookup);
    run("Scene JSON persistence", testSceneSaveSerializesCollections);
    run("Render mesh CPU properties", testMeshCpuProperties);
    run("Render model mesh batching", testModelMeshBatching);
    run("Recast adapter geometry loading and coordinate conversion", testRecastAdapterGeometrySurface);
    run("Navp binary fixture loading", testNavpBinaryLoading);
    run("Marker handles arbitrary mesh names", testMarkerObjectNames);
    run("Module defaults and extraction gating", testModuleDefaultsAndRpkgGating);
    run("Module state and capability predicates", testModuleStateAndCapabilities);
}
