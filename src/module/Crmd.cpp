#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/util/FilteredListPicker.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/model/Json.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/NavKit/render/CoreProfileDraw.h"
#include "../../include/NavKit/util/Bin1.h"
#include "../../include/NavKit/util/ErrorHandler.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/UiIds.h"
#include "../../include/RecastDemo/InputGeom.h"
#include "../../include/RecastDemo/MeshLoaderGlb.h"
#include <nfd.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <vector>
#include <cpptrace/from_current.hpp>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"

namespace {
    constexpr size_t crmdRootSize = 0xf0;

    void ensureCrmdHeapArenaInitialized() {
        static std::once_flag initialization;
        std::call_once(initialization, [] {
            ZHMArena* heapArena = ZHMArenas::GetHeapArena();
            if (heapArena->m_Id != ZHMHeapArenaId) {
                heapArena->Initialize(ZHMHeapArenaId);
            }
        });
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

    class CrowdGridHeights {
    public:
        CrowdGridHeights(const SCrowdMapData& data, const size_t pointsX, const size_t pointsY, const float spacingX,
            const float spacingY) :
            data(data), pointsX(pointsX), pointsY(pointsY), spacingX(spacingX), spacingY(spacingY) {}

        float at(const float x, const float y) const {
            const float gridX = std::clamp(x / spacingX, 0.0f, static_cast<float>(pointsX - 1));
            const float gridY = std::clamp(y / spacingY, 0.0f, static_cast<float>(pointsY - 1));
            const size_t x0 = static_cast<size_t>(gridX);
            const size_t y0 = static_cast<size_t>(gridY);
            const size_t x1 = std::min(x0 + 1, pointsX - 1);
            const size_t y1 = std::min(y0 + 1, pointsY - 1);
            const float fractionX = gridX - static_cast<float>(x0);
            const float fractionY = gridY - static_cast<float>(y0);
            float weightedHeight = 0.0f;
            float totalWeight = 0.0f;
            const auto accumulate = [&](const size_t pointX, const size_t pointY, const float weight) {
                const size_t index = pointY * pointsX + pointX;
                if (weight <= 0.0f || index >= data.m_GridMap.size() || data.m_GridMap[index].m_nOnNavGrid == 0) {
                    return;
                }
                weightedHeight += weight *
                    (static_cast<float>(data.m_GridMap[index].m_nHeightOffset) * data.m_fGridMapHeightOffsetsScale +
                        data.m_fGridMapHeightOffsetsBias);
                totalWeight += weight;
            };
            accumulate(x0, y0, (1.0f - fractionX) * (1.0f - fractionY));
            accumulate(x1, y0, fractionX * (1.0f - fractionY));
            accumulate(x0, y1, (1.0f - fractionX) * fractionY);
            accumulate(x1, y1, fractionX * fractionY);
            return totalWeight > 0.0f ? weightedHeight / totalWeight : 0.0f;
        }

    private:
        const SCrowdMapData& data;
        size_t pointsX;
        size_t pointsY;
        float spacingX;
        float spacingY;
    };

    void addCellRectangle(std::vector<CoreProfileVertex>& vertices, const CrowdGridHeights& heights, const float x,
        const float y, const float width, const float height, const glm::vec4& color, const float zOffset = 0.01f) {
        const float left = x;
        const float right = x + width;
        const float top = y;
        const float bottom = y + height;
        constexpr glm::vec3 normal(0.0f, 0.0f, 1.0f);
        const glm::vec3 topLeft(left, top, heights.at(left, top) + zOffset);
        const glm::vec3 topRight(right, top, heights.at(right, top) + zOffset);
        const glm::vec3 bottomRight(right, bottom, heights.at(right, bottom) + zOffset);
        const glm::vec3 bottomLeft(left, bottom, heights.at(left, bottom) + zOffset);
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
        const uint32_t dataSize = Bin1::readDataSize(bytes, crmdRootSize);
        const auto readArray = [&bytes, dataSize](const size_t pointerOffset, const size_t elementSize) {
            return Bin1::readArray(bytes, pointerOffset, dataSize, crmdRootSize, elementSize);
        };

        const size_t root = Bin1::headerSize;
        auto loaded = std::make_unique<SCrowdMapData>();
        loaded->m_nVersion = Bin1::readLittleEndian<uint32_t>(bytes, root);
        loaded->m_vCellConsts.x = Bin1::readLittleEndian<float>(bytes, root + 4);
        loaded->m_vCellConsts.y = Bin1::readLittleEndian<float>(bytes, root + 8);
        loaded->m_vCellConsts.z = Bin1::readLittleEndian<float>(bytes, root + 12);
        loaded->m_vCellConsts.w = Bin1::readLittleEndian<float>(bytes, root + 16);
        loaded->m_nGridSizeX = Bin1::readLittleEndian<uint32_t>(bytes, root + 0x14);
        loaded->m_nGridSizeY = Bin1::readLittleEndian<uint32_t>(bytes, root + 0x18);
        loaded->m_nCellSizeX = Bin1::readLittleEndian<uint32_t>(bytes, root + 0x1c);
        loaded->m_nCellSizeY = Bin1::readLittleEndian<uint32_t>(bytes, root + 0x20);

        const Bin1::Array gridMap = readArray(root + 0x28, sizeof(ZCrowdGridPoint));
        const Bin1::Array cellFlags = readArray(root + 0x48, sizeof(SCrowdCells));
        const Bin1::Array cellGroups = readArray(root + 0x60, sizeof(SCrowdCells));
        const Bin1::Array flowChannels = readArray(root + 0x80, sizeof(SCrowdFlowChannel));
        loaded->m_fGridMapHeightOffsetsScale = Bin1::readLittleEndian<float>(bytes, root + 0x40);
        loaded->m_fGridMapHeightOffsetsBias = Bin1::readLittleEndian<float>(bytes, root + 0x44);
        loaded->m_nGroupFlowStartChannel = Bin1::readLittleEndian<int32_t>(bytes, root + 0x78);
        loaded->m_nGridCellSize = Bin1::readLittleEndian<float>(bytes, root + 0x98);

        auto readFloat4 = [&bytes](float4& target, const size_t offset) {
            target.x = Bin1::readLittleEndian<float>(bytes, offset);
            target.y = Bin1::readLittleEndian<float>(bytes, offset + 4);
            target.z = Bin1::readLittleEndian<float>(bytes, offset + 8);
            target.w = Bin1::readLittleEndian<float>(bytes, offset + 12);
        };
        readFloat4(loaded->m_mObjectToWorld.XAxis, root + 0xa0);
        readFloat4(loaded->m_mObjectToWorld.YAxis, root + 0xb0);
        readFloat4(loaded->m_mObjectToWorld.ZAxis, root + 0xc0);
        readFloat4(loaded->m_mObjectToWorld.Trans, root + 0xd0);
        loaded->m_vGlobalSize.x = Bin1::readLittleEndian<float>(bytes, root + 0xe0);
        loaded->m_vGlobalSize.y = Bin1::readLittleEndian<float>(bytes, root + 0xe4);
        loaded->m_vGlobalSize.z = Bin1::readLittleEndian<float>(bytes, root + 0xe8);
        loaded->m_nGridGeneratorOffset = Bin1::readLittleEndian<float>(bytes, root + 0xec);

        if (loaded->m_nGridSizeX == 0 || loaded->m_nGridSizeY == 0 ||
            loaded->m_nGridSizeX > std::numeric_limits<size_t>::max() / loaded->m_nGridSizeY) {
            throw std::runtime_error("CRMD grid dimensions are invalid.");
        }
        ensureCrmdHeapArenaInitialized();
        if (gridMap.count != 0) {
            loaded->m_GridMap.resize(gridMap.count);
            for (size_t i = 0; i < gridMap.count; ++i) {
                const size_t offset = gridMap.offset + i * sizeof(ZCrowdGridPoint);
                loaded->m_GridMap[i].m_nHeightOffset = Bin1::readLittleEndian<int16_t>(bytes, offset);
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
                const Bin1::Array vectorIndices = readArray(channelOffset, sizeof(uint8_t));
                const Bin1::Array costs = readArray(channelOffset + 0x18, sizeof(uint16_t));
                if (vectorIndices.count != 0) {
                    loaded->m_aFlowChannels[i].m_aFlowVectorIndex.resize(vectorIndices.count);
                    std::memcpy(loaded->m_aFlowChannels[i].m_aFlowVectorIndex.begin(),
                        bytes.data() + vectorIndices.offset, vectorIndices.count);
                }
                if (costs.count != 0) {
                    loaded->m_aFlowChannels[i].m_aFlowCost.resize(costs.count);
                    for (size_t j = 0; j < costs.count; ++j) {
                        loaded->m_aFlowChannels[i].m_aFlowCost[j] =
                            Bin1::readLittleEndian<uint16_t>(bytes, costs.offset + j * sizeof(uint16_t));
                    }
                }
            }
        }

        return loaded;
    }

    void putFloat4(Bin1::Writer& writer, const size_t offset, const float4& value) {
        writer.put(offset, value.x);
        writer.put(offset + 4, value.y);
        writer.put(offset + 8, value.z);
        writer.put(offset + 12, value.w);
    }

    void appendCells(Bin1::Writer& writer, const size_t pointerOffset, const TArray<SCrowdCells>& cells) {
        writer.appendArray(pointerOffset, cells.size(), sizeof(SCrowdCells), [&] {
            for (const SCrowdCells& block : cells) {
                writer.data.insert(writer.data.end(), block.m_cells.begin(), block.m_cells.end());
            }
        });
    }

    std::vector<uint8_t> serializeCrowdMap(const SCrowdMapData& crowdMap) {
        Bin1::Writer writer(crmdRootSize);
        writer.put(0x00, crowdMap.m_nVersion);
        writer.put(0x04, crowdMap.m_vCellConsts.x);
        writer.put(0x08, crowdMap.m_vCellConsts.y);
        writer.put(0x0c, crowdMap.m_vCellConsts.z);
        writer.put(0x10, crowdMap.m_vCellConsts.w);
        writer.put(0x14, crowdMap.m_nGridSizeX);
        writer.put(0x18, crowdMap.m_nGridSizeY);
        writer.put(0x1c, crowdMap.m_nCellSizeX);
        writer.put(0x20, crowdMap.m_nCellSizeY);
        writer.put(0x40, crowdMap.m_fGridMapHeightOffsetsScale);
        writer.put(0x44, crowdMap.m_fGridMapHeightOffsetsBias);
        writer.put(0x78, crowdMap.m_nGroupFlowStartChannel);
        writer.put(0x98, crowdMap.m_nGridCellSize);
        putFloat4(writer, 0xa0, crowdMap.m_mObjectToWorld.XAxis);
        putFloat4(writer, 0xb0, crowdMap.m_mObjectToWorld.YAxis);
        putFloat4(writer, 0xc0, crowdMap.m_mObjectToWorld.ZAxis);
        putFloat4(writer, 0xd0, crowdMap.m_mObjectToWorld.Trans);
        writer.put(0xe0, crowdMap.m_vGlobalSize.x);
        writer.put(0xe4, crowdMap.m_vGlobalSize.y);
        writer.put(0xe8, crowdMap.m_vGlobalSize.z);
        writer.put(0xec, crowdMap.m_nGridGeneratorOffset);

        writer.appendArray(0x28, crowdMap.m_GridMap.size(), alignof(int16_t), [&] {
            for (const ZCrowdGridPoint& point : crowdMap.m_GridMap) {
                writer.append(point.m_nHeightOffset);
                writer.append(point.m_nOnNavGrid);
                writer.append(static_cast<uint8_t>(0));
            }
        });
        appendCells(writer, 0x48, crowdMap.m_CellFlags);
        appendCells(writer, 0x60, crowdMap.m_CellGroups);

        const TArray<SCrowdFlowChannel>& channels = crowdMap.m_aFlowChannels;
        const size_t channelsBegin = writer.appendArray(0x80, channels.size(), 8,
            [&] { writer.data.resize(writer.data.size() + channels.size() * sizeof(SCrowdFlowChannel), 0); });
        for (size_t i = 0; i < channels.size(); ++i) {
            const size_t channelOffset = channelsBegin + i * sizeof(SCrowdFlowChannel);
            const SCrowdFlowChannel& channel = channels[i];
            writer.appendArray(channelOffset, channel.m_aFlowVectorIndex.size(), 1, [&] {
                for (const uint8_t direction : channel.m_aFlowVectorIndex) {
                    writer.append(direction);
                }
            });
            writer.appendArray(channelOffset + 0x18, channel.m_aFlowCost.size(), alignof(uint16_t), [&] {
                for (const uint16_t cost : channel.m_aFlowCost) {
                    writer.append(cost);
                }
            });
        }
        std::vector<uint8_t> file = writer.finish(0x80, 2);
        Bin1::appendEmptyTypeSegment(file);
        return file;
    }

    std::unique_ptr<SCrowdMapData> toCrowdMapData(const CrowdMapBuilder::CrowdMap& map) {
        ensureCrmdHeapArenaInitialized();
        auto data = std::make_unique<SCrowdMapData>();
        data->m_nVersion = CrowdMapBuilder::crowdMapVersion;
        data->m_vCellConsts.x = static_cast<float>(map.cellsX);
        data->m_vCellConsts.y = static_cast<float>(map.cellsY);
        data->m_vCellConsts.z = map.cellSize;
        data->m_vCellConsts.w = static_cast<float>(1.0 / map.cellSize);
        data->m_nGridSizeX = map.cellsX + 1;
        data->m_nGridSizeY = map.cellsY + 1;
        data->m_nCellSizeX = map.cellsX;
        data->m_nCellSizeY = map.cellsY;
        data->m_fGridMapHeightOffsetsScale = map.heightScale;
        data->m_fGridMapHeightOffsetsBias = map.heightBias;
        data->m_nGroupFlowStartChannel = map.groupFlowStartChannel;
        data->m_nGridCellSize = map.cellSize;
        data->m_nGridGeneratorOffset = map.generatorOffset;
        data->m_vGlobalSize.x = map.globalSize.x;
        data->m_vGlobalSize.y = map.globalSize.y;
        data->m_vGlobalSize.z = map.globalSize.z;
        const auto setFloat4 = [](float4& target, const glm::vec4& value) {
            target.x = value.x;
            target.y = value.y;
            target.z = value.z;
            target.w = value.w;
        };
        setFloat4(data->m_mObjectToWorld.XAxis, map.objectToWorld[0]);
        setFloat4(data->m_mObjectToWorld.YAxis, map.objectToWorld[1]);
        setFloat4(data->m_mObjectToWorld.ZAxis, map.objectToWorld[2]);
        setFloat4(data->m_mObjectToWorld.Trans, map.objectToWorld[3]);

        data->m_GridMap.resize(map.gridMap.size());
        for (size_t i = 0; i < map.gridMap.size(); ++i) {
            data->m_GridMap[i].m_nHeightOffset = map.gridMap[i].heightOffset;
            data->m_GridMap[i].m_nOnNavGrid = map.gridMap[i].onNavGrid;
        }
        const auto setCells = [](TArray<SCrowdCells>& target, const std::vector<uint8_t>& cells) {
            target.resize(cells.size() / CrowdMapBuilder::cellsPerBlock);
            for (size_t block = 0; block < target.size(); ++block) {
                std::memcpy(target[block].m_cells.begin(), cells.data() + block * CrowdMapBuilder::cellsPerBlock,
                    CrowdMapBuilder::cellsPerBlock);
            }
        };
        setCells(data->m_CellFlags, map.cellFlags);
        setCells(data->m_CellGroups, map.cellGroups);
        data->m_aFlowChannels.resize(map.flowChannels.size());
        for (size_t c = 0; c < map.flowChannels.size(); ++c) {
            const CrowdMapBuilder::FlowChannel& source = map.flowChannels[c];
            SCrowdFlowChannel& channel = data->m_aFlowChannels[c];
            if (!source.directions.empty()) {
                channel.m_aFlowVectorIndex.resize(source.directions.size());
                std::memcpy(channel.m_aFlowVectorIndex.begin(), source.directions.data(), source.directions.size());
            }
            if (!source.costs.empty()) {
                channel.m_aFlowCost.resize(source.costs.size());
                for (size_t i = 0; i < source.costs.size(); ++i) {
                    channel.m_aFlowCost[i] = source.costs[i];
                }
            }
        }
        return data;
    }

    std::vector<glm::dvec3> collectNavmeshTriangles(const NavPower::NavMesh& navMesh) {
        std::vector<glm::dvec3> triangles;
        for (const auto& section : navMesh.m_aSections) {
            for (const auto& navGraph : section.m_aNavGraphs) {
                for (const auto& area : navGraph.m_areas) {
                    if (area.m_edges.size() < 3) {
                        continue;
                    }
                    const NavPower::Binary::Edge* first = area.m_edges.front();
                    for (size_t i = 1; i + 1 < area.m_edges.size(); ++i) {
                        for (const NavPower::Binary::Edge* edge :
                            std::array<const NavPower::Binary::Edge*, 3>{first, area.m_edges[i], area.m_edges[i + 1]}) {
                            triangles.emplace_back(edge->m_pos.X, edge->m_pos.Y, edge->m_pos.Z);
                        }
                    }
                }
            }
        }
        return triangles;
    }

    std::string resourceFileName(const std::string& ioiString) {
        const size_t separator = ioiString.find_last_of('/');
        std::string fileName = separator == std::string::npos ? ioiString : ioiString.substr(separator + 1);
        if (const size_t pathEnd = fileName.find(']'); pathEnd != std::string::npos) {
            fileName.erase(pathEnd);
        }
        return fileName;
    }

    std::string joinCounts(const std::vector<size_t>& counts) {
        std::string joined;
        for (const size_t count : counts) {
            joined += (joined.empty() ? "" : ", ") + std::to_string(count);
        }
        return "[" + joined + "]";
    }
} // namespace

LoadedCrowdMap::~LoadedCrowdMap() = default;

Crmd::Crmd() : loading(false), saving(false), building(false), snapHeightsToGlb(true) {}

Crmd::~Crmd() = default;

wxDialog* Crmd::hCrmdDialog = nullptr;
wxDialog* Crmd::hCrmdSettingsDialog = nullptr;
std::map<std::string, std::string> Crmd::crmdHashIoiStringMap;
std::mutex Crmd::crmdHashIoiStringMapMutex;
ZHMArena ZHMArenas::g_Arenas[8];

void Crmd::renderCrowdMapData() {
    std::lock_guard lock(crowdMapsMutex);
    for (const auto& crowdMap : crowdMapsAwaitingRelease) {
        crowdMap->cellTriangleBuffer.release();
        crowdMap->flowArrowLineBuffer.release();
    }
    crowdMapsAwaitingRelease.clear();
    if (selectedCrowdMapIndex < 0 || static_cast<size_t>(selectedCrowdMapIndex) >= crowdMaps.size()) {
        return;
    }
    LoadedCrowdMap& crowdMap = *crowdMaps[selectedCrowdMapIndex];
    if (!crowdMap.data || crowdMap.data->m_nGridSizeX == 0 || crowdMap.data->m_nGridSizeY == 0) {
        return;
    }
    if (crowdMap.renderGeometryDirty) {
        crowdMap.renderGeometryDirty = false;
        rebuildRenderGeometry(crowdMap);
    }
    const Renderer& renderer = Renderer::getInstance();
    crowdMap.cellTriangleBuffer.draw(GL_TRIANGLES, crowdMap.renderModel, renderer.view, renderer.projection);
    crowdMap.flowArrowLineBuffer.draw(GL_LINES, crowdMap.renderModel, renderer.view, renderer.projection);
}

void Crmd::rebuildRenderGeometry(LoadedCrowdMap& crowdMap) {
    CoreProfileVertexBuffer& cellTriangleBuffer = crowdMap.cellTriangleBuffer;
    CoreProfileVertexBuffer& flowArrowLineBuffer = crowdMap.flowArrowLineBuffer;
    cellTriangleBuffer.upload({});
    flowArrowLineBuffer.upload({});
    const SCrowdMapData& data = *crowdMap.data;
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
        cellHeight = data.m_vGlobalSize.y > 0.0f ? data.m_vGlobalSize.y / static_cast<float>(gridHeight) : 1.0f;
    }
    if (!(cellWidth > 0.0f) || !(cellHeight > 0.0f) || !std::isfinite(cellWidth) || !std::isfinite(cellHeight)) {
        return;
    }
    const CrowdGridHeights heights(data, gridWidth, gridHeight, cellWidth, cellHeight);

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
        const float x = (static_cast<float>(xIndex) - 0.5f) * cellWidth;
        const float y = (static_cast<float>(yIndex) - 0.5f) * cellHeight;
        const uint8_t onNavGrid = data.m_GridMap[i].m_nOnNavGrid;
        if (onNavGrid == 0) {
            continue;
        }
        const glm::vec4 mapColor = glm::vec4(0.2f, 0.45f, 0.9f, 0.3f);
        addCellRectangle(rectangles, heights, x, y, cellWidth, cellHeight, mapColor, -0.2f);
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
            addCellRectangle(rectangles, heights, x, y, cellWidth, cellHeight, flagColor);
        }

        const uint8_t group = getPackedCellValue(data.m_CellGroups, i);
        if (group != 0xff) {
            const float insetX = cellWidth * 0.2f;
            const float insetY = cellHeight * 0.2f;
            addCellRectangle(rectangles, heights, x + insetX, y + insetY, cellWidth - 2.0f * insetX,
                cellHeight - 2.0f * insetY, groupColors[group % groupColors.size()], 0.04f);
        }
    }

    constexpr float displayLiftAboveGround = 0.25f;
    crowdMap.renderModel = glm::translate(crowdMapTransform(data),
        glm::vec3(-0.5f * static_cast<float>(cellGridWidth) * cellWidth,
            -0.5f * static_cast<float>(cellGridHeight) * cellHeight, displayLiftAboveGround));
    cellTriangleBuffer.upload(rectangles);

    constexpr std::array<glm::vec4, 8> flowChannelColors = {glm::vec4(1.0f, 0.65f, 0.9f, 0.95f),
        glm::vec4(1.0f, 0.72f, 0.25f, 0.95f), glm::vec4(0.35f, 0.85f, 1.0f, 0.95f), glm::vec4(0.95f, 0.9f, 0.3f, 0.95f),
        glm::vec4(0.75f, 0.55f, 1.0f, 0.95f), glm::vec4(0.4f, 1.0f, 0.55f, 0.95f), glm::vec4(1.0f, 0.45f, 0.35f, 0.95f),
        glm::vec4(0.45f, 0.95f, 0.85f, 0.95f)};
    constexpr glm::vec3 arrowNormal(0.0f, 0.0f, 1.0f);
    const float arrowLength = std::min(cellWidth, cellHeight) * 0.38f;
    const float arrowHeadLength = arrowLength * 0.25f;
    const float arrowHeadWidth = arrowLength * 0.18f;
    std::vector<CoreProfileVertex> arrows;
    for (size_t channelIndex = 0; channelIndex < data.m_aFlowChannels.size(); ++channelIndex) {
        const SCrowdFlowChannel& channel = data.m_aFlowChannels[channelIndex];
        const glm::vec4 arrowColor = flowChannelColors[channelIndex % flowChannelColors.size()];
        const size_t vectorCount = std::min(cellCount, channel.m_aFlowVectorIndex.size());
        if (vectorCount > (arrows.max_size() - arrows.size()) / 6) {
            continue;
        }
        arrows.reserve(arrows.size() + vectorCount * 6);
        for (size_t i = 0; i < vectorCount; ++i) {
            constexpr uint8_t unitCircleSplitCount = 120;
            const uint8_t vectorIndex = channel.m_aFlowVectorIndex[i] & 0x7f;
            const uint8_t flags = getPackedCellValue(data.m_CellFlags, i);
            if (vectorIndex >= unitCircleSplitCount || flags == 0xff ||
                (flags & static_cast<uint8_t>(CrowdMapImpl_CellFlags::CELLFLAG_WALKABLE)) == 0) {
                continue;
            }
            const float angle = static_cast<float>(vectorIndex) *
                (2.0f * std::numbers::pi_v<float> / static_cast<float>(unitCircleSplitCount));
            const glm::vec2 direction(std::cos(angle), std::sin(angle));
            const glm::vec2 perpendicular(-direction.y, direction.x);
            const glm::vec2 center((static_cast<float>(i % cellGridWidth) + 0.5f) * cellWidth,
                (static_cast<float>(i / cellGridWidth) + 0.5f) * cellHeight);
            const glm::vec2 tip = center + direction * arrowLength;
            const glm::vec2 headBase = tip - direction * arrowHeadLength;
            const glm::vec2 leftWing = headBase + perpendicular * arrowHeadWidth;
            const glm::vec2 rightWing = headBase - perpendicular * arrowHeadWidth;
            const float arrowZ = heights.at(center.x, center.y) + 0.06f;
            arrows.insert(arrows.end(),
                {{glm::vec3(center, arrowZ), arrowNormal, arrowColor, {}},
                    {glm::vec3(tip, arrowZ), arrowNormal, arrowColor, {}},
                    {glm::vec3(tip, arrowZ), arrowNormal, arrowColor, {}},
                    {glm::vec3(leftWing, arrowZ), arrowNormal, arrowColor, {}},
                    {glm::vec3(tip, arrowZ), arrowNormal, arrowColor, {}},
                    {glm::vec3(rightWing, arrowZ), arrowNormal, arrowColor, {}}});
        }
    }
    flowArrowLineBuffer.upload(arrows);
}
void Crmd::loadCrmdFromFiles(const std::vector<std::pair<std::string, std::string>>& filesAndNames) {
    if (filesAndNames.empty()) {
        return;
    }
    if (loading || saving || building) {
        Logger::log(NK_WARN, "Crmd load skipped: another Crmd load, save, or build is in progress.");
        return;
    }
    loading = true;
    Menu::updateMenuState();
    backgroundWorker.emplace([this, filesAndNames] {
        for (const auto& [fileName, name] : filesAndNames) {
            loadCrowdMapData(fileName, name);
        }
        loading = false;
        Menu::updateMenuState();
    });
}

