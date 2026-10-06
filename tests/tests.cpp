#include "../include/NavKit/module/Airg.h"
#include "../include/NavKit/module/ProcessFlow.h"
#include "../include/NavKit/util/GridGenerator.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

void runDomainTests();

namespace {
    void require(const bool condition, const std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    void runTest(const std::string_view name, const auto& test) {
        test();
        std::cout << "[pass] " << name << std::endl;
    }

    std::filesystem::path uniqueTempPath(const std::string& fileName) {
        const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("navkit-tests-" + std::to_string(uniqueId)) / fileName;
    }

    void testSceneExtractionSuccess() {
        const std::filesystem::path outputPath = uniqueTempPath("scene.json");
        std::filesystem::create_directories(outputPath.parent_path());
        {
            std::ofstream existing(outputPath);
            existing << "stale scene data";
        }
        std::vector<std::string> calls;

        const bool extracted = NavKit::ProcessFlow::extractScene(
            outputPath,
            [&calls] {
                calls.emplace_back("connect");
                return 0;
            },
            [&calls] {
                calls.emplace_back("extract");
                return 0;
            },
            [&calls] {
                calls.emplace_back("close");
                return 0;
            });

        require(extracted, "scene extraction should succeed");
        require(calls == std::vector<std::string>{"connect", "extract", "close"}, "game steps ran out of order");
        require(std::filesystem::exists(outputPath), "scene output file was not created");
        require(std::filesystem::file_size(outputPath) == 0, "old scene content was not cleared");
        std::filesystem::remove_all(outputPath.parent_path());
    }

    void testSceneExtractionFailures() {
        for (const std::string failingOperation : {"connect", "extract", "close"}) {
            const std::filesystem::path outputPath = uniqueTempPath(failingOperation + ".json");
            std::filesystem::create_directories(outputPath.parent_path());
            std::vector<std::string> calls;
            const auto operation = [&calls, &failingOperation](const std::string& name) {
                calls.push_back(name);
                return name == failingOperation ? 1 : 0;
            };

            const bool extracted = NavKit::ProcessFlow::extractScene(
                outputPath, [&] { return operation("connect"); }, [&] { return operation("extract"); },
                [&] { return operation("close"); });

            require(!extracted, "failed scene extraction should report failure");
            const size_t expectedCalls = failingOperation == "connect" ? 1 : failingOperation == "extract" ? 2 : 3;
            require(calls.size() == expectedCalls, "scene extraction did not stop after the first failure");
            require(calls.front() == "connect", "connection was not attempted first");
            require(calls.back() == failingOperation, "scene extraction continued after failure");
            std::filesystem::remove_all(outputPath.parent_path());
        }
    }

    void testSceneOutputCreationFailure() {
        std::vector<std::string> calls;
        const std::filesystem::path outputPath = uniqueTempPath("missing-parent/scene.json");

        const bool extracted = NavKit::ProcessFlow::extractScene(
            outputPath,
            [&calls] {
                calls.emplace_back("connect");
                return 0;
            },
            [&calls] {
                calls.emplace_back("extract");
                return 0;
            },
            [&calls] {
                calls.emplace_back("close");
                return 0;
            });

        require(!extracted, "unwritable output path should fail");
        require(calls.empty(), "game connection should not start without an output file");
    }

    void testNavpBuildCallbacks() {
        std::vector<std::string> calls;
        bool built = NavKit::ProcessFlow::buildNavp(
            [&calls] {
                calls.emplace_back("build");
                return true;
            },
            [&calls] { calls.emplace_back("prune"); });

        require(built, "successful Navp build should return success");
        require(calls == std::vector<std::string>{"build", "prune"}, "Navp pruning should follow a successful build");

        calls.clear();
        built = NavKit::ProcessFlow::buildNavp(
            [&calls] {
                calls.emplace_back("build");
                return false;
            },
            [&calls] { calls.emplace_back("prune"); });

        require(!built, "failed Navp build should return failure");
        require(calls == std::vector<std::string>{"build"}, "failed Navp build should not prune");
    }

    void testAirgBuildCallbacks() {
        bool finished = false;
        bool finishResult = false;
        bool built = NavKit::ProcessFlow::buildAirg([] { return true; },
            [&finished, &finishResult](const bool succeeded) {
                finished = true;
                finishResult = succeeded;
            });

        require(built && finished && finishResult, "successful Airg generation should finish as successful");

        finished = false;
        finishResult = true;
        built = NavKit::ProcessFlow::buildAirg([] { return false; },
            [&finished, &finishResult](const bool succeeded) {
                finished = true;
                finishResult = succeeded;
            });

        require(!built && finished && !finishResult, "failed Airg generation should finish as failed");
    }

    void testAirgVisibilityDataSize() {
        ReasoningGrid grid;
        grid.m_WaypointList.resize(2);
        grid.m_WaypointList[0].nVisionDataOffset = 3;
        grid.m_WaypointList[1].nVisionDataOffset = 8;
        grid.m_pVisibilityData.resize(12);

        require(Airg::visibilityDataSize(&grid, 0) == 5, "visibility range should end at next waypoint offset");
        require(Airg::visibilityDataSize(&grid, 1) == 4, "last visibility range should end at data size");
    }

    void testAirgVisibilityDefaults() {
        ReasoningGrid grid;
        grid.m_nNodeCount = 2;
        bool rejected = false;
        try {
            GridGenerator::addVisibilityData(&grid);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "vision generation should require scene collision geometry");
        require(grid.m_pVisibilityData.empty(), "missing geometry should not publish visibility data");
    }

    void testAirgWaypointConnections() {
        Airg& airg = Airg::getInstance();
        ReasoningGrid* const originalGrid = airg.reasoningGrid;
        auto testGrid = std::make_unique<ReasoningGrid>();
        struct RestoreGrid {
            Airg& airg;
            ReasoningGrid* originalGrid;
            ~RestoreGrid() {
                airg.reasoningGrid = originalGrid;
            }
        } restore{airg, originalGrid};
        airg.reasoningGrid = testGrid.get();

        constexpr std::array directions = {std::tuple{0.0f, -1.0f, 0}, std::tuple{1.0f, -1.0f, 1},
            std::tuple{1.0f, 0.0f, 2}, std::tuple{1.0f, 1.0f, 3}, std::tuple{0.0f, 1.0f, 4}, std::tuple{-1.0f, 1.0f, 5},
            std::tuple{-1.0f, 0.0f, 6}, std::tuple{-1.0f, -1.0f, 7}};
        for (const auto& [dx, dy, slot] : directions) {
            testGrid->m_WaypointList.clear();
            testGrid->m_WaypointList.resize(2);
            testGrid->m_WaypointList[0].vPos = {1.0f, 2.0f, 0.0f, 0.0f};
            testGrid->m_WaypointList[1].vPos = {1.0f + dx, 2.0f + dy, 0.0f, 0.0f};

            airg.connectWaypoints(0, 1);
            require(testGrid->m_WaypointList[0].nNeighbors[slot] == 1, "connection chose the wrong direction slot");
            const int oppositeSlot = (slot + 4) % 8;
            require(testGrid->m_WaypointList[1].nNeighbors[oppositeSlot] == 0,
                "connection should create the reciprocal link");

            airg.disconnectWaypoints(0, 1);
            require(testGrid->m_WaypointList[0].nNeighbors[slot] == 65535,
                "disconnect should restore the forward link's unconnected sentinel");
            require(testGrid->m_WaypointList[1].nNeighbors[oppositeSlot] == 65535,
                "disconnect should restore the reciprocal link's unconnected sentinel");
        }

        airg.reasoningGrid = originalGrid;
    }

    void testGridConnectivityBlockedCells() {
        bool bitmap[25]{};
        int connectivity[25]{};
        GridGenerator::CalculateConnectivity(bitmap, connectivity);
        require(std::ranges::all_of(connectivity, [](const int value) { return value == 0; }),
            "empty bitmap should have no blocked-cell connectivity");

        std::ranges::fill(bitmap, true);
        GridGenerator::CalculateConnectivity(bitmap, connectivity);
        require(std::ranges::all_of(connectivity, [](const int value) { return value == 100; }),
            "occupied cells should use the blocked-cell sentinel");
    }

    void testAirgWaypointGeometry() {
        Waypoint waypoint;
        waypoint.vPos = {2.0f, 3.0f, 4.0f, 0.0f};
        std::vector<AirgVertex> triangles;
        std::vector<AirgVertex> lines;

        Airg::addWaypointGeometry(triangles, lines, waypoint, false, glm::vec4(1.0f));
        require(triangles.empty(), "unselected waypoint should not render a triangle fan");
        require(lines.size() == 16, "unselected waypoint should render an eight-segment line loop");
        require(std::abs(lines[0].pos.x - 2.1f) < 0.0001f, "waypoint marker has the wrong x position");
        require(std::abs(lines[0].pos.y - 4.53f) < 0.0001f, "waypoint marker has the wrong render height");
        require(std::abs(lines[0].pos.z + 3.0f) < 0.0001f, "waypoint marker has the wrong z position");

        Airg::addWaypointGeometry(triangles, lines, waypoint, true, glm::vec4(1.0f));
        require(triangles.size() == 24, "selected waypoint should render eight fan triangles");
        require(lines.size() == 16, "selected waypoint should not add line geometry");
    }
} // namespace

int main() {
    try {
        runTest("scene extraction succeeds in order", testSceneExtractionSuccess);
        runTest("scene extraction stops on errors", testSceneExtractionFailures);
        runTest("scene output creation failure", testSceneOutputCreationFailure);
        runTest("Navp build callback order", testNavpBuildCallbacks);
        runTest("Airg build completion callback", testAirgBuildCallbacks);
        runTest("Airg visibility data ranges", testAirgVisibilityDataSize);
        runTest("Airg visibility defaults", testAirgVisibilityDefaults);
        runTest("Airg waypoint connections", testAirgWaypointConnections);
        runTest("grid cell connectivity", testGridConnectivityBlockedCells);
        runTest("Airg waypoint geometry", testAirgWaypointGeometry);
        runDomainTests();
    } catch (const std::exception& error) {
        std::cerr << "[fail] " << error.what() << '\n';
        return 1;
    }
    return 0;
}
