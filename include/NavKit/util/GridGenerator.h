#pragma once

#include "../model/ReasoningGrid.h"
#include "../util/Pathfinding.h"
#include <DetourNavMesh.h>
#include <unordered_map>

class Properties;

class GridGenerator {
public:
    static GridGenerator &getInstance() {
        static GridGenerator instance;
        return instance;
    }

    static bool initRecastAirgAdapter();

    std::unordered_map<int, std::vector<int> > m_WaypointMap{};
    std::map<int, std::vector<Pathfinding::SGCell> > waypointCells{};

    static void build();

    static void addVisibilityData(ReasoningGrid *grid);

    void GenerateGrid();

    static void GetGridProperties();

    void GenerateWaypointNodes();

    void GenerateWaypointConnectivityMap();

    static void AlignNodes();

    void GenerateConnections();

    void GenerateLayerIndices();

    static Pathfinding::ZPFLocation *MapLocation_Internal(dtNavMeshQuery *navQuery, Pathfinding::ZPFLocation *result,
                                                          const float4 *vPosNavPower, float fAcceptance, dtPolyRef startPolyRef);

    static bool MapLocation(dtNavMeshQuery *navQuery, const float4 *vNavPowerPos, Pathfinding::ZPFLocation *lMapped);

    static float4 MapToCell(dtNavMeshQuery *navQuery, const float4 *vCellNavPowerUpperLeft, const NavPower::Area &area);

    static bool IsInside(dtNavMeshQuery *navQuery, Pathfinding::ZPFLocation *location);

    static bool NearestOuterEdge(dtNavMeshQuery *navQuery, Pathfinding::ZPFLocation &lFrom, float fRadius,
                                 float4 *edgeNavPowerResult, float4 *edgeNavPowerNormal);

    static void buildVisionAndDeadEndData();

    static void CalculateConnectivity(const bool *cellBitmap, int *pCellConnectivity);

    static void GetCellBitmap(const float4 *vNavPowerPosition, bool *pBitmap);
};
