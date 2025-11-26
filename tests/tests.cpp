#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "../include/NavKit/adapter/RecastAdapter.h"
#include "../include/NavKit/util/GridGenerator.h"
#include "../include/NavKit/module/Airg.h"
#include "../include/NavKit/module/Navp.h"
#include "../include/NavKit/module/SceneExtract.h"
#include <filesystem>

dtPolyRef findFirstValidPolyRef(const RecastAdapter& adapter) {
    const dtNavMesh* navMesh = adapter.sample->getNavMesh();
    if (!navMesh) {
        FAIL("NavMesh is null when trying to find first PolyRef.");
        return 0;
    }

    for (int i = 0; i < navMesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = navMesh->getTile(i);
        if (tile && tile->header && tile->header->polyCount > 0) {
            return navMesh->encodePolyId(tile->salt, i, 0);
        }
    }

    FAIL("No valid polygons found in the NavMesh.");
    return 0;
}

void setup() {
    delete Airg::getInstance().reasoningGrid;
    Airg::getInstance().reasoningGrid = new ReasoningGrid();
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    std::string navpFileName = std::filesystem::current_path().string() + "\\rectangles.navp";
    Navp::loadNavMesh(navpFileName.data(), false, false, false);
    sceneExtract.outputFolder = std::filesystem::current_path().string();
}

TEST_CASE("GridGenerator::build generates the correct number of nodes and connects them", "[GridGenerator][build]") {
    setup();
    GridGenerator::build();
    const Airg &airg = Airg::getInstance();
    REQUIRE(airg.reasoningGrid->m_nNodeCount == 10);
    REQUIRE(airg.reasoningGrid->m_WaypointList[0].nNeighbors[2] == 1);
    REQUIRE(airg.reasoningGrid->m_WaypointList[0].nNeighbors[4] == 5);
}

// This one crashes if the reasoninggrid is deleted in the setup
// TEST_CASE("GridGenerator::GenerateWaypointNodes puts the first node in the correct spot", "[GridGenerator][GenerateWaypointNodes]") {
//     setup();
//     GridGenerator::initRecastAirgAdapter();
//
//     GridGenerator::GetGridProperties();
//     GridGenerator &gridGenerator = GridGenerator::getInstance();
//     gridGenerator.GenerateWaypointNodes();
//
//     const Airg &airg = Airg::getInstance();
//     REQUIRE(airg.reasoningGrid->m_WaypointList[0].vPos.x == 1.125f);
//     REQUIRE(airg.reasoningGrid->m_WaypointList[0].vPos.y == 1.125f);
//     REQUIRE(abs(airg.reasoningGrid->m_WaypointList[0].vPos.z - 0.05f) < 0.1f);
// }

// This one crashes if the reasoninggrid is deleted in the setup
// TEST_CASE("GridGenerator::GetCellBitmap has all valid", "[GridGenerator][GetCellBitmap]") {
//     setup();
//     GridGenerator::initRecastAirgAdapter();
//
//     GridGenerator::GetGridProperties();
//     GridGenerator &gridGenerator = GridGenerator::getInstance();
//     gridGenerator.GenerateWaypointNodes();
//     gridGenerator.GenerateWaypointConnectivityMap();
//     Airg &airg = Airg::getInstance();
//     Waypoint &waypoint = airg.reasoningGrid->m_WaypointList[0];
//     bool cellBitmap[25];
//     float4 vNavPowerPos = {waypoint.vPos.x, waypoint.vPos.y, waypoint.vPos.z, waypoint.vPos.w};
//     gridGenerator.GetCellBitmap(&vNavPowerPos, reinterpret_cast<bool *>(&cellBitmap));
//
//     std::string cellBitmapString = "";
//     for (int i = 0; i < 25; i++) {
//         cellBitmapString += std::to_string(cellBitmap[i]);
//     }
//
//     REQUIRE(cellBitmapString == "1111111111111111111111111");
// }

TEST_CASE("GridGenerator::NearestOuterEdge is false for a bit on the inner edge of the first cell", "[GridGenerator][NearestOuterEdge]") {
    setup();
    GridGenerator::initRecastAirgAdapter();

    GridGenerator::GetGridProperties();
    GridGenerator &gridGenerator = GridGenerator::getInstance();
    gridGenerator.GenerateWaypointNodes();
    gridGenerator.GenerateWaypointConnectivityMap();
    Pathfinding::ZPFLocation cellLocation;
    cellLocation.pos = {1.125f, 0.675f, 0.1f};
    cellLocation.polyRef = 4210689;
    cellLocation.mapped = true;
    float radius = 0.1;
    bool nearestOuterEdge = GridGenerator::NearestOuterEdge(cellLocation, radius, nullptr, nullptr);

    REQUIRE(nearestOuterEdge == false);
}

