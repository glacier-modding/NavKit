#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/NavKit/render/CoreProfileDraw.h"
#include "../../include/NavKit/util/ErrorHandler.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/UiIds.h"
#include <nfd.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <cpptrace/from_current.hpp>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"

namespace {
    constexpr size_t crmdHeaderSize = 0x10;
    constexpr size_t crmdRootSize = 0xf0;
    constexpr uint64_t nullCrmdPointer = std::numeric_limits<uint64_t>::max();

    void ensureCrmdHeapArenaInitialized() {
        static std::once_flag initialization;
        std::call_once(initialization, [] {
            ZHMArena* heapArena = ZHMArenas::GetHeapArena();
            if (heapArena->m_Id != ZHMHeapArenaId) {
                heapArena->Initialize(ZHMHeapArenaId);
            }
        });
    }

    template <typename T> T readCrmdValue(const std::vector<uint8_t>& bytes, const size_t offset) {
        static_assert(std::is_arithmetic_v<T>);
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
            throw std::runtime_error("Unexpected end of CRMD data.");
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

    struct CrmdArray {
        size_t offset;
        size_t count;
    };

    CrmdArray getCrmdArray(const std::vector<uint8_t>& bytes, const size_t pointerOffset, const size_t coreSize,
        const size_t elementSize) {
        const uint64_t begin = readCrmdValue<uint64_t>(bytes, pointerOffset);
        const uint64_t end = readCrmdValue<uint64_t>(bytes, pointerOffset + sizeof(uint64_t));
        const uint64_t allocationEnd = readCrmdValue<uint64_t>(bytes, pointerOffset + 2 * sizeof(uint64_t));
        if (begin == nullCrmdPointer && end == begin && allocationEnd == begin) {
            return {0, 0};
        }
        if (end < begin || allocationEnd < end || begin > coreSize || end > coreSize || allocationEnd > coreSize) {
            throw std::runtime_error("CRMD array pointers are invalid.");
        }
        const uint64_t size = end - begin;
        if (size != 0 && begin < crmdRootSize) {
            throw std::runtime_error("CRMD array points into its root object.");
        }
        if (size % elementSize != 0) {
            throw std::runtime_error("CRMD array size does not match its element type.");
        }
        if (begin > std::numeric_limits<size_t>::max() - crmdHeaderSize ||
            size > std::numeric_limits<size_t>::max() - crmdHeaderSize - begin) {
            throw std::runtime_error("CRMD array extends beyond the end of the file.");
        }
        const size_t dataOffset = crmdHeaderSize + static_cast<size_t>(begin);
        if (dataOffset > bytes.size() || size > bytes.size() - dataOffset) {
            throw std::runtime_error("CRMD array extends beyond the end of the file.");
        }
        return {dataOffset, static_cast<size_t>(size / elementSize)};
    }

    glm::vec3 toRenderCoordinates(const float4& value) {
        return {value.x, value.z, -value.y};
    }

    glm::mat4 crowdMapTransform(const SCrowdMapData& data) {
        const auto axisLength = [](const float4& axis) {
            return std::abs(axis.x) + std::abs(axis.y) + std::abs(axis.z);
        };
        const auto axisIsFinite = [](const float4& axis) {
            return std::isfinite(axis.x) && std::isfinite(axis.y) && std::isfinite(axis.z);
        };
        if (axisLength(data.m_mObjectToWorld.XAxis) == 0.0f && axisLength(data.m_mObjectToWorld.YAxis) == 0.0f &&
            axisLength(data.m_mObjectToWorld.ZAxis) == 0.0f) {
            return glm::mat4(1.0f);
        }
        if (!axisIsFinite(data.m_mObjectToWorld.XAxis) || !axisIsFinite(data.m_mObjectToWorld.YAxis) ||
            !axisIsFinite(data.m_mObjectToWorld.ZAxis) || !axisIsFinite(data.m_mObjectToWorld.Trans)) {
            return glm::mat4(1.0f);
        }
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(toRenderCoordinates(data.m_mObjectToWorld.XAxis), 0.0f);
        model[1] = glm::vec4(toRenderCoordinates(data.m_mObjectToWorld.YAxis), 0.0f);
        model[2] = glm::vec4(toRenderCoordinates(data.m_mObjectToWorld.ZAxis), 0.0f);
        model[3] = glm::vec4(toRenderCoordinates(data.m_mObjectToWorld.Trans), 1.0f);
        return model;
    }

    void addCellRectangle(std::vector<CoreProfileVertex>& vertices, const float x, const float y, const float width,
        const float height, const glm::vec4& color, const float zOffset = 0.01f) {
        const float left = x;
        const float right = x + width;
        const float top = y;
        const float bottom = y + height;
        constexpr glm::vec3 normal(0.0f, 0.0f, 1.0f);
        const glm::vec3 topLeft(left, top, zOffset);
        const glm::vec3 topRight(right, top, zOffset);
        const glm::vec3 bottomRight(right, bottom, zOffset);
        const glm::vec3 bottomLeft(left, bottom, zOffset);
        vertices.insert(vertices.end(),
            {{topLeft, normal, color, {}}, {topRight, normal, color, {}}, {bottomRight, normal, color, {}},
                {topLeft, normal, color, {}}, {bottomRight, normal, color, {}}, {bottomLeft, normal, color, {}}});
    }

    uint8_t getPackedCellValue(const TArray<SCrowdCells>& cells, const size_t index) {
        const size_t groupIndex = index / 128;
        return groupIndex < cells.size() ? cells[groupIndex].m_cells[index % 128] : 0xff;
    }

    std::unique_ptr<SCrowdMapData> readCrowdMap(const std::string& fileName) {
        std::ifstream stream(fileName, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("Could not open CRMD file for reading: " + fileName);
        }
        stream.seekg(0, std::ios::end);
        const std::istream::pos_type end = stream.tellg();
        if (end == std::istream::pos_type(-1) || static_cast<uint64_t>(end) > std::numeric_limits<size_t>::max()) {
            throw std::runtime_error("Could not determine the CRMD file size.");
        }
        if (static_cast<uint64_t>(end) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            throw std::length_error("CRMD file is too large to read.");
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(end));
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            throw std::runtime_error("Failed to read CRMD file: " + fileName);
        }
        if (bytes.size() < crmdHeaderSize || std::memcmp(bytes.data(), "BIN1", 4) != 0) {
            throw std::runtime_error("CRMD file does not have a valid BIN1 header.");
        }
        const uint32_t coreSize = (static_cast<uint32_t>(bytes[8]) << 24) | (static_cast<uint32_t>(bytes[9]) << 16) |
            (static_cast<uint32_t>(bytes[10]) << 8) | static_cast<uint32_t>(bytes[11]);
        if (coreSize < crmdRootSize || coreSize > bytes.size() - crmdHeaderSize) {
            throw std::runtime_error("CRMD BIN1 data segment has an invalid size.");
        }

        const size_t root = crmdHeaderSize;
        auto loaded = std::make_unique<SCrowdMapData>();
        loaded->m_nVersion = readCrmdValue<uint32_t>(bytes, root);
        loaded->m_vCellConsts.x = readCrmdValue<float>(bytes, root + 4);
        loaded->m_vCellConsts.y = readCrmdValue<float>(bytes, root + 8);
        loaded->m_vCellConsts.z = readCrmdValue<float>(bytes, root + 12);
        loaded->m_vCellConsts.w = readCrmdValue<float>(bytes, root + 16);
        loaded->m_nGridSizeX = readCrmdValue<uint32_t>(bytes, root + 0x14);
        loaded->m_nGridSizeY = readCrmdValue<uint32_t>(bytes, root + 0x18);
        loaded->m_nCellSizeX = readCrmdValue<uint32_t>(bytes, root + 0x1c);
        loaded->m_nCellSizeY = readCrmdValue<uint32_t>(bytes, root + 0x20);

        const CrmdArray gridMap = getCrmdArray(bytes, root + 0x28, coreSize, sizeof(ZCrowdGridPoint));
        const CrmdArray cellFlags = getCrmdArray(bytes, root + 0x48, coreSize, sizeof(SCrowdCells));
        const CrmdArray cellGroups = getCrmdArray(bytes, root + 0x60, coreSize, sizeof(SCrowdCells));
        const CrmdArray flowChannels = getCrmdArray(bytes, root + 0x80, coreSize, sizeof(SCrowdFlowChannel));
        loaded->m_fGridMapHeightOffsetsScale = readCrmdValue<float>(bytes, root + 0x40);
        loaded->m_fGridMapHeightOffsetsBias = readCrmdValue<float>(bytes, root + 0x44);
        loaded->m_nGroupFlowStartChannel = readCrmdValue<int32_t>(bytes, root + 0x78);
        loaded->m_nGridCellSize = readCrmdValue<float>(bytes, root + 0x98);

        auto readFloat4 = [&bytes](float4& target, const size_t offset) {
            target.x = readCrmdValue<float>(bytes, offset);
            target.y = readCrmdValue<float>(bytes, offset + 4);
            target.z = readCrmdValue<float>(bytes, offset + 8);
            target.w = readCrmdValue<float>(bytes, offset + 12);
        };
        readFloat4(loaded->m_mObjectToWorld.XAxis, root + 0xa0);
        readFloat4(loaded->m_mObjectToWorld.YAxis, root + 0xb0);
        readFloat4(loaded->m_mObjectToWorld.ZAxis, root + 0xc0);
        readFloat4(loaded->m_mObjectToWorld.Trans, root + 0xd0);
        loaded->m_vGlobalSize.x = readCrmdValue<float>(bytes, root + 0xe0);
        loaded->m_vGlobalSize.y = readCrmdValue<float>(bytes, root + 0xe4);
        loaded->m_vGlobalSize.z = readCrmdValue<float>(bytes, root + 0xe8);
        loaded->m_nGridGeneratorOffset = readCrmdValue<float>(bytes, root + 0xec);

        if (loaded->m_nGridSizeX == 0 || loaded->m_nGridSizeY == 0 ||
            loaded->m_nGridSizeX > std::numeric_limits<size_t>::max() / loaded->m_nGridSizeY) {
            throw std::runtime_error("CRMD grid dimensions are invalid.");
        }
        ensureCrmdHeapArenaInitialized();
        if (gridMap.count != 0) {
            loaded->m_GridMap.resize(gridMap.count);
            for (size_t i = 0; i < gridMap.count; ++i) {
                const size_t offset = gridMap.offset + i * sizeof(ZCrowdGridPoint);
                loaded->m_GridMap[i].m_nHeightOffset = readCrmdValue<int16_t>(bytes, offset);
                loaded->m_GridMap[i].m_nOnNavGrid = bytes[offset + 2];
            }
        }
        if (cellFlags.count != 0) {
            loaded->m_CellFlags.resize(cellFlags.count);
            for (size_t i = 0; i < cellFlags.count; ++i) {
                std::memcpy(loaded->m_CellFlags[i].m_cells.begin(),
                    bytes.data() + cellFlags.offset + i * sizeof(SCrowdCells), sizeof(SCrowdCells));
            }
        }
        if (cellGroups.count != 0) {
            loaded->m_CellGroups.resize(cellGroups.count);
            for (size_t i = 0; i < cellGroups.count; ++i) {
                std::memcpy(loaded->m_CellGroups[i].m_cells.begin(),
                    bytes.data() + cellGroups.offset + i * sizeof(SCrowdCells), sizeof(SCrowdCells));
            }
        }
        if (flowChannels.count != 0) {
            loaded->m_aFlowChannels.resize(flowChannels.count);
            for (size_t i = 0; i < flowChannels.count; ++i) {
                const size_t channelOffset = flowChannels.offset + i * sizeof(SCrowdFlowChannel);
                const CrmdArray vectorIndices = getCrmdArray(bytes, channelOffset, coreSize, sizeof(uint8_t));
                const CrmdArray costs = getCrmdArray(bytes, channelOffset + 0x18, coreSize, sizeof(uint16_t));
                if (vectorIndices.count != 0) {
                    loaded->m_aFlowChannels[i].m_aFlowVectorIndex.resize(vectorIndices.count);
                    std::memcpy(loaded->m_aFlowChannels[i].m_aFlowVectorIndex.begin(),
                        bytes.data() + vectorIndices.offset, vectorIndices.count);
                }
                if (costs.count != 0) {
                    loaded->m_aFlowChannels[i].m_aFlowCost.resize(costs.count);
                    for (size_t j = 0; j < costs.count; ++j) {
                        loaded->m_aFlowChannels[i].m_aFlowCost[j] =
                            readCrmdValue<uint16_t>(bytes, costs.offset + j * sizeof(uint16_t));
                    }
                }
            }
        }

        return loaded;
    }
} // namespace