void Crmd::loadCrowdMapData(const std::string& fileName, const std::string& name) {
    Logger::log(NK_INFO, ("Loading Crmd file: '" + fileName + "'...").c_str());
    const auto start = std::chrono::high_resolution_clock::now();
    CPPTRACE_TRY {
        std::error_code error;
        const std::filesystem::path canonicalPath =
            std::filesystem::weakly_canonical(std::filesystem::u8path(fileName), error);
        addCrowdMap(readCrowdMap(fileName), name, "file:" + (error ? fileName : canonicalPath.string()));
    }
    CPPTRACE_CATCH(const std::exception& e) {
        const std::string stackTrace = cpptrace::from_current_exception().to_string();
        ErrorHandler::openErrorDialog(
            "Error loading Crmd file '" + fileName + "': " + std::string(e.what()) + "\n\nStack Trace:\n" + stackTrace);
        Logger::log(NK_ERROR,
            ("Error loading Crmd file '" + fileName + "': " + e.what() + " Stack trace: " + stackTrace).c_str());
        return;
    }
    catch (...) {
        Logger::log(NK_ERROR, ("Invalid Crmd file: '" + fileName + "'...").c_str());
        return;
    }
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);
    Logger::log(
        NK_INFO, ("Finished loading Crmd " + fileName + " in " + std::to_string(duration.count()) + " ms").c_str());
}

void Crmd::handleOpenCrmdClicked() {
    std::vector<std::pair<std::string, std::string>> filesAndNames;
    for (const std::string& fileName : openLoadCrmdFilesDialog()) {
        filesAndNames.emplace_back(fileName, std::filesystem::u8path(fileName).filename().string());
    }
    loadCrmdFromFiles(filesAndNames);
}

void Crmd::addCrowdMap(std::unique_ptr<SCrowdMapData> data, const std::string& name, const std::string& sourceKey) {
    {
        std::lock_guard lock(crowdMapsMutex);
        const auto existing = sourceKey.empty()
            ? crowdMaps.end()
            : std::ranges::find_if(crowdMaps, [&](const auto& map) { return map->sourceKey == sourceKey; });
        if (existing == crowdMaps.end()) {
            crowdMaps.push_back(std::make_unique<LoadedCrowdMap>());
            selectedCrowdMapIndex = static_cast<int>(crowdMaps.size() - 1);
        } else {
            selectedCrowdMapIndex = static_cast<int>(existing - crowdMaps.begin());
        }
        LoadedCrowdMap& crowdMap = *crowdMaps[selectedCrowdMapIndex];
        crowdMap.data = std::move(data);
        crowdMap.name = name;
        crowdMap.sourceKey = sourceKey;
        crowdMap.renderGeometryDirty = true;
    }
    Menu::updateMenuState();
}

void Crmd::clear() {
    {
        std::lock_guard lock(crowdMapsMutex);
        std::ranges::move(crowdMaps, std::back_inserter(crowdMapsAwaitingRelease));
        crowdMaps.clear();
        selectedCrowdMapIndex = -1;
    }
    Menu::updateMenuState();
}

void Crmd::selectCrowdMap(const int index) {
    std::lock_guard lock(crowdMapsMutex);
    selectedCrowdMapIndex = index >= 0 && static_cast<size_t>(index) < crowdMaps.size() ? index : -1;
}

int Crmd::getSelectedCrowdMapIndex() const {
    std::lock_guard lock(crowdMapsMutex);
    return selectedCrowdMapIndex;
}

size_t Crmd::crowdMapCount() const {
    std::lock_guard lock(crowdMapsMutex);
    return crowdMaps.size();
}

std::vector<std::string> Crmd::crowdMapNames() const {
    std::lock_guard lock(crowdMapsMutex);
    std::vector<std::string> names;
    names.reserve(crowdMaps.size());
    for (const auto& crowdMap : crowdMaps) {
        names.push_back(crowdMap->name);
    }
    return names;
}

std::string Crmd::selectedCrowdMapName() const {
    std::lock_guard lock(crowdMapsMutex);
    return selectedCrowdMapIndex >= 0 ? crowdMaps[selectedCrowdMapIndex]->name : std::string{};
}

