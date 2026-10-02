#include "../../include/NavKit/model/ReasoningGrid.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
    template <typename T> void writeLittleEndian(std::vector<uint8_t>& bytes, const size_t offset, const T value) {
        static_assert(std::is_arithmetic_v<T>);
        std::array<uint8_t, sizeof(T)> valueBytes{};
        std::memcpy(valueBytes.data(), &value, sizeof(T));
        if constexpr (std::endian::native == std::endian::big) {
            std::ranges::reverse(valueBytes);
        }
        if (offset > bytes.max_size() - valueBytes.size()) {
            throw std::length_error("AIRG data is too large to serialize.");
        }
        if (bytes.size() < offset + valueBytes.size()) {
            bytes.resize(offset + valueBytes.size());
        }
        std::copy(valueBytes.begin(), valueBytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    template <typename T> void appendLittleEndian(std::vector<uint8_t>& bytes, const T value) {
        writeLittleEndian(bytes, bytes.size(), value);
    }

    template <typename T> T readValue(std::istream& stream) {
        static_assert(std::is_arithmetic_v<T>);
        std::array<char, sizeof(T)> bytes{};
        stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw std::runtime_error("Unexpected end of AIRG data.");
        }
        if constexpr (std::endian::native == std::endian::big) {
            std::ranges::reverse(bytes);
        }
        T value;
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }

    uint32_t checkedSize(const size_t size) {
        if (size > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("AIRG array is too large to serialize.");
        }
        return static_cast<uint32_t>(size);
    }

    void ensureRemaining(std::istream& stream, const uint64_t size) {
        const std::istream::pos_type current = stream.tellg();
        if (current == std::istream::pos_type(-1)) {
            throw std::runtime_error("Could not determine the remaining AIRG data size.");
        }
        stream.seekg(0, std::ios::end);
        const std::istream::pos_type end = stream.tellg();
        stream.seekg(current);
        if (!stream || end == std::istream::pos_type(-1) || end < current ||
            static_cast<uint64_t>(end - current) < size) {
            throw std::runtime_error("AIRG array extends beyond the end of the file.");
        }
    }

    template <typename T> T readLittleEndian(const std::vector<uint8_t>& bytes, const size_t offset) {
        static_assert(std::is_arithmetic_v<T>);
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
            throw std::runtime_error("Unexpected end of AIRG data.");
        }
        std::array<uint8_t, sizeof(T)> valueBytes{};
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), sizeof(T), valueBytes.begin());
        if constexpr (std::endian::native == std::endian::big) {
            std::ranges::reverse(valueBytes);
        }
        T value;
        std::memcpy(&value, valueBytes.data(), sizeof(T));
        return value;
    }

    uint64_t readArraySize(
        const std::vector<uint8_t>& bytes, const size_t offset, const size_t coreEnd, const size_t elementSize) {
        const uint64_t begin = readLittleEndian<uint64_t>(bytes, offset);
        const uint64_t end = readLittleEndian<uint64_t>(bytes, offset + sizeof(uint64_t));
        const uint64_t allocationEnd = readLittleEndian<uint64_t>(bytes, offset + 2 * sizeof(uint64_t));
        if (begin == std::numeric_limits<uint64_t>::max() && end == begin && allocationEnd == begin) {
            return 0;
        }
        if (end < begin || allocationEnd < end) {
            throw std::runtime_error("AIRG array pointers are invalid.");
        }
        constexpr uint64_t headerSize = 16;
        const uint64_t coreSize = coreEnd - headerSize;
        if (begin > coreSize || end > coreSize || allocationEnd > coreSize) {
            throw std::runtime_error("AIRG array pointers extend beyond the data segment.");
        }
        const uint64_t size = end - begin;
        if (size % elementSize != 0) {
            throw std::runtime_error("AIRG array size does not match its element type.");
        }
        if (size == 0) {
            return 0;
        }
        if (begin > std::numeric_limits<size_t>::max() - headerSize ||
            size > std::numeric_limits<size_t>::max() - headerSize - begin) {
            throw std::runtime_error("AIRG array extends beyond the end of the file.");
        }
        const size_t dataOffset = static_cast<size_t>(headerSize + begin);
        if (dataOffset > coreEnd || size > coreEnd - dataOffset) {
            throw std::runtime_error("AIRG array extends beyond the end of the file.");
        }
        return size;
    }

    std::vector<uint8_t> readAirgArray(
        const std::vector<uint8_t>& bytes, const size_t pointerOffset, const size_t coreEnd, const size_t elementSize) {
        const uint64_t size = readArraySize(bytes, pointerOffset, coreEnd, elementSize);
        const uint64_t begin = readLittleEndian<uint64_t>(bytes, pointerOffset);
        if (size == 0) {
            return {};
        }
        const size_t dataOffset = static_cast<size_t>(16 + begin);
        return {bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + size)};
    }

    uint32_t readBigEndian32(const std::vector<uint8_t>& bytes, const size_t offset) {
        if (offset > bytes.size() || sizeof(uint32_t) > bytes.size() - offset) {
            throw std::runtime_error("Unexpected end of AIRG header.");
        }
        return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 8) | static_cast<uint32_t>(bytes[offset + 3]);
    }

    ReasoningGrid readBin1Airg(const std::vector<uint8_t>& bytes) {
        constexpr size_t headerSize = 0x10;
        constexpr size_t rootSize = 0xd0;
        if (bytes.size() < headerSize || std::memcmp(bytes.data(), "BIN1", 4) != 0) {
            throw std::runtime_error("AIRG file does not have a valid BIN1 header.");
        }
        const uint32_t coreSize = readBigEndian32(bytes, 8);
        if (coreSize < rootSize || coreSize > bytes.size() - headerSize) {
            throw std::runtime_error("AIRG BIN1 data segment has an invalid size.");
        }
        const size_t coreEnd = headerSize + coreSize;

        ReasoningGrid loaded;
        constexpr size_t rootOffset = headerSize;
        constexpr size_t propertiesOffset = rootOffset + 0x60;
        loaded.m_Properties.vMin = {readLittleEndian<float>(bytes, propertiesOffset),
            readLittleEndian<float>(bytes, propertiesOffset + 4), readLittleEndian<float>(bytes, propertiesOffset + 8),
            readLittleEndian<float>(bytes, propertiesOffset + 12)};
        loaded.m_Properties.vMax = {readLittleEndian<float>(bytes, propertiesOffset + 16),
            readLittleEndian<float>(bytes, propertiesOffset + 20),
            readLittleEndian<float>(bytes, propertiesOffset + 24),
            readLittleEndian<float>(bytes, propertiesOffset + 28)};
        loaded.m_Properties.nGridWidth = static_cast<uint32_t>(readLittleEndian<int32_t>(bytes, propertiesOffset + 32));
        loaded.m_Properties.fGridSpacing = readLittleEndian<float>(bytes, propertiesOffset + 36);
        loaded.m_Properties.nVisibilityRange =
            static_cast<uint32_t>(readLittleEndian<int32_t>(bytes, propertiesOffset + 40));
        loaded.m_nNodeCount = readLittleEndian<uint32_t>(bytes, rootOffset + 0x90);

        loaded.m_HighVisibilityBits.m_aBytes = readAirgArray(bytes, rootOffset + 0x38, coreEnd, sizeof(uint8_t));
        loaded.m_HighVisibilityBits.m_nSize = readLittleEndian<uint32_t>(bytes, rootOffset + 0x50);
        loaded.m_LowVisibilityBits.m_aBytes = readAirgArray(bytes, rootOffset + 0x18, coreEnd, sizeof(uint8_t));
        loaded.m_LowVisibilityBits.m_nSize = readLittleEndian<uint32_t>(bytes, rootOffset + 0x30);
        loaded.m_deadEndData.m_aBytes = readAirgArray(bytes, rootOffset + 0xb0, coreEnd, sizeof(uint8_t));
        loaded.m_deadEndData.m_nSize = readLittleEndian<uint32_t>(bytes, rootOffset + 0xc8);
        loaded.m_pVisibilityData = readAirgArray(bytes, rootOffset + 0x98, coreEnd, sizeof(uint8_t));

        const std::vector<uint8_t> waypointData = readAirgArray(bytes, rootOffset, coreEnd, 0x30);
        const size_t waypointCount = waypointData.size() / 0x30;
        if (waypointCount > loaded.m_WaypointList.max_size()) {
            throw std::length_error("AIRG waypoint array is too large.");
        }
        loaded.m_WaypointList.reserve(waypointCount);
        for (size_t i = 0; i < waypointCount; ++i) {
            const size_t offset = i * 0x30;
            Waypoint waypoint;
            for (size_t neighbor = 0; neighbor < waypoint.nNeighbors.size(); ++neighbor) {
                waypoint.nNeighbors[neighbor] = readLittleEndian<uint16_t>(waypointData, offset + neighbor * 2);
            }
            waypoint.vPos = {readLittleEndian<float>(waypointData, offset + 16),
                readLittleEndian<float>(waypointData, offset + 20), readLittleEndian<float>(waypointData, offset + 24),
                readLittleEndian<float>(waypointData, offset + 28)};
            waypoint.nVisionDataOffset = readLittleEndian<uint32_t>(waypointData, offset + 32);
            waypoint.nLayerIndex = readLittleEndian<int16_t>(waypointData, offset + 36);
            loaded.m_WaypointList.push_back(std::move(waypoint));
        }
        return loaded;
    }

    void alignBin1Data(std::vector<uint8_t>& data) {
        constexpr size_t alignment = sizeof(uint64_t);
        const size_t padding = (alignment - data.size() % alignment) % alignment;
        if (padding > data.max_size() - data.size()) {
            throw std::length_error("AIRG data is too large to serialize.");
        }
        data.resize(data.size() + padding, 0);
    }

    void appendBin1Array(std::vector<uint8_t>& data, const size_t pointerOffset, const size_t elementCount,
        const std::vector<uint8_t>& values) {
        if (values.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("AIRG array is too large to serialize.");
        }
        if (elementCount > std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("AIRG array is too large to serialize.");
        }
        if (values.empty()) {
            constexpr uint64_t nullPointer = std::numeric_limits<uint64_t>::max();
            writeLittleEndian(data, pointerOffset, nullPointer);
            writeLittleEndian(data, pointerOffset + 8, nullPointer);
            writeLittleEndian(data, pointerOffset + 16, nullPointer);
            return;
        }
        alignBin1Data(data);
        const size_t prefixOffset = data.size();
        if (prefixOffset > std::numeric_limits<uint32_t>::max() - 8) {
            throw std::length_error("AIRG data is too large to serialize.");
        }
        data.resize(prefixOffset + 8, 0);
        writeLittleEndian(data, prefixOffset + 4, static_cast<uint32_t>(elementCount));
        const size_t begin = data.size();
        if (values.size() > data.max_size() - begin) {
            throw std::length_error("AIRG data is too large to serialize.");
        }
        data.insert(data.end(), values.begin(), values.end());
        const size_t end = data.size();
        writeLittleEndian(data, pointerOffset, static_cast<uint64_t>(begin));
        writeLittleEndian(data, pointerOffset + 8, static_cast<uint64_t>(end));
        writeLittleEndian(data, pointerOffset + 16, static_cast<uint64_t>(end));
    }

    Vec4 readVec4(std::istream& stream) {
        return Vec4{
            readValue<float>(stream), readValue<float>(stream), readValue<float>(stream), readValue<float>(stream)};
    }

    Properties readProperties(std::istream& stream) {
        Properties properties;
        properties.vMin = readVec4(stream);
        properties.vMax = readVec4(stream);
        properties.nGridWidth = readValue<uint32_t>(stream);
        properties.fGridSpacing = readValue<float>(stream);
        properties.nVisibilityRange = readValue<uint32_t>(stream);
        return properties;
    }

    std::vector<uint8_t> readBytes(std::istream& stream) {
        const uint32_t size = readValue<uint32_t>(stream);
        ensureRemaining(stream, size);
        std::vector<uint8_t> bytes(size);
        if (!bytes.empty()) {
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!stream) {
                throw std::runtime_error("Unexpected end of AIRG data.");
            }
        }
        return bytes;
    }

    SizedArray readSizedArray(std::istream& stream) {
        SizedArray array;
        array.m_aBytes = readBytes(stream);
        array.m_nSize = readValue<uint32_t>(stream);
        return array;
    }

    Waypoint readWaypoint(std::istream& stream) {
        Waypoint waypoint;
        for (uint16_t& neighbor : waypoint.nNeighbors) {
            neighbor = readValue<uint16_t>(stream);
        }
        waypoint.vPos = readVec4(stream);
        waypoint.nVisionDataOffset = readValue<uint32_t>(stream);
        waypoint.nLayerIndex = readValue<int32_t>(stream);
        return waypoint;
    }
} // namespace