TEST_CASE("GridGenerator::MapToCell finds the correct point", "[GridGenerator][MapToCell]") {
    setup();
    GridGenerator::initRecastAirgAdapter();

    GridGenerator::GetGridProperties();
    float4 pos {0, 0, 0, 0.0f};
    Pathfinding::ZPFLocation cellLocation;
    GridGenerator::MapToCell(&pos, Navp::getInstance().navMesh->m_areas[0]);
    // TODO: Finish writing this test
    // REQUIRE(cellLocation.polyRef == 4210689);
}

TEST_CASE("GridGenerator::MapLocation maps to the correct polygon", "[GridGenerator][MapLocation]") {
    setup();
    GridGenerator::initRecastAirgAdapter();

    GridGenerator::GetGridProperties();
    float4 pos {1.125f, 0.675f, 0.1f, 0.0f};
    // float4 pos {1.675f, 0.675f, 0.1f, 0.0f};
    Pathfinding::ZPFLocation cellLocation;
    GridGenerator::MapLocation(&pos, &cellLocation);
    REQUIRE(cellLocation.polyRef == 4210689);
}

TEST_CASE("RecastAdapter::getEdges gets the edges properly", "[RecastAdapter][getEdges]") {
    setup();
    GridGenerator::build();
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
    REQUIRE(firstPolyRef != 0);
    const std::vector<Vec3> expectedEdges = {
        Vec3(1.2f, 0.1f, 0),
        Vec3(1.2f, 0.1f, -2.9f),
        Vec3(0.1f, 0.1f, -2.9f)
    };

    std::vector<Vec3> calculatedEdges = recastAirgAdapter.getEdges(firstPolyRef);

    REQUIRE(calculatedEdges.size() == expectedEdges.size());
    if (calculatedEdges.size() == expectedEdges.size()) {
        for (size_t i = 0; i < calculatedEdges.size(); ++i) {
            INFO("Comparing vertex " << i);
            REQUIRE(abs(calculatedEdges[i].X - expectedEdges[i].X) < 0.001);
            REQUIRE(abs(calculatedEdges[i].Y - expectedEdges[i].Y) < 0.001);
            REQUIRE(abs(calculatedEdges[i].Z - expectedEdges[i].Z) < 0.001);
        }
    }
}

TEST_CASE("RecastAdapter::calculateNormal calculates the correct normal", "[RecastAdapter][calculateNormal]") {
    setup();
    GridGenerator::build();
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
    const Vec3 expectedNormal(0.0f, 1.0f, 0.0f);

    Vec3 calculatedRecastNormal = recastAirgAdapter.calculateNormal(firstPolyRef);

    REQUIRE(calculatedRecastNormal.X == Catch::Approx(expectedNormal.X));
    REQUIRE(calculatedRecastNormal.Y == Catch::Approx(expectedNormal.Y));
    REQUIRE(calculatedRecastNormal.Z == Catch::Approx(expectedNormal.Z));
}

TEST_CASE("RecastAdapter::calculateCentroid calculates the correct centroid", "[RecastAdapter][calculateCentroid]") {
    setup();
    GridGenerator::build();
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
    const Vec3 expectedCentroid(0.83f, 0.1f, -1.93f);
    Vec3 calculatedRecastCentroid = recastAirgAdapter.calculateCentroid(firstPolyRef);

    REQUIRE(calculatedRecastCentroid.X == Catch::Approx(expectedCentroid.X).epsilon(0.1f));
    REQUIRE(calculatedRecastCentroid.Y == Catch::Approx(expectedCentroid.Y).epsilon(0.1f));
    REQUIRE(calculatedRecastCentroid.Z == Catch::Approx(expectedCentroid.Z).epsilon(0.1f));
}

TEST_CASE("RecastAdapter::getPoly gets the correct poly", "[RecastAdapter][getPoly]") {
    setup();
    GridGenerator::build();
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    dtPolyRef expectedPolyRef = 4194304;
    dtPolyRef actualPolyRef = recastAirgAdapter.getPoly(0, 0);
    REQUIRE(expectedPolyRef == actualPolyRef);
}