Crmd::Crmd() : crmdLoaded(false), loading(false), showCrmd(true) {}

wxDialog* Crmd::hCrmdDialog = nullptr;
std::string Crmd::selectedRpkgCrmd{};
std::map<std::string, std::string> Crmd::crmdHashIoiStringMap;
std::mutex Crmd::crmdHashIoiStringMapMutex;
ZHMArena ZHMArenas::g_Arenas[8];

void Crmd::renderCrowdMapData() const {
    if (loading || !crmdLoaded || !crowdMapData || crowdMapData->m_nGridSizeX == 0 || crowdMapData->m_nGridSizeY == 0) {
        return;
    }
    const SCrowdMapData& data = *crowdMapData;
    const size_t gridWidth = data.m_nGridSizeX;
    const size_t gridHeight = data.m_nGridSizeY;
    if (gridWidth > std::numeric_limits<size_t>::max() / gridHeight) {
        return;
    }
    const size_t mapCount = gridWidth * gridHeight;
    const size_t cellGridWidth = data.m_nCellSizeX;
    const size_t cellGridHeight = data.m_nCellSizeY;
    if (cellGridWidth == 0 || cellGridHeight == 0 ||
        cellGridWidth > std::numeric_limits<size_t>::max() / cellGridHeight) {
        return;
    }
    const size_t cellCount = cellGridWidth * cellGridHeight;
    const auto packedCellCount = [](const size_t count) {
        return count > std::numeric_limits<size_t>::max() / 128 ? std::numeric_limits<size_t>::max() : count * 128;
    };
    const size_t renderCellCount = std::min(
        cellCount, std::max(packedCellCount(data.m_CellFlags.size()), packedCellCount(data.m_CellGroups.size())));
    const size_t renderMapCount = std::min(mapCount, data.m_GridMap.size());
    float cellWidth = data.m_nGridCellSize;
    float cellHeight = data.m_nGridCellSize;
    if (!(cellWidth > 0.0f) || !std::isfinite(cellWidth)) {
        cellWidth = data.m_vGlobalSize.x > 0.0f ? data.m_vGlobalSize.x / static_cast<float>(gridWidth) : 1.0f;
    }
    if (!(cellHeight > 0.0f) || !std::isfinite(cellHeight)) {
        cellHeight = data.m_vGlobalSize.z > 0.0f ? data.m_vGlobalSize.z / static_cast<float>(gridHeight) : 1.0f;
    }
    if (!(cellWidth > 0.0f) || !(cellHeight > 0.0f) || !std::isfinite(cellWidth) || !std::isfinite(cellHeight)) {
        return;
    }

    constexpr std::array<glm::vec4, 8> groupColors = {glm::vec4(0.35f, 0.85f, 0.45f, 0.48f),
        glm::vec4(0.95f, 0.68f, 0.25f, 0.48f), glm::vec4(0.7f, 0.45f, 0.9f, 0.48f), glm::vec4(0.2f, 0.8f, 0.8f, 0.48f),
        glm::vec4(0.95f, 0.4f, 0.65f, 0.48f), glm::vec4(0.75f, 0.8f, 0.25f, 0.48f),
        glm::vec4(0.4f, 0.55f, 0.95f, 0.48f), glm::vec4(0.95f, 0.5f, 0.25f, 0.48f)};
    std::vector<CoreProfileVertex> rectangles;
    if (renderCellCount > rectangles.max_size() / 12 || renderMapCount > rectangles.max_size() / 6 ||
        renderCellCount * 12 > rectangles.max_size() - renderMapCount * 6) {
        return;
    }
    rectangles.reserve(renderCellCount * 12 + renderMapCount * 6);
    for (size_t i = 0; i < renderMapCount; ++i) {
        const size_t xIndex = i % gridWidth;
        const size_t yIndex = i / gridWidth;
        const float x = static_cast<float>(xIndex) * cellWidth;
        const float y = static_cast<float>(yIndex) * cellHeight;
        const uint8_t onNavGrid = data.m_GridMap[i].m_nOnNavGrid;
        if (onNavGrid == 0) {
            continue;
        }
        const glm::vec4 mapColor =
            onNavGrid != 0 ? glm::vec4(0.2f, 0.45f, 0.9f, 0.3f) : glm::vec4(0.12f, 0.2f, 0.35f, 0.22f);
        addCellRectangle(rectangles, x, y, cellWidth, cellHeight, mapColor, -0.2);
    }

    for (size_t i = 0; i < renderCellCount; ++i) {
        const size_t xIndex = i % cellGridWidth;
        const size_t yIndex = i / cellGridWidth;
        const float x = static_cast<float>(xIndex) * cellWidth;
        const float y = static_cast<float>(yIndex) * cellHeight;
        const uint8_t flags = getPackedCellValue(data.m_CellFlags, i);
        if (flags != 0xff) {
            constexpr uint8_t restrictedMask = static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_RESTRICTED_A) |
                static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_RESTRICTED_B) |
                static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_RESTRICTED_C);
            glm::vec4 flagColor(0.5f, 0.45f, 0.2f, 0.35f);
            constexpr uint8_t teleportMask = static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_TELEPORT_IN) |
                static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_TELEPORT_OUT);
            if ((flags & teleportMask) != 0) {
                flagColor = glm::vec4(1.0f, 0.35f, 0.8f, 0.5f);
            } else if ((flags & restrictedMask) != 0) {
                flagColor = glm::vec4(0.95f, 0.25f, 0.2f, 0.48f);
            } else if ((flags & static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_WALKABLE)) != 0) {
                flagColor = glm::vec4(0.2f, 0.9f, 0.35f, 0.35f);
            } else {
                continue;
            }
            addCellRectangle(rectangles, x, y, cellWidth, cellHeight, flagColor);
        }

        const uint8_t group = getPackedCellValue(data.m_CellGroups, i);
        if (group != 0xff) {
            const float insetX = cellWidth * 0.2f;
            const float insetY = cellHeight * 0.2f;
            addCellRectangle(rectangles, x + insetX, y + insetY, cellWidth - 2.0f * insetX, cellHeight - 2.0f * insetY,
                groupColors[group % groupColors.size()], 0.04f);
        }
    }

    Renderer& renderer = Renderer::getInstance();
    const glm::mat4 model = crowdMapTransform(data);
    drawCoreProfileVertices(rectangles, GL_TRIANGLES, model, renderer.view, renderer.projection);

    constexpr std::array<glm::vec4, 8> flowChannelColors = {glm::vec4(1.0f, 0.65f, 0.9f, 0.95f),
        glm::vec4(1.0f, 0.72f, 0.25f, 0.95f), glm::vec4(0.35f, 0.85f, 1.0f, 0.95f), glm::vec4(0.95f, 0.9f, 0.3f, 0.95f),
        glm::vec4(0.75f, 0.55f, 1.0f, 0.95f), glm::vec4(0.4f, 1.0f, 0.55f, 0.95f), glm::vec4(1.0f, 0.45f, 0.35f, 0.95f),
        glm::vec4(0.45f, 0.95f, 0.85f, 0.95f)};
    constexpr glm::vec3 arrowNormal(0.0f, 0.0f, 1.0f);
    const float arrowLength = std::min(cellWidth, cellHeight) * 0.38f;
    const float arrowHeadLength = arrowLength * 0.25f;
    const float arrowHeadWidth = arrowLength * 0.18f;
    for (size_t channelIndex = 0; channelIndex < data.m_aFlowChannels.size(); ++channelIndex) {
        const SCrowdFlowChannel& channel = data.m_aFlowChannels[channelIndex];
        const glm::vec4 arrowColor = flowChannelColors[channelIndex % flowChannelColors.size()];
        const size_t vectorCount = std::min(cellCount, channel.m_aFlowVectorIndex.size());
        std::vector<CoreProfileVertex> arrows;
        if (vectorCount > arrows.max_size() / 6) {
            continue;
        }
        arrows.reserve(vectorCount * 6);
        for (size_t i = 0; i < vectorCount; ++i) {
            const uint8_t vectorIndex = channel.m_aFlowVectorIndex[i];
            const uint8_t flags = getPackedCellValue(data.m_CellFlags, i);
            if (vectorIndex == 0xff || flags == 0xff ||
                (flags & static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_WALKABLE)) == 0) {
                continue;
            }
            const uint8_t reversedIndex = static_cast<uint8_t>(vectorIndex + 128);
            const float angle = static_cast<float>(reversedIndex) * (2.0f * std::numbers::pi_v<float> / 256.0f);
            const glm::vec2 direction(std::cos(angle), std::sin(angle));
            const glm::vec2 perpendicular(-direction.y, direction.x);
            const glm::vec2 center((static_cast<float>(i % cellGridWidth) + 0.5f) * cellWidth,
                (static_cast<float>(i / cellGridWidth) + 0.5f) * cellHeight);
            const glm::vec2 tip = center + direction * arrowLength;
            const glm::vec2 headBase = tip - direction * arrowHeadLength;
            const glm::vec2 leftWing = headBase + perpendicular * arrowHeadWidth;
            const glm::vec2 rightWing = headBase - perpendicular * arrowHeadWidth;
            arrows.insert(arrows.end(),
                {{glm::vec3(center, 0.06f), arrowNormal, arrowColor, {}},
                    {glm::vec3(tip, 0.06f), arrowNormal, arrowColor, {}},
                    {glm::vec3(tip, 0.06f), arrowNormal, arrowColor, {}},
                    {glm::vec3(leftWing, 0.06f), arrowNormal, arrowColor, {}},
                    {glm::vec3(tip, 0.06f), arrowNormal, arrowColor, {}},
                    {glm::vec3(rightWing, 0.06f), arrowNormal, arrowColor, {}}});
        }
        drawCoreProfileVertices(arrows, GL_LINES, model, renderer.view, renderer.projection);
    }
}
void Crmd::loadCrmdFromFile(const std::string& fileName) {
    Logger::log(NK_INFO, ("Loading Crmd from file: " + fileName).c_str());
    setLastLoadFileName(fileName.c_str());
    std::string extension = loadCrmdName.substr(loadCrmdName.length() - 4, loadCrmdName.length());
    std::ranges::transform(extension, extension.begin(), ::toupper);

    if (extension == "CRMD") {
        std::string msg = "Loading Crmd file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        backgroundWorker.emplace(&Crmd::loadCrowdMapData, this, lastLoadCrmdFile);
    }
}
void Crmd::loadCrowdMapData(const std::string& fileName) {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Crmd from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.c_str());
    const auto start = std::chrono::high_resolution_clock::now();
    loading = true;
    CPPTRACE_TRY {
        std::unique_ptr<SCrowdMapData> loadedData = readCrowdMap(fileName);
        delete crowdMapData;
        crowdMapData = loadedData.release();
    }
    CPPTRACE_CATCH(const std::exception& e) {
        loading = false;
        Menu::updateMenuState();
        ErrorHandler::openErrorDialog("Error loading Crmd file '" + std::string(fileName) +
            "': " + std::string(e.what()) + "\n\nStack Trace:\n" + cpptrace::from_current_exception().to_string());
        msg = "Error loading Crmd file '";
        msg += fileName;
        msg += "': ";
        msg += e.what();
        msg += " Stack trace: ";
        msg += cpptrace::from_current_exception().to_string();
        Logger::log(NK_ERROR, msg.c_str());
        return;
    }
    catch (...) {
        loading = false;
        Menu::updateMenuState();
        msg = "Invalid Crmd file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_ERROR, msg.c_str());
        return;
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished loading Crmd in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.c_str());
    loading = false;
    crmdLoaded = true;
    Menu::updateMenuState();
}
void Crmd::handleOpenCrmdClicked() {
    if (const char* fileName = openLoadCrmdFileDialog()) {
        loadedCrmdText = fileName;
        loadCrmdFromFile(fileName);
    }
}
void Crmd::setLastLoadFileName(const char* fileName) {
    Logger::log(NK_INFO, ("Setting last Crmd loaded file name: " + std::string(fileName)).c_str());
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadCrmdName = fileName;
        lastLoadCrmdFile = loadCrmdName.data();
        loadCrmdName = loadCrmdName.substr(loadCrmdName.find_last_of("/\\") + 1);
    }
}
void Crmd::clear() {
    delete crowdMapData;
    crowdMapData = new SCrowdMapData();
    crmdLoaded = false;
    loading = false;
    loadedCrmdText.clear();
    loadCrmdName = "Load Crmd";
    lastLoadCrmdFile = loadCrmdName;
}

