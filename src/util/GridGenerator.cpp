#include <chrono>
#include <queue>
#include <thread>
#include <vector>

#include <DetourNavMeshQuery.h>

#include "../../include/NavKit/util/GridGenerator.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"

#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/util/Math.h"
#include "../../include/NavKit/util/Pathfinding.h"

std::optional<std::jthread> GridGenerator::backgroundWorker;

bool GridGenerator::initRecastAirgAdapter() {
    Logger::log(NK_INFO, "Generating Obj from current navmesh...");
    Airg &airg = Airg::getInstance();
    airg.airgLoading = true;
    airg.airgLoaded = false;
    Menu::updateMenuState();
    Obj &obj = Obj::getInstance();
    obj.buildObjFromNavp(false);
    const RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    recastAirgAdapter.cleanup();
    const SceneExtract &sceneExtract = SceneExtract::getInstance();
    const std::string objFileName = sceneExtract.lastOutputFolder + "\\outputNavp.obj";
    Logger::log(NK_INFO, "Loading navmesh Obj into Recast...");
    if (!recastAirgAdapter.loadInputGeom(objFileName)) {
        airg.airgLoaded = false;
        airg.airgLoading = false;
        airg.airgBuilding = false;
        Menu::updateMenuState();
        Logger::log(NK_ERROR, "Error loading navmesh Obj into Recast...");
        return true;
    }
    recastAirgAdapter.handleMeshChanged();
    Navp &navp = Navp::getInstance();
    float bBoxPos[3];
    float bBoxScale[3];
    bBoxPos[0] = navp.bBoxPosX;
    bBoxPos[1] = navp.bBoxPosY;
    bBoxPos[2] = navp.bBoxPosZ;
    bBoxScale[0] = navp.bBoxScaleX;
    bBoxScale[1] = navp.bBoxScaleY;
    bBoxScale[2] = navp.bBoxScaleZ;

    const float bBoxMin[3] = {
        bBoxPos[0] - bBoxScale[0] / 2,
        bBoxPos[1] - bBoxScale[1] / 2,
        bBoxPos[2] - bBoxScale[2] / 2
    };
    const float bBoxMax[3] = {
        bBoxPos[0] + bBoxScale[0] / 2,
        bBoxPos[1] + bBoxScale[1] / 2,
        bBoxPos[2] + bBoxScale[2] / 2
    };
    recastAirgAdapter.setMeshBBox(bBoxMin, bBoxMax);
    Logger::log(NK_INFO, "Building Recast detour navmesh from navmesh Obj...");
    if (!recastAirgAdapter.handleBuildForAirg()) {
        airg.airgLoading = false;
        airg.airgLoaded = false;
        airg.airgBuilding = false;
        Menu::updateMenuState();

        Logger::log(NK_ERROR, "Error building Recast detour navmesh from navmesh Obj...");
        return true;
    }
    std::string outputNavpFilename = sceneExtract.lastOutputFolder + "\\output.navp.json";
    recastAirgAdapter.save(outputNavpFilename);

    backgroundWorker.emplace(&Navp::loadNavMesh, Navp::getAirgInstance(), outputNavpFilename.data(), true, true);

    return false;
}

void GridGenerator::build() {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Started building Airg at ";
    char timeBuffer[26];
    ctime_s(timeBuffer, sizeof(timeBuffer), &start_time);
    msg += timeBuffer;
    Logger::log(NK_INFO, msg.data());
    const auto start = std::chrono::high_resolution_clock::now();

    if (initRecastAirgAdapter()) return;

    Logger::log(NK_INFO, "Building waypoints within areas...");
    GenerateGrid();

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished building Airg in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.data());

    Airg &airg = Airg::getInstance();
    airg.airgLoading = false;
    airg.airgLoaded = true;
    airg.airgBuilding = false;
    Grid::getInstance().loadBoundsFromAirg();
    Menu::updateMenuState();
}

void GridGenerator::addVisibilityData(ReasoningGrid *grid) {
    // Add visibility data
    std::vector<uint8_t> newVisibilityData(grid->m_nNodeCount * 556, 255);
    grid->m_pVisibilityData = newVisibilityData;
    grid->m_HighVisibilityBits.m_nSize = 0;
    grid->m_LowVisibilityBits.m_nSize = 0;
    grid->m_deadEndData.m_nSize = grid->m_nNodeCount;
    std::vector<uint8_t> deadEndData;
    grid->m_deadEndData.m_aBytes = deadEndData;
}

void GridGenerator::GenerateGrid() {
    const Airg &airg = Airg::getInstance();
    ReasoningGrid *grid = airg.reasoningGrid;

    GetGridProperties();
    GenerateWaypointNodes();
    GenerateWaypointConnectivityMap();
    AlignNodes();
    GenerateConnections();
    GenerateLayerIndices();
    buildVisionAndDeadEndData();

    addVisibilityData(grid);
    Logger::log(NK_INFO,
                ("Built " + std::to_string(grid->m_WaypointList.size()) + " waypoints total.").c_str());
}

void GridGenerator::GetGridProperties() {
    const Grid &grid = Grid::getInstance();
    const Navp &navp = Navp::getInstance();
    Vec3 min = navp.navMesh->m_graphHdr->m_bbox.m_min;
    const Vec3 max = navp.navMesh->m_graphHdr->m_bbox.m_max;
    min.X += grid.xOffset;
    min.Y += grid.yOffset;
    const int gridXSize = std::ceil((max.X - min.X) / grid.spacing);
    auto &[vMin, vMax, nGridWidth, fGridSpacing, nVisibilityRange] =
            Airg::getInstance().reasoningGrid->m_Properties;
    fGridSpacing = grid.spacing;
    nGridWidth = gridXSize;
    vMin.x = min.X;
    vMin.y = min.Y;
    vMin.z = min.Z;
    vMin.w = 1;
    vMax.x = max.X;
    vMax.y = max.Y;
    vMax.z = max.Z;
    vMax.w = 1;
    nVisibilityRange = 23;
}

void GridGenerator::GenerateWaypointNodes() {
    Airg &airg = Airg::getInstance();
    Navp &navp = Navp::getInstance();
    Logger::log(NK_INFO, "Generating waypoint nodes.");
    NavPower::NavMesh *navMesh = navp.navMesh;
    ReasoningGrid *grid = airg.reasoningGrid;
    float spacing = grid->m_Properties.fGridSpacing;
    const float invSpacing = 1.0f / spacing;

    waypointCells.clear();
    int areaCount = navMesh->m_areas.size();
    const unsigned int num_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::jthread> threads;
    std::mutex waypointCellsMutex;
    const int areas_per_thread = (areaCount + num_threads - 1) / num_threads;
    Logger::log(NK_INFO, "Generating waypoint nodes using %u threads.", num_threads);

    auto worker = [&](int start_index, int end_index, int thread_id) {
        RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
        dtNavMeshQuery navQuery;
        navQuery.init(recastAirgAdapter.getNavMesh(), 2048);

        for (int areaIndex = start_index; areaIndex < end_index; ++areaIndex) {
            auto &area = navMesh->m_areas[areaIndex];
            if (areaIndex % 100 == 0) {
                Logger::log(NK_INFO, "[Thread %d] Processing area %d / %d", thread_id, areaIndex, areaCount);
            }
            auto [m_min, m_max] = Pathfinding::calculateBBox(&area);
            const Vec3 normal = area.CalculateNormal();
            const Vec3 &v1 = {area.m_edges[0]->m_pos.X, area.m_edges[0]->m_pos.Y, area.m_edges[0]->m_pos.Z};
            const float d = -(v1.X * normal.X + v1.Y * normal.Y + v1.Z * normal.Z);

            int minX = floor((m_min.X - grid->m_Properties.vMin.x) * invSpacing);
            int minY = floor((m_min.Y - grid->m_Properties.vMin.y) * invSpacing);
            int maxX = floor((m_max.X - grid->m_Properties.vMin.x) * invSpacing);
            int maxY = floor((m_max.Y - grid->m_Properties.vMin.y) * invSpacing);
            for (int yi = minY; yi <= maxY; ++yi) {
                for (int xi = minX; xi <= maxX; ++xi) {
                    float worldX = xi * spacing + grid->m_Properties.vMin.x;
                    float worldY = yi * spacing + grid->m_Properties.vMin.y;
                    if (normal.Z == 0.0f) continue;
                    float worldZ = -(worldX * normal.X + worldY * normal.Y + d) / normal.Z;

                    float4 cellLowerLeft = {worldX, worldY, worldZ, 0.0f}; // Cell upper left is -X, -Y corner
                    const float4 vMappedPos = MapToCell(&navQuery, &cellLowerLeft, area);

                    bool cellInArea = vMappedPos.w != -1;

                    int vMappedPosXIndex = floor((vMappedPos.x - grid->m_Properties.vMin.x) / spacing);
                    int vMappedPosYIndex = floor((vMappedPos.y - grid->m_Properties.vMin.y) / spacing);
                    int offset = vMappedPosXIndex + vMappedPosYIndex * grid->m_Properties.nGridWidth; {
                        std::lock_guard<std::mutex> lock(waypointCellsMutex);
                        auto &cells = waypointCells[offset];
                        float cellZIndex = vMappedPos.z;

                        // Always add a new cell for Stairs
                        Pathfinding::ZPFLocation pfLocation;
                        MapLocation(&navQuery, &vMappedPos, &pfLocation);
                        if (cells.empty()) {
                            // Create a new cell
                            Pathfinding::SGCell newCell;
                            newCell.fZ = cellZIndex;
                            if (cellInArea) {
                                newCell.m_Points.push_back({vMappedPos.x, vMappedPos.y, vMappedPos.z, 0.0});
                            }
                            cells.push_back(newCell);
                        } else {
                            bool shouldAddNewCell = true;
                            for (auto cell: cells) {
                                // 2.25 * tan(18) degrees = 0.73
                                if (abs(cellZIndex - cell.fZ) < 0.73) {
                                    if (cellInArea) {
                                        // Add the point to an existing cell
                                        if (cells.back().m_Points.empty()) {
                                            cells.back().m_Points.push_back({
                                                vMappedPos.x, vMappedPos.y, vMappedPos.z, 0.0
                                            });
                                        }
                                        shouldAddNewCell = false;
                                        break;
                                    }
                                    shouldAddNewCell = false;
                                }
                            }
                            if (shouldAddNewCell) {
                                // Create a new cell
                                Pathfinding::SGCell newCell;
                                newCell.fZ = cellZIndex;
                                if (cellInArea) {
                                    if (newCell.m_Points.empty()) {
                                        newCell.m_Points.push_back({vMappedPos.x, vMappedPos.y, vMappedPos.z, 0.0});
                                    }
                                }
                                cells.push_back(newCell);
                            }
                        }
                    }
                }
            }
        }
    };
    for (unsigned int i = 0; i < num_threads; ++i) {
        const int start_index = i * areas_per_thread;
        const int end_index = std::min(start_index + areas_per_thread, areaCount);
        if (start_index < end_index) {
            threads.emplace_back(worker, start_index, end_index, i);
        }
    }

    const int minX = floor((grid->m_Properties.vMin.x) / spacing);
    const int minY = floor((grid->m_Properties.vMin.y) / spacing);
    const int maxX = floor((grid->m_Properties.vMax.x) / spacing);
    const int maxY = floor((grid->m_Properties.vMax.y) / spacing);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            int offset = x + y * grid->m_Properties.nGridWidth;
            auto &cells = waypointCells[offset];
            std::ranges::sort(cells, [](const Pathfinding::SGCell &a, const Pathfinding::SGCell &b) {
                return a.fZ < b.fZ;
            });
        }
    }
    Logger::log(NK_INFO, "Finished generating waypoint nodes.");
}

void GridGenerator::GenerateWaypointConnectivityMap() {
    Logger::log(NK_INFO, "Generating Waypoint Connectivity map.");

    Airg &airg = Airg::getInstance();
    // Clear the existing waypoint map
    airg.reasoningGrid->m_WaypointList.clear();
    m_WaypointMap.clear();

    int waypointIndex = 0;
    for (const auto &cells: waypointCells | std::views::values) {
        for (auto cell: cells) {
            for (const auto point: cell.m_Points) {
                Waypoint waypoint;
                waypoint.vPos = {point.x, point.y, point.z + 0.001f, 1.0};
                waypoint.nVisionDataOffset = 0;
                waypoint.nLayerIndex = -1;
                int nOffset = -1;

                // Calculate the grid index based on waypoint position and grid spacing
                const float fGridSpacing = airg.reasoningGrid->m_Properties.fGridSpacing;
                const float4 vPos = {point.x, point.y, point.z, 0.0};
                const Vec4 vMinVec4 = airg.reasoningGrid->m_Properties.vMin;
                const float4 vMin = {vMinVec4.x, vMinVec4.y, vMinVec4.z, 0.0};
                const int nGridWidth = airg.reasoningGrid->m_Properties.nGridWidth;

                const int x = floor(floor((vPos.x - vMin.x) / fGridSpacing));
                const int y = floor(floor((vPos.y - vMin.y) / fGridSpacing));
                waypoint.xi = x;
                waypoint.yi = y;
                waypoint.zi = floor(vPos.z - vMin.z);

                // Check if the waypoint is within grid bounds
                if (x >= 0 && y >= 0 && x < nGridWidth) {
                    nOffset = x + y * nGridWidth;
                }

                // Add waypoint to the waypoint list and corresponding entry in the map
                if (nOffset != -1) {
                    if (waypointIndex % 100 == 0) {
                        Logger::log(NK_INFO,
                                    ("Adding new waypoint. position: X: " + std::to_string(waypoint.vPos.x) + " Y: " +
                                     std::to_string(waypoint.vPos.y) + " Z: " + std::to_string(waypoint.vPos.z)).
                                    c_str());
                    }
                    m_WaypointMap[nOffset].push_back(waypointIndex);
                    airg.reasoningGrid->m_WaypointList.push_back(waypoint);
                    waypointIndex++;
                }
            }
        }
    }
    airg.reasoningGrid->m_nNodeCount = airg.reasoningGrid->m_WaypointList.size();
    Logger::log(NK_INFO, "Finished generating Waypoint Connectivity.");
}

void GridGenerator::AlignNodes() {
    Airg &airg = Airg::getInstance();
    Logger::log(NK_INFO, "GridGenerator::AlignNodes() -> Aligning nodes.");
    float gridSpacing = airg.reasoningGrid->m_Properties.fGridSpacing;
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    dtNavMeshQuery navQuery;
    navQuery.init(recastAirgAdapter.getNavMesh(), 2048);
    int curWaypoint = 0;
    // Iterate over each waypoint in the active grid
    for (Waypoint &waypoint: airg.reasoningGrid->m_WaypointList) {
        if (curWaypoint % 100 == 0) {
            Logger::log(NK_INFO,
                        ("GridGenerator::AlignNodes() -> Finished aligning waypoint " + std::to_string(curWaypoint) +
                         " / " + std::to_string(airg.reasoningGrid->m_WaypointList.size())).c_str());
        }
        curWaypoint++;
        // Get the cell bitmap for the current waypoint
        bool cellBitmap[25];
        float4 vNavPowerPos = {waypoint.vPos.x, waypoint.vPos.y, waypoint.vPos.z, waypoint.vPos.w};
        GetCellBitmap(&vNavPowerPos, reinterpret_cast<bool *>(&cellBitmap));
        std::ranges::copy(cellBitmap, std::begin(waypoint.cellBitmap));

        std::string cellBitmapString = "";
        for (int i = 0; i < 25; i++) {
            cellBitmapString += std::to_string(cellBitmap[i]);
        }
        // Calculate connectivity for each direction
        int maxConnectivity = 0;
        int bestDirection = -1;
        int pCellConnectivity[25];
        CalculateConnectivity(cellBitmap, pCellConnectivity);
        for (int i = 0; i < 25; ++i) {
            int connectivity = pCellConnectivity[i];
            if (connectivity > maxConnectivity) {
                maxConnectivity = connectivity;
                bestDirection = i;
            }
        }
        // Offsets make a clockwise spiral starting from the middle
        //	21 14 10 15 22
        //	13 5  2  6  16
        //	9  1  0  3  11
        //	20 8  4  7  17
        //	24 19 12 18 23

        std::vector<std::pair<int, int> > offsets = {
            {2, 2}, {1, 2}, {2, 3}, {3, 2}, {2, 1}, {1, 3}, {3, 3}, {3, 1}, {1, 1}, {0, 2}, {2, 4}, {4, 2}, {2, 0},
            {0, 3}, {1, 4}, {3, 4}, {4, 3}, {4, 1}, {3, 0}, {1, 0}, {0, 1}, {0, 4}, {4, 4}, {4, 0}, {0, 0}
        };
        int offsetX;
        int offsetY;

        for (int i = 0; i < 25; ++i) {
            offsetX = offsets[i].first;
            offsetY = offsets[i].second;
            int offset = offsetX + 5 * offsetY;
            if (cellBitmap[offset] && pCellConnectivity[offset] == maxConnectivity) {
                break;
            }
        }
        std::string cellConnString = "";
        for (int i = 0; i < 25; i++) {
            cellConnString += (std::to_string(pCellConnectivity[i]) + " ").c_str();
        }

        // If a valid direction is found, adjust the waypoint position
        if (bestDirection != -1) {
            float4 wayPointNavPowerPos{waypoint.vPos.x, waypoint.vPos.y, waypoint.vPos.z, 1.0f};
            auto [vMinX, vMinY, vMinZ, vMinW] = airg.reasoningGrid->m_Properties.vMin;
            float4 vMin{vMinX, vMinY, vMinZ, vMinW};
            float4 vNavPowerPosOffsetFromMin = wayPointNavPowerPos - vMin;

            float gridOriginNavPowerX = floor(vNavPowerPosOffsetFromMin.x / gridSpacing) * gridSpacing + vMin.x;
            float gridOriginNavPowerY = floor(vNavPowerPosOffsetFromMin.y / gridSpacing) * gridSpacing + vMin.y;

            float4 gridOriginNavPower(gridOriginNavPowerX, gridOriginNavPowerY, wayPointNavPowerPos.z, 1.0f);

            float cellOffsetX = (static_cast<float>(offsetX) * 0.2f + 0.1f) * gridSpacing;
            float cellOffsetY = (static_cast<float>(offsetY) * 0.2f + 0.1f) * gridSpacing;

            float4 cellNavPowerPosition = gridOriginNavPower + float4{cellOffsetX, cellOffsetY, 0.0f, 0.0f};
            Pathfinding::ZPFLocation remappedLocation;
            MapLocation(&navQuery, &cellNavPowerPosition, &remappedLocation);
            float distanceThreshold = static_cast<float>(gridSpacing * 0.2) * 0.33399999;
            if (IsInside(&navQuery, &remappedLocation)) {
                SVector3 remappedNavPowerPosVec3 = remappedLocation.pos;
                float4 remappedNavPowerPos{
                    remappedNavPowerPosVec3.x, remappedNavPowerPosVec3.y, remappedNavPowerPosVec3.z, 0.0
                };
                float4 distance = {remappedNavPowerPos - cellNavPowerPosition};
                if (distance.x < distanceThreshold &&
                    distance.y < distanceThreshold
                ) {
                    Navp &navp = Navp::getInstance();
                    auto centroidNavPower = recastAirgAdapter.calculateCentroid(remappedLocation.polyRef);
                    auto area = navp.posToAreaMap.find(centroidNavPower);
                    float radius = 0.1;
                    if ((area != navp.posToAreaMap.end() && area->second->m_area->m_usageFlags ==
                         NavPower::AreaUsageFlags::AREA_STEPS) ||
                        !NearestOuterEdge(&navQuery, remappedLocation, radius, nullptr, nullptr)) {
                        waypoint.vPos = {remappedNavPowerPos.x, remappedNavPowerPos.y, remappedNavPowerPos.z, 0.0};
                    }
                }
            }
        }
    }
    Logger::log(NK_INFO, "GridGenerator::AlignNodes() -> Finished aligning nodes.");
}