TEST_CASE("RecastAdapter::getAdjacentPoly gets the correct adjacent poly", "[RecastAdapter][getAdjacentPoly]") {
    setup();
    GridGenerator::build();
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    dtPolyRef firstPolyRef = findFirstValidPolyRef(recastAirgAdapter);
    dtPolyRef adjacentPolyRefEdge0 = recastAirgAdapter.getAdjacentPoly(firstPolyRef, 2);
    unsigned int expectedTileIndex = 0;
    unsigned int expectedPolyIndex = 1;
    dtPolyRef expectedPolyRefEdge0 = recastAirgAdapter.getPoly(0, 1);
    unsigned int actualSalt;
    unsigned int actualTileIndex;
    unsigned int actualPolyIndex;
    recastAirgAdapter.sample->m_navMesh->decodePolyId(adjacentPolyRefEdge0, actualSalt, actualTileIndex, actualPolyIndex);
    REQUIRE(expectedTileIndex == actualTileIndex);
    REQUIRE(expectedPolyIndex == actualPolyIndex);
    REQUIRE(adjacentPolyRefEdge0 == expectedPolyRefEdge0);
}

TEST_CASE("RecastAdapter::getClosestReachablePolys finds correct polygons", "[RecastAdapter][getClosestReachablePolys]") {
    setup();
    GridGenerator::build();
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();

    dtPolyRef startRef = recastAirgAdapter.getPoly(1, 2);
    REQUIRE(startRef != 0);
    Vec3 centerOfStartPolyRecast = recastAirgAdapter.calculateCentroid(startRef);
    Vec3 navpowerPos = recastAirgAdapter.convertFromRecastToNavPower(centerOfStartPolyRecast);
    const int maxPolys = 5;
    std::vector<dtPolyRef> expectedPolys = {
        startRef,
        recastAirgAdapter.getPoly(1, 3),
        recastAirgAdapter.getPoly(2, 0),
        recastAirgAdapter.getPoly(2, 1),
    };
    std::sort(expectedPolys.begin(), expectedPolys.end());

    std::vector<dtPolyRef> calculatedPolys = recastAirgAdapter.getClosestReachablePolys(navpowerPos, startRef, maxPolys);

    std::sort(calculatedPolys.begin(), calculatedPolys.end());

    INFO("Start Ref: " << startRef);
    INFO("NavPower Pos (Search Center): " << navpowerPos.X << ", " << navpowerPos.Y << ", " << navpowerPos.Z);

    auto logVector = [](const std::vector<dtPolyRef>& vec) {
        std::stringstream ss;
        for(const auto& p : vec) ss << p << " ";
        return ss.str();
    };

    INFO("Calculated Polys (" << calculatedPolys.size() << "): " << logVector(calculatedPolys));
    INFO("Expected Polys (" << expectedPolys.size() << "): " << logVector(expectedPolys));

    REQUIRE(calculatedPolys == expectedPolys);

    // --- Optional: More granular checks if needed ---
    // REQUIRE(calculatedPolys.size() == expectedPolys.size());
    // if (calculatedPolys.size() == expectedPolys.size()) {
    //     for (size_t i = 0; i < calculatedPolys.size(); ++i) {
    //         REQUIRE(calculatedPolys[i] == expectedPolys[i]); // Compare element by element after sorting
    //     }
    // }
}

TEST_CASE("RecastAdapter::getClosestPolys finds correct polygons", "[RecastAdapter][getClosestPolys]") {
    setup();
    GridGenerator::build();
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();

    Vec3 navPowerPos = {3.5, 1.5, 0.0};
    const int maxPolys = 5;
    std::vector<dtPolyRef> expectedPolys = {
        recastAirgAdapter.getPoly(1, 0),
        recastAirgAdapter.getPoly(1, 1),
        recastAirgAdapter.getPoly(1, 2),
        recastAirgAdapter.getPoly(1, 3),
    };
    std::sort(expectedPolys.begin(), expectedPolys.end());

    std::vector<dtPolyRef> calculatedPolys = recastAirgAdapter.getClosestPolys(navPowerPos, maxPolys);

    std::sort(calculatedPolys.begin(), calculatedPolys.end());

    INFO("NavPower Pos (Search Center): " << navPowerPos.X << ", " << navPowerPos.Y << ", " << navPowerPos.Z);

    auto logVector = [](const std::vector<dtPolyRef>& vec) {
        std::stringstream ss;
        for(const auto& p : vec) ss << p << " ";
        return ss.str();
    };

    INFO("Calculated Polys (" << calculatedPolys.size() << "): " << logVector(calculatedPolys));
    INFO("Expected Polys (" << expectedPolys.size() << "): " << logVector(expectedPolys));

    REQUIRE(calculatedPolys == expectedPolys);

    // --- Optional: More granular checks if needed ---
    // REQUIRE(calculatedPolys.size() == expectedPolys.size());
    // if (calculatedPolys.size() == expectedPolys.size()) {
    //     for (size_t i = 0; i < calculatedPolys.size(); ++i) {
    //         REQUIRE(calculatedPolys[i] == expectedPolys[i]); // Compare element by element after sorting
    //     }
    // }
}