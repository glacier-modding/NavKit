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
#include <unordered_map>
#include "../../include/NavKit/util/Bin1.h"
#include "../../include/NavKit/util/Threading.h"

void ReasoningGrid::generateVisionData(const std::function<bool(const Math::Vec4&, const Math::Vec4&)>& unblocked,
    const float lowHeight, const float highHeight, const std::function<void(size_t, size_t)>& progress) {
    if (!unblocked || !std::isfinite(m_Properties.fGridSpacing) || m_Properties.fGridSpacing <= 0 ||
        m_Properties.nVisibilityRange > 512 || !std::isfinite(lowHeight) || !std::isfinite(highHeight) ||
        lowHeight <= 0 || highHeight <= 0) {
        throw std::invalid_argument("Invalid AIRG visibility generation settings.");
    }
    using Cell = std::pair<int, int>;
    const auto cellKey = [](int x, int y) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
    };
    std::unordered_map<uint64_t, std::map<int32_t, size_t>> cells;
    cells.reserve(m_WaypointList.size());
    std::vector<Cell> waypointCells;
    waypointCells.reserve(m_WaypointList.size());
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
        cells[cellKey(cell.first, cell.second)].try_emplace(waypoint.nLayerIndex, i);
    }
    struct CellEntry {
        int x;
        const std::map<int32_t, size_t>* layers;
    };
    std::unordered_map<int, std::vector<CellEntry>> rows;
    for (const auto& [key, layers] : cells) {
        const int x = std::bit_cast<int32_t>(static_cast<uint32_t>(key >> 32));
        const int y = std::bit_cast<int32_t>(static_cast<uint32_t>(key));
        rows[y].push_back({x, &layers});
    }
    for (auto& [y, row] : rows)
        std::ranges::sort(row, {}, &CellEntry::x);
    const auto& indexedRows = rows;
    const int range = static_cast<int>(m_Properties.nVisibilityRange);
    const size_t width = 2 * static_cast<size_t>(range) + 1;
    std::vector<std::vector<uint8_t>> records(m_WaypointList.size());
    std::mutex progressMutex;
    size_t completed = 0;
    Threading::parallelFor(m_WaypointList.size(), [&](size_t source) {
        auto& data = records[source];
        const auto& from = m_WaypointList[source];
        const auto [cx, cy] = waypointCells[source];
        std::map<int32_t, size_t> layers;
        struct Target {
            int dx, dy;
            size_t index;
        };
        std::vector<Target> targets;
        for (int dy = -range; dy <= range; ++dy) {
            const auto row = indexedRows.find(cy + dy);
            if (row == indexedRows.end())
                continue;
            auto cell = std::lower_bound(row->second.begin(), row->second.end(), cx - range,
                [](const CellEntry& entry, int x) { return entry.x < x; });
            for (; cell != row->second.end() && cell->x <= cx + range; ++cell) {
                for (const auto& [layer, index] : *cell->layers) {
                    targets.push_back({cell->x - cx, dy, index});
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
                Math::Vec4 a = from.vPos, b = to.vPos;
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
        if (progress) {
            std::lock_guard lock(progressMutex);
            ++completed;
            if (completed % 100 == 0 || completed == m_WaypointList.size())
                progress(completed, m_WaypointList.size());
        }
    });
    std::vector<uint32_t> offsets;
    offsets.reserve(records.size());
    size_t totalSize = 0;
    for (const auto& record : records) {
        if (record.size() > UINT32_MAX - totalSize)
            throw std::length_error("AIRG visibility data exceeds 32-bit offsets.");
        offsets.push_back(static_cast<uint32_t>(totalSize));
        totalSize += record.size();
    }
    std::vector<uint8_t> data;
    data.reserve(totalSize);
    for (auto& record : records) {
        data.insert(data.end(), record.begin(), record.end());
        std::vector<uint8_t>().swap(record);
    }
    // Publish only after all rays and bounds checks succeeded.
    m_pVisibilityData = std::move(data);
    for (size_t i = 0; i < offsets.size(); ++i)
        m_WaypointList[i].nVisionDataOffset = offsets[i];
    m_HighVisibilityBits = {};
    m_LowVisibilityBits = {};
}

void ReasoningGrid::generateDeadEndData(const std::function<void(size_t, size_t)>& progress) {
    const size_t nodeCount = m_WaypointList.size();
    m_deadEndData.m_nSize = static_cast<uint32_t>(nodeCount);
    m_deadEndData.m_aBytes.assign((nodeCount + 7) / 8, 0);
    if (nodeCount <= 2) {
        if (progress) {
            progress(nodeCount, nodeCount);
        }
        return;
    }

    // Identify ramp / slope / connector nodes:
    // A node is on a ramp/slope if its neighbors have significantly different z (e.g. |z - nb.z| > 0.3)
    // or it connects across height levels.
    std::vector<bool> isRamp(nodeCount, false);
    for (size_t i = 0; i < nodeCount; ++i) {
        for (int d = 0; d < 8; ++d) {
            const int nb = m_WaypointList[i].nNeighbors[d];
            if (nb != 65535 && static_cast<size_t>(nb) < nodeCount) {
                if (std::abs(m_WaypointList[i].vPos.z - m_WaypointList[nb].vPos.z) > 0.3f) {
                    isRamp[i] = true;
                    isRamp[nb] = true;
                }
            }
        }
    }

    // Propagate ramp along connected slope nodes
    bool rampProp = true;
    while (rampProp) {
        rampProp = false;
        for (size_t i = 0; i < nodeCount; ++i) {
            if (isRamp[i])
                continue;
            for (int d = 0; d < 8; ++d) {
                const int nb = m_WaypointList[i].nNeighbors[d];
                if (nb != 65535 && static_cast<size_t>(nb) < nodeCount && isRamp[nb]) {
                    if (std::abs(m_WaypointList[i].vPos.z - m_WaypointList[nb].vPos.z) > 0.05f) {
                        isRamp[i] = true;
                        rampProp = true;
                        break;
                    }
                }
            }
        }
    }

    // Candidate cuts are sets of 1 or 2 adjacent nodes
    std::vector<std::vector<int>> cuts;
    for (size_t i = 0; i < nodeCount; ++i) {
        cuts.push_back({static_cast<int>(i)});
        for (int d = 0; d < 8; ++d) {
            const int v = m_WaypointList[i].nNeighbors[d];
            if (v != 65535 && static_cast<size_t>(v) < nodeCount && v > static_cast<int>(i)) {
                cuts.push_back({static_cast<int>(i), v});
            }
        }
    }

    std::vector<uint8_t> deadEndFlags(nodeCount, 0);
    std::mutex progressMutex;
    size_t completed = 0;

    Threading::parallelFor(cuts.size(), [&](size_t cutIdx) {
        const auto& cut = cuts[cutIdx];
        std::vector<bool> inCut(nodeCount, false);
        for (const int k : cut) {
            if (k >= 0 && static_cast<size_t>(k) < nodeCount) {
                inCut[k] = true;
            }
        }

        std::vector<bool> visited(nodeCount, false);
        for (const int k : cut) {
            if (k >= 0 && static_cast<size_t>(k) < nodeCount) {
                visited[k] = true;
            }
        }

        std::vector<std::vector<int>> comps;
        for (size_t i = 0; i < nodeCount; ++i) {
            if (visited[i])
                continue;
            std::vector<int> comp;
            std::vector<int> q = {static_cast<int>(i)};
            visited[i] = true;
            size_t qh = 0;
            while (qh < q.size()) {
                const int curr = q[qh++];
                comp.push_back(curr);
                for (int dir = 0; dir < 8; ++dir) {
                    const int nxt = m_WaypointList[curr].nNeighbors[dir];
                    if (nxt != 65535 && static_cast<size_t>(nxt) < nodeCount && !visited[nxt]) {
                        visited[nxt] = true;
                        q.push_back(nxt);
                    }
                }
            }
            comps.push_back(comp);
        }

        if (comps.size() > 1) {
            size_t maxIdx = 0;
            for (size_t c = 1; c < comps.size(); ++c) {
                if (comps[c].size() > comps[maxIdx].size()) {
                    maxIdx = c;
                }
            }

            for (size_t c = 0; c < comps.size(); ++c) {
                if (c == maxIdx)
                    continue;
                // Ramp/stair connectors are not dead ends
                bool hasRamp = false;
                for (const int u : comps[c]) {
                    if (isRamp[u]) {
                        hasRamp = true;
                        break;
                    }
                }
                if (hasRamp)
                    continue;

                std::vector<bool> inComp(nodeCount, false);
                for (const int u : comps[c])
                    inComp[u] = true;

                bool onlyToCut = true;
                for (const int u : comps[c]) {
                    for (int dir = 0; dir < 8; ++dir) {
                        const int nxt = m_WaypointList[u].nNeighbors[dir];
                        if (nxt != 65535 && static_cast<size_t>(nxt) < nodeCount && !inComp[nxt]) {
                            if (!inCut[nxt]) {
                                onlyToCut = false;
                                break;
                            }
                        }
                    }
                    if (!onlyToCut)
                        break;
                }

                if (onlyToCut) {
                    for (const int u : comps[c]) {
                        deadEndFlags[u] = 1;
                    }
                }
            }
        }

        if (progress) {
            std::lock_guard lock(progressMutex);
            ++completed;
            if (completed % 100 == 0 || completed == cuts.size()) {
                progress(completed, cuts.size());
            }
        }
    });

    for (size_t i = 0; i < nodeCount; ++i) {
        if (deadEndFlags[i]) {
            m_deadEndData.m_aBytes[i / 8] |= static_cast<uint8_t>(1 << (i % 8));
        }
    }
}

namespace {
    constexpr size_t airgRootSize = 0xd0;
    using Bin1::readLittleEndian;
    using Bin1::writeLittleEndian;

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

    std::vector<uint8_t> readAirgArray(const std::vector<uint8_t>& bytes, const size_t pointerOffset,
        const uint32_t dataSize, const size_t elementSize) {
        const Bin1::Array array = Bin1::readArray(bytes, pointerOffset, dataSize, airgRootSize, elementSize);
        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(array.offset);
        return {begin, begin + static_cast<std::ptrdiff_t>(array.count * elementSize)};
    }

    ReasoningGrid readBin1Airg(const std::vector<uint8_t>& bytes) {
        const uint32_t dataSize = Bin1::readDataSize(bytes, airgRootSize);

        ReasoningGrid loaded;
        constexpr size_t rootOffset = Bin1::headerSize;
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

        loaded.m_HighVisibilityBits.m_aBytes = readAirgArray(bytes, rootOffset + 0x38, dataSize, sizeof(uint8_t));
        loaded.m_HighVisibilityBits.m_nSize = readLittleEndian<uint32_t>(bytes, rootOffset + 0x50);
        loaded.m_LowVisibilityBits.m_aBytes = readAirgArray(bytes, rootOffset + 0x18, dataSize, sizeof(uint8_t));
        loaded.m_LowVisibilityBits.m_nSize = readLittleEndian<uint32_t>(bytes, rootOffset + 0x30);
        loaded.m_deadEndData.m_aBytes = readAirgArray(bytes, rootOffset + 0xb0, dataSize, sizeof(uint8_t));
        loaded.m_deadEndData.m_nSize = readLittleEndian<uint32_t>(bytes, rootOffset + 0xc8);
        loaded.m_pVisibilityData = readAirgArray(bytes, rootOffset + 0x98, dataSize, sizeof(uint8_t));

        const std::vector<uint8_t> waypointData = readAirgArray(bytes, rootOffset, dataSize, 0x30);
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

    void appendBin1Array(Bin1::Writer& writer, const size_t pointerOffset, const size_t elementCount,
        const std::vector<uint8_t>& values, const bool aligned) {
        const auto appendValues = [&] { writer.data.insert(writer.data.end(), values.begin(), values.end()); };
        if (aligned) {
            writer.appendArray(pointerOffset, elementCount, pointerOffset == 0 ? 16 : 1, appendValues);
            return;
        }
        if (values.empty()) {
            writer.putNullArray(pointerOffset);
            return;
        }
        // The legacy dialect pads to 8 bytes and puts the count in the last half of an 8-byte prefix.
        while (writer.data.size() % 8 != 0) {
            writer.data.push_back(0);
        }
        writer.append(uint32_t{0});
        writer.append(static_cast<uint32_t>(elementCount));
        const size_t begin = writer.data.size();
        appendValues();
        writer.putPointers(pointerOffset, begin, writer.data.size());
    }

    Math::Vec4 readVec4(std::istream& stream) {
        return Math::Vec4{
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
    Bin1::Writer writer(airgRootSize);

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
    appendBin1Array(writer, 0x00, m_WaypointList.size(), waypoints, m_alignedBin1);
    appendBin1Array(writer, 0x18, m_LowVisibilityBits.m_aBytes.size(), m_LowVisibilityBits.m_aBytes, m_alignedBin1);
    appendBin1Array(writer, 0x38, m_HighVisibilityBits.m_aBytes.size(), m_HighVisibilityBits.m_aBytes, m_alignedBin1);

    const size_t propertiesOffset = 0x60;
    writer.put(propertiesOffset, m_Properties.vMin.x);
    writer.put(propertiesOffset + 4, m_Properties.vMin.y);
    writer.put(propertiesOffset + 8, m_Properties.vMin.z);
    writer.put(propertiesOffset + 12, m_Properties.vMin.w);
    writer.put(propertiesOffset + 16, m_Properties.vMax.x);
    writer.put(propertiesOffset + 20, m_Properties.vMax.y);
    writer.put(propertiesOffset + 24, m_Properties.vMax.z);
    writer.put(propertiesOffset + 28, m_Properties.vMax.w);
    writer.put(propertiesOffset + 32, static_cast<int32_t>(m_Properties.nGridWidth));
    writer.put(propertiesOffset + 36, m_Properties.fGridSpacing);
    writer.put(propertiesOffset + 40, static_cast<int32_t>(m_Properties.nVisibilityRange));
    writer.put(0x90, m_nNodeCount);
    writer.put(0x30, m_LowVisibilityBits.m_nSize);
    writer.put(0x50, m_HighVisibilityBits.m_nSize);
    writer.put(0xc8, m_deadEndData.m_nSize);
    appendBin1Array(writer, 0x98, m_pVisibilityData.size(), m_pVisibilityData, m_alignedBin1);
    appendBin1Array(writer, 0xb0, m_deadEndData.m_aBytes.size(), m_deadEndData.m_aBytes, m_alignedBin1);

    while (!m_alignedBin1 && writer.data.size() % 4 != 0) {
        writer.data.push_back(0);
    }
    std::vector<uint8_t> file = writer.finish(m_alignedBin1 ? 0x10 : 0x04, m_alignedBin1 ? 2 : 1);
    if (!m_bin1Extensions.empty()) {
        file.insert(file.end(), m_bin1Extensions.begin(), m_bin1Extensions.end());
    } else if (m_alignedBin1) {
        Bin1::appendEmptyTypeSegment(file);
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
        ReasoningGrid loaded = readBin1Airg(bytes);
        loaded.m_alignedBin1 = bytes[5] == 0x10 && bytes[6] == 2;
        const size_t coreEnd = Bin1::headerSize + Bin1::readDataSize(bytes, airgRootSize);
        if (bytes.size() - coreEnd >= 8 && readLittleEndian<uint32_t>(bytes, coreEnd) == Bin1::relocationSegmentId) {
            const size_t sectionSize = readLittleEndian<uint32_t>(bytes, coreEnd + 4);
            if (sectionSize > bytes.size() - coreEnd - 8) {
                throw std::runtime_error("AIRG BIN1 relocation section is truncated.");
            }
            const size_t extensionOffset = coreEnd + 8 + sectionSize;
            loaded.m_bin1Extensions.assign(bytes.begin() + static_cast<std::ptrdiff_t>(extensionOffset), bytes.end());
        }
        *this = std::move(loaded);
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