void GridGenerator::GenerateConnections() {
    Airg &airg = Airg::getInstance();

    Properties *properties = &airg.reasoningGrid->m_Properties;
    Logger::log(NK_INFO, "GridGenerator::GenerateConnections() -> Generating connections");

    int nConnectionCount = 0;
    int currentWaypointCount = 0;

    // Iterate through waypoints in the active grid
    for (Waypoint &waypoint: airg.reasoningGrid->m_WaypointList) {
        // Calculate grid cell coordinates for the waypoint
        int gridX = std::floor((waypoint.vPos.x - properties->vMin.x) / properties->fGridSpacing);
        int gridY = std::floor((waypoint.vPos.y - properties->vMin.y) / properties->fGridSpacing);

        // Check if grid coordinates are within bounds
        if (gridX >= properties->nGridWidth) {
            continue;
        }

        int cellIndex = gridX + gridY * properties->nGridWidth;

        // Check neighbors for potential connections
        for (int direction = 0; direction < 8; ++direction) {
            int neighborCellIndex = Pathfinding::GetNeighborCellIndex(cellIndex, direction, properties->nGridWidth);

            // Skip invalid or already connected neighbors
            if (neighborCellIndex == -1 || neighborCellIndex == cellIndex) {
                continue;
            }
            int neighborIndex = 65535;
            Waypoint *neighbor = nullptr;
            for (int waypointIndex: m_WaypointMap[neighborCellIndex]) {
                if (abs(waypoint.vPos.z - airg.reasoningGrid->m_WaypointList[waypointIndex].vPos.z) < 1.8) {
                    // What should the max distance be to connect to waypoints? The agent height? 45 degrees?
                    neighbor = &airg.reasoningGrid->m_WaypointList[waypointIndex];
                    neighborIndex = waypointIndex;
                    break;
                }
            }
            if (neighbor == nullptr) {
                continue;
            }

            Vec3 wayPointPosRecast = RecastAdapter::convertFromNavPowerToRecast({
                waypoint.vPos.x, waypoint.vPos.y, waypoint.vPos.z
            });
            Vec3 neighborPosRecast = RecastAdapter::convertFromNavPowerToRecast({
                neighbor->vPos.x, neighbor->vPos.y, neighbor->vPos.z
            });
            // Check if path between waypoints is not blocked
            RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
            if (!recastAirgAdapter.PFLineBlocked(wayPointPosRecast, neighborPosRecast)) {
                // Connect waypoints and update connection count
                waypoint.nNeighbors[direction] = neighborIndex;
                neighbor->nNeighbors[(direction + 4) % 8] = currentWaypointCount;
                ++nConnectionCount;
                if (nConnectionCount % 100 == 0) {
                    Logger::log(NK_INFO,
                                ("GridGenerator::GenerateConnections() -> In progress. Generated " + std::to_string(
                                     nConnectionCount) + " connections").c_str());
                }
            }
        }
        ++currentWaypointCount;
    }

    Logger::log(NK_INFO,
                ("GridGenerator::GenerateConnections() -> Connections " + std::to_string(nConnectionCount) +
                 " Current node count " +
                 std::to_string(currentWaypointCount)).c_str());
}

