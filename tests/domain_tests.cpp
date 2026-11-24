#include "../include/NavKit/util/Threading.h"
#include "../include/NavKit/UiIds.h"
#include "../include/NavKit/adapter/RecastAdapter.h"
#include "../include/NavKit/model/Json.h"
#include "../include/NavKit/model/ReasoningGrid.h"
#include "../include/NavKit/model/VisionData.h"
#include "../include/NavKit/module/Airg.h"
#include "../include/NavKit/module/Crmd.h"
#include "../include/NavKit/module/Grid.h"
#include "../include/NavKit/module/Gfxf.h"
#include "../include/NavKit/module/Menu.h"
#include "../include/NavKit/module/NavKitSettings.h"
#include "../include/NavKit/module/Navp.h"
#include "../include/NavKit/module/Rpkg.h"
#include "../include/NavKit/module/Renderer.h"
#include "../include/NavKit/module/Scene.h"
#include "../include/NavKit/module/SceneMesh.h"
#include "../include/NavKit/render/Mesh.h"
#include "../include/NavKit/render/Model.h"
#include "../include/NavKit/util/UpdateChecker.h"
#include "../include/NavKit/util/BridgeJoiner.h"
#include "../include/NavKit/util/CrowdMapBuilder.h"
#include "../include/NavKit/util/GridGenerator.h"
#include "../include/NavKit/util/Math.h"
#include "../include/NavKit/util/Pathfinding.h"
#include "../include/RecastDemo/UnpartitionedAreaRegions.h"
#include "../include/RecastDemo/InputGeom.h"
#include <Recast.h>
#include "../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <cfloat>
#include <cstring>
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

    void testJsonCrowdRoundTrip() {
        const std::string editorJson =
            R"({"id":"crowd-id","name":"Crowd","tblu":"00AB",)"
            R"("transform":{"x":{"x":0,"y":1,"z":0},"y":{"x":-1,"y":0,"z":0},"z":{"x":0,"y":0,"z":1},)"
            R"("t":{"x":-6.5,"y":-27.5,"z":1}},"globalSize":{"x":17,"y":17.25,"z":2},"cellSize":0.45,)"
            R"("navMeshShrink":0.5,"generatorOffset":0.25,"smoothenExitFlows":false,"flowCandidates":"SPAWNED_ON_FLOW",)"
            R"("exclusionBoxes":[3,7],"inclusionBoxes":[],"panicOnlyBoxes":[1],"flowBoxes":[0],)"
            R"("flowBoxesTwoWay":[2],)"
            R"("groupBoxes":[{"id":"group-id","name":"Group","position":{"x":-6.5,"y":-27.5,"z":0},)"
            R"("rotation":{"x":0,"y":0,"z":0,"w":1},"scale":{"type":"SVector3","data":{"x":5,"y":6,"z":1}},)"
            R"("groupSize":{"type":"uint32","data":4}}],)"
            R"("exclusionSpheres":[5],"exclusionShapes":[0,2],"inclusionShapes":[],"panicOnlyShapes":[1],)"
            R"("teleports":[{"id":"tp-id","name":"Teleport","exitZone":{"id":"exit-id","name":"Exit",)"
            R"("position":{"x":1.5,"y":-32,"z":0},"rotation":{"x":0,"y":0,"z":0,"w":1},)"
            R"("scale":{"type":"SVector3","data":{"x":0.9,"y":8,"z":2}}},"enterZone":null,)"
            R"("exitActionAmbient":"STAY","exitActionScared":"LEAVE_CROWD"}],)"
            R"("splineFlows":[{"id":"spline-id","name":"Spline","transform":{"x":{"x":1,"y":0,"z":0},"y":{"x":0,"y":1,"z":0},)"
            R"("z":{"x":0,"y":0,"z":1},"t":{"x":0,"y":0,"z":0}},"flowWidth":1.5,)"
            R"("lengthTessellation":8.0,"widthTessellation":0.25,"extrusionHeight":4.5,"closed":true,"points":[{"x":1,"y":2,"z":3}]}]})";
        const auto readCrowd = [](Json::Crowd& value, simdjson::ondemand::object object) { value.readJson(object); };
        const Json::Crowd crowd = readJsonObject<Json::Crowd>(editorJson, readCrowd);
        std::ostringstream savedJson;
        crowd.writeJson(savedJson);
        const Json::Crowd parsed = readJsonObject<Json::Crowd>(savedJson.str(), readCrowd);

        for (const Json::Crowd* c : {&crowd, &parsed}) {
            check(c->groupBoxes.size() == 1 && c->groupBoxes[0].groupSize == 4,
                "crowd group box size should be read from its type/data wrapper");
            check(c->teleports.size() == 1 && c->teleports[0].exitZone && !c->teleports[0].enterZone,
                "a null teleport zone should parse as unset");
        }

        check(parsed.id == "crowd-id" && parsed.name == "Crowd" && parsed.tblu == "00AB",
            "crowd identity did not round trip");
        checkNear(parsed.transform.x.y, 1.0f);
        checkNear(parsed.transform.y.x, -1.0f);
        checkNear(parsed.transform.t.y, -27.5f);
        checkNear(parsed.globalSize.y, 17.25f);
        checkNear(parsed.navMeshShrink, 0.5f);
        check(!parsed.smoothenExitFlows, "crowd smoothing flag did not round trip");
        check(parsed.exclusionBoxIndices == std::vector<size_t>{3, 7} && parsed.inclusionBoxIndices.empty() &&
                parsed.panicOnlyBoxIndices == std::vector<size_t>{1} &&
                parsed.flowBoxIndices == std::vector<size_t>{0} &&
                parsed.flowBoxTwoWayIndices == std::vector<size_t>{2},
            "crowd box indices did not round trip");
        check(parsed.exclusionSphereIndices == std::vector<size_t>{5}, "crowd sphere indices did not round trip");
        check(parsed.exclusionShapeIndices == std::vector<size_t>{0, 2} && parsed.inclusionShapeIndices.empty() &&
                parsed.panicOnlyShapeIndices == std::vector<size_t>{1},
            "crowd shape indices did not round trip");
        checkNear(parsed.groupBoxes[0].position.x, -6.5f);
        checkNear(parsed.groupBoxes[0].scale.data.y, 6.0f);
        check(
            parsed.flowCandidates == ECrowdFlowCandidates::SPAWNED_ON_FLOW, "crowd flow candidates did not round trip");
        checkNear(parsed.splineFlows[0].extrusionHeight, 4.5f);
        checkNear(parsed.teleports[0].exitZone->position.y, -32.0f);
        checkNear(parsed.teleports[0].exitZone->scale.data.y, 8.0f);
        check(parsed.teleports[0].exitActionAmbient == TeleportActionType::STAY &&
                parsed.teleports[0].exitActionScared == TeleportActionType::LEAVE_CROWD,
            "crowd teleport actions did not round trip");
        checkNear(parsed.splineFlows[0].lengthTessellation, 8.0f);
        checkNear(parsed.splineFlows[0].widthTessellation, 0.25f);
        check(
            parsed.splineFlows.size() == 1 && parsed.splineFlows[0].closed && parsed.splineFlows[0].points.size() == 1,
            "crowd spline flows did not round trip");
        check(savedJson.str().find(R"("enterZone":null)") != std::string::npos,
            "an unset teleport zone should be written as null");
        check(savedJson.str().find(R"("exclusionBoxes":[3,7])") != std::string::npos,
            "crowd volume indices should be written as an index array");
    }

    void testJsonVolumeShapeRoundTrip() {
        const std::string editorJson =
            R"({"id":"shape-id","name":"Shape","tblu":"00EF",)"
            R"("transform":{"x":{"x":1,"y":0,"z":0},"y":{"x":0,"y":1,"z":0},"z":{"x":0,"y":0,"z":1},)"
            R"("t":{"x":2,"y":3,"z":4}},)"
            R"("points":[{"x":0,"y":0,"z":0},{"x":4,"y":0,"z":0},{"x":4,"y":4,"z":0}],)"
            R"("extrusionHeight":3,"flipSurfaceNormal":true,"closed":false})";
        const auto readShape = [](Json::VolumeShape& value, simdjson::ondemand::object object) {
            value.readJson(object);
        };
        const Json::VolumeShape shape = readJsonObject<Json::VolumeShape>(editorJson, readShape);
        std::ostringstream savedJson;
        shape.writeJson(savedJson);
        const Json::VolumeShape parsed = readJsonObject<Json::VolumeShape>(savedJson.str(), readShape);
        check(parsed.id == "shape-id" && parsed.name == "Shape" && parsed.tblu == "00EF",
            "volume shape identity did not round trip");
        checkNear(parsed.transform.t.z, 4.0f);
        check(parsed.points.size() == 3, "volume shape points did not round trip");
        checkNear(parsed.points[2].y, 4.0f);
        checkNear(parsed.extrusionHeight, 3.0f);
        check(
            parsed.flipSurfaceNormal && !parsed.closed, "volume shape normal flip and closed flag did not round trip");
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

    void testRoomsAndVolumesLoadWithoutMapSpaceConfigs() {
        Scene& scene = Scene::getInstance();
        const std::vector<Json::MapSpaceConfig> originalConfigs = scene.mapSpaceConfigs;
        const std::vector<Json::VolumeSphere> originalSpheres = scene.volumeSpheres;
        const auto loadRoomsAndVolumes = [&scene](const std::string& mapSpaceConfigsField) {
            const std::string json =
                R"({"gates":[],"rooms":[],"aiAreaWorld":[],"aiArea":[],"volumeBoxes":[],)"
                R"("volumeSpheres":[{"id":"sphere-a","name":"Head Collision",)"
                R"("position":{"x":1.0,"y":2.0,"z":3.0},"rotation":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},)"
                R"("radius":{"type":"float32","data":0.5}}])" +
                mapSpaceConfigsField + "}";
            const simdjson::padded_string input(json);
            simdjson::ondemand::parser parser;
            auto document = parser.iterate(input);
            const size_t errorsBefore = scene.getLoadErrors().size();
            bool errorCallbackCalled = false;
            scene.loadRoomsAndVolumes([&errorCallbackCalled] { errorCallbackCalled = true; }, document);
            check(!errorCallbackCalled, "loading rooms and volumes should not call the error callback");
            return scene.getLoadErrors().size() - errorsBefore;
        };

        check(loadRoomsAndVolumes(R"(,"mapSpaceConfigs":[{"id":"map-a","name":"MapSpaceConfig_00",)"
                                  R"("inclusions":{"type":"TArray<ZEntityReference>","data":["volume-a"]}}])") == 0,
            "a scene with map space configs should load without errors");
        check(scene.mapSpaceConfigs.size() == 1 && scene.mapSpaceConfigs.front().name == "MapSpaceConfig_00",
            "map space configs should be read");

        // Scenes without a UI map (such as Hantu Port) have no mapSpaceConfigs field.
        check(loadRoomsAndVolumes("") == 0, "a scene without map space configs should load without errors");
        check(scene.volumeSpheres.size() == 1 && scene.volumeSpheres.front().id == "sphere-a",
            "volume spheres should load when map space configs are missing");
        check(scene.mapSpaceConfigs.empty(), "map space configs from the previous scene should be cleared");

        scene.mapSpaceConfigs = originalConfigs;
        scene.volumeSpheres = originalSpheres;
    }

    void testPlannerExcludedBridgeBoxParsing() {
        Scene& scene = Scene::getInstance();
        const Json::PfBox originalInclude = scene.includeBox;
        const std::vector<Json::PfBox> originalExclusions = scene.exclusionBoxes;
        Json::Entity bridge;
        bridge.id = "bridge-a";
        bridge.name = Json::PfBoxes::PLANNER_EXCLUDED_BRIDGE_NAME_PREFIX + "_Gate07";
        bridge.scale = {"SVector3", {0.6f, 2.0f, 3.5f}};
        bridge.type.data = Json::PfBoxes::EXCLUDE_TYPE;

        Json::Entity includeTypedBridge = bridge;
        includeTypedBridge.id = "bridge-b";
        includeTypedBridge.type.data = Json::PfBoxes::INCLUDE_TYPE;

        const simdjson::padded_string emptyArray(std::string_view("[]"));
        simdjson::ondemand::parser parser;
        auto document = parser.iterate(emptyArray);
        Json::PfBoxes boxes{document.get_array().value()};
        boxes.entities = {bridge, includeTypedBridge};
        boxes.readPathfindingBBoxes();
        check(scene.plannerExcludedBridgeBoxes.size() == 2, "named bridge boxes should be read whatever their type");
        check(scene.exclusionBoxes.empty(), "a bridge box must not be read as an exclusion box");
        check(scene.includeBox.id == Json::PfBoxes::NO_INCLUDE_BOX_FOUND, "a bridge box must not be read as include");

        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("navkit-bridge-scene-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                ".json");
        scene.saveScene(path.string());
        {
            std::ifstream savedFile(path);
            const std::string serialized((std::istreambuf_iterator<char>(savedFile)), std::istreambuf_iterator<char>());
            check(serialized.find(R"("id":"bridge-a")") != std::string::npos &&
                    serialized.find(R"("id":"bridge-b")") != std::string::npos,
                "saved scene should keep the bridge boxes");
        }
        std::filesystem::remove(path);

        boxes.entities.clear();
        boxes.readPathfindingBBoxes();
        check(scene.plannerExcludedBridgeBoxes.empty(), "reading an empty box list should clear old bridge boxes");
        scene.includeBox = originalInclude;
        scene.exclusionBoxes = originalExclusions;
    }

    void testPlannerExcludedAreaUsage() {
        Navp& navp = Navp::getInstance();
        navp.loadNavMesh("tests/resources/triangles.navp", false, false, false);
        check(Navp::getTotalAreaCount(navp.navMesh) > 0, "test NAVP should have areas");
        for (int i = 0; i < Navp::getTotalAreaCount(navp.navMesh); ++i) {
            Navp::getAreaByIndex(navp.navMesh, i).m_area->m_usageFlags =
                NavPower::AreaUsageFlags::AREA_PLANNER_EXCLUDED;
        }
        navp.setStairsFlags();
        for (int i = 0; i < Navp::getTotalAreaCount(navp.navMesh); ++i) {
            check(Navp::getAreaByIndex(navp.navMesh, i).m_area->m_usageFlags ==
                    NavPower::AreaUsageFlags::AREA_PLANNER_EXCLUDED,
                "setStairsFlags must leave planner-excluded areas alone");
        }

        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("navkit-planner-excluded-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                ".navp.json");
        {
            std::ofstream navpJson(path);
            navp.navMesh->writeJson(navpJson);
        }
        {
            std::ifstream savedFile(path);
            const std::string serialized((std::istreambuf_iterator<char>(savedFile)), std::istreambuf_iterator<char>());
            check(serialized.find(R"("Type":"PlannerExcluded")") != std::string::npos,
                "planner-excluded areas should be written by name");
        }
        const NavPower::NavMesh reloaded(path.string().c_str());
        check(Navp::getTotalAreaCount(&reloaded) == Navp::getTotalAreaCount(navp.navMesh),
            "reloaded NAVP should keep its areas");
        for (int i = 0; i < Navp::getTotalAreaCount(&reloaded); ++i) {
            check(Navp::getAreaByIndex(&reloaded, i).m_area->m_usageFlags ==
                    NavPower::AreaUsageFlags::AREA_PLANNER_EXCLUDED,
                "planner-excluded usage should survive a .navp.json round trip");
        }
        std::filesystem::remove(path);
        navp.navpLoaded = false;
    }

    void testUnpartitionedAreaRegionAssignment() {
        rcContext context(false);
        rcHeightfield* heightfield = rcAllocHeightfield();
        const float bmin[3] = {0.0f, 0.0f, 0.0f};
        const float bmax[3] = {5.0f, 10.0f, 1.0f};
        check(rcCreateHeightfield(&context, *heightfield, 5, 1, bmin, bmax, 1.0f, 0.1f), "heightfield allocation");
        for (int x = 0; x < 5; ++x) {
            rcAddSpan(&context, *heightfield, x, 0, 0, 10, RC_WALKABLE_AREA, 1);
        }
        rcCompactHeightfield* compact = rcAllocCompactHeightfield();
        check(rcBuildCompactHeightfield(&context, 2, 1, *heightfield, *compact), "compact heightfield build");
        constexpr unsigned char bridgeArea = 6;
        for (const int x : {0, 1, 3, 4}) {
            compact->areas[compact->cells[x].index] = bridgeArea;
        }
        compact->spans[compact->cells[2].index].reg = 5;
        compact->spans[compact->cells[4].index].reg = 3;
        compact->maxRegions = 5;

        assignRegionsToUnpartitionedArea(*compact, bridgeArea);
        const auto region = [&](const int x) { return compact->spans[compact->cells[x].index].reg; };
        check(region(0) == 6 && region(1) == 6, "a connected patch should share one new region");
        check(region(3) == 7, "a separate patch should get its own new region");
        check(region(2) == 5 && region(4) == 3, "spans that kept a region should be untouched");
        check(compact->maxRegions == 7, "new regions should raise the region count");
        rcFreeCompactHeightfield(compact);
        rcFreeHeightField(heightfield);
    }

    NavPower::Area squareArea(const float minX, const float minY, const float size) {
        auto* area = new NavPower::Binary::Area();
        std::vector<NavPower::Binary::Edge*> edges;
        for (const auto& [x, y] : std::vector<std::pair<float, float>>{
                 {minX, minY}, {minX + size, minY}, {minX + size, minY + size}, {minX, minY + size}}) {
            auto* edge = new NavPower::Binary::Edge();
            edge->m_pAdjArea = nullptr;
            edge->m_pos = Vec3(x, y, 0.0f);
            edges.push_back(edge);
        }
        return {area, edges};
    }

    void checkBridgeJoinAcrossGap(const Json::Rotation& rotation, const Json::Vec3& scale, const std::string& label) {
        NavPower::NavMesh navMesh;
        navMesh.m_aSections.emplace_back();
        navMesh.m_aSections[0].m_aNavGraphs.emplace_back();
        std::vector<NavPower::Area>& areas = navMesh.m_aSections[0].m_aNavGraphs[0].m_areas;
        areas.push_back(squareArea(0.0f, 0.0f, 1.0f));
        areas.push_back(squareArea(0.0f, 1.5f, 1.0f));
        const Json::PfBox box("id", "bridge", {0.5f, 1.25f, 0.0f}, scale, rotation, {});

        const std::vector<BridgeJoinResult> results = joinPlannerExcludedBridges(navMesh, {box});
        check(results.size() == 1 && results[0].joined, label + ": the facing edges should be joined");
        check(areas.size() == 3, label + ": one bridge area should be added");
        const NavPower::Area& bridge = areas[2];
        check(bridge.m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_PLANNER_EXCLUDED,
            label + ": the bridge should be planner-excluded");
        const std::vector<std::pair<float, float>> expectedCorners{{0, 1}, {1, 1}, {1, 1.5f}, {0, 1.5f}};
        for (size_t i = 0; i < 4; ++i) {
            check(bridge.m_edges[i]->m_pos.X == expectedCorners[i].first &&
                    bridge.m_edges[i]->m_pos.Y == expectedCorners[i].second,
                label + ": bridge corners should reuse the facing edge endpoints in order");
        }
        check(bridge.m_edges[0]->m_pAdjArea == areas[0].m_area && bridge.m_edges[1]->m_pAdjArea == nullptr &&
                bridge.m_edges[2]->m_pAdjArea == areas[1].m_area && bridge.m_edges[3]->m_pAdjArea == nullptr,
            label + ": bridge ends should link to both floors and its sides to nothing");
        check(areas[0].m_edges[2]->m_pAdjArea == bridge.m_area && areas[1].m_edges[0]->m_pAdjArea == bridge.m_area,
            label + ": both floor edges should link back to the bridge");
        check(areas[0].m_edges[1]->m_pAdjArea == nullptr && areas[0].m_edges[3]->m_pAdjArea == nullptr,
            label + ": side edges inside the box should stay unlinked");
    }

    void testBridgeJoinAcrossGap() {
        checkBridgeJoinAcrossGap({0, 0, 0, 1}, {1.2f, 2.0f, 2.0f}, "unrotated box");
        const float halfTurn = std::sqrt(0.5f);
        checkBridgeJoinAcrossGap({0, 0, halfTurn, halfTurn}, {2.0f, 1.2f, 2.0f}, "box turned 90 degrees");
    }

    void testBridgeJoinFindsEdgesLongerThanTheBox() {
        NavPower::NavMesh navMesh;
        navMesh.m_aSections.emplace_back();
        navMesh.m_aSections[0].m_aNavGraphs.emplace_back();
        std::vector<NavPower::Area>& areas = navMesh.m_aSections[0].m_aNavGraphs[0].m_areas;
        areas.push_back(squareArea(0.0f, 0.0f, 4.0f));
        areas.push_back(squareArea(0.0f, 4.5f, 4.0f));
        const Json::PfBox box("id", "bridge", {0.5f, 4.25f, 0.0f}, {1.0f, 2.0f, 2.0f}, {0, 0, 0, 1}, {});

        const std::vector<BridgeJoinResult> results = joinPlannerExcludedBridges(navMesh, {box});
        check(results[0].joined, "facing edges whose midpoints are outside the box should still be joined");
        check(areas.size() == 3 && areas[0].m_edges[2]->m_pAdjArea == areas[2].m_area &&
                areas[1].m_edges[0]->m_pAdjArea == areas[2].m_area,
            "the edges crossing the box should be linked to the bridge");
    }

    void testBridgeJoinIgnoresEdgesTouchingTheBoxSide() {
        NavPower::NavMesh navMesh;
        navMesh.m_aSections.emplace_back();
        navMesh.m_aSections[0].m_aNavGraphs.emplace_back();
        std::vector<NavPower::Area>& areas = navMesh.m_aSections[0].m_aNavGraphs[0].m_areas;
        areas.push_back(squareArea(0.0f, 0.0f, 1.0f));
        areas.push_back(squareArea(0.0f, 2.0f, 1.0f));
        areas.push_back(squareArea(1.0f, 0.5f, 1.0f));
        areas.push_back(squareArea(1.0f, 1.8f, 1.0f));
        const Json::PfBox box("id", "bridge", {0.5f, 1.5f, 0.0f}, {1.0f, 3.0f, 2.0f}, {0, 0, 0, 1}, {});

        joinPlannerExcludedBridges(navMesh, {box});
        check(areas.size() == 5 && areas[0].m_edges[2]->m_pAdjArea == areas[4].m_area &&
                areas[1].m_edges[0]->m_pAdjArea == areas[4].m_area,
            "the pads inside the box should be joined, not the closer edges that only touch its side");
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

    void testCrmdFixtureLoading() {
        Crmd crmd;
        crmd.loadCrowdMapData("tests/resources/yacht.crmd");

        check(crmd.crmdLoaded, "CRMD fixture should load successfully");
        check(crmd.crowdMapData != nullptr, "CRMD fixture should populate crowd-map data");
        check(crmd.crowdMapData->m_nGridSizeX == 163 && crmd.crowdMapData->m_nGridSizeY == 249,
            "CRMD fixture should load grid dimensions");
        check(crmd.crowdMapData->m_nCellSizeX == 162 && crmd.crowdMapData->m_nCellSizeY == 248,
            "CRMD fixture should load cell dimensions separately from map dimensions");
        check(crmd.crowdMapData->m_GridMap.size() == 163 * 249, "CRMD fixture should load grid map cells");
        check(crmd.crowdMapData->m_CellFlags.size() == 314, "CRMD fixture should load cell flags");
        check(crmd.crowdMapData->m_CellGroups.size() == 314, "CRMD fixture should load cell groups");
        check(crmd.crowdMapData->m_aFlowChannels.size() == 6, "CRMD fixture should load flow channels");

        delete crmd.crowdMapData;
        crmd.crowdMapData = nullptr;
    }

    void testCrmdSaveRoundTrip() {
        const std::string fixturePath = "tests/resources/yacht.crmd";
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path savedPath =
            std::filesystem::temp_directory_path() / ("navkit-crmd-roundtrip-" + std::to_string(stamp) + ".crmd");
        Crmd crmd;
        crmd.loadCrowdMapData(fixturePath);
        check(crmd.crmdLoaded, "CRMD fixture should load before saving");
        crmd.saveCrmd(savedPath.string());

        const auto readBytes = [](const std::filesystem::path& path) {
            std::ifstream stream(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
        };
        check(readBytes(fixturePath) == readBytes(savedPath), "saved CRMD should be byte-identical to the fixture");

        std::filesystem::remove(savedPath);
        delete crmd.crowdMapData;
        crmd.crowdMapData = nullptr;
    }

    std::vector<unsigned char> readGfxfTestBytes(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream), {});
    }

    std::filesystem::path gfxfTestTempPath(const std::string& name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("navkit-gfxf-" + std::to_string(stamp) + "-" + name);
    }

    void testGfxfLoadExtractsGfx() {
        const std::vector<unsigned char> gfx =
            Gfxf::loadGfxFromFile("tests/resources/polarbear005 00B7DDECA456B2EE official.gfxf");
        const std::vector<unsigned char> extracted =
            Gfxf::extractGfxFromGfxf(readGfxfTestBytes("tests/resources/polarbear005 00B7DDECA456B2EE official.gfxf"));

        check(gfx == extracted, "loading a GFXF should yield the GFX extracted from its container");
        check(gfx.size() == 29682, "extracted GFX should have the vanilla payload size");
        check(std::equal(gfx.begin(), gfx.begin() + 3, "GFX"), "extracted GFX should start with the GFX signature");
        const std::uint32_t declaredLength = static_cast<std::uint32_t>(gfx[4]) |
            static_cast<std::uint32_t>(gfx[5]) << 8 | static_cast<std::uint32_t>(gfx[6]) << 16 |
            static_cast<std::uint32_t>(gfx[7]) << 24;
        check(declaredLength == gfx.size(), "extracted GFX header length should match its payload size");
    }

    void testGfxLoadReadsRawGfx() {
        const std::vector<unsigned char> gfx = Gfxf::loadGfxFromFile("src/resource/map_template.gfx");
        check(gfx == readGfxfTestBytes("src/resource/map_template.gfx"), "loading a raw GFX should return its bytes");
    }

    void testGfxfRejectsInvalidContainers() {
        const auto throws = [](const std::vector<unsigned char>& bytes) {
            try {
                Gfxf::extractGfxFromGfxf(bytes);
            } catch (const std::runtime_error&) {
                return true;
            }
            return false;
        };
        check(throws(readGfxfTestBytes("src/resource/map_template.gfx")), "a raw GFX is not a GFXF container");
        std::vector<unsigned char> truncated =
            readGfxfTestBytes("tests/resources/polarbear005 00B7DDECA456B2EE official.gfxf");
        truncated.resize(truncated.size() / 2);
        check(throws(truncated), "a truncated GFXF should be rejected");
    }

    constexpr size_t gfxfDataSectionOffset = 16;

    std::uint64_t readGfxfPayloadOffset(const std::vector<unsigned char>& gfxf) {
        std::uint64_t value = 0;
        for (size_t i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(gfxf[gfxfDataSectionOffset + i]) << (8 * i);
        }
        return value;
    }

    std::uint32_t readGfxfDataSize(const std::vector<unsigned char>& gfxf) {
        return static_cast<std::uint32_t>(gfxf[8]) << 24 | static_cast<std::uint32_t>(gfxf[9]) << 16 |
            static_cast<std::uint32_t>(gfxf[10]) << 8 | static_cast<std::uint32_t>(gfxf[11]);
    }

    // The game's GFXF files store a 32-bit element count directly before the GFX payload. The glacier_bin1
    // serializer leaves array and string counts out, which the game accepts. Returns the vanilla GFXF as that
    // serializer writes it: without the count, with the payload offset and data size 4 bytes smaller.
    std::vector<unsigned char> withoutBin1ArrayCount(std::vector<unsigned char> gfxf) {
        const std::uint64_t payloadOffset = readGfxfPayloadOffset(gfxf);
        const std::uint32_t dataSize = readGfxfDataSize(gfxf);
        const size_t countPosition = gfxfDataSectionOffset + static_cast<size_t>(payloadOffset) - 4;
        gfxf.erase(gfxf.begin() + static_cast<std::ptrdiff_t>(countPosition),
            gfxf.begin() + static_cast<std::ptrdiff_t>(countPosition + 4));
        const std::uint64_t newOffset = payloadOffset - 4;
        for (size_t i = 0; i < 8; ++i) {
            gfxf[gfxfDataSectionOffset + i] = static_cast<unsigned char>(newOffset >> (8 * i));
        }
        const std::uint32_t newSize = dataSize - 4;
        for (size_t i = 0; i < 4; ++i) {
            gfxf[8 + i] = static_cast<unsigned char>(newSize >> (8 * (3 - i)));
        }
        return gfxf;
    }

    void testGfxfSaveMatchesVanillaGfxf() {
        const std::filesystem::path savedPath = gfxfTestTempPath("vanilla.gfxf");
        const std::vector<unsigned char> vanilla =
            readGfxfTestBytes("tests/resources/polarbear005 00B7DDECA456B2EE official.gfxf");
        const std::vector<unsigned char> gfx = Gfxf::extractGfxFromGfxf(vanilla);

        // The vanilla file carries the count the serializer leaves out: the payload size, right before the payload.
        const std::uint64_t vanillaOffset = readGfxfPayloadOffset(vanilla);
        check(vanillaOffset == 0x44, "the vanilla GFXF payload should follow its 32-bit element count");
        const size_t countPosition = gfxfDataSectionOffset + static_cast<size_t>(vanillaOffset) - 4;
        const std::uint32_t vanillaCount = static_cast<std::uint32_t>(vanilla[countPosition]) |
            static_cast<std::uint32_t>(vanilla[countPosition + 1]) << 8 |
            static_cast<std::uint32_t>(vanilla[countPosition + 2]) << 16 |
            static_cast<std::uint32_t>(vanilla[countPosition + 3]) << 24;
        check(vanillaCount == gfx.size(), "the vanilla GFXF element count should be the GFX payload size");

        check(Gfxf::packGfxToGfxfFile(gfx, savedPath.string()), "packing the extracted GFX should succeed");
        const std::vector<unsigned char> repacked = readGfxfTestBytes(savedPath);
        std::filesystem::remove(savedPath);
        check(readGfxfPayloadOffset(repacked) == 0x40, "the serializer should write the payload without a count");
        check(repacked.size() + 4 == vanilla.size(), "the repacked GFXF should only lack the 4-byte count");
        check(repacked == withoutBin1ArrayCount(vanilla),
            "repacking a vanilla GFXF's GFX should match the vanilla GFXF apart from the missing count");
        check(Gfxf::extractGfxFromGfxf(repacked) == gfx, "the repacked GFXF should read back to the vanilla GFX");
    }

    void testGfxfReadsBothCountLayouts() {
        const std::vector<unsigned char> vanilla =
            readGfxfTestBytes("tests/resources/polarbear005 00B7DDECA456B2EE official.gfxf");
        const std::vector<unsigned char> withoutCount = withoutBin1ArrayCount(vanilla);
        const std::vector<unsigned char> gfx = Gfxf::extractGfxFromGfxf(vanilla);
        check(Gfxf::extractGfxFromGfxf(withoutCount) == gfx,
            "a GFXF without the array count should read to the same GFX as one with it");

        std::vector<unsigned char> wrongSize = vanilla;
        const size_t sizeField = gfxfDataSectionOffset + 8;
        wrongSize[sizeField] = static_cast<unsigned char>(wrongSize[sizeField] - 1);
        bool rejected = false;
        try {
            Gfxf::extractGfxFromGfxf(wrongSize);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "a GFXF whose payload size disagrees with the GFX header should be rejected");
    }

    void testGfxPackRoundTrip() {
        const std::filesystem::path savedPath = gfxfTestTempPath("roundtrip.gfxf");
        const std::vector<unsigned char> gfx = Gfxf::loadGfxFromFile("src/resource/map_template.gfx");

        check(Gfxf::packGfxToGfxfFile(gfx, savedPath.string()), "packing the GFX should succeed");
        const std::vector<unsigned char> reloaded = Gfxf::loadGfxFromFile(savedPath.string());
        std::filesystem::remove(savedPath);
        check(reloaded == gfx, "a GFX packed into a GFXF should extract back unchanged");
    }

    void testGfxfSaveFromStoredGfx() {
        const std::filesystem::path savedPath = gfxfTestTempPath("saved.gfxf");
        Gfxf& gfxf = Gfxf::getInstance();
        gfxf.setGfxData(Gfxf::loadGfxFromFile("src/resource/map_template.gfx"));

        check(gfxf.canSave(), "Save Gfxf should be enabled once GFX data is stored");
        gfxf.exportGfxfToGfxfFile(savedPath.string());
        const std::vector<unsigned char> reloaded = Gfxf::loadGfxFromFile(savedPath.string());
        std::filesystem::remove(savedPath);
        gfxf.setGfxData({});
        check(!gfxf.saving, "Save Gfxf should clear the saving flag when it finishes");
        check(reloaded == readGfxfTestBytes("src/resource/map_template.gfx"),
            "Save Gfxf should write the stored GFX into the GFXF");
    }

    void testGfxSaveFromStoredGfx() {
        const std::filesystem::path savedPath = gfxfTestTempPath("saved.gfx");
        Gfxf& gfxf = Gfxf::getInstance();
        const std::vector<unsigned char> gfx = Gfxf::loadGfxFromFile("src/resource/map_template.gfx");
        gfxf.setGfxData(gfx);

        check(gfxf.canSave(), "Save Gfx should be enabled once GFX data is stored");
        gfxf.exportGfxToGfxFile(savedPath.string());
        const std::vector<unsigned char> saved = readGfxfTestBytes(savedPath);
        const std::vector<unsigned char> reloaded = Gfxf::loadGfxFromFile(savedPath.string());
        std::filesystem::remove(savedPath);
        check(!gfxf.saving, "Save Gfx should clear the saving flag when it finishes");
        check(saved == gfx, "Save Gfx should write the raw GFX, without a Gfxf container");
        check(reloaded == gfx, "a saved GFX should load back unchanged");

        gfxf.clear();
        check(!gfxf.canSave(), "clearing the map should leave nothing to save");
    }

    Json::Entity crowdTestBox(const std::string& name, const Json::Vec3& position, const Json::Vec3& size) {
        Json::Entity box;
        box.name = name;
        box.position = position;
        box.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        box.scale = {"SVector3", size};
        return box;
    }

    Json::Crowd syntheticCrowd() {
        Json::Crowd crowd;
        crowd.name = "Synthetic";
        crowd.globalSize = {4.0f, 4.0f, 2.0f};
        crowd.cellSize = 0.5f;
        crowd.navMeshShrink = 0.0f;
        crowd.exclusionBoxIndices.push_back(1);
        Json::CrowdTeleport exit;
        exit.name = "West";
        exit.exitZone = crowdTestBox("WestZone", {-1.75f, 0.0f, 0.0f}, {0.6f, 4.2f, 2.0f});
        exit.exitActionScared = TeleportActionType::LEAVE_CROWD;
        crowd.teleports.push_back(exit);
        Json::CrowdGroupBox group;
        static_cast<Json::Entity&>(group) = crowdTestBox("East", {1.5f, -1.5f, 0.0f}, {1.1f, 1.1f, 2.0f});
        crowd.groupBoxes.push_back(group);
        return crowd;
    }

    CrowdMapBuilder::SceneVolumes syntheticSceneVolumes() {
        static const std::vector<Json::Entity> boxes = {
            crowdTestBox("Unreferenced", {-0.5f, -1.5f, 0.0f}, {0.9f, 0.9f, 2.0f}),
            crowdTestBox("Pillar", {0.5f, 0.5f, 0.0f}, {0.9f, 0.9f, 2.0f}),
            crowdTestBox("FlowEast", {-1.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 2.0f}),
            crowdTestBox("EastCorner", {1.75f, -1.75f, 0.0f}, {0.6f, 0.6f, 2.0f})};
        static const std::vector<Json::VolumeSphere> spheres;
        static const std::vector<Json::VolumeShape> shapes;
        return {boxes, spheres, shapes};
    }

    std::vector<glm::dvec3> syntheticFloor(const double slope = 0.0) {
        const auto corner = [slope](const double x, const double y) { return glm::dvec3(x, y, slope * x); };
        return {corner(-3, -3), corner(3, -3), corner(3, 3), corner(-3, -3), corner(3, 3), corner(-3, 3)};
    }

    size_t syntheticCell(const int x, const int y) {
        return static_cast<size_t>(y) * 8 + x;
    }

    float crmdPointHeight(const CrowdMapBuilder::CrowdMap& map, const size_t index) {
        return static_cast<float>(map.gridMap[index].heightOffset) * map.heightScale + map.heightBias;
    }

    void testCrmdBuilderFromSyntheticFloor() {
        const Json::Crowd crowd = syntheticCrowd();
        CrowdMapBuilder::Report report;
        const CrowdMapBuilder::CrowdMap map =
            CrowdMapBuilder::build(crowd, syntheticSceneVolumes(), syntheticFloor(), {}, report);

        check(map.cellsX == 8 && map.cellsY == 8, "crowd map should have size / cell size cells");
        check(map.gridMap.size() == 81, "crowd map should have one grid point per cell corner");
        check(report.gridPointsOnGeometry == 80 && report.gridPointsOffGrid == 1,
            "only the grid point whose floor hit is inside the pillar exclusion should be off the grid");
        check(report.walkableCells == 60, "the four cells around the excluded grid point should not be walkable");
        check(report.exitCells == std::vector<size_t>{8}, "the exit zone should cover the west column");
        check(report.groupCells == std::vector<size_t>{4}, "the group box should cover four cells");
        check(map.cellFlags.size() == CrowdMapBuilder::cellsPerBlock && map.cellGroups.size() == map.cellFlags.size(),
            "cell arrays should be padded to whole blocks");
        const auto cell = syntheticCell;
        check(map.cellFlags[cell(4, 4)] == 0, "excluded cells should not be walkable");
        check(map.cellFlags[cell(0, 3)] == (CrowdMapBuilder::cellFlagWalkable | CrowdMapBuilder::cellFlagTeleportOut),
            "exit cells should be walkable teleport-out cells");
        check(map.cellGroups[cell(6, 0)] == 0 && map.cellGroups[cell(0, 0)] == CrowdMapBuilder::noGroup,
            "group cells should carry the group index");

        check(map.flowChannels.size() == 3 && map.groupFlowStartChannel == 2,
            "flow channels should be ambient flow, then exits, then groups");
        check(map.flowChannels[0].costs.empty() &&
                std::ranges::all_of(map.flowChannels[0].directions,
                    [](const uint8_t direction) { return direction == CrowdMapBuilder::noDirection; }),
            "channel 0 should have no directions without flow boxes");
        check(map.flowChannels[1].costs[cell(0, 1)] == 0, "exit cells should cost nothing in their own channel");
        check(map.flowChannels[1].directions[cell(2, 1)] == 60, "a cell east of the west exit should flow west");
        check(map.flowChannels[1].costs[cell(4, 4)] == CrowdMapBuilder::unreachableCost,
            "excluded cells should be unreachable");
        check(map.flowChannels[2].costs[cell(7, 1)] == 0, "group cells should cost nothing in their own channel");
        check(std::ranges::max(map.flowChannels[1].costs, {},
                  [](const uint16_t cost) { return cost == CrowdMapBuilder::unreachableCost ? 0 : cost; }) == 65534 ||
                std::ranges::max(map.flowChannels[2].costs, {},
                    [](const uint16_t cost) { return cost == CrowdMapBuilder::unreachableCost ? 0 : cost; }) == 65534,
            "the most expensive reachable cell across all channels should cost 65534");
        for (size_t i = 0; i < map.gridMap.size(); ++i) {
            checkNear(crmdPointHeight(map, i), 0.0f, 1e-3f);
        }

        CrowdMapBuilder::Report withoutGlbReport;
        bool threwWithoutGlb = false;
        try {
            CrowdMapBuilder::build(crowd, syntheticSceneVolumes(), {}, {}, withoutGlbReport);
        } catch (const std::runtime_error&) {
            threwWithoutGlb = true;
        }
        check(threwWithoutGlb, "building without ground geometry under the crowd should fail");

        Json::Crowd danglingCrowd = crowd;
        danglingCrowd.exclusionShapeIndices.push_back(0);
        CrowdMapBuilder::Report danglingReport;
        bool threwOnDanglingIndex = false;
        try {
            CrowdMapBuilder::build(danglingCrowd, syntheticSceneVolumes(), syntheticFloor(), {}, danglingReport);
        } catch (const std::runtime_error&) {
            threwOnDanglingIndex = true;
        }
        check(threwOnDanglingIndex, "a crowd volume index past the end of its scene list should fail the build");

        const std::vector<float> recastVerts = {-3.0f, -0.1f, 3.0f, 3.0f, -0.1f, 3.0f, 3.0f, -0.1f, -3.0f, -3.0f, -0.1f,
            -3.0f, 100.0f, 0.0f, 0.0f, 101.0f, 0.0f, 0.0f, 100.0f, 0.0f, -1.0f};
        const std::vector<int> tris = {0, 1, 2, 0, 2, 3, 4, 5, 6};
        const std::vector<glm::dvec3> glbTriangles =
            CrowdMapBuilder::groundTrianglesNearCrowd(crowd, recastVerts.data(), tris.data(), 3);
        check(glbTriangles.size() == 6, "only GLB triangles near the crowd box should be kept");
        checkNear(static_cast<float>(glbTriangles[2].y), 3.0f);
        checkNear(static_cast<float>(glbTriangles[2].z), -0.1f);
        CrowdMapBuilder::Report snappedReport;
        const CrowdMapBuilder::CrowdMap snapped =
            CrowdMapBuilder::build(crowd, syntheticSceneVolumes(), syntheticFloor(), glbTriangles, snappedReport);
        for (size_t i = 0; i < snapped.gridMap.size(); ++i) {
            if (snapped.gridMap[i].onNavGrid != CrowdMapBuilder::offNavGrid) {
                checkNear(crmdPointHeight(snapped, i), -0.1f, 1e-3f);
            }
        }

        Crmd crmd;
        crmd.setCrowdMap(map, crowd.name);
        check(crmd.crmdLoaded && crmd.crowdMapData->m_nCellSizeX == 8 && crmd.crowdMapData->m_CellFlags.size() == 1 &&
                crmd.crowdMapData->m_aFlowChannels.size() == 3 &&
                crmd.crowdMapData->m_aFlowChannels[1].m_aFlowCost.size() == 64,
            "a built crowd map should become the loaded CRMD");
        delete crmd.crowdMapData;
        crmd.crowdMapData = nullptr;
    }

    void testCrmdBuilderReservesChannelsForSkippedExits() {
        Json::Crowd crowd = syntheticCrowd();
        crowd.teleports[0].exitZone->position = {50.0f, 50.0f, 0.0f};
        CrowdMapBuilder::Report report;
        const CrowdMapBuilder::CrowdMap map =
            CrowdMapBuilder::build(crowd, syntheticSceneVolumes(), syntheticFloor(), {}, report);
        check(report.skippedExits == std::vector<std::string>{"West"} && report.exitCells.empty(),
            "an exit that covers no walkable cells should be skipped and reported");
        check(map.flowChannels.size() == 3 && map.groupFlowStartChannel == 1,
            "a skipped exit should still reserve a channel, left after the group channels");
        check(std::ranges::all_of(map.flowChannels[2].directions, [](const uint8_t d) { return d == 0; }) &&
                std::ranges::all_of(map.flowChannels[2].costs, [](const uint16_t c) { return c == 0; }),
            "the reserved channel of a skipped exit should be all zeros");
        check(std::ranges::none_of(map.cellFlags,
                  [](const uint8_t flags) { return (flags & CrowdMapBuilder::cellFlagTeleportOut) != 0; }),
            "a skipped exit should not mark teleport-out cells");
    }

    void testCrmdBuilderAmbientFlowAndTeleportIn() {
        Json::Crowd crowd = syntheticCrowd();
        crowd.flowBoxTwoWayIndices.push_back(2);
        crowd.teleports[0].enterZone = crowdTestBox("EastEnter", {1.75f, 0.0f, 0.0f}, {0.6f, 4.2f, 2.0f});
        CrowdMapBuilder::Report report;
        const CrowdMapBuilder::CrowdMap map =
            CrowdMapBuilder::build(crowd, syntheticSceneVolumes(), syntheticFloor(), {}, report);
        check(map.flowChannels[0].directions[syntheticCell(1, 6)] == CrowdMapBuilder::directionTwoWay,
            "a cell in an east-facing two-way flow box should flow east with the two-way bit");
        check(map.flowChannels[0].directions[syntheticCell(5, 1)] == CrowdMapBuilder::noDirection,
            "a cell outside every flow box should have no ambient direction");
        check((map.cellFlags[syntheticCell(7, 5)] & CrowdMapBuilder::cellFlagTeleportIn) != 0 &&
                (map.cellFlags[syntheticCell(6, 5)] & CrowdMapBuilder::cellFlagTeleportIn) == 0,
            "cells inside a teleport enter zone should be flagged");
    }

    void testCrmdBuilderSplineLanesSpanFlowWidth() {
        Json::Crowd crowd = syntheticCrowd();
        crowd.flowBoxTwoWayIndices.push_back(2);
        Json::CrowdSplineFlow spline;
        spline.transform.x = {1.0f, 0.0f, 0.0f};
        spline.transform.y = {0.0f, 1.0f, 0.0f};
        spline.transform.z = {0.0f, 0.0f, 1.0f};
        spline.flowWidth = 1.5f;
        spline.widthTessellation = 0.5f;
        spline.lengthTessellation = 0.25f;
        spline.points = {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
        crowd.splineFlows.push_back(spline);
        const auto northDirection = static_cast<uint8_t>(30);

        CrowdMapBuilder::Report report;
        const CrowdMapBuilder::CrowdMap map =
            CrowdMapBuilder::build(crowd, syntheticSceneVolumes(), syntheticFloor(), {}, report);
        const auto& directions = map.flowChannels[0].directions;
        check(directions[syntheticCell(4, 3)] == northDirection && directions[syntheticCell(5, 3)] == northDirection &&
                directions[syntheticCell(6, 3)] == northDirection,
            "spline lanes should steer every cell across the flow width");
        check(directions[syntheticCell(7, 3)] == CrowdMapBuilder::noDirection,
            "spline lanes start at one edge, so the last lane stops one spacing short of the other");
    }

    void testCrmdBuilderGroupSeedPointsAndExclusions() {
        Json::Crowd seeded = syntheticCrowd();
        seeded.groupBoxes[0].seedPointIndices.push_back(3);
        CrowdMapBuilder::Report seededReport;
        const CrowdMapBuilder::CrowdMap seededMap =
            CrowdMapBuilder::build(seeded, syntheticSceneVolumes(), syntheticFloor(), {}, seededReport);
        check(seededReport.groupCells == std::vector<size_t>{1} && seededMap.cellGroups[syntheticCell(6, 0)] == 0,
            "seed points should limit a group's flow sources but not its membership");

        Json::Crowd excluded = syntheticCrowd();
        excluded.groupBoxes[0].exclusionBoxIndices.push_back(3);
        CrowdMapBuilder::Report excludedReport;
        const CrowdMapBuilder::CrowdMap excludedMap =
            CrowdMapBuilder::build(excluded, syntheticSceneVolumes(), syntheticFloor(), {}, excludedReport);
        check(excludedReport.groupCells == std::vector<size_t>{3} &&
                excludedMap.cellGroups[syntheticCell(7, 0)] == CrowdMapBuilder::noGroup,
            "a group's exclusion boxes should remove cells from the group");
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
        const auto savedThreads = Threading::getMaxThreads();
        Threading::setMaxThreads(1);
        GridGenerator::addVisibilityData(&vision);
        const auto serialVision = vision.m_pVisibilityData;
        Threading::setMaxThreads(4);
        GridGenerator::addVisibilityData(&vision);
        check(vision.m_pVisibilityData == serialVision, "parallel collision visibility differs from serial");
        Threading::setMaxThreads(savedThreads);
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
        SceneMesh::getInstance().buildGlbFromNavp(false, true);
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

    void testClearAndNewProjectOptions() {
        Scene& scene = Scene::getInstance();
        SceneMesh& sceneMesh = SceneMesh::getInstance();
        Navp& navp = Navp::getInstance();
        Airg& airg = Airg::getInstance();

        // 1. Test Scene::clear and Menu New Scene
        scene.sceneLoaded = true;
        scene.lastLoadSceneFile = "test.nav.json";
        scene.version = 3;
        scene.gates.push_back({});
        scene.rooms.push_back({});
        scene.exclusionBoxes.push_back({});
        scene.pfSeedPoints.push_back({});
        navp.selectedExclusionBoxIndex = 0;
        navp.selectedPfSeedPointIndex = 0;

        Menu::handleMenuClicked(IDM_FILE_NEW_SCENE);
        check(!scene.sceneLoaded, "New Scene should set sceneLoaded to false");
        check(scene.lastLoadSceneFile.empty(), "New Scene should clear lastLoadSceneFile");
        check(scene.gates.empty(), "New Scene should clear gates");
        check(scene.rooms.empty(), "New Scene should clear rooms");
        check(scene.exclusionBoxes.empty(), "New Scene should clear exclusionBoxes");
        check(scene.pfSeedPoints.empty(), "New Scene should clear pfSeedPoints");
        check(navp.selectedExclusionBoxIndex == -1, "New Scene should reset selectedExclusionBoxIndex");
        check(navp.selectedPfSeedPointIndex == -1, "New Scene should reset selectedPfSeedPointIndex");

        // 2. Test SceneMesh::clear and Menu New Scene Mesh
        sceneMesh.glbLoaded = true;
        sceneMesh.lastGlbFileName = "test.glb";
        sceneMesh.blendFileBuilt = true;
        RecastAdapter::getInstance().selectedObject = "SomeObject";
        RecastAdapter::getInstance().markerPositionSet = true;

        Menu::handleMenuClicked(IDM_FILE_NEW_SCENE_MESH);
        check(!sceneMesh.glbLoaded, "New Scene Mesh should set glbLoaded to false");
        check(!sceneMesh.blendFileBuilt, "New Scene Mesh should set blendFileBuilt to false");
        check(sceneMesh.model.meshes.empty(), "New Scene Mesh should clear model meshes");
        check(RecastAdapter::getInstance().selectedObject.empty(),
            "New Scene Mesh should clear selectedObject in RecastAdapter");
        check(!RecastAdapter::getInstance().markerPositionSet, "New Scene Mesh should clear markerPositionSet");

        // 3. Test Navp::clear and Menu New Navp
        navp.navpLoaded = true;
        navp.selectedNavpAreaIndex = 5;
        navp.loadedNavpText = "test.navp";
        navp.kdTreeNodes.push_back({});

        Menu::handleMenuClicked(IDM_FILE_NEW_NAVP);
        check(!navp.navpLoaded, "New Navp should set navpLoaded to false");
        check(navp.selectedNavpAreaIndex == -1, "New Navp should reset selectedNavpAreaIndex");
        check(navp.loadedNavpText.empty(), "New Navp should clear loadedNavpText");
        check(navp.kdTreeNodes.empty(), "New Navp should clear kdTreeNodes");

        // 4. Test Airg::clear and Menu New Airg
        airg.airgLoaded = true;
        airg.selectedWaypointIndex = 3;
        airg.lastLoadAirgFile = "test.airg";
        airg.reasoningGrid->m_WaypointList.push_back({});

        Menu::handleMenuClicked(IDM_FILE_NEW_AIRG);
        check(!airg.airgLoaded, "New Airg should set airgLoaded to false");
        check(airg.selectedWaypointIndex == -1, "New Airg should reset selectedWaypointIndex");
        check(airg.reasoningGrid->m_WaypointList.empty(), "New Airg should reset reasoningGrid");

        // 5. Test New Project clears everything
        scene.sceneLoaded = true;
        sceneMesh.glbLoaded = true;
        navp.navpLoaded = true;
        airg.airgLoaded = true;
        Gfxf::getInstance().gfxfLoaded = true;
        Gfxf::getInstance().loadedFile = "test.gfx";
        Gfxf::getInstance().layerNames = {"Level 00", "Level 01"};
        Gfxf::getInstance().selectedLayerIndex = 1;
        scene.gates.push_back({});
        navp.selectedNavpAreaIndex = 2;
        airg.selectedWaypointIndex = 1;

        Menu::handleMenuClicked(IDM_FILE_NEW_PROJECT);
        check(!scene.sceneLoaded, "New Project should clear Scene");
        check(!sceneMesh.glbLoaded, "New Project should clear SceneMesh");
        check(!navp.navpLoaded, "New Project should clear Navp");
        check(!airg.airgLoaded, "New Project should clear Airg");
        check(scene.gates.empty(), "New Project should clear scene gates");
        check(navp.selectedNavpAreaIndex == -1, "New Project should reset navp selection");
        check(airg.selectedWaypointIndex == -1, "New Project should reset airg selection");
        check(!Gfxf::getInstance().gfxfLoaded, "New Project should clear GFXF");
        check(Gfxf::getInstance().loadedFile.empty(), "New Project should clear the loaded GFXF path");
        check(Gfxf::getInstance().layerNames.empty(), "New Project should clear the loaded map layers");
        check(Gfxf::getInstance().selectedLayerIndex == 0, "New Project should reset the selected map layer");

        Gfxf::getInstance().gfxfLoaded = true;
        Gfxf::getInstance().loadedFile = "test.gfx";
        Gfxf::getInstance().layerNames = {"Level 00", "Level 01"};
        Gfxf::getInstance().selectedLayerIndex = 1;
        Menu::handleMenuClicked(IDM_FILE_NEW_GFXF);
        check(!Gfxf::getInstance().gfxfLoaded, "New GFXF should clear the loaded GFXF");
        check(Gfxf::getInstance().loadedFile.empty(), "New GFXF should clear the loaded GFXF path");
        check(Gfxf::getInstance().layerNames.empty(), "New GFXF should clear the loaded map layers");
        check(Gfxf::getInstance().selectedLayerIndex == 0, "New GFXF should reset the selected map layer");

        Gfxf::getInstance().showGfxf = true;
        Menu::handleMenuClicked(IDM_VIEW_GFXF_SHOW_GFXF);
        check(!Gfxf::getInstance().showGfxf, "Show GFXF should toggle viewport visibility");
        Gfxf::getInstance().showGfxf = true;
    }

    // Navmesh fixture for the waypoint connection tests: a walkable plane with a wall sliver between
    // x=4.6 and x=4.7 running from z=1 to z=9, with an open corridor along the bottom (z < 1) that connects
    // both sides of the wall. The two regions are joined exclusively through that corridor, so the path
    // between points placed above the bottom strip walks all the way down, across, and back up.
    struct WaypointNavFixture {
        dtNavMesh* navMesh = nullptr;
        dtNavMeshQuery* navQuery = nullptr;
        dtPolyRef polyBase = 0;

        dtPolyRef poly(const int index) const {
            return polyBase | static_cast<dtPolyRef>(index);
        }

        ~WaypointNavFixture() {
            if (navQuery != nullptr)
                dtFreeNavMeshQuery(navQuery);
            if (navMesh != nullptr)
                dtFreeNavMesh(navMesh);
        }
    };

    bool buildWaypointNavmesh(WaypointNavFixture& fixture) {
        // Coordinates are quantized (x10) against cs = 0.1, so 46 means x = 4.6.
        const unsigned short verts[] = {
            0, 0,
            0, // 0 (0, 0)
            46, 0,
            0, // 1 (4.6, 0)
            46, 0,
            10, // 2 (4.6, 1)
            0, 0,
            10, // 3 (0, 1)
            90, 0,
            0, // 4 (9, 0)
            90, 0,
            10, // 5 (9, 1)
            0, 0,
            90, // 6 (0, 9)
            46, 0,
            90, // 7 (4.6, 9)
            47, 0,
            90, // 8 (4.7, 9)
            47, 0,
            10, // 9 (4.7, 1)
            90, 0,
            90, // 10 (9, 9)
        };
        // Polygon i is followed by the neighbor indices of its four edges (0xffff = border):
        //   0: lower left,  1: upper left,  2: lower right,  3: right.
        // The passage between the left and right regions only exists along the bottom strip. The wall sliver
        // edges at x = 4.6 (polygon 1) and x = 4.7 (polygon 3) are borders without links.
        const unsigned short polyData[] = {
            0, 1, 2, 3, 0xffff, 2, 1,
            0xffff, // polygon 0 edges: bottom, right(sliver open), top(-x), left
            3, 2, 7, 6, 0, 0xffff, 0xffff,
            0xffff, // polygon 1 edges: bottom, right(sliver wall), top, left
            1, 4, 5, 2, 0xffff, 0xffff, 3,
            0, // polygon 2 edges: bottom, right, top, left(sliver open)
            9, 5, 10, 8, 2, 0xffff, 0xffff,
            0xffff, // polygon 3 edges: bottom, right, top, left(sliver wall)
        };
        constexpr int polyCount = 4;
        constexpr int nvp = 4;

        std::vector<unsigned short> polyFlags(polyCount, 0x01);
        std::vector<unsigned char> polyAreas(polyCount, 0);

        dtNavMeshCreateParams params;
        memset(&params, 0, sizeof(params));
        params.verts = const_cast<unsigned short*>(verts);
        params.vertCount = 11;
        params.polys = const_cast<unsigned short*>(polyData);
        params.polyFlags = polyFlags.data();
        params.polyAreas = polyAreas.data();
        params.polyCount = polyCount;
        params.nvp = nvp;
        params.tileX = 0;
        params.tileY = 0;
        params.tileLayer = 0;
        params.bmin[0] = -0.1f;
        params.bmin[1] = -0.1f;
        params.bmin[2] = -0.1f;
        params.bmax[0] = 9.1f;
        params.bmax[1] = 0.2f;
        params.bmax[2] = 9.1f;
        params.walkableHeight = 2.0f;
        params.walkableRadius = 0.1f;
        params.walkableClimb = 0.2f;
        params.cs = 0.1f;
        params.ch = 0.1f;
        params.buildBvTree = true;

        unsigned char* navData = nullptr;
        int navDataSize = 0;
        if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
            return false;

        fixture.navMesh = dtAllocNavMesh();
        if (dtStatusFailed(fixture.navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA))) {
            dtFreeNavMesh(fixture.navMesh);
            fixture.navMesh = nullptr;
            return false;
        }

        fixture.navQuery = dtAllocNavMeshQuery();
        if (dtStatusFailed(fixture.navQuery->init(fixture.navMesh, 2048))) {
            dtFreeNavMeshQuery(fixture.navQuery);
            fixture.navQuery = nullptr;
            dtFreeNavMesh(fixture.navMesh);
            fixture.navMesh = nullptr;
            return false;
        }

        const dtMeshTile* tile = fixture.navMesh->getTileAt(0, 0, 0);
        fixture.polyBase = fixture.navMesh->getPolyRefBase(tile);
        return true;
    }

    // Matches the RecastAdapter query filter that runs with generated areas.
    class WaypointQueryFilter final : public dtQueryFilter {
    public:
        WaypointQueryFilter() {
            setIncludeFlags(0xffff);
            setExcludeFlags(0);
            for (int i = 0; i < DT_MAX_AREAS; ++i)
                setAreaCost(i, 1.0f);
        }
    };

    void testWaypointConnectionsRespectWalls() {
        RecastAdapter& adapter = RecastAdapter::getInstance();
        WaypointNavFixture fixture;
        check(buildWaypointNavmesh(fixture), "waypoint navmesh should build");
        WaypointQueryFilter queryFilter;
        constexpr float spacing = 2.25f;
        constexpr float maxNeighborPathCost = spacing * 1.41421356f;
        constexpr float unlimited = FLT_MAX;

        // Start and end sit in adjacent walkable regions on opposite sides of a wall sliver and are close in
        // a straight line, but the open connecting corridor is along the bottom of the mesh, so the walked
        // distance is far larger than the budget.
        const Vec3 wallStart{4.8f, 0.0f, 8.2f};
        const Vec3 wallEnd{4.4f, 0.0f, 8.2f};
        check(!adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, wallStart, wallEnd, maxNeighborPathCost),
            "waypoints on opposite sides of a wall must not connect via the far away opening at the budget");
        check(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, wallStart, wallEnd, unlimited),
            "waypoints on opposite sides of a wall connect without a cost budget");

        // Flat ground inside one large polygon: adjacent waypoints connect when the corridor matches the
        // straight line distance.
        const Vec3 flatStart{5.0f, 0.0f, 2.0f};
        const Vec3 flatEnd{6.6f, 0.0f, 3.6f};
        check(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, flatStart, flatEnd, maxNeighborPathCost),
            "waypoints on flat ground within the diagonal spacing budget connect");

        const Vec3 exactDiagStart{5.0f, 0.0f, 2.0f};
        const Vec3 exactDiagEnd{7.25f, 0.0f, 4.25f};
        check(adapter.pfWaypointsConnected(
                  fixture.navQuery, &queryFilter, exactDiagStart, exactDiagEnd, maxNeighborPathCost),
            "waypoints separated by the exact diagonal spacing connect");

        // Large polygons cover multiple waypoints: positions inside one polygon can still be farther apart
        // than the budget, and the string-pulled distance decides, not the polygon cost.
        const Vec3 farSamePolyStart{8.8f, 0.0f, 1.5f};
        const Vec3 farSamePolyEnd{4.8f, 0.0f, 8.5f};
        check(!adapter.pfWaypointsConnected(
                  fixture.navQuery, &queryFilter, farSamePolyStart, farSamePolyEnd, maxNeighborPathCost),
            "same-polygon waypoints farther apart than the budget must not connect");
        check(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, farSamePolyStart, farSamePolyEnd, unlimited),
            "same-polygon waypoints connect without a cost budget");
    }

    void run(const std::string_view name, const auto& test) {
        test();
        std::cout << "[pass] " << name << std::endl;
    }

    // Game files are not part of this repository: tests that compare against one run where it has been provided.
    void runWithGameFile(const std::string_view name, const std::filesystem::path& gameFile, const auto& test) {
        if (!std::filesystem::exists(gameFile)) {
            std::cout << "[skip] " << name << " (needs " << gameFile.generic_string() << ")" << std::endl;
            return;
        }
        run(name, test);
    }
} // namespace