void Crmd::updateCrmdDialogControls(wxDialog* dialog) {
    auto* choice = static_cast<wxChoice*>(dialog->FindWindow(IDC_COMBOBOX_CRMD));
    choice->Clear();
    std::vector<std::pair<std::string, std::string>> entries(crmdHashIoiStringMap.begin(), crmdHashIoiStringMap.end());
    std::ranges::sort(entries,
        [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
    for (const auto& [hash, ioiString] : entries) {
        const std::string& display = ioiString.empty() ? hash : ioiString;
        choice->Append(wxString::FromUTF8(display));
        if (selectedRpkgCrmd.empty()) {
            selectedRpkgCrmd = display;
        }
    }
    if (!entries.empty()) {
        choice->SetSelection(0);
    }
}
void Crmd::showExtractCrmdDialog() {
    if (hCrmdDialog) {
        hCrmdDialog->Raise();
        return;
    }
    auto* dialog =
        new wxDialog(getMainFrame(), wxID_ANY, "Load Crmd from resource package", wxDefaultPosition, wxSize(750, 150));
    hCrmdDialog = dialog;
    auto* choice = new wxChoice(dialog, IDC_COMBOBOX_CRMD);
    updateCrmdDialogControls(dialog);
    auto* buttons = new wxStdDialogButtonSizer();
    auto* open = new wxButton(dialog, wxID_OK, "Open Crmd");
    auto* cancel = new wxButton(dialog, wxID_CANCEL, "Cancel");
    buttons->AddButton(open);
    buttons->AddButton(cancel);
    buttons->Realize();
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(
        new wxStaticText(dialog, wxID_ANY, "Crmd file in resource packages (Rpkg) to extract and load"), 0, wxALL, 10);
    sizer->Add(choice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
    dialog->SetSizer(sizer);
    choice->Bind(wxEVT_CHOICE, [](wxCommandEvent& event) { selectedRpkgCrmd = event.GetString().ToStdString(); });
    open->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) {
        for (auto& [hash, ioiString] : crmdHashIoiStringMap) {
            if (hash == selectedRpkgCrmd) {
                getInstance().loadedCrmdText = hash;
            } else if (ioiString == selectedRpkgCrmd) {
                getInstance().loadedCrmdText = ioiString;
            } else {
                continue;
            }
            extractCrmdFromRpkgs(hash);
        }
        dialog->Close();
    });
    cancel->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) { dialog->Close(); });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hCrmdDialog == dialog) {
            hCrmdDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
}

void Crmd::extractCrmdFromRpkgs(const std::string& hash) {
    if (!Rpkg::extractResourcesFromRpkgs({hash}, CRMD)) {
        const std::string fileName =
            (std::filesystem::u8path(NavKitSettings::getInstance().outputFolder) / "crmd" / (hash + ".CRMD")).string();
        Logger::log(NK_INFO, ("Loading Crmd from file: " + fileName).c_str());
        getInstance().loadCrmdFromFile(fileName);
    }
}

char* Crmd::openLoadCrmdFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Crmd files", "crmd"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}