void GridGenerator::GenerateLayerIndices() {
    Logger::log(NK_INFO, "GridGenerator::GenerateLayerIndices() -> Generating layer clusters");
    Airg &airg = Airg::getInstance();

    const float gridSpacingInv = 1.0f / airg.reasoningGrid->m_Properties.fGridSpacing;
    const int gridWidth = airg.reasoningGrid->m_Properties.nGridWidth;
    auto waypoint = airg.reasoningGrid->m_WaypointList.begin();
    int waypointIndex = 0;
    int neighborIndex = 0;
    bool allLayersAssigned = false;
    while (!allLayersAssigned) {
        allLayersAssigned = true;
        while (waypoint != airg.reasoningGrid->m_WaypointList.end()) {
            if (waypoint->nLayerIndex == -1) {
                int layerIndex = 0;
                allLayersAssigned = false;

                // Calculate grid coordinates of current waypoint
                int gridX = floor((waypoint->vPos.x - airg.reasoningGrid->m_Properties.vMin.x) *
                                  gridSpacingInv);
                int gridY = floor((waypoint->vPos.y - airg.reasoningGrid->m_Properties.vMin.y) *
                                  gridSpacingInv);

                int gridIndex = -1;
                if (gridX >= 0 && gridY >= 0 && gridX < gridWidth) {
                    gridIndex = gridX + gridY * gridWidth;
                }
                bool doneUpdatingLayerIndex = false;
                while (!doneUpdatingLayerIndex) {
                    auto cellWaypointIndices = m_WaypointMap[gridIndex];
                    if (cellWaypointIndices.empty()) {
                        break;
                    }
                    int i = 0;
                    while (true) {
                        Waypoint &cellWaypoint = airg.reasoningGrid->m_WaypointList[cellWaypointIndices[i]];
                        if (cellWaypoint.nLayerIndex == layerIndex) {
                            break;
                        }
                        i++;
                        if (i == cellWaypointIndices.size()) {
                            doneUpdatingLayerIndex = true;
                            break;
                        }
                    }
                    if (!doneUpdatingLayerIndex) {
                        layerIndex++;
                    }
                }
                std::queue<int> queue;
                queue.push(neighborIndex);
                for (int neighborToCheck = 0; neighborToCheck < 8; neighborToCheck++) {
                    int neighborIndexToCheck = waypoint->nNeighbors[neighborToCheck];
                    if (neighborIndexToCheck >= 0 && neighborIndexToCheck < 65535) {
                        Waypoint &neighborWaypoint = airg.reasoningGrid->m_WaypointList[neighborIndexToCheck];
                        if (neighborWaypoint.nLayerIndex == -1) {
                            queue.push(neighborIndexToCheck);
                        }
                    }
                }
                while (!queue.empty()) {
                    int currentQueueWaypointIndex = queue.front();
                    queue.pop();
                    Waypoint &currentQueueWaypoint = airg.reasoningGrid->m_WaypointList[
                        currentQueueWaypointIndex];
                    if (currentQueueWaypoint.nLayerIndex == -1) {
                        // Calculate grid coordinates of current waypoint
                        gridX = floor(
                            (currentQueueWaypoint.vPos.x - airg.reasoningGrid->m_Properties.vMin.x) *
                            gridSpacingInv);
                        gridY = floor(
                            (currentQueueWaypoint.vPos.y - airg.reasoningGrid->m_Properties.vMin.y) *
                            gridSpacingInv);

                        gridIndex = -1;
                        if (gridX >= 0 && gridY >= 0 && gridX < gridWidth) {
                            gridIndex = gridX + gridY * gridWidth;
                        }
                        if (auto cellNeighborIndices = m_WaypointMap[gridIndex]; cellNeighborIndices.empty()) {
                            currentQueueWaypoint.nLayerIndex = layerIndex;
                            for (int neighborToCheck = 0; neighborToCheck < 8; neighborToCheck++) {
                                if (int neighborIndexToCheck = currentQueueWaypoint.nNeighbors[neighborToCheck];
                                    neighborIndexToCheck >= 0 && neighborIndexToCheck < 65535) {
                                    const Waypoint &neighborWaypoint = airg.reasoningGrid->m_WaypointList[
                                        neighborIndexToCheck];
                                    if (neighborWaypoint.nLayerIndex == -1) {
                                        queue.push(neighborIndexToCheck);
                                    }
                                }
                            }
                        } else {
                            int i = 0;
                            while (true) {
                                Waypoint &cellNeighborWaypoint = airg.reasoningGrid->m_WaypointList[
                                    cellNeighborIndices[i]];
                                if (cellNeighborWaypoint.nLayerIndex == layerIndex) {
                                    break;
                                }
                                i++;
                                if (i == cellNeighborIndices.size()) {
                                    currentQueueWaypoint.nLayerIndex = layerIndex;
                                    for (int neighborToCheck = 0; neighborToCheck < 8; neighborToCheck++) {
                                        int neighborIndexToCheck = currentQueueWaypoint.nNeighbors[neighborToCheck];
                                        if (neighborIndexToCheck >= 0 && neighborIndexToCheck < 65535) {
                                            Waypoint &neighborWaypoint = airg.reasoningGrid->m_WaypointList[
                                                neighborIndexToCheck];
                                            if (neighborWaypoint.nLayerIndex == -1) {
                                                queue.push(neighborIndexToCheck);
                                            }
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            ++waypoint;
            waypointIndex++;
            neighborIndex = waypointIndex;
        }
    }
    Logger::log(NK_INFO, "GridGenerator::GenerateLayerIndices() -> Done generating layer clusters");
}

Pathfinding::ZPFLocation *GridGenerator::MapLocation_Internal(
    dtNavMeshQuery *navQuery,
    Pathfinding::ZPFLocation *result,
    const float4 *vPosNavPower,
    const float fAcceptance,
    dtPolyRef startPolyRef
) {
    bool valid = false;
    std::vector<dtPolyRef> closestReachablePolys;
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    valid = startPolyRef != 0;

    if (valid) {
        closestReachablePolys = recastAirgAdapter.getClosestReachablePolys(navQuery,
                                                                           {
                                                                               vPosNavPower->x, vPosNavPower->y,
                                                                               vPosNavPower->z
                                                                           }, startPolyRef, 4);
    } else {
        closestReachablePolys = recastAirgAdapter.getClosestPolys(
            navQuery, {vPosNavPower->x, vPosNavPower->y, vPosNavPower->z},
            20);
    }

    if (closestReachablePolys.size() <= 0) {
        result->mapped = false;
        result->polyRef = 0;
        return result;
    }
    Vec3 closestPoint;

    int closestIndex = 0;
    float closestDistanceSqr = std::numeric_limits<float>::max();
    float acceptanceSquared = fAcceptance * fAcceptance;

    const Vec3 vPosNavPowerVec3 = {vPosNavPower->x, vPosNavPower->y, vPosNavPower->z};
    Pathfinding::ClosestPositionData data;
    Vec3 candidatePosNavPower;

    int areasToCheckCount = 0;
    areasToCheckCount = closestReachablePolys.size();
    for (int i = 0; i < areasToCheckCount; ++i) {
        dtPolyRef polyRef = 0;
        polyRef = closestReachablePolys[i];
        GetClosestPosInArea2d_G2_ClosestPos(&candidatePosNavPower, polyRef, &vPosNavPowerVec3, &data);

        float4 candidatePosNavPower4 = {
            candidatePosNavPower.X, candidatePosNavPower.Y, candidatePosNavPower.Z, static_cast<float>(i)
        };
        const float4 vectorToCandidate = *vPosNavPower - candidatePosNavPower4;
        const float distanceSqr = vectorToCandidate.x * vectorToCandidate.x +
                                  vectorToCandidate.y * vectorToCandidate.y +
                                  vectorToCandidate.z * vectorToCandidate.z;

        if (!data.isEdgePos && distanceSqr < 1.0f) {
            closestPoint = {candidatePosNavPower.X, candidatePosNavPower.Y, candidatePosNavPower.Z};
            closestDistanceSqr = distanceSqr;
            closestIndex = i;
            break;
        }

        if (distanceSqr < closestDistanceSqr) {
            closestDistanceSqr = distanceSqr;
            closestIndex = i;
            closestPoint = {candidatePosNavPower.X, candidatePosNavPower.Y, candidatePosNavPower.Z};
        }
    }

    if (closestIndex >= 0 && closestDistanceSqr < acceptanceSquared) {
        result->pos = {closestPoint.X, closestPoint.Y, closestPoint.Z};
        result->polyRef = closestReachablePolys[closestIndex];
        result->mapped = result->polyRef != 0;
    } else {
        result->mapped = false;
        result->polyRef = 0;
    }

    return result;
}

bool GridGenerator::MapLocation(dtNavMeshQuery *navQuery, const float4 *vNavPowerPos,
                                Pathfinding::ZPFLocation *lMapped) {
    constexpr float fAcceptance = 2.0;
    Pathfinding::ZPFLocation *pfLocation;
    Pathfinding::ZPFLocation result;
    bool valid = false;
    valid = lMapped->polyRef != 0;
    if (valid) {
        pfLocation = MapLocation_Internal(navQuery, &result, vNavPowerPos, fAcceptance, lMapped->polyRef);
        lMapped->pos.x = pfLocation->pos.x;
        lMapped->pos.y = pfLocation->pos.y;
        lMapped->pos.z = pfLocation->pos.z;
        lMapped->polyRef = pfLocation->polyRef;

        lMapped->mapped = pfLocation->mapped;
    }
    valid = lMapped->polyRef != 0;

    if (!valid) {
        pfLocation = MapLocation_Internal(
            navQuery,
            &result,
            vNavPowerPos,
            fAcceptance, lMapped->polyRef);
        lMapped->pos.x = pfLocation->pos.x;
        lMapped->pos.y = pfLocation->pos.y;
        lMapped->pos.z = pfLocation->pos.z;
        lMapped->polyRef = pfLocation->polyRef;
        lMapped->mapped = pfLocation->mapped;
    }
    return lMapped->polyRef != 0;
}

float4 GridGenerator::MapToCell(dtNavMeshQuery *navQuery, const float4 *vCellNavPowerUpperLeft,
                                const NavPower::Area &area) {
    const Airg &airg = Airg::getInstance();
    Pathfinding::ZPFLocation pfLocation;

    const std::vector<std::pair<int, int> > offsets = {
        {2, 2}, {1, 2}, {2, 3}, {3, 2}, {2, 1}, {1, 3}, {3, 3}, {3, 1}, {1, 1}, {0, 2}, {2, 4}, {4, 2}, {2, 0}, {0, 3},
        {1, 4}, {3, 4}, {4, 3}, {4, 1}, {3, 0}, {1, 0}, {0, 1}, {0, 4}, {4, 4}, {4, 0}, {0, 0}
    };

    const float4 distanceThreshold = {
        (airg.reasoningGrid->m_Properties.fGridSpacing * 0.2f) / 3.0f,
        (airg.reasoningGrid->m_Properties.fGridSpacing * 0.2f) / 3.0f,
        1.5f, 0.0f
    };
    bool found = false;
    float4 foundNavPowerPos{};
    for (const auto &[first, second]: offsets) {
        const float offsetX = first * 0.2f + 0.1f;
        const float offsetY = second * 0.2f + 0.1f;
        float4 sampleNavPowerPoint = *vCellNavPowerUpperLeft + float4(
                                         offsetX * airg.reasoningGrid->m_Properties.fGridSpacing,
                                         offsetY * airg.reasoningGrid->m_Properties.fGridSpacing,
                                         0.0f, 0.0f);
        MapLocation(navQuery, &sampleNavPowerPoint, &pfLocation);
        if (IsInside(navQuery, &pfLocation) && pfLocation.mapped) {
            const float4 candidateNavPowerPoint = {pfLocation.pos.x, pfLocation.pos.y, pfLocation.pos.z, 0.0f};
            const float4 distance = {
                abs(candidateNavPowerPoint.x - sampleNavPowerPoint.x),
                abs(candidateNavPowerPoint.y - sampleNavPowerPoint.y),
                abs(candidateNavPowerPoint.z - sampleNavPowerPoint.z),
                0.0f,
            };
            if (distance.x <= distanceThreshold.x &&
                distance.y <= distanceThreshold.y &&
                distance.z <= distanceThreshold.z) {
                Vec3 centroidNavPower;
                RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
                centroidNavPower = RecastAdapter::convertFromRecastToNavPower(
                    recastAirgAdapter.calculateCentroid(pfLocation.polyRef));
                if (const auto areaNavPowerPos = area.m_area->m_pos;
                    abs(centroidNavPower.X - areaNavPowerPos.X) < 0.1 && abs(centroidNavPower.Y - areaNavPowerPos.Y) <
                    0.1 && abs(
                        centroidNavPower.X - areaNavPowerPos.Z) < 0.1) {
                    if (area.m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_STEPS) {
                        found = true;
                        foundNavPowerPos = {pfLocation.pos.x, pfLocation.pos.y, pfLocation.pos.z, 0.0};
                        break;
                    }
                }
                float radius = 0.1;
                if (!found && !NearestOuterEdge(navQuery, pfLocation, radius, nullptr, nullptr)) {
                    found = true;
                    foundNavPowerPos = {pfLocation.pos.x, pfLocation.pos.y, pfLocation.pos.z, 0.0};
                }
            }
        }
    }
    if (found) {
        return foundNavPowerPos;
    }
    // If no valid point found, return a zero vector
    return {0.0f, 0.0f, 0.0f, -1.0f};
}

bool GridGenerator::IsInside(dtNavMeshQuery *navQuery, Pathfinding::ZPFLocation *location) {
    // Check if the area is already valid
    if (location->polyRef != 0) {
        return true;
    }

    // Check if the location has been mapped to the pathfinder
    if (!location->mapped) {
        return false;
    }

    // Map the location to the pathfinder grid
    Pathfinding::ZPFLocation remappedLocation;
    float4 navPowerPos{location->pos.x, location->pos.y, location->pos.z, 1.0};
    MapLocation(navQuery, &navPowerPos, &remappedLocation);

    bool remappedLocationValid = false;
    remappedLocationValid = remappedLocation.polyRef != 0;
    // Check if the remapped location has a valid area
    if (remappedLocationValid) {
        // Calculate the distance between the original and remapped locations
        float4 remappedNavPowerPos = {remappedLocation.pos.x, remappedLocation.pos.y, remappedLocation.pos.z, 1.0f};
        float4 distance = remappedNavPowerPos - navPowerPos;
        float distanceSquared = distance.x * distance.x + distance.y * distance.y + distance.z * distance.z;

        // Check if the distance is within a small threshold
        if (distanceSquared < (0.001f * 0.001f)) {
            // If the distance is small enough, consider the location to be inside
            location->polyRef = remappedLocation.polyRef;
        }
    }

    bool locationValid = false;
    locationValid = location->polyRef != 0;
    return locationValid;
}

void GridGenerator::GetCellBitmap(const float4 *vNavPowerPosition, bool *pBitmap) {
    const Airg &airg = Airg::getInstance();
    const float gridSpacing = airg.reasoningGrid->m_Properties.fGridSpacing;
    const float gridOffset = 0.2f * gridSpacing;

    const float4 gridNavPowerOrigin = {
        floor((vNavPowerPosition->x - airg.reasoningGrid->m_Properties.vMin.x) / gridSpacing) * gridSpacing +
        airg.reasoningGrid->m_Properties.vMin.x,
        floor((vNavPowerPosition->y - airg.reasoningGrid->m_Properties.vMin.y) / gridSpacing) * gridSpacing +
        airg.reasoningGrid->m_Properties.vMin.y,
        vNavPowerPosition->z,
        0.0f
    };
    float4 worldNavPowerPosition;
    float4 cellNavPowerOffset;
    cellNavPowerOffset.z = 0;
    cellNavPowerOffset.w = 0;

    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    dtNavMeshQuery navQuery;
    navQuery.init(recastAirgAdapter.getNavMesh(), 2048);

    // Iterate through a 5x5 grid of cells around the current cell
    for (int yi = 0; yi < 5; ++yi) {
        constexpr float gridStep = 0.2f;
        cellNavPowerOffset.y = ((yi * gridStep) + 0.1) * gridSpacing;
        for (int xi = 0; xi < 5; ++xi) {
            cellNavPowerOffset.x = ((xi * gridStep) + 0.1) * gridSpacing;
            worldNavPowerPosition = cellNavPowerOffset + gridNavPowerOrigin;

            // Map world position to pathfinder coordinates
            Pathfinding::ZPFLocation cellLocation;

            MapLocation(&navQuery, &worldNavPowerPosition, &cellLocation);

            bool isValid;

            // Check if cell is within the pathfinder map bounds
            if (!IsInside(&navQuery, &cellLocation)) {
                isValid = false;
            } else {
                float4 testNavPowerPosition = {cellLocation.pos.x, cellLocation.pos.y, cellLocation.pos.z, 0.0f};
                // Check if cell is within a valid distance from the grid position
                float4 delta = (testNavPowerPosition - worldNavPowerPosition);
                float4 absDistance = {abs(delta.x), abs(delta.y), 0.0f, 0.0f};
                if (std::max(absDistance.x, absDistance.y) > gridOffset) {
                    isValid = false;
                } else {
                    // Check if cell is walkable (not stairs or other obstacles)
                    Navp &navp = Navp::getInstance();
                    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
                    auto centroidNavPowerVec3 = RecastAdapter::convertFromRecastToNavPower(
                        recastAirgAdapter.calculateCentroid(cellLocation.polyRef));
                    auto area = navp.posToAreaMap.find(centroidNavPowerVec3);
                    if (area != navp.posToAreaMap.end() && area->second->m_area->m_usageFlags ==
                        NavPower::AreaUsageFlags::AREA_STEPS) {
                        isValid = true;
                    } else {
                        // Check if cell is blocked by any obstacles
                        float radius = 0.1;
                        if (NearestOuterEdge(&navQuery, cellLocation, radius, nullptr, nullptr)) {
                            isValid = false;
                        } else {
                            isValid = true;
                        }
                    }
                }
            }
            *pBitmap++ = isValid;
        }
    }
}

void GridGenerator::CalculateConnectivity(const bool *cellBitmap, int *pCellConnectivity) {
    // This logic involves checking the occupancy of neighboring cells based on the given direction and returning a connectivity score.

    int cellIndex = 0;
    for (int yi = 0; yi < 5; yi++) {
        for (int xi = 0; xi < 5; xi++) {
            cellIndex = yi * 5 + xi;
            // Get cell occupancy status
            bool isOccupied = *(cellBitmap + static_cast<unsigned int>(cellIndex)) != 0;

            // Initialize connectivity to 0
            pCellConnectivity[cellIndex] = 0;

            // Handle special case: cell is completely blocked
            if (isOccupied && *(cellBitmap + static_cast<unsigned int>(cellIndex))) {
                pCellConnectivity[cellIndex] = 100; // Assign high value for blocked cells
                continue;
            }

            // Check connectivity in each direction (right, left, down, up)
            if (isOccupied) {
                // Check right connectivity
                int rightConnectivity = 1;
                for (int i = xi + 2; i < 5 && isOccupied; ++i) {
                    isOccupied = *(cellBitmap + 4 * yi + yi + i) != 0;
                }
                if (!isOccupied) {
                    rightConnectivity = 0;
                }

                // Check left connectivity
                int leftConnectivity = 1;
                for (int i = xi; i >= 0 && isOccupied; --i) {
                    isOccupied = *(cellBitmap + 4 * yi + yi + i) != 0;
                }
                if (!isOccupied) {
                    leftConnectivity--;
                }

                // Check down connectivity
                int downConnectivity = 1;
                for (int i = xi + 1; i < 5 && isOccupied; ++i) {
                    isOccupied = *(cellBitmap + 4 * i + i + xi + 1) != 0;
                }
                if (!isOccupied) {
                    downConnectivity--;
                }

                // Check up connectivity
                int upConnectivity = 1;
                for (int i = xi - 1; i >= 0 && isOccupied; --i) {
                    isOccupied = *(cellBitmap + 4 * i + i + xi + 1) != 0;
                }
                if (!isOccupied) {
                    upConnectivity--;
                }

                // Calculate total connectivity
                pCellConnectivity[cellIndex] = rightConnectivity + leftConnectivity + downConnectivity + upConnectivity;
            }
        }
    }
}

bool GridGenerator::NearestOuterEdge(dtNavMeshQuery *navQuery, Pathfinding::ZPFLocation &lFrom, float fRadius,
                                     float4 *edgeNavPowerResult,
                                     float4 *edgeNavPowerNormal) {
    // Check if the input location is inside a valid area
    if (!IsInside(navQuery, &lFrom)) {
        return false;
    }

    // Find the closest reachable areas within the specified radius
    std::vector<dtPolyRef> polys;
    int numAreas = 0;
    RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
    polys = recastAirgAdapter.getClosestReachablePolys(navQuery, {lFrom.pos.x, lFrom.pos.y, lFrom.pos.z}, lFrom.polyRef,
                                                       4);
    numAreas = polys.size();
    if (numAreas <= 0) {
        return false;
    }

    // Initialize variables
    float minDistanceSqr = fRadius * fRadius;
    float closestDistance = 0.0f;
    bool foundEdge = false;

    // Iterate through each reachable area
    for (int areaIndex = 0; areaIndex < numAreas; ++areaIndex) {
        int numEdges = 0;
        dtPolyRef polyRef = polys[areaIndex];
        numEdges = recastAirgAdapter.getEdges(polyRef).size();
        // Iterate through each edge of the current area
        for (int edgeIndex = 0; edgeIndex < numEdges; ++edgeIndex) {
            // Get adjacent area and check if it's valid
            bool adJacentAreaIsValid = false;
            polyRef = polys[areaIndex];
            adJacentAreaIsValid = recastAirgAdapter.getAdjacentPoly(polyRef, edgeIndex) != 0;
            if (adJacentAreaIsValid) {
                continue;
            }
            Vec3 edgeNavPowerStart;
            Vec3 edgeNavPowerEnd;
            polyRef = polys[areaIndex];
            auto edgesRecast = recastAirgAdapter.getEdges(polyRef);
            edgeNavPowerStart = RecastAdapter::convertFromRecastToNavPower(edgesRecast[edgeIndex]);
            edgeNavPowerEnd = RecastAdapter::convertFromRecastToNavPower(edgesRecast[(edgeIndex + 1) % numEdges]);

            // Calculate the closest point on the edge
            float4 edgeNavPowerStartPos = {edgeNavPowerStart.X, edgeNavPowerStart.Y, edgeNavPowerStart.Z, 1.0f};
            float4 edgeNavPowerEndPos = {edgeNavPowerEnd.X, edgeNavPowerEnd.Y, edgeNavPowerEnd.Z, 1.0f};
            float4 currentNavPowerPoint;
            float4 pointNavPower = {lFrom.pos.x, lFrom.pos.y, lFrom.pos.z, 1.0};
            Pathfinding::ClosestPointOnSegment(&currentNavPowerPoint, &edgeNavPowerStartPos, &edgeNavPowerEndPos,
                                               &pointNavPower);

            // Calculate distance squared from the input location to the current point
            const float4 distance = pointNavPower - currentNavPowerPoint;

            // Update the closest point and normal if the current distance is smaller
            if (const float distanceSqr = distance.x * distance.x + distance.y * distance.y;
                distanceSqr < minDistanceSqr) {
                const float4 closestNavPowerPoint = currentNavPowerPoint;

                // Calculate edge normal
                const float4 edgeNavPowerVector = edgeNavPowerEndPos - edgeNavPowerStartPos;
                float4 closestNavPowerNormal = {-edgeNavPowerVector.y, edgeNavPowerVector.x, 0.0f, 0.0f};
                closestNavPowerNormal = Math::Normalize(closestNavPowerNormal);
                if (foundEdge &&
                    edgeNavPowerResult != nullptr &&
                    (currentNavPowerPoint.x == edgeNavPowerResult->x) &&
                    (currentNavPowerPoint.y == edgeNavPowerResult->y) &&
                    (currentNavPowerPoint.z == edgeNavPowerResult->z) &&
                    (currentNavPowerPoint.w == edgeNavPowerResult->w)
                ) {
                    if (edgeNavPowerNormal != nullptr) {
                        *edgeNavPowerNormal = *edgeNavPowerNormal + closestNavPowerNormal;
                    }
                    closestDistance += 1.0f;
                } else {
                    if (edgeNavPowerNormal != nullptr) {
                        *edgeNavPowerNormal = closestNavPowerNormal;
                    }
                    closestDistance = 1.0f;
                }
                foundEdge = true;

                if (edgeNavPowerResult != nullptr) {
                    *edgeNavPowerResult = closestNavPowerPoint;
                }
                minDistanceSqr = distanceSqr;
            }
        }
    }

    return foundEdge;
}

void GridGenerator::buildVisionAndDeadEndData() {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Started building Vision and Dead End data at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();
    Airg &airg = Airg::getInstance();

    ReasoningGrid *grid = airg.reasoningGrid;

    for (int waypointIndex = 0; waypointIndex < grid->m_WaypointList.size(); waypointIndex++) {
        Waypoint &waypoint = grid->m_WaypointList[waypointIndex];
        waypoint.nVisionDataOffset = waypointIndex * 556;
    }
    std::vector<uint8_t> newVisibilityData(grid->m_nNodeCount * 556, 255);
    grid->m_pVisibilityData = newVisibilityData;
    grid->m_deadEndData.m_aBytes.clear();
    grid->m_deadEndData.m_nSize = grid->m_nNodeCount;

    Logger::log(NK_INFO, "Built Vision and Dead End data.");

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished building Vision and Dead End data in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.data());
    airg.buildingVisionAndDeadEndData = false;
}