const SCrowdMapData* Crmd::crowdMapData(const size_t index) const {
    std::lock_guard lock(crowdMapsMutex);
    return index < crowdMaps.size() ? crowdMaps[index]->data.get() : nullptr;
}

void Crmd::updateCrmdDialogControls(wxDialog* dialog) {
    auto* picker = static_cast<FilteredListPicker*>(dialog->FindWindow(IDC_COMBOBOX_CRMD));
    std::vector<std::pair<std::string, std::string>> entries(crmdHashIoiStringMap.begin(), crmdHashIoiStringMap.end());
    std::ranges::sort(entries,
        [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
    std::vector<std::string> items;
    items.reserve(entries.size());
    for (const auto& [hash, ioiString] : entries) {
        items.push_back(Rpkg::displayName(hash, ioiString));
    }
    picker->setItems(std::move(items));
}
void Crmd::showExtractCrmdDialog() {
    if (hCrmdDialog) {
        hCrmdDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Load Crmd from resource package", wxDefaultPosition,
        wxSize(750, 450), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    hCrmdDialog = dialog;
    auto* picker = new FilteredListPicker(dialog, IDC_COMBOBOX_CRMD, true);
    updateCrmdDialogControls(dialog);
    auto* buttons = new wxStdDialogButtonSizer();
    auto* open = new wxButton(dialog, wxID_OK, "Open Crmds");
    auto* cancel = new wxButton(dialog, wxID_CANCEL, "Cancel");
    buttons->AddButton(open);
    buttons->AddButton(cancel);
    buttons->Realize();
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(dialog, wxID_ANY,
                   "Crmd files in resource packages (Rpkg) to extract and load (Ctrl/Shift-click for several)"),
        0, wxALL, 10);
    sizer->Add(picker, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
    dialog->SetSizer(sizer);
    open->Bind(wxEVT_BUTTON, [dialog, picker](wxCommandEvent&) {
        std::vector<std::pair<std::string, std::string>> hashesAndNames;
        for (const std::string& selection : picker->getSelections()) {
            for (const auto& [hash, ioiString] : crmdHashIoiStringMap) {
                if (hash == selection || ioiString == selection) {
                    hashesAndNames.emplace_back(hash, selection);
                }
            }
        }
        extractCrmdsFromRpkgs(hashesAndNames);
        dialog->Close();
    });
    cancel->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) { dialog->Close(); });
    picker->onActivate([open] {
        wxCommandEvent clickEvent(wxEVT_BUTTON, open->GetId());
        clickEvent.SetEventObject(open);
        open->ProcessWindowEvent(clickEvent);
    });
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

void Crmd::extractCrmdsFromRpkgs(const std::vector<std::pair<std::string, std::string>>& hashesAndNames) {
    if (hashesAndNames.empty()) {
        return;
    }
    std::vector<std::string> hashes;
    for (const auto& [hash, name] : hashesAndNames) {
        hashes.push_back(hash);
    }
    if (Rpkg::extractResourcesFromRpkgs(hashes, CRMD)) {
        return;
    }
    const std::filesystem::path crmdFolder =
        std::filesystem::u8path(NavKitSettings::getInstance().outputFolder) / "crmd";
    std::vector<std::pair<std::string, std::string>> filesAndNames;
    for (const auto& [hash, name] : hashesAndNames) {
        filesAndNames.emplace_back((crmdFolder / (hash + ".CRMD")).string(), resourceFileName(name));
    }
    getInstance().loadCrmdFromFiles(filesAndNames);
}

bool Crmd::canBuildAllCrmds() const {
    const Navp& navp = Navp::getInstance();
    const InputGeom* inputGeom = RecastAdapter::getInstance().inputGeom;
    const bool glbReady = SceneMesh::getInstance().glbLoaded && inputGeom != nullptr && inputGeom->getMesh() != nullptr;
    const bool navpBusy = navp.loading || navp.building || navp.navpBuildDone || Airg::getInstance().airgBuilding;
    return !Scene::getInstance().crowds.empty() && navp.navpLoaded && navp.navMesh != nullptr && !navpBusy &&
        (glbReady || !snapHeightsToGlb) && !building && !loading && !saving;
}

void Crmd::handleBuildAllCrmdsClicked() {
    if (!canBuildAllCrmds()) {
        return;
    }
    const std::vector<Json::Crowd> crowds = Scene::getInstance().crowds;
    std::vector<std::vector<glm::dvec3>> glbTriangles(crowds.size());
    if (snapHeightsToGlb) {
        const rcMeshLoaderGlb* mesh = RecastAdapter::getInstance().inputGeom->getMesh();
        for (size_t i = 0; i < crowds.size(); ++i) {
            try {
                glbTriangles[i] = CrowdMapBuilder::groundTrianglesNearCrowd(
                    crowds[i], mesh->getVerts(), mesh->getTris(), mesh->getTriCount());
            } catch (const std::exception& error) {
                ErrorHandler::openErrorDialog("Error building Crmd for crowd " + crowds[i].name + ": " + error.what());
                Logger::log(NK_ERROR, "Failed to build Crmd for crowd %s: %s", crowds[i].name.c_str(), error.what());
                return;
            }
        }
    }

    building = true;
    Menu::updateMenuState();
    const Scene& scene = Scene::getInstance();
    backgroundWorker.emplace(
        [this, crowds, boxes = scene.volumeBoxes, spheres = scene.volumeSpheres, shapes = scene.volumeShapes,
            navmeshTriangles = collectNavmeshTriangles(*Navp::getInstance().navMesh),
            glbTriangles = std::move(glbTriangles)] {
            buildCrmds(crowds, {boxes, spheres, shapes}, navmeshTriangles, glbTriangles);
        });
}

void Crmd::buildCrmds(const std::vector<Json::Crowd>& crowds, const CrowdMapBuilder::SceneVolumes& sceneVolumes,
    const std::vector<glm::dvec3>& navmeshTriangles, const std::vector<std::vector<glm::dvec3>>& glbTriangles) {
    Logger::log(NK_INFO, "Building Crmds for %zu crowds...", crowds.size());
    for (size_t i = 0; i < crowds.size(); ++i) {
        buildCrmd(crowds[i], sceneVolumes, navmeshTriangles, glbTriangles[i]);
    }
    building = false;
    Menu::updateMenuState();
}

void Crmd::buildCrmd(const Json::Crowd& crowd, const CrowdMapBuilder::SceneVolumes& sceneVolumes,
    const std::vector<glm::dvec3>& navmeshTriangles, const std::vector<glm::dvec3>& glbTriangles) {
    Logger::log(NK_INFO, "Building Crmd for crowd %s (%s) from %zu Navp triangles, %s...", crowd.name.c_str(),
        crowd.id.c_str(), navmeshTriangles.size() / 3,
        glbTriangles.empty() ? "keeping Navp heights" : "snapping heights to the GLB");
    const auto start = std::chrono::high_resolution_clock::now();
    CPPTRACE_TRY {
        CrowdMapBuilder::Report report;
        const CrowdMapBuilder::CrowdMap map =
            CrowdMapBuilder::build(crowd, sceneVolumes, navmeshTriangles, glbTriangles, report);
        setCrowdMap(map, crowd.name, crowd.id);
        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);
        Logger::log(NK_INFO,
            "Built Crmd for %s in %lld ms: %ux%u cells, %zu walkable, %zu panic-only, exit cells %s, group cells %s.",
            crowd.name.c_str(), static_cast<long long>(duration.count()), map.cellsX, map.cellsY, report.walkableCells,
            report.panicOnlyCells, joinCounts(report.exitCells).c_str(), joinCounts(report.groupCells).c_str());
        for (const std::string& exit : report.skippedExits) {
            Logger::log(NK_WARN, "Crmd for %s: skipped exit %s, it covers no walkable cells.", crowd.name.c_str(),
                exit.c_str());
        }
        for (size_t g = 0; g < report.groupCells.size(); ++g) {
            if (report.groupCells[g] == 0) {
                Logger::log(NK_WARN, "Crmd for %s: group %s covers no walkable cells.", crowd.name.c_str(),
                    crowd.groupBoxes[g].name.c_str());
            }
        }
        Logger::log(NK_INFO, "Crmd grid points: %zu on the Navp, %zu on inclusion volumes, %zu off the grid.",
            report.gridPointsOnGeometry, report.gridPointsOnInclusion, report.gridPointsOffGrid);
    }
    CPPTRACE_CATCH(const std::exception& error) {
        const std::string stackTrace = cpptrace::from_current_exception().to_string();
        ErrorHandler::openErrorDialog(
            "Error building Crmd for crowd " + crowd.name + ": " + error.what() + "\n\nStack Trace:\n" + stackTrace);
        Logger::log(NK_ERROR, "Failed to build Crmd for crowd %s: %s Stack trace: %s", crowd.name.c_str(), error.what(),
            stackTrace.c_str());
    }
    catch (...) {
        ErrorHandler::openErrorDialog("Error building Crmd for crowd " + crowd.name + ": unknown error.");
        Logger::log(NK_ERROR, "Failed to build Crmd for crowd %s due to an unknown error.", crowd.name.c_str());
    }
}

void Crmd::loadSettings() {
    snapHeightsToGlb =
        PersistedSettings::getInstance().getValue("Crmd", "snapHeightsToGlb", "true") == std::string("true");
}

void Crmd::saveSettings() const {
    PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    persistedSettings.setValue("Crmd", "snapHeightsToGlb", snapHeightsToGlb ? "true" : "false");
    persistedSettings.save();
}

void Crmd::showCrmdSettingsDialog() {
    if (hCrmdSettingsDialog != nullptr) {
        hCrmdSettingsDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Crmd Settings");
    hCrmdSettingsDialog = dialog;
    auto* snapCheck = new wxCheckBox(dialog, IDC_CHECK_CRMD_SNAP_HEIGHTS_TO_GLB, "Snap Navp heights to GLB");
    snapCheck->SetValue(snapHeightsToGlb);
    auto* content = new wxBoxSizer(wxVERTICAL);
    content->Add(snapCheck, 0, wxALL, 10);
    dialog->SetSizerAndFit(content);

    snapCheck->Bind(wxEVT_CHECKBOX, [this](const wxCommandEvent& event) {
        snapHeightsToGlb = event.IsChecked();
        saveSettings();
        Menu::updateMenuState();
    });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hCrmdSettingsDialog == dialog) {
            hCrmdSettingsDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->CentreOnParent();
    dialog->Show();
}

void Crmd::setCrowdMap(const CrowdMapBuilder::CrowdMap& map, const std::string& name, const std::string& crowdId) {
    addCrowdMap(toCrowdMapData(map), name, crowdId.empty() ? std::string{} : "crowd:" + crowdId);
}

bool Crmd::canSave() const {
    return !loading && !saving && !building && getSelectedCrowdMapIndex() >= 0;
}

void Crmd::saveCrmd(const std::string& fileName, const size_t index) const {
    std::vector<uint8_t> bytes;
    {
        std::lock_guard lock(crowdMapsMutex);
        if (index >= crowdMaps.size() || !crowdMaps[index]->data) {
            throw std::runtime_error("No Crmd is loaded at index " + std::to_string(index) + ".");
        }
        bytes = serializeCrowdMap(*crowdMaps[index]->data);
    }
    std::ofstream stream(fileName, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Could not open CRMD file for writing: " + fileName);
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("Failed to write CRMD file: " + fileName);
    }
}

void Crmd::handleSaveCrmdClicked() {
    const int index = getSelectedCrowdMapIndex();
    if (index < 0) {
        return;
    }
    std::string defaultName = selectedCrowdMapName();
    if (const size_t extension = defaultName.find_last_of('.'); extension != std::string::npos) {
        defaultName.erase(extension);
    }
    char* fileName = openSaveCrmdFileDialog(defaultName.empty() ? "output.CRMD" : defaultName + ".CRMD");
    if (!fileName) {
        return;
    }
    const std::string fileNameString = fileName;
    NFD_FreePathU8(fileName);
    Logger::log(NK_INFO, ("Saving Crmd file: '" + fileNameString + "'...").c_str());
    saving = true;
    Menu::updateMenuState();
    backgroundWorker.emplace(&Crmd::saveCrmdAndReport, this, fileNameString, static_cast<size_t>(index));
}

void Crmd::saveCrmdAndReport(const std::string& fileName, const size_t index) {
    const auto start = std::chrono::high_resolution_clock::now();
    try {
        saveCrmd(fileName, index);
        const auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);
        Logger::log(NK_INFO,
            ("Finished saving Crmd to " + fileName + " in " + std::to_string(duration.count()) + " ms").c_str());
    } catch (const std::exception& error) {
        ErrorHandler::openErrorDialog("Error saving Crmd file '" + fileName + "': " + error.what());
        Logger::log(NK_ERROR, ("Failed to save CRMD file: " + std::string(error.what())).c_str());
    } catch (...) {
        Logger::log(NK_ERROR, "Failed to save CRMD file due to an unknown error.");
    }
    saving = false;
    Menu::updateMenuState();
}

char* Crmd::openSaveCrmdFileDialog(const std::string& defaultName) {
    nfdu8filteritem_t filter = {"Crmd files", "CRMD"};
    return FileUtil::openNfdSaveDialog(&filter, 1, defaultName.c_str());
}

std::vector<std::string> Crmd::openLoadCrmdFilesDialog() {
    nfdu8filteritem_t filters[1] = {{"Crmd files", "CRMD"}};
    return FileUtil::openNfdLoadMultipleDialog(filters, 1);
}