void ReasoningGrid::writeAirg(const std::filesystem::path& path) const {
    constexpr size_t rootSize = 0xd0;
    std::vector<uint8_t> data(rootSize, 0);

    std::vector<uint8_t> waypoints;
    if (m_WaypointList.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("AIRG waypoint array is too large to serialize.");
    }
    for (const Waypoint& waypoint : m_WaypointList) {
        if (waypoint.nNeighbors.size() != 8) {
            throw std::runtime_error("AIRG waypoints must have exactly eight neighbors.");
        }
        if (waypoint.nLayerIndex < std::numeric_limits<int16_t>::min() ||
            waypoint.nLayerIndex > std::numeric_limits<int16_t>::max()) {
            throw std::runtime_error("AIRG waypoint layer index is outside the BIN1 16-bit range.");
        }
        const size_t waypointOffset = waypoints.size();
        waypoints.resize(waypointOffset + 0x30, 0);
        for (size_t neighbor = 0; neighbor < waypoint.nNeighbors.size(); ++neighbor) {
            writeLittleEndian(waypoints, waypointOffset + neighbor * sizeof(uint16_t), waypoint.nNeighbors[neighbor]);
        }
        writeLittleEndian(waypoints, waypointOffset + 16, waypoint.vPos.x);
        writeLittleEndian(waypoints, waypointOffset + 20, waypoint.vPos.y);
        writeLittleEndian(waypoints, waypointOffset + 24, waypoint.vPos.z);
        writeLittleEndian(waypoints, waypointOffset + 28, waypoint.vPos.w);
        writeLittleEndian(waypoints, waypointOffset + 32, waypoint.nVisionDataOffset);
        writeLittleEndian(waypoints, waypointOffset + 36, static_cast<int16_t>(waypoint.nLayerIndex));
    }
    appendBin1Array(data, 0x00, m_WaypointList.size(), waypoints);
    appendBin1Array(data, 0x18, m_LowVisibilityBits.m_aBytes.size(), m_LowVisibilityBits.m_aBytes);
    appendBin1Array(data, 0x38, m_HighVisibilityBits.m_aBytes.size(), m_HighVisibilityBits.m_aBytes);

    const size_t propertiesOffset = 0x60;
    writeLittleEndian(data, propertiesOffset, m_Properties.vMin.x);
    writeLittleEndian(data, propertiesOffset + 4, m_Properties.vMin.y);
    writeLittleEndian(data, propertiesOffset + 8, m_Properties.vMin.z);
    writeLittleEndian(data, propertiesOffset + 12, m_Properties.vMin.w);
    writeLittleEndian(data, propertiesOffset + 16, m_Properties.vMax.x);
    writeLittleEndian(data, propertiesOffset + 20, m_Properties.vMax.y);
    writeLittleEndian(data, propertiesOffset + 24, m_Properties.vMax.z);
    writeLittleEndian(data, propertiesOffset + 28, m_Properties.vMax.w);
    writeLittleEndian(data, propertiesOffset + 32, static_cast<int32_t>(m_Properties.nGridWidth));
    writeLittleEndian(data, propertiesOffset + 36, m_Properties.fGridSpacing);
    writeLittleEndian(data, propertiesOffset + 40, static_cast<int32_t>(m_Properties.nVisibilityRange));
    writeLittleEndian(data, 0x90, m_nNodeCount);
    writeLittleEndian(data, 0x30, m_LowVisibilityBits.m_nSize);
    writeLittleEndian(data, 0x50, m_HighVisibilityBits.m_nSize);
    writeLittleEndian(data, 0xc8, m_deadEndData.m_nSize);
    appendBin1Array(data, 0x98, m_pVisibilityData.size(), m_pVisibilityData);
    appendBin1Array(data, 0xb0, m_deadEndData.m_aBytes.size(), m_deadEndData.m_aBytes);

    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
    if (data.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("AIRG BIN1 data segment is too large.");
    }
    std::vector<uint8_t> file(16, 0);
    file[0] = 'B';
    file[1] = 'I';
    file[2] = 'N';
    file[3] = '1';
    file[5] = 0x04;
    file[6] = 0x01;
    writeLittleEndian(file, 8, static_cast<uint32_t>(data.size()));
    std::reverse(file.begin() + 8, file.begin() + 12);
    file.insert(file.end(), data.begin(), data.end());

    constexpr std::array<uint32_t, 15> relocationOffsets = {
        0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x38, 0x40, 0x48, 0x98, 0xa0, 0xa8, 0xb0, 0xb8, 0xc0};
    appendLittleEndian(file, uint32_t{0x12eba5ed});
    appendLittleEndian(file, uint32_t{4 + relocationOffsets.size() * sizeof(uint32_t)});
    appendLittleEndian(file, checkedSize(relocationOffsets.size()));
    for (const uint32_t relocationOffset : relocationOffsets) {
        appendLittleEndian(file, relocationOffset);
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Could not open AIRG file for writing: " + path.string());
    }
    stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    stream.flush();
    if (!stream) {
        throw std::runtime_error("Failed to finish writing AIRG file: " + path.string());
    }
}

