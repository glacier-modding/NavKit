#include "../include/NavKit/model/ReasoningGrid.h"
#include "../include/NavKit/adapter/RecastAdapter.h"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <cfloat>

#include "../include/NavKit/util/Threading.h"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <set>

namespace {
    template <typename T> T readLittle(const std::vector<uint8_t>& bytes, const size_t offset) {
        std::array<uint8_t, sizeof(T)> data{};
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(T), data.begin());
        if constexpr (std::endian::native == std::endian::big) {
            std::ranges::reverse(data);
        }
        T value;
        std::memcpy(&value, data.data(), sizeof(T));
        return value;
    }

    template <typename T> void writeLittle(std::vector<uint8_t>& bytes, const size_t offset, const T value) {
        std::array<uint8_t, sizeof(T)> data{};
        std::memcpy(data.data(), &value, sizeof(T));
        if constexpr (std::endian::native == std::endian::big) {
            std::ranges::reverse(data);
        }
        if (bytes.size() < offset + data.size()) {
            bytes.resize(offset + data.size());
        }
        std::copy(data.begin(), data.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    void require(const bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    std::filesystem::path writeBin1Fixture() {
        constexpr size_t headerSize = 0x10;
        constexpr size_t rootOffset = headerSize;
        std::vector<uint8_t> bytes(headerSize + 0xd0, 0);
        std::memcpy(bytes.data(), "BIN1", 4);
        bytes[4] = 0;
        bytes[5] = 0x10;
        bytes[6] = 2;
        constexpr uint64_t nullPointer = std::numeric_limits<uint64_t>::max();
        for (const size_t pointerOffset : {0x18, 0x20, 0x28, 0x38, 0x40, 0x48}) {
            writeLittle(bytes, rootOffset + pointerOffset, nullPointer);
        }

        auto appendArray = [&bytes](const std::vector<uint8_t>& data, const size_t pointerOffset) {
            if (data.empty()) {
                return;
            }
            const uint64_t begin = bytes.size() - headerSize;
            bytes.insert(bytes.end(), data.begin(), data.end());
            const uint64_t end = bytes.size() - headerSize;
            writeLittle(bytes, pointerOffset, begin);
            writeLittle(bytes, pointerOffset + 8, end);
            writeLittle(bytes, pointerOffset + 16, end);
        };

        std::vector<uint8_t> waypoint(0x30, 0);
        for (uint16_t i = 0; i < 8; ++i) {
            writeLittle(waypoint, i * 2, i);
        }
        writeLittle(waypoint, 16, 21.0f);
        writeLittle(waypoint, 20, 22.0f);
        writeLittle(waypoint, 24, 23.0f);
        writeLittle(waypoint, 28, 24.0f);
        writeLittle(waypoint, 32, uint32_t{25});
        writeLittle(waypoint, 36, int16_t{-26});
        appendArray(waypoint, rootOffset);

        writeLittle(bytes, rootOffset + 0x60, 1.0f);
        writeLittle(bytes, rootOffset + 0x64, 2.0f);
        writeLittle(bytes, rootOffset + 0x68, 3.0f);
        writeLittle(bytes, rootOffset + 0x6c, 4.0f);
        writeLittle(bytes, rootOffset + 0x70, 5.0f);
        writeLittle(bytes, rootOffset + 0x74, 6.0f);
        writeLittle(bytes, rootOffset + 0x78, 7.0f);
        writeLittle(bytes, rootOffset + 0x7c, 8.0f);
        writeLittle(bytes, rootOffset + 0x80, int32_t{9});
        writeLittle(bytes, rootOffset + 0x84, 10.0f);
        writeLittle(bytes, rootOffset + 0x88, int32_t{11});
        writeLittle(bytes, rootOffset + 0x90, uint32_t{1});
        writeLittle(bytes, rootOffset + 0xc8, uint32_t{1});

        appendArray({27, 28}, rootOffset + 0x98);
        appendArray({17, 18}, rootOffset + 0xb0);
        const uint32_t coreSize = static_cast<uint32_t>(bytes.size() - headerSize);
        bytes[8] = static_cast<uint8_t>(coreSize >> 24);
        bytes[9] = static_cast<uint8_t>(coreSize >> 16);
        bytes[10] = static_cast<uint8_t>(coreSize >> 8);
        bytes[11] = static_cast<uint8_t>(coreSize);

        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "NavKitReasoningGridBin1Fixture.airg";
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw std::runtime_error("Could not write BIN1 test fixture");
        }
        return path;
    }

    void testRoundTrip() {
        ReasoningGrid expected;
        expected.m_Properties.vMin = {1.0f, 2.0f, 3.0f, 4.0f};
        expected.m_Properties.vMax = {5.0f, 6.0f, 7.0f, 8.0f};
        expected.m_Properties.nGridWidth = 9;
        expected.m_Properties.fGridSpacing = 10.0f;
        expected.m_Properties.nVisibilityRange = 11;
        expected.m_HighVisibilityBits = {{12, 13}, 14};
        expected.m_LowVisibilityBits = {{15}, 16};
        expected.m_deadEndData = {{17, 18, 19}, 20};
        expected.m_nNodeCount = 1;

        Waypoint waypoint;
        waypoint.nNeighbors = {0, 1, 2, 3, 4, 5, 6, 7};
        waypoint.vPos = {21.0f, 22.0f, 23.0f, 24.0f};
        waypoint.nVisionDataOffset = 25;
        waypoint.nLayerIndex = -26;
        expected.m_WaypointList.push_back(waypoint);
        expected.m_pVisibilityData = {27, 28};

        const std::filesystem::path path = std::filesystem::temp_directory_path() / "NavKitReasoningGridRoundTrip.airg";
        std::filesystem::remove(path);
        expected.writeAirg(path);
        std::ifstream stream(path, std::ios::binary);
        std::vector<uint8_t> bytes;
        bytes.assign(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
        stream.close();
        require(bytes.size() > 16, "BIN1 output was empty");
        require(std::memcmp(bytes.data(), "BIN1", 4) == 0, "writer did not emit a BIN1 header");
        const uint32_t coreSize = (static_cast<uint32_t>(bytes[8]) << 24) | (static_cast<uint32_t>(bytes[9]) << 16) |
            (static_cast<uint32_t>(bytes[10]) << 8) | bytes[11];
        const size_t relocationOffset = 16 + coreSize;
        require(relocationOffset + 12 <= bytes.size(), "BIN1 relocation segment is truncated");
        require(readLittle<uint32_t>(bytes, relocationOffset) == 0x12eba5ed, "BIN1 relocation segment is missing");
        require(readLittle<uint32_t>(bytes, relocationOffset + 4) == 64, "BIN1 relocation segment has wrong size");
        require(readLittle<uint32_t>(bytes, relocationOffset + 8) == 15, "BIN1 relocation count is incorrect");

        ReasoningGrid actual;
        actual.readAirg(path);
        std::filesystem::remove(path);

        require(actual.m_Properties.vMin.x == expected.m_Properties.vMin.x, "vMin.x did not round trip");
        require(actual.m_Properties.vMin.y == expected.m_Properties.vMin.y, "vMin.y did not round trip");
        require(actual.m_Properties.vMin.z == expected.m_Properties.vMin.z, "vMin.z did not round trip");
        require(actual.m_Properties.vMin.w == expected.m_Properties.vMin.w, "vMin.w did not round trip");
        require(actual.m_Properties.vMax.x == expected.m_Properties.vMax.x, "vMax.x did not round trip");
        require(actual.m_Properties.vMax.y == expected.m_Properties.vMax.y, "vMax.y did not round trip");
        require(actual.m_Properties.vMax.z == expected.m_Properties.vMax.z, "vMax.z did not round trip");
        require(actual.m_Properties.vMax.w == expected.m_Properties.vMax.w, "vMax.w did not round trip");
        require(actual.m_Properties.nGridWidth == expected.m_Properties.nGridWidth, "grid width did not round trip");
        require(actual.m_Properties.fGridSpacing == expected.m_Properties.fGridSpacing, "spacing did not round trip");
        require(actual.m_Properties.nVisibilityRange == expected.m_Properties.nVisibilityRange,
            "visibility range did not round trip");
        require(actual.m_HighVisibilityBits.m_aBytes == expected.m_HighVisibilityBits.m_aBytes,
            "high visibility bytes did not round trip");
        require(actual.m_HighVisibilityBits.m_nSize == expected.m_HighVisibilityBits.m_nSize,
            "high visibility size did not round trip");
        require(actual.m_LowVisibilityBits.m_aBytes == expected.m_LowVisibilityBits.m_aBytes,
            "low visibility bytes did not round trip");
        require(actual.m_LowVisibilityBits.m_nSize == expected.m_LowVisibilityBits.m_nSize,
            "low visibility size did not round trip");
        require(actual.m_deadEndData.m_aBytes == expected.m_deadEndData.m_aBytes, "dead-end bytes did not round trip");
        require(actual.m_deadEndData.m_nSize == expected.m_deadEndData.m_nSize, "dead-end size did not round trip");
        require(actual.m_nNodeCount == expected.m_nNodeCount, "node count did not round trip");
        require(actual.m_WaypointList.size() == expected.m_WaypointList.size(), "waypoints did not round trip");
        require(actual.m_WaypointList[0].nNeighbors == expected.m_WaypointList[0].nNeighbors,
            "waypoint neighbors did not round trip");
        require(actual.m_WaypointList[0].vPos.x == expected.m_WaypointList[0].vPos.x, "waypoint x did not round trip");
        require(actual.m_WaypointList[0].vPos.y == expected.m_WaypointList[0].vPos.y, "waypoint y did not round trip");
        require(actual.m_WaypointList[0].vPos.z == expected.m_WaypointList[0].vPos.z, "waypoint z did not round trip");
        require(actual.m_WaypointList[0].vPos.w == expected.m_WaypointList[0].vPos.w, "waypoint w did not round trip");
        require(actual.m_WaypointList[0].nVisionDataOffset == expected.m_WaypointList[0].nVisionDataOffset,
            "waypoint visibility offset did not round trip");
        require(actual.m_WaypointList[0].nLayerIndex == expected.m_WaypointList[0].nLayerIndex,
            "waypoint layer did not round trip");
        require(actual.m_pVisibilityData == expected.m_pVisibilityData, "visibility data did not round trip");
    }

    void testTruncatedFile() {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / "NavKitTruncated.airg";
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.put('\0');
        }

        ReasoningGrid grid;
        grid.m_nNodeCount = 42;
        bool failedAsExpected = false;
        try {
            grid.readAirg(path);
        } catch (const std::runtime_error&) {
            failedAsExpected = true;
        }
        std::filesystem::remove(path);
        require(failedAsExpected, "truncated AIRG file was accepted");
        require(grid.m_nNodeCount == 42, "failed read replaced the existing grid");
    }

    void testBin1Airg() {
        ReasoningGrid grid;
        const std::filesystem::path path = writeBin1Fixture();
        grid.readAirg(path);
        std::filesystem::remove(path);

        require(grid.m_WaypointList.size() == 1, "BIN1 waypoint count was not read");
        require(grid.m_nNodeCount == 1, "BIN1 node count was not read");
        require(grid.m_Properties.nGridWidth == 9, "BIN1 grid width was not read");
        require(grid.m_Properties.nVisibilityRange == 11, "BIN1 visibility range was not read");
        require(grid.m_deadEndData.m_aBytes == std::vector<uint8_t>({17, 18}), "BIN1 dead-end bytes were not read");
        require(grid.m_deadEndData.m_nSize == 1, "BIN1 dead-end bit count was not read");
        require(grid.m_pVisibilityData == std::vector<uint8_t>({27, 28}), "BIN1 visibility data was not read");
        require(grid.m_WaypointList[0].nNeighbors[2] == 2, "BIN1 waypoint neighbors were not read");
        require(grid.m_WaypointList[0].nLayerIndex == -26, "BIN1 waypoint layer index was not read");
    }

    void testGeneratedVisionData() {
        ReasoningGrid grid;
        grid.m_Properties.fGridSpacing = 1;
        grid.m_Properties.nVisibilityRange = 1;
        for (const auto pos : {Vec4{0.25f, 0.25f, 0, 1}, Vec4{1.25f, 0.25f, 0, 1}, Vec4{1.25f, 1.25f, 2, 1},
                 Vec4{3.25f, 0.25f, 0, 1}, Vec4{1.3f, 0.3f, 8, 1}}) {
            Waypoint waypoint;
            waypoint.vPos = pos;
            grid.m_WaypointList.push_back(waypoint);
        }
        grid.m_WaypointList[2].nLayerIndex = -7;
        grid.m_nNodeCount = static_cast<uint32_t>(grid.m_WaypointList.size());
        std::atomic<int> rays{0};
        grid.generateVisionData([&](const Vec4& a, const Vec4& b) {
            ++rays;
            // A short wall at x=0.75 blocks the low ray but lets the high ray pass.
            if ((a.x < 0.75f) == (b.x < 0.75f))
                return true;
            const float t = (0.75f - a.x) / (b.x - a.x);
            return a.z + t * (b.z - a.z) > 1;
        });
        require(rays > 0, "visibility generation did not cast rays");
        const auto visible = grid.getVisibility(0, 1);
        require(visible && !visible->low && visible->high, "low/high ray heights or channels are wrong");
        const auto duplicate = grid.getVisibility(0, 4);
        require(duplicate && !duplicate->low && duplicate->high, "duplicate representative policy changed");
        const auto crossLayer = grid.getVisibility(0, 2);
        require(crossLayer && crossLayer->low && crossLayer->high, "cross-layer corner visibility is wrong");
        require(!grid.getVisibility(0, 3), "out-of-range waypoint has visibility");
        const auto record = grid.getWaypointVisionData(0);
        require(record.size() == 9 && record[0] == 1 && record[1] == 0 && record[2] == 0xf9 && record[3] == 0xff,
            "layer count/IDs or record size are wrong");
        // Cell (+1,0), channel 0 is bit 5. Channel 1 is bit 14. Empty (-1,-1) is bit 0.
        require((record[4] & 0x20) && !(record[5] & 0x40) && !(record[4] & 1),
            "visibility packing is not LSB-first or missing cells are set");
        const auto path = std::filesystem::temp_directory_path() / "NavKitGeneratedVision.airg";
        grid.writeAirg(path);
        ReasoningGrid loaded;
        loaded.readAirg(path);
        std::filesystem::remove(path);
        require(loaded.m_pVisibilityData == grid.m_pVisibilityData, "generated vision did not round trip");
        require(loaded.getVisibility(0, 2)->high, "generated layer lookup failed after serialization");
        const auto original = grid.m_pVisibilityData;
        const auto savedThreads = Threading::getMaxThreads();
        const auto ray = [](const Vec4& a, const Vec4& b) {
            if ((a.x < 0.75f) == (b.x < 0.75f))
                return true;
            const float t = (0.75f - a.x) / (b.x - a.x);
            return a.z + t * (b.z - a.z) > 1;
        };
        Threading::setMaxThreads(1);
        grid.generateVisionData(ray);
        require(grid.m_pVisibilityData == original, "serial visibility differs");
        Threading::setMaxThreads(4);
        grid.generateVisionData(ray);
        require(grid.m_pVisibilityData == original, "parallel visibility differs");
        bool failed = false;
        try {
            grid.generateVisionData([](const Vec4&, const Vec4&) -> bool { throw std::runtime_error("ray failure"); });
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed && grid.m_pVisibilityData == original, "ray failure published incomplete vision data");
        Threading::setMaxThreads(savedThreads);
        grid.m_WaypointList[1].nVisionDataOffset = 1;
        require(!grid.getVisibility(0, 1), "truncated generated record was accepted");
    }

    void testParallelVision() {
        const auto savedThreads = Threading::getMaxThreads();
        ReasoningGrid grid;
        grid.m_Properties.fGridSpacing = 1;
        grid.m_Properties.nVisibilityRange = 3;
        for (int y = -7; y <= 7; ++y) {
            for (int x = -7; x <= 7; ++x) {
                Waypoint waypoint;
                waypoint.vPos = {static_cast<float>(x), static_cast<float>(y), 0, 1};
                waypoint.nLayerIndex = (x + y) % 3;
                grid.m_WaypointList.push_back(waypoint);
            }
        }
        const auto ray = [](const Vec4& a, const Vec4& b) { return a.x <= b.x || a.z > 1; };
        Threading::setMaxThreads(1);
        grid.generateVisionData(ray);
        const auto expected = grid.m_pVisibilityData;
        std::vector<uint32_t> offsets;
        for (const auto& waypoint : grid.m_WaypointList)
            offsets.push_back(waypoint.nVisionDataOffset);
        Threading::setMaxThreads(4);
        std::vector<size_t> reports;
        grid.generateVisionData(ray, 0.6f, 1.6f, [&](size_t completed, size_t total) {
            require(total == 225, "progress total changed");
            reports.push_back(completed);
        });
        require(grid.m_pVisibilityData == expected, "parallel layered visibility differs");
        require(reports == std::vector<size_t>{100, 200, 225}, "progress is not ordered or complete");
        for (size_t i = 0; i < offsets.size(); ++i)
            require(grid.m_WaypointList[i].nVisionDataOffset == offsets[i], "parallel record order changed");
        bool failed = false;
        try {
            grid.generateVisionData(
                ray, 0.6f, 1.6f, [](size_t, size_t) { throw std::runtime_error("progress failure"); });
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed && grid.m_pVisibilityData == expected, "progress failure published partial data");
        for (size_t i = 0; i < offsets.size(); ++i)
            require(grid.m_WaypointList[i].nVisionDataOffset == offsets[i], "failure changed offsets");

        // Verify actual concurrency and the cap, including the participating caller.
        Threading::setMaxThreads(2);
        std::atomic<int> active{0}, peak{0};
        Threading::parallelFor(100, [&](size_t) {
            const int running = ++active;
            int previous = peak.load();
            while (previous < running && !peak.compare_exchange_weak(previous, running)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            --active;
        });
        require(peak == 2 && active == 0, "parallel worker cap or joining failed");
        Threading::setMaxThreads(0);
        require(Threading::getMaxThreads() == 1, "zero thread cap must fall back to one");
        const auto caller = std::this_thread::get_id();
        Threading::parallelFor(
            5, [&](size_t) { require(std::this_thread::get_id() == caller, "single-thread work did not use caller"); });
        Threading::setMaxThreads(savedThreads);
    }

    void testEmptyArrayRoundTrip() {
        ReasoningGrid expected;
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "NavKitReasoningGridEmptyArrays.airg";
        expected.writeAirg(path);

        ReasoningGrid actual;
        actual.readAirg(path);
        std::filesystem::remove(path);
        require(actual.m_WaypointList.empty(), "empty BIN1 waypoint array did not round trip");
        require(actual.m_HighVisibilityBits.m_aBytes.empty(), "empty BIN1 high visibility array did not round trip");
        require(actual.m_LowVisibilityBits.m_aBytes.empty(), "empty BIN1 low visibility array did not round trip");
        require(actual.m_deadEndData.m_aBytes.empty(), "empty BIN1 dead-end array did not round trip");
        require(actual.m_pVisibilityData.empty(), "empty BIN1 visibility array did not round trip");
    }

    std::vector<uint8_t> fileBytes(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        require(static_cast<bool>(stream), "Could not open AIRG fixture");
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    void testExactBin1RoundTrip(const std::filesystem::path& source) {
        const auto original = fileBytes(source);
        const auto output = std::filesystem::temp_directory_path() / "NavKitExactBin1.airg";
        ReasoningGrid grid;
        grid.readAirg(source);
        grid.writeAirg(output);
        require(fileBytes(output) == original, "BIN1 round trip differs from original bytes");

        // Verify writing uses the current model, including floats with nontrivial
        // mantissas, rather than returning a cached copy of the input file.
        grid.m_WaypointList[0].vPos.z = std::bit_cast<float>(uint32_t{0x3f800001});
        grid.m_WaypointList[0].nLayerIndex = -7;
        grid.m_deadEndData.m_aBytes.push_back(0x5a);
        grid.writeAirg(output);
        ReasoningGrid edited;
        edited.readAirg(output);
        require(
            std::bit_cast<uint32_t>(edited.m_WaypointList[0].vPos.z) == 0x3f800001, "BIN1 waypoint float bits changed");
        require(edited.m_WaypointList[0].nLayerIndex == -7, "BIN1 layer edit was lost");
        require(edited.m_deadEndData.m_aBytes == grid.m_deadEndData.m_aBytes, "BIN1 array resize was lost");
        std::filesystem::remove(output);
    }

    void testOfficialIntelDeadEnd(const std::filesystem::path& path) {
        ReasoningGrid officialGrid;
        officialGrid.readAirg(path);

        ReasoningGrid generatedGrid = officialGrid;
        generatedGrid.generateDeadEndData();

        require(generatedGrid.m_deadEndData.m_nSize == officialGrid.m_deadEndData.m_nSize,
            "generated dead-end size did not match official size");
        require(generatedGrid.m_deadEndData.m_aBytes == officialGrid.m_deadEndData.m_aBytes,
            "generated dead-end bytes did not match official bytes");
    }

    void testRealBin1Airg(const std::filesystem::path& path) {
        ReasoningGrid grid;
        grid.readAirg(path);
        require(grid.m_WaypointList.size() == 649, "intro AIRG waypoint count was not read");
        require(grid.m_nNodeCount == 649, "intro AIRG node count was not read");
        require(grid.m_Properties.nGridWidth == 52, "intro AIRG grid width was not read");
        require(grid.m_Properties.nVisibilityRange == 23, "intro AIRG visibility range was not read");
        require(grid.m_pVisibilityData.size() == 360844, "intro AIRG visibility data was not read");
        require(grid.m_deadEndData.m_aBytes.empty(), "intro AIRG dead-end bytes were not read");
    }

    struct NavmeshFixture {
        dtNavMesh* navMesh = nullptr;
        dtNavMeshQuery* navQuery = nullptr;
        dtPolyRef polyBase = 0;

        ~NavmeshFixture() {
            if (navQuery != nullptr)
                dtFreeNavMeshQuery(navQuery);
            if (navMesh != nullptr)
                dtFreeNavMesh(navMesh);
        }
    };

    class TestQueryFilter final : public dtQueryFilter {
    public:
        TestQueryFilter() {
            setIncludeFlags(0xffff);
            setExcludeFlags(0);
            for (int i = 0; i < DT_MAX_AREAS; ++i)
                setAreaCost(i, 1.0f);
        }
    };

    bool buildTriangleNavmesh(NavmeshFixture& fixture) {
        // Coordinates are quantized (x10) against cs = 0.1, so 100 means x = 10.0.
        const unsigned short verts[] = {
            0, 0, 0, // 0 (0, 0, 0)
            100, 0, 0, // 1 (10, 0, 0)
            0, 0, 100, // 2 (0, 0, 10)
            100, 0, 100, // 3 (10, 0, 10)
        };
        // 2 triangles with nvp = 3
        // Poly 0: 0, 1, 2. Neighbors: none (0xffff), poly 1, none (0xffff)
        // Poly 1: 1, 3, 2. Neighbors: none (0xffff), none (0xffff), poly 0
        const unsigned short polyData[] = {
            0,
            1,
            2,
            0xffff,
            1,
            0xffff,
            1,
            3,
            2,
            0xffff,
            0xffff,
            0,
        };
        constexpr int polyCount = 2;
        constexpr int nvp = 3;

        std::vector<unsigned short> polyFlags(polyCount, 0x01);
        std::vector<unsigned char> polyAreas(polyCount, 0);

        dtNavMeshCreateParams params;
        memset(&params, 0, sizeof(params));
        params.verts = const_cast<unsigned short*>(verts);
        params.vertCount = 4;
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
        params.bmax[0] = 10.1f;
        params.bmax[1] = 0.2f;
        params.bmax[2] = 10.1f;
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

    void testWaypointConnectionsTriangleFarFromMidpoint() {
        RecastAdapter& adapter = RecastAdapter::getInstance();
        NavmeshFixture fixture;
        require(buildTriangleNavmesh(fixture), "triangle navmesh should build");
        TestQueryFilter queryFilter;
        constexpr float spacing = 2.25f;
        constexpr float maxNeighborPathCost = spacing * 1.41421356f;
        constexpr float unlimited = FLT_MAX;

        // Two waypoints in adjacent triangles, close to each other (dist = 1.0) with a clear path,
        // but far from the shared edge midpoint (5.0, 0.0, 5.0).
        const Vec3 nearStart{0.5f, 0.0f, 9.0f};
        const Vec3 nearEnd{1.5f, 0.0f, 9.0f};
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, nearStart, nearEnd, maxNeighborPathCost),
            "waypoints across shared triangle edge far from edge midpoint must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, nearStart, nearEnd, unlimited),
            "waypoints across shared triangle edge connect with unlimited budget");

        // Waypoints in the same triangle far apart (dist = 8.0 > maxCost).
        const Vec3 farSamePolyStart{0.5f, 0.0f, 9.0f};
        const Vec3 farSamePolyEnd{0.5f, 0.0f, 1.0f};
        require(!adapter.pfWaypointsConnected(
                    fixture.navQuery, &queryFilter, farSamePolyStart, farSamePolyEnd, maxNeighborPathCost),
            "same-triangle waypoints farther apart than budget must not connect");
        require(
            adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, farSamePolyStart, farSamePolyEnd, unlimited),
            "same-triangle waypoints connect with unlimited budget");

        // Waypoints in adjacent triangles far apart (dist = 7.0 > maxCost).
        const Vec3 farAdjStart{8.0f, 0.0f, 1.5f};
        const Vec3 farAdjEnd{8.0f, 0.0f, 8.5f};
        require(
            !adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, farAdjStart, farAdjEnd, maxNeighborPathCost),
            "adjacent-triangle waypoints farther apart than budget must not connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, farAdjStart, farAdjEnd, unlimited),
            "adjacent-triangle waypoints connect with unlimited budget");
    }

    bool buildWallNavmesh(NavmeshFixture& fixture) {
        // Coordinates are quantized (x10) against cs = 0.1, so 46 means x = 4.6.
        const unsigned short verts[] = {
            0, 0, 0, // 0 (0, 0)
            46, 0, 0, // 1 (4.6, 0)
            46, 0, 10, // 2 (4.6, 1)
            0, 0, 10, // 3 (0, 1)
            90, 0, 0, // 4 (9, 0)
            90, 0, 10, // 5 (9, 1)
            0, 0, 90, // 6 (0, 9)
            46, 0, 90, // 7 (4.6, 9)
            47, 0, 90, // 8 (4.7, 9)
            47, 0, 10, // 9 (4.7, 1)
            90, 0, 90, // 10 (9, 9)
        };
        // Polygon i is followed by the neighbor indices of its four edges (0xffff = border):
        //   0: lower left,  1: upper left,  2: lower right,  3: right.
        // The passage between the left and right regions only exists along the bottom strip. The wall sliver
        // edges at x = 4.6 (polygon 1) and x = 4.7 (polygon 3) are borders without links.
        const unsigned short polyData[] = {
            0, 1, 2, 3, 0xffff, 2, 1, 0xffff, // polygon 0 edges: bottom, right(sliver open), top(-x), left
            3, 2, 7, 6, 0, 0xffff, 0xffff, 0xffff, // polygon 1 edges: bottom, right(sliver wall), top, left
            1, 4, 5, 2, 0xffff, 0xffff, 3, 0, // polygon 2 edges: bottom, right, top, left(sliver open)
            9, 5, 10, 8, 2, 0xffff, 0xffff, 0xffff, // polygon 3 edges: bottom, right, top, left(sliver wall)
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

    void testWaypointConnectionsWallObstacle() {
        RecastAdapter& adapter = RecastAdapter::getInstance();
        NavmeshFixture fixture;
        require(buildWallNavmesh(fixture), "wall navmesh should build");
        TestQueryFilter queryFilter;
        constexpr float spacing = 2.25f;
        constexpr float maxNeighborPathCost = spacing * 1.41421356f;
        constexpr float unlimited = FLT_MAX;

        // Start and end sit on opposite sides of a wall sliver and are close in a straight line (dist = 0.4),
        // but the open connecting passage is along the bottom, making the walkable route ~16.0 > budget.
        const Vec3 wallStart{4.8f, 0.0f, 8.2f};
        const Vec3 wallEnd{4.4f, 0.0f, 8.2f};
        require(!adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, wallStart, wallEnd, maxNeighborPathCost),
            "waypoints on opposite sides of a wall must not connect via the far away opening at the budget");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, wallStart, wallEnd, unlimited),
            "waypoints on opposite sides of a wall connect without a cost budget");
    }

    void testWaypointConnectionsSquareDiagonals() {
        RecastAdapter& adapter = RecastAdapter::getInstance();
        NavmeshFixture fixture;
        require(buildTriangleNavmesh(fixture), "triangle navmesh should build");
        TestQueryFilter queryFilter;
        constexpr float spacing = 2.25f;
        constexpr float maxNeighborPathCost = spacing * 1.41421356f;

        // 4 waypoints forming a connected square with valid walkable space inside:
        // W00 (bottom-left), W10 (bottom-right), W01 (top-left), W11 (top-right).
        const Vec3 w00{2.0f, 0.0f, 2.0f};
        const Vec3 w10{4.25f, 0.0f, 2.0f};
        const Vec3 w01{2.0f, 0.0f, 4.25f};
        const Vec3 w11{4.25f, 0.0f, 4.25f};

        // All 4 orthogonal edges of the square must connect.
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00, w10, maxNeighborPathCost),
            "square orthogonal connection (w00-w10) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00, w01, maxNeighborPathCost),
            "square orthogonal connection (w00-w01) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w10, w11, maxNeighborPathCost),
            "square orthogonal connection (w10-w11) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w01, w11, maxNeighborPathCost),
            "square orthogonal connection (w01-w11) must connect");

        // Both diagonals across the square with valid walkable space must connect properly.
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00, w11, maxNeighborPathCost),
            "diagonal waypoints (w00-w11) in connected square must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w10, w01, maxNeighborPathCost),
            "diagonal waypoints (w10-w01) across triangle boundary in connected square must connect");

        // Another square positioned across the upper portion of the navmesh.
        const Vec3 w00b{4.0f, 0.0f, 4.0f};
        const Vec3 w10b{6.25f, 0.0f, 4.0f};
        const Vec3 w01b{4.0f, 0.0f, 6.25f};
        const Vec3 w11b{6.25f, 0.0f, 6.25f};

        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00b, w10b, maxNeighborPathCost),
            "second square orthogonal connection (w00b-w10b) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00b, w01b, maxNeighborPathCost),
            "second square orthogonal connection (w00b-w01b) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w10b, w11b, maxNeighborPathCost),
            "second square orthogonal connection (w10b-w11b) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w01b, w11b, maxNeighborPathCost),
            "second square orthogonal connection (w01b-w11b) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00b, w11b, maxNeighborPathCost),
            "second square diagonal connection (w00b-w11b) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w10b, w01b, maxNeighborPathCost),
            "second square diagonal connection (w10b-w01b) must connect");
    }

    void testWaypointConnectionsSquareBlockedByWall() {
        RecastAdapter& adapter = RecastAdapter::getInstance();
        NavmeshFixture fixture;
        require(buildWallNavmesh(fixture), "wall navmesh should build");
        TestQueryFilter queryFilter;
        constexpr float spacing = 2.25f;
        constexpr float maxNeighborPathCost = spacing * 1.41421356f;

        // 4 waypoints straddling a wall: bottom waypoints in open area, top waypoints on opposite sides of wall.
        const Vec3 w00{3.5f, 0.0f, 0.5f};
        const Vec3 w10{5.75f, 0.0f, 0.5f};
        const Vec3 w01{3.5f, 0.0f, 2.75f};
        const Vec3 w11{5.75f, 0.0f, 2.75f};

        // Open orthogonal edges connect.
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00, w10, maxNeighborPathCost),
            "open bottom edge (w00-w10) must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00, w01, maxNeighborPathCost),
            "left edge (w00-w01) without wall must connect");
        require(adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w10, w11, maxNeighborPathCost),
            "right edge (w10-w11) without wall must connect");

        // Top edge and diagonals intersected by the wall must NOT connect under the budget.
        require(!adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w01, w11, maxNeighborPathCost),
            "top edge (w01-w11) separated by wall must not connect");
        require(!adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w00, w11, maxNeighborPathCost),
            "diagonal (w00-w11) crossing wall obstacle must not connect");
        require(!adapter.pfWaypointsConnected(fixture.navQuery, &queryFilter, w10, w01, maxNeighborPathCost),
            "diagonal (w10-w01) crossing wall obstacle must not connect");
    }
} // namespace

int main(const int argc, char** argv) {
    try {
        // CLion may append Catch-style runner options to this standalone test
        // executable. Only AIRG arguments override the fixture paths.
        std::vector<std::filesystem::path> fixtures;
        for (int i = 1; i < argc; ++i) {
            const std::filesystem::path argument(argv[i]);
            const auto extension = argument.extension();
            if (extension == ".airg" || extension == ".AIRG") {
                fixtures.push_back(argument);
            }
        }
        const std::filesystem::path resourceDir = NAVKIT_REASONING_GRID_RESOURCE_DIR;
        testRoundTrip();
        testTruncatedFile();
        testBin1Airg();
        testEmptyArrayRoundTrip();
        testGeneratedVisionData();
        testParallelVision();
        testWaypointConnectionsTriangleFarFromMidpoint();
        testWaypointConnectionsWallObstacle();
        testWaypointConnectionsSquareDiagonals();
        testWaypointConnectionsSquareBlockedByWall();
        const std::filesystem::path realAirgPath = fixtures.empty() ? resourceDir / "intro.airg" : fixtures[0];
        testRealBin1Airg(realAirgPath);
        testExactBin1RoundTrip(realAirgPath);
        testExactBin1RoundTrip(fixtures.size() > 1 ? fixtures[1] : resourceDir / "intelcpudemo_official.airg");
        testOfficialIntelDeadEnd(fixtures.size() > 1 ? fixtures[1] : resourceDir / "intelcpudemo_official.airg");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
