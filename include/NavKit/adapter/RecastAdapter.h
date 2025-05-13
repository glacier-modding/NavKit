#pragma once
#include <deque>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <mutex>
#include <string>
#include <vector>

#include "../../RecastDemo/Sample_TileMesh.h"

#include "../../NavWeakness/Vec3.h"

struct ConvexVolume;

namespace ZPathfinding {
    class Vec3;
    class PfBox;
}

class Sample;
class BuildContext;
class InputGeom;
class DebugDrawGL;

class RecastAdapter {
    RecastAdapter();

public:
    static RecastAdapter &getInstance() {
        static RecastAdapter instance;
        return instance;
    }

    static RecastAdapter &getAirgInstance() {
        static RecastAdapter airgInstance;
        return airgInstance;
    }

    void log(int category, const std::string &message) const;

    void drawInputGeom() const;

    [[nodiscard]] bool loadInputGeom(const std::string &fileName) const;

    void setMeshBBox(const float *bBoxMin, const float *bBoxMax) const;

    [[nodiscard]] const float *getBBoxMin() const;

    [[nodiscard]] const float *getBBoxMax() const;

    [[nodiscard]] std::pair<int, int> getGridSize() const;

    void handleMeshChanged() const;

    [[nodiscard]] bool handleBuild() const;

    void cleanup() const;

    bool handleBuildForAirg() const;

    void handleCommonSettings() const;

    void resetCommonSettings() const;

    void renderRecastNavmesh(bool isAirgInstance);

    dtPolyRef getPoly(int tileIndex, int polyIndex) const;

    dtStatus findNearestPoly(const float *recastPos, dtPolyRef *polyRef, float *nearestPt, bool includeExcludedAreas) const;

    void findPfSeedPointAreas();

    void excludeNonReachableAreas();

    void save(const std::string &data) const;

    [[nodiscard]] std::mutex &getLogMutex() const;

    [[nodiscard]] int getVertCount() const;

    [[nodiscard]] int getTriCount() const;

    [[nodiscard]] int getLogCount() const;

    std::deque<std::string> &getLogBuffer() const;

    void addConvexVolume(ZPathfinding::PfBox &pfBox) const;

    [[nodiscard]] const ConvexVolume *getConvexVolumes() const;

    int getConvexVolumeCount() const;

    void clearConvexVolumes() const;

    dtPolyRef getPolyRefForLink(const dtLink &link) const;

    dtPolyRef getFirstLink(dtPolyRef polyRef) const;

    bool PFLineBlocked(const Vec3 &recastStart, const Vec3 &recastEnd) const;

    dtPolyRef getAdjacentPoly(dtPolyRef poly, int edgeIndex) const;

    void doHitTest(int mx, int my);

    static Vec3 convertFromNavPowerToRecast(Vec3 pos);

    static Vec3 convertFromRecastToNavPower(Vec3 pos);

    std::vector<Vec3> getEdges(dtPolyRef polyRef) const;

    Vec3 calculateNormal(dtPolyRef polyRef) const;

    Vec3 calculateCentroid(dtPolyRef polyRef) const;

    std::vector<dtPolyRef> getClosestReachablePolys(Vec3 navpowerPos, dtPolyRef navpowerStart, int maxPolys) const;

    std::vector<dtPolyRef> getClosestPolys(Vec3 navPowerPos, int maxPolys) const;

    Sample_TileMesh *sample;
    BuildContext *buildContext;
    InputGeom *inputGeom;
    DebugDrawGL *debugDraw;

    dtQueryFilter *filter;
    dtQueryFilter *filterWithExcluded;
    bool markerPositionSet;
    bool processHitTestShift;
    float markerPosition[3]{};
    std::string selectedObject;

private:
    std::vector<dtPolyRef> pfSeedPointAreas;
};