void ReasoningGrid::readAirg(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Could not open AIRG file for reading: " + path.string());
    }

    std::array<char, 4> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (stream.gcount() == static_cast<std::streamsize>(magic.size()) &&
        std::memcmp(magic.data(), "BIN1", magic.size()) == 0) {
        stream.seekg(0, std::ios::end);
        const std::istream::pos_type end = stream.tellg();
        if (end == std::istream::pos_type(-1) || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max()) {
            throw std::runtime_error("Could not determine the AIRG file size.");
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(end));
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw std::runtime_error("Failed to read AIRG file: " + path.string());
        }
        *this = readBin1Airg(bytes);
        return;
    }
    stream.clear();
    stream.seekg(0, std::ios::beg);

    ReasoningGrid loaded;
    loaded.m_Properties = readProperties(stream);
    loaded.m_HighVisibilityBits = readSizedArray(stream);
    loaded.m_LowVisibilityBits = readSizedArray(stream);
    loaded.m_deadEndData = readSizedArray(stream);
    loaded.m_nNodeCount = readValue<uint32_t>(stream);

    const uint32_t waypointCount = readValue<uint32_t>(stream);
    if (waypointCount > loaded.m_WaypointList.max_size()) {
        throw std::length_error("AIRG waypoint array is too large.");
    }
    ensureRemaining(stream, static_cast<uint64_t>(waypointCount) * 40 + sizeof(uint32_t));
    loaded.m_WaypointList.reserve(waypointCount);
    for (uint32_t i = 0; i < waypointCount; ++i) {
        loaded.m_WaypointList.push_back(readWaypoint(stream));
    }

    loaded.m_pVisibilityData = readBytes(stream);
    if (stream.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("AIRG file contains unexpected trailing data.");
    }
    if (stream.bad()) {
        throw std::runtime_error("Failed to read AIRG file: " + path.string());
    }

    *this = std::move(loaded);
}

std::vector<uint8_t> ReasoningGrid::getWaypointVisionData(int waypointIndex) {
    const Waypoint& waypoint = m_WaypointList[waypointIndex];
    const auto first = m_pVisibilityData.begin() + waypoint.nVisionDataOffset;
    const auto last = (waypointIndex + 1) < m_WaypointList.size()
        ? m_pVisibilityData.begin() + m_WaypointList[waypointIndex + 1].nVisionDataOffset
        : m_pVisibilityData.end();
    return {first, last};
}
