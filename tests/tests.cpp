#define CATCH_CONFIG_MAIN
// #include <catch2/catch_all.hpp>
// #include "../include/NavKit/adapter/RecastAdapter.h"
// #include "../include/NavKit/model/ReasoningGrid.h"
// #include "../include/NavKit/module/Airg.h"
// #include "../include/NavKit/module/Navp.h"
// #include "../include/NavKit/module/SceneExtract.h"
// #include <filesystem>

// dtPolyRef findFirstValidPolyRef(const RecastAdapter& adapter) {
//     const dtNavMesh* navMesh = adapter.sample->getNavMesh();
//     if (!navMesh) {
//         FAIL("NavMesh is null when trying to find first PolyRef.");
//         return 0;
//     }
//
//     for (int i = 0; i < navMesh->getMaxTiles(); ++i) {
//         const dtMeshTile* tile = navMesh->getTile(i);
//         if (tile && tile->header && tile->header->polyCount > 0) {
//             return navMesh->encodePolyId(tile->salt, i, 0);
//         }
//     }
//
//     FAIL("No valid polygons found in the NavMesh.");
//     return 0;
// }
//
// TEST_CASE("GridGenerator generates the correct number of nodes and connects them", "[GridGenerator][build]") {
//     SceneExtract &sceneExtract = SceneExtract::getInstance();
//     std::string navpFileName = std::filesystem::current_path().string() + "\\rectangles.navp";
//     Navp::loadNavMesh(navpFileName.data(), false, false);
//     sceneExtract.lastOutputFolder = std::filesystem::current_path().string();
//     Airg &airg = Airg::getInstance();
//     airg.build();
//     REQUIRE(airg.reasoningGrid->m_nNodeCount == 10);
//     REQUIRE(airg.reasoningGrid->m_WaypointList[0].nNeighbors[2] == 1);
//     REQUIRE(airg.reasoningGrid->m_WaypointList[0].nNeighbors[4] == 5);
// }
//
// TEST_CASE("getEdges gets the edges properly", "[RecastAdapter][getEdges]") {
//     std::string navpFileName = std::filesystem::current_path().string() + "\\rectangles.navp";
//     INFO("Loading NavMesh from: " << navpFileName);
//     Navp::loadNavMesh(navpFileName.data(), false, false);
//
//     RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
//     dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
//     REQUIRE(firstPolyRef != 0);
//     const std::vector<Vec3> expectedEdges = {
//         Vec3(0.1f, 0.1f, -2.9f),
//         Vec3(0.1f, 0.1f, 0),
//         Vec3(3.0f, 0.1f, 0)
//     };
//
//     std::vector<Vec3> calculatedEdges = recastAirgAdapter.getEdges(firstPolyRef);
//
//     REQUIRE(calculatedEdges.size() == expectedEdges.size());
//     if (calculatedEdges.size() == expectedEdges.size()) {
//         for (size_t i = 0; i < calculatedEdges.size(); ++i) {
//             INFO("Comparing vertex " << i);
//             REQUIRE(abs(calculatedEdges[i].X - expectedEdges[i].X) < 0.001);
//             REQUIRE(abs(calculatedEdges[i].Y - expectedEdges[i].Y) < 0.001);
//             REQUIRE(abs(calculatedEdges[i].Z - expectedEdges[i].Z) < 0.001);
//         }
//     }
// }
//
// TEST_CASE("calculateNormal calculates the correct normal", "[RecastAdapter][calculateNormal]") {
//     SceneExtract &sceneExtract = SceneExtract::getInstance();
//     std::string navpFileName = std::filesystem::current_path().string() + "\\rectangles.navp";
//     Navp::loadNavMesh(navpFileName.data(), false, false);
//     sceneExtract.lastOutputFolder = std::filesystem::current_path().string();
//     Airg &airg = Airg::getInstance();
//     airg.build();
//     RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
//     dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
//     const Vec3 expectedNormal(0.0f, 1.0f, 0.0f);
//
//     Vec3 calculatedNormal = recastAirgAdapter.calculateNormal(firstPolyRef);
//
//     REQUIRE(calculatedNormal.X == Catch::Approx(expectedNormal.X));
//     REQUIRE(calculatedNormal.Y == Catch::Approx(expectedNormal.Y));
//     REQUIRE(calculatedNormal.Z == Catch::Approx(expectedNormal.Z));
// }
//
// TEST_CASE("calculateCentroid calculates the correct centroid", "[RecastAdapter][calculateCentroid]") {
//     std::string navpFilePath = std::filesystem::current_path().string() + "\\rectangles.navp";
//     INFO("Loading NavMesh from: " << navpFilePath);
//     Navp::loadNavMesh(navpFilePath.data(), false, false);
//
//     RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
//     dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
//     const Vec3 expectedCentroid(1.07f, 0.1f, -0.97f);
//     Vec3 calculatedCentroid = recastAirgAdapter.calculateCentroid(firstPolyRef);
//
//     REQUIRE(calculatedCentroid.X == Catch::Approx(expectedCentroid.X).epsilon(0.1f));
//     REQUIRE(calculatedCentroid.Y == Catch::Approx(expectedCentroid.Y).epsilon(0.1f));
//     REQUIRE(calculatedCentroid.Z == Catch::Approx(expectedCentroid.Z).epsilon(0.1f));
// }
//
// TEST_CASE("getPoly gets the correct poly", "[RecastAdapter][getPoly]") {
//     std::string navpFilePath = std::filesystem::current_path().string() + "\\rectangles.navp";
//     INFO("Loading NavMesh from: " << navpFilePath);
//     Navp::loadNavMesh(navpFilePath.data(), false, false);
//
//     RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
//     dtPolyRef expectedPolyRef = 4194304;
//     dtPolyRef actualPolyRef = recastAirgAdapter.getPoly(0, 0);
//     REQUIRE(expectedPolyRef == actualPolyRef);
// }
//
// TEST_CASE("getAdjacentPoly gets the correct adjacent poly", "[RecastAdapter][getAdjacentPoly]") {
//     std::string navpFilePath = std::filesystem::current_path().string() + "\\rectangles.navp";
//     INFO("Loading NavMesh from: " << navpFilePath);
//     Navp::loadNavMesh(navpFilePath.data(), false, false);
//
//     RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
//     dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
//     dtPolyRef adjacentPolyRefEdge0 = recastAirgAdapter.getAdjacentPoly(firstPolyRef, 2);
//     unsigned int expectedTileIndex = 0;
//     unsigned int expectedPolyIndex = 1;
//     dtPolyRef expectedPolyRefEdge0 = recastAirgAdapter.getPoly(0, 1);
//     unsigned int actualSalt;
//     unsigned int actualTileIndex;
//     unsigned int actualPolyIndex;
//     recastAirgAdapter.sample->m_navMesh->decodePolyId(adjacentPolyRefEdge0, actualSalt, actualTileIndex, actualPolyIndex);
//     REQUIRE(expectedTileIndex == actualTileIndex);
//     REQUIRE(expectedPolyIndex == actualPolyIndex);
//     REQUIRE(adjacentPolyRefEdge0 == expectedPolyRefEdge0);
// }
//
// TEST_CASE("getClosestReachablePolys finds correct polygons", "[RecastAdapter][getClosestReachablePolys]") {
//     std::string navpFilePath = std::filesystem::current_path().string() + "\\rectangles.navp";
//     INFO("Loading NavMesh from: " << navpFilePath);
//     Navp::loadNavMesh(navpFilePath.data(), false, false);
//
//     RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
//
//     dtPolyRef startRef = recastAirgAdapter.getPoly(0, 2);
//     REQUIRE(startRef != 0);
//     Vec3 centerOfStartPolyRecast = recastAirgAdapter.calculateCentroid(startRef);
//     Vec3 navpowerPos = recastAirgAdapter.convertFromRecastToNavPower(centerOfStartPolyRecast);
//     const int maxPolys = 5;
//     std::vector<dtPolyRef> expectedPolys = {
//         startRef,
//         recastAirgAdapter.getPoly(0, 3),
//         recastAirgAdapter.getPoly(0, 5),
//     };
//     std::sort(expectedPolys.begin(), expectedPolys.end());
//
//     std::vector<dtPolyRef> calculatedPolys = recastAirgAdapter.getClosestReachablePolys(navpowerPos, startRef, maxPolys);
//
//     std::sort(calculatedPolys.begin(), calculatedPolys.end());
//
//     INFO("Start Ref: " << startRef);
//     INFO("NavPower Pos (Search Center): " << navpowerPos.X << ", " << navpowerPos.Y << ", " << navpowerPos.Z);
//
//     auto logVector = [](const std::vector<dtPolyRef>& vec) {
//         std::stringstream ss;
//         for(const auto& p : vec) ss << p << " ";
//         return ss.str();
//     };
//
//     INFO("Calculated Polys (" << calculatedPolys.size() << "): " << logVector(calculatedPolys));
//     INFO("Expected Polys (" << expectedPolys.size() << "): " << logVector(expectedPolys));
//
//     REQUIRE(calculatedPolys == expectedPolys);
//
//     // --- Optional: More granular checks if needed ---
//     // REQUIRE(calculatedPolys.size() == expectedPolys.size());
//     // if (calculatedPolys.size() == expectedPolys.size()) {
//     //     for (size_t i = 0; i < calculatedPolys.size(); ++i) {
//     //         REQUIRE(calculatedPolys[i] == expectedPolys[i]); // Compare element by element after sorting
//     //     }
//     // }
// }
//
// TEST_CASE("getClosestPolys finds correct polygons", "[RecastAdapter][getClosestPolys]") {
//     std::string navpFilePath = std::filesystem::current_path().string() + "\\rectangles.navp";
//     INFO("Loading NavMesh from: " << navpFilePath);
//     Navp::loadNavMesh(navpFilePath.data(), false, false);
//
//     RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
//
//     Vec3 navPowerPos = {3.5, 1.5, 0.0};
//     const int maxPolys = 5;
//     std::vector<dtPolyRef> expectedPolys = {
//         recastAirgAdapter.getPoly(0, 0),
//         recastAirgAdapter.getPoly(0, 1),
//         recastAirgAdapter.getPoly(0, 2),
//         recastAirgAdapter.getPoly(0, 3),
//     };
//     std::sort(expectedPolys.begin(), expectedPolys.end());
//
//     std::vector<dtPolyRef> calculatedPolys = recastAirgAdapter.getClosestPolys(navPowerPos, maxPolys);
//
//     std::sort(calculatedPolys.begin(), calculatedPolys.end());
//
//     INFO("NavPower Pos (Search Center): " << navPowerPos.X << ", " << navPowerPos.Y << ", " << navPowerPos.Z);
//
//     auto logVector = [](const std::vector<dtPolyRef>& vec) {
//         std::stringstream ss;
//         for(const auto& p : vec) ss << p << " ";
//         return ss.str();
//     };
//
//     INFO("Calculated Polys (" << calculatedPolys.size() << "): " << logVector(calculatedPolys));
//     INFO("Expected Polys (" << expectedPolys.size() << "): " << logVector(expectedPolys));
//
//     REQUIRE(calculatedPolys == expectedPolys);
//
//     // --- Optional: More granular checks if needed ---
//     // REQUIRE(calculatedPolys.size() == expectedPolys.size());
//     // if (calculatedPolys.size() == expectedPolys.size()) {
//     //     for (size_t i = 0; i < calculatedPolys.size(); ++i) {
//     //         REQUIRE(calculatedPolys[i] == expectedPolys[i]); // Compare element by element after sorting
//     //     }
//     // }
// }