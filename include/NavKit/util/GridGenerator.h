#pragma once

#include "../model/ReasoningGrid.h"
#include "../util/Pathfinding.h"
#include <DetourNavMesh.h>

class Properties;

class GridGenerator {
public:
    static GridGenerator &getInstance() {
        static GridGenerator instance;
        return instance;
    }

    static bool initRecastAirgAdapter();

    std::map<int, std::vector<int> > m_WaypointMap{};
    std::map<int, std::vector<Pathfinding::SGCell> > waypointCells{};

    static void build();

    void addVisibilityData(ReasoningGrid *grid);

    void GenerateGrid();

    static void GetGridProperties();

    void GenerateWaypointNodes();

    void GenerateWaypointConnectivityMap();

    static void AlignNodes();

    void GenerateConnections();

    void GenerateLayerIndices();

    static Pathfinding::ZPFLocation *MapLocation_Internal(Pathfinding::ZPFLocation *result, const float4 *vPosNavPower,
                                                   float fAcceptance, dtPolyRef startPolyRef);

    static bool MapLocation(const float4 *vNavPowerPos, Pathfinding::ZPFLocation *lMapped);

    static float4 MapToCell(const float4 *vCellNavPowerUpperLeft, const NavPower::Area &area);

    static bool IsInside(Pathfinding::ZPFLocation *location);

    static bool NearestOuterEdge(Pathfinding::ZPFLocation &location, float tolerance, float4 *edgeNavPowerResult,
                          float4 *edgeNavPowerNormal);

    static void buildVisionAndDeadEndData();

    static void CalculateConnectivity(const bool *cellBitmap, int *pCellConnectivity);

    static void GetCellBitmap(const float4 *vNavPowerPosition, bool *pBitmap);
};
