#include "../include/NavKit/model/ReasoningGrid.h"

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
        testRoundTrip();
        testTruncatedFile();
        testBin1Airg();
        testEmptyArrayRoundTrip();
        const std::filesystem::path realAirgPath =
            argc > 1 ? std::filesystem::path(argv[1]) : "tests/resources/intro.airg";
        testRealBin1Airg(realAirgPath);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
