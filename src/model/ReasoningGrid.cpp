#include "../../include/NavKit/model/ReasoningGrid.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

void ReasoningGrid::generateVisionData(const std::function<bool(const Vec4&, const Vec4&)>& unblocked,
    const float lowHeight, const float highHeight, const std::function<void(size_t, size_t)>& progress) {
    if (!unblocked || !std::isfinite(m_Properties.fGridSpacing) || m_Properties.fGridSpacing <= 0 ||
        m_Properties.nVisibilityRange > 512 || !std::isfinite(lowHeight) || !std::isfinite(highHeight) ||
        lowHeight <= 0 || highHeight <= 0) {
        throw std::invalid_argument("Invalid AIRG visibility generation settings.");
    }
    using Cell = std::pair<int, int>;
    std::map<Cell, std::map<int32_t, size_t>> cells;
    std::vector<Cell> waypointCells;
    for (size_t i = 0; i < m_WaypointList.size(); ++i) {
        const auto& waypoint = m_WaypointList[i];
        const auto coordinate = [&](float value, float minimum) {
            const double cell = std::floor((static_cast<double>(value) - minimum) / m_Properties.fGridSpacing);
            if (!std::isfinite(cell) || cell < std::numeric_limits<int>::min() + 512.0 ||
                cell > std::numeric_limits<int>::max() - 512.0) {
                throw std::invalid_argument("Invalid AIRG waypoint position.");
            }
            return static_cast<int>(cell);
        };
        if (!std::isfinite(waypoint.vPos.z) || waypoint.nLayerIndex < INT16_MIN || waypoint.nLayerIndex > INT16_MAX) {
            throw std::invalid_argument("Invalid AIRG waypoint layer or height.");
        }
        const Cell cell{
            coordinate(waypoint.vPos.x, m_Properties.vMin.x), coordinate(waypoint.vPos.y, m_Properties.vMin.y)};
        waypointCells.push_back(cell);
        // The first waypoint in list order represents duplicate cells on the same layer.
        cells[cell].try_emplace(waypoint.nLayerIndex, i);
    }
    const int range = static_cast<int>(m_Properties.nVisibilityRange);
    const size_t width = 2 * static_cast<size_t>(range) + 1;
    std::vector<uint8_t> data;
    std::vector<uint32_t> offsets;
    for (size_t source = 0; source < m_WaypointList.size(); ++source) {
        const auto& from = m_WaypointList[source];
        const auto [cx, cy] = waypointCells[source];
        std::map<int32_t, size_t> layers;
        struct Target {
            int dx, dy;
            size_t index;
        };
        std::vector<Target> targets;
        for (int dy = -range; dy <= range; ++dy) {
            for (int dx = -range; dx <= range; ++dx) {
                const auto cell = cells.find({cx + dx, cy + dy});
                if (cell == cells.end())
                    continue;
                for (const auto& [layer, index] : cell->second) {
                    targets.push_back({dx, dy, index});
                    if (layer != from.nLayerIndex)
                        layers.try_emplace(layer, 0);
                }
            }
        }
        const size_t headerSize = 2 + 2 * layers.size();
        const size_t byteCount = (width * width * 2 * (layers.size() + 1) + 7) / 8;
        if (data.size() > UINT32_MAX || headerSize + byteCount > UINT32_MAX - data.size()) {
            throw std::length_error("AIRG visibility data exceeds 32-bit offsets.");
        }
        offsets.push_back(static_cast<uint32_t>(data.size()));
        const size_t start = data.size();
        data.push_back(static_cast<uint8_t>(layers.size()));
        data.push_back(static_cast<uint8_t>(layers.size() >> 8));
        size_t layerIndex = 1;
        for (auto& [layer, index] : layers) {
            index = layerIndex++;
            const auto id = static_cast<uint16_t>(layer);
            data.push_back(static_cast<uint8_t>(id));
            data.push_back(static_cast<uint8_t>(id >> 8));
        }
        data.resize(start + headerSize + byteCount, 0);
        for (const auto& target : targets) {
            const auto& to = m_WaypointList[target.index];
            const size_t layer = to.nLayerIndex == from.nLayerIndex ? 0 : layers.at(to.nLayerIndex);
            for (size_t channel = 0; channel < 2; ++channel) {
                Vec4 a = from.vPos, b = to.vPos;
                const float height = channel == 0 ? highHeight : lowHeight;
                a.z += height;
                b.z += height;
                if (unblocked(a, b)) {
                    const size_t bit = static_cast<size_t>(target.dx + range) +
                        width * (static_cast<size_t>(target.dy + range) + width * (channel + 2 * layer));
                    data[start + headerSize + bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
                }
            }
        }
        const size_t completed = source + 1;
        if (progress && (completed % 100 == 0 || completed == m_WaypointList.size())) {
            progress(completed, m_WaypointList.size());
        }
    }
    // Publish only after all rays and bounds checks succeeded.
    m_pVisibilityData = std::move(data);
    for (size_t i = 0; i < offsets.size(); ++i)
        m_WaypointList[i].nVisionDataOffset = offsets[i];
    m_HighVisibilityBits = {};
    m_LowVisibilityBits = {};
}

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

std::vector<uint8_t> ReasoningGrid::getWaypointVisionData(const int waypointIndex) const {
    if (waypointIndex < 0 || waypointIndex >= static_cast<int>(m_WaypointList.size())) {
        return {};
    }
    const Waypoint& waypoint = m_WaypointList[waypointIndex];
    const size_t start = waypoint.nVisionDataOffset;
    const size_t end = (waypointIndex + 1) < static_cast<int>(m_WaypointList.size())
        ? m_WaypointList[waypointIndex + 1].nVisionDataOffset
        : m_pVisibilityData.size();
    if (start > end || end > m_pVisibilityData.size()) {
        return {};
    }
    return {m_pVisibilityData.begin() + static_cast<std::ptrdiff_t>(start),
        m_pVisibilityData.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::optional<WaypointVisibility> ReasoningGrid::getVisibility(
    const int fromWaypointIndex, const int toWaypointIndex) const {
    if (fromWaypointIndex < 0 || toWaypointIndex < 0 || fromWaypointIndex >= static_cast<int>(m_WaypointList.size()) ||
        toWaypointIndex >= static_cast<int>(m_WaypointList.size()) || m_Properties.fGridSpacing <= 0.0f ||
        m_Properties.nVisibilityRange > 512) {
        return std::nullopt;
    }
    const Waypoint& from = m_WaypointList[fromWaypointIndex];
    const Waypoint& to = m_WaypointList[toWaypointIndex];
    const auto cellX = [this](const float x) {
        return static_cast<int>(std::floor((x - m_Properties.vMin.x) / m_Properties.fGridSpacing));
    };
    const auto cellY = [this](const float y) {
        return static_cast<int>(std::floor((y - m_Properties.vMin.y) / m_Properties.fGridSpacing));
    };
    const int fromCellX = cellX(from.vPos.x);
    const int fromCellY = cellY(from.vPos.y);
    const int toCellX = cellX(to.vPos.x);
    const int toCellY = cellY(to.vPos.y);
    const int range = static_cast<int>(m_Properties.nVisibilityRange);
    const int dx = toCellX - fromCellX;
    const int dy = toCellY - fromCellY;
    if (std::abs(dx) > range || std::abs(dy) > range) {
        return std::nullopt;
    }

    const size_t recordStart = from.nVisionDataOffset;
    const size_t recordEnd = fromWaypointIndex + 1 < static_cast<int>(m_WaypointList.size())
        ? m_WaypointList[fromWaypointIndex + 1].nVisionDataOffset
        : m_pVisibilityData.size();
    if (recordStart > recordEnd || recordEnd > m_pVisibilityData.size() || recordEnd - recordStart < 2) {
        return std::nullopt;
    }
    const size_t recordSize = recordEnd - recordStart;
    const size_t layerCount = static_cast<size_t>(m_pVisibilityData[recordStart]) |
        (static_cast<size_t>(m_pVisibilityData[recordStart + 1]) << 8);
    if (layerCount > (recordSize - 2) / 2) {
        return std::nullopt;
    }

    size_t layerDataIndex = 0;
    if (to.nLayerIndex != from.nLayerIndex) {
        bool found = false;
        for (size_t i = 0; i < layerCount; ++i) {
            const size_t layerOffset = 2 + 2 * i;
            const int16_t layer = static_cast<int16_t>(
                static_cast<uint16_t>(m_pVisibilityData[recordStart + layerOffset]) |
                static_cast<uint16_t>(static_cast<uint16_t>(m_pVisibilityData[recordStart + layerOffset + 1]) << 8));
            if (layer == to.nLayerIndex) {
                layerDataIndex = i + 1;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::nullopt;
        }
    }

    const size_t width = static_cast<size_t>(range) * 2 + 1;
    const size_t bitsOffset = 2 + layerCount * 2;
    const size_t bitCount = width * width * (layerCount + 1) * 2;
    if (bitsOffset > recordSize || (bitCount + 7) / 8 > recordSize - bitsOffset) {
        return std::nullopt;
    }
    const size_t xOffset = static_cast<size_t>(range + dx);
    const size_t yOffset = static_cast<size_t>(range + dy);
    const auto readBit = [&](const size_t channel) {
        const size_t bitIndex = xOffset + width * (yOffset + width * (channel + 2 * layerDataIndex));
        return (m_pVisibilityData[recordStart + bitsOffset + bitIndex / 8] & (1u << (bitIndex % 8))) != 0;
    };
    return WaypointVisibility{readBit(1), readBit(0)};
}