void runDomainTests() {
    const std::filesystem::path vanillaGfxf = "tests/resources/polarbear005 00B7DDECA456B2EE official.gfxf";
    const std::filesystem::path crmdFixture = "tests/resources/yacht.crmd";
    run("Math vector and plane operations", testMathFunctions);
    run("Math ray/AABB intersections", testMathRayAabbIntersection);
    run("Pathfinding segment, neighbor, and intersection helpers", testPathfindingHelpers);
    run("JSON entity serialization and parsing", testJsonSerializationAndParsing);
    run("JSON scene-record round trips", testJsonSceneRecordRoundTrips);
    run("JSON material-record round trips", testJsonMaterialRecords);
    run("Pathfinding box aggregation and fallback", testPathfindingBoxAggregation);
    run("Rooms and volumes load without map space configs", testRoomsAndVolumesLoadWithoutMapSpaceConfigs);
    run("Planner-excluded bridge box parsing", testPlannerExcludedBridgeBoxParsing);
    run("Planner-excluded area usage", testPlannerExcludedAreaUsage);
    run("Unpartitioned bridge area region assignment", testUnpartitionedAreaRegionAssignment);
    run("Bridge join across a gap", testBridgeJoinAcrossGap);
    run("Bridge join finds edges longer than the box", testBridgeJoinFindsEdgesLongerThanTheBox);
    run("Bridge join ignores edges touching the box side", testBridgeJoinIgnoresEdgesTouchingTheBoxSide);
    run("Update version comparison", testUpdateVersionComparison);
    run("Scene mesh lookup", testSceneMeshLookup);
    run("Grid bounds from AIRG", testGridBoundsFromAirg);
    run("Reasoning grid AIRG persistence", testReasoningGridAirgRoundTrip);
    run("Reasoning grid rejects invalid AIRG", testReasoningGridRejectsInvalidAirg);
    runWithGameFile("CRMD binary fixture loading", crmdFixture, testCrmdFixtureLoading);
    runWithGameFile("CRMD save round trip", crmdFixture, testCrmdSaveRoundTrip);
    run("CRMD builder from a synthetic floor", testCrmdBuilderFromSyntheticFloor);
    run("CRMD builder reserves channels for skipped exits", testCrmdBuilderReservesChannelsForSkippedExits);
    run("CRMD builder ambient flow and teleport-in cells", testCrmdBuilderAmbientFlowAndTeleportIn);
    run("CRMD builder spline lanes span the flow width", testCrmdBuilderSplineLanesSpanFlowWidth);
    run("CRMD builder group seed points and exclusions", testCrmdBuilderGroupSeedPointsAndExclusions);
    runWithGameFile("GFXF load extracts GFX", vanillaGfxf, testGfxfLoadExtractsGfx);
    run("GFX load reads raw GFX", testGfxLoadReadsRawGfx);
    runWithGameFile("GFXF rejects invalid containers", vanillaGfxf, testGfxfRejectsInvalidContainers);
    run("GFX pack round trip", testGfxPackRoundTrip);
    run("GFXF save from stored GFX", testGfxfSaveFromStoredGfx);
    run("GFX save from stored GFX", testGfxSaveFromStoredGfx);
    run("Waypoint connectivity-map generation", testWaypointConnectivityMapGeneration);
    run("Waypoint layer generation", testWaypointLayerGeneration);
    run("AIRG visibility type mapping", testVisionDataTypes);
    run("Packed waypoint visibility lookup", testPackedWaypointVisibilityLookup);
    run("Scene JSON persistence", testSceneSaveSerializesCollections);
    run("Crowd JSON round trip", testJsonCrowdRoundTrip);
    run("Volume shape JSON round trip", testJsonVolumeShapeRoundTrip);
    run("Render mesh CPU properties", testMeshCpuProperties);
    run("Render model mesh batching", testModelMeshBatching);
    run("Recast adapter geometry loading and coordinate conversion", testRecastAdapterGeometrySurface);
    run("Waypoint connections respect walls and the path budget", testWaypointConnectionsRespectWalls);
    run("Navp binary fixture loading", testNavpBinaryLoading);
    run("Marker handles arbitrary mesh names", testMarkerObjectNames);
    run("Module defaults and extraction gating", testModuleDefaultsAndRpkgGating);
    run("Module state and capability predicates", testModuleStateAndCapabilities);
    run("Clear and New Project file menu options", testClearAndNewProjectOptions);
    runWithGameFile(
        "GFXF save matches vanilla GFXF apart from the array count", vanillaGfxf, testGfxfSaveMatchesVanillaGfxf);
    runWithGameFile("GFXF reads payloads with and without the array count", vanillaGfxf, testGfxfReadsBothCountLayouts);
}
