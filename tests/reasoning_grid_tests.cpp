#include "../include/NavKit/model/ReasoningGrid.h"

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
#include <vector>

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
        const std::filesystem::path realAirgPath = fixtures.empty() ? resourceDir / "intro.airg" : fixtures[0];
        testRealBin1Airg(realAirgPath);
        testExactBin1RoundTrip(realAirgPath);
        testExactBin1RoundTrip(fixtures.size() > 1 ? fixtures[1] : resourceDir / "intelcpudemo.airg");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
