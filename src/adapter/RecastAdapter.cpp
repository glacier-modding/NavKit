#include "../../include/NavKit/adapter/RecastAdapter.h"

#include <DetourNavMeshQuery.h>
#include <RecastDebugDraw.h>

#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/util/Math.h"
#include "../../include/NavWeakness/NavPower.h"
#include "../../include/RecastDemo/InputGeom.h"

#include <queue>
#include <SDL_keyboard.h>

#include <GL/glu.h>

RecastAdapter::RecastAdapter() {
    buildContext = new BuildContext();
    sample = new Sample_TileMesh();
    sample->setContext(buildContext);
    debugDraw = new DebugDrawGL();
    inputGeom = new InputGeom();
    filter = new dtQueryFilter();
    filter->setIncludeFlags(SAMPLE_POLYFLAGS_ALL ^ SAMPLE_POLYFLAGS_DISABLED);
    filter->setExcludeFlags(0);
    filter->setAreaCost(SAMPLE_POLYAREA_GROUND, 1.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_WATER, 10000.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_ROAD, 1.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_DOOR, 1.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_GRASS, 2.0f);
    filter->setAreaCost(SAMPLE_POLYAREA_JUMP, 1.5f);
    filterWithExcluded = new dtQueryFilter();
    filterWithExcluded->setIncludeFlags(SAMPLE_POLYFLAGS_ALL);
    filterWithExcluded->setExcludeFlags(0);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_GROUND, 1.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_WATER, 10000.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_ROAD, 1.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_DOOR, 1.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_GRASS, 2.0f);
    filterWithExcluded->setAreaCost(SAMPLE_POLYAREA_JUMP, 1.5f);
    markerPositionSet = false;
    processHitTestShift = false;
    markerPosition[0] = 0;
    markerPosition[1] = 0;
    markerPosition[2] = 0;
}

void RecastAdapter::log(const int category, const std::string &message) const {
    buildContext->log(static_cast<rcLogCategory>(category), message.c_str());
}

void RecastAdapter::drawInputGeom() const {
    duDebugDrawTriMesh(debugDraw, inputGeom->getMesh()->getVerts(), inputGeom->getMesh()->getVertCount(),
                       inputGeom->getMesh()->getTris(), inputGeom->getMesh()->getNormals(),
                       inputGeom->getMesh()->getTriCount(),
                       nullptr, 1.0f);
}

bool RecastAdapter::loadInputGeom(const std::string &fileName) const {
    return inputGeom->load(buildContext, fileName);
}

void RecastAdapter::setMeshBBox(const float *bBoxMin, const float *bBoxMax) const {
    if (inputGeom == nullptr) {
        return;
    }
    inputGeom->m_meshBMin[0] = bBoxMin[0];
    inputGeom->m_meshBMin[1] = bBoxMin[1];
    inputGeom->m_meshBMin[2] = bBoxMin[2];
    inputGeom->m_meshBMax[0] = bBoxMax[0];
    inputGeom->m_meshBMax[1] = bBoxMax[1];
    inputGeom->m_meshBMax[2] = bBoxMax[2];
}

const float *RecastAdapter::getBBoxMin() const {
    return inputGeom->getNavMeshBoundsMin();
}

const float *RecastAdapter::getBBoxMax() const {
    return inputGeom->getNavMeshBoundsMax();
}

std::pair<int, int> RecastAdapter::getGridSize() const {
    const float *bmin = inputGeom->getNavMeshBoundsMin();
    const float *bmax = inputGeom->getNavMeshBoundsMax();
    int gw = 0, gh = 0;
    rcCalcGridSize(bmin, bmax, sample->m_cellSize, &gw, &gh);
    return {gw, gh};
}

void RecastAdapter::handleMeshChanged() const {
    sample->handleMeshChanged(inputGeom);
}

void RecastAdapter::cleanup() const {
    sample->cleanup();
}

bool RecastAdapter::handleBuild() const {
    sample->cleanup();
    return sample->handleBuild();
}

bool RecastAdapter::handleBuildForAirg() const {
    float cellSize = sample->m_cellSize;
    float cellHeight = sample->m_cellHeight;
    float agentHeight = sample->m_agentHeight;
    float agentRadius = sample->m_agentRadius;
    float agentMaxClimb = sample->m_agentMaxClimb;
    float agentMaxSlope = sample->m_agentMaxSlope;
    float regionMinSize = sample->m_regionMinSize;
    float regionMergeSize = sample->m_regionMergeSize;
    float edgeMaxLen = sample->m_edgeMaxLen;
    float edgeMaxError = sample->m_edgeMaxError;
    float vertsPerPoly = sample->m_vertsPerPoly;
    float detailSampleDist = sample->m_detailSampleDist;
    float detailSampleMaxError = sample->m_detailSampleMaxError;
    sample->m_cellSize = cellSize;
    sample->m_cellHeight = cellHeight;
    sample->m_agentHeight = agentHeight;
    sample->m_agentRadius = 0;
    sample->m_agentMaxClimb = agentMaxClimb;
    sample->m_agentMaxSlope = agentMaxSlope + 1;
    sample->m_regionMinSize = regionMinSize;
    sample->m_regionMergeSize = regionMergeSize;
    sample->m_edgeMaxLen = edgeMaxLen;
    sample->m_edgeMaxError = edgeMaxError;
    sample->m_vertsPerPoly = vertsPerPoly;
    sample->m_detailSampleDist = detailSampleDist;
    // sample->m_detailSampleMaxError = 0.1;
    sample->handleTileSettingsWithNoUI();
    bool success = sample->handleBuild();
    sample->m_cellSize = cellSize;
    sample->m_cellHeight = cellHeight;
    sample->m_agentHeight = agentHeight;
    sample->m_agentRadius = agentRadius;
    sample->m_agentMaxClimb = agentMaxClimb;
    sample->m_agentMaxSlope = agentMaxSlope;
    sample->m_regionMinSize = regionMinSize;
    sample->m_regionMergeSize = regionMergeSize;
    sample->m_edgeMaxLen = edgeMaxLen;
    sample->m_edgeMaxError = edgeMaxError;
    sample->m_vertsPerPoly = vertsPerPoly;
    sample->m_detailSampleDist = detailSampleDist;
    sample->m_detailSampleMaxError = detailSampleMaxError;
    return success;
}

void RecastAdapter::handleCommonSettings() const {
    // sample->handleCommonSettings();
    sample->handleSettings();
}

void RecastAdapter::resetCommonSettings() const {
    sample->resetCommonSettings();
}

void RecastAdapter::renderRecastNavmesh(bool isAirgInstance) {
    const dtNavMesh *mesh = sample->getNavMesh();
    if (!mesh) {
        return;
    }
    for (int tileIndex = 0; tileIndex < mesh->getMaxTiles(); tileIndex++) {
        const dtMeshTile *tile = mesh->getTile(tileIndex);
        if (!tile || !tile->header) {
            continue;
        }
        const Vec3 redColor = {0.8, 0.0, 0.0};
        const Vec3 purpleColor = {0.8, 0.0, 0.8};
        const Vec3 color = isAirgInstance ? redColor : purpleColor;
        const Vec3 min{tile->header->bmin[0], tile->header->bmin[1], tile->header->bmin[2]};
        const Vec3 max{tile->header->bmax[0], tile->header->bmax[1], tile->header->bmax[2]};
        glColor4f(color.X, color.Y, color.Z, 0.6);
        Renderer &renderer = Renderer::getInstance();
        Vec3 camPos{renderer.cameraPos[0], renderer.cameraPos[1], renderer.cameraPos[2]};

        float distance = camPos.DistanceTo((min + max) / 2);
        if (distance > 100) {
            continue;
        }
        glBegin(GL_LINE_LOOP);
        glVertex3f(min.X, 0, min.Z);
        glVertex3f(max.X, 0, min.Z);
        glVertex3f(max.X, 0, max.Z);
        glVertex3f(min.X, 0, max.Z);
        glEnd();
        renderer.drawText(std::to_string(tileIndex + 1), { min.X, 0, min.Z }, color);
        const Vec3 tealColor = {0.8, 0.0, 0.0};
        const Vec3 greyColor = {0.8, 0.8, 0.8};
        const Vec3 polyColor = isAirgInstance ? tealColor : greyColor;
        glColor4f(polyColor.X, polyColor.Y, polyColor.Z, 0.6);

        for (int polyIndex = 0; polyIndex < tile->header->polyCount; polyIndex++) {
            dtPolyRef polyRef = getPoly(tileIndex, polyIndex);
            auto edges = getEdges(polyRef);
            glBegin(GL_LINE_LOOP);
            glVertex3f(edges[0].X, edges[0].Y, edges[0].Z);
            glVertex3f(edges[1].X, edges[1].Y, edges[1].Z);
            glVertex3f(edges[2].X, edges[2].Y, edges[2].Z);
            glEnd();
            auto centroid = calculateCentroid(polyRef); 
            renderer.drawText(("ref: " + std::to_string(polyRef) + " idx: " + std::to_string(polyIndex)).c_str(), { centroid.X, centroid.Y, centroid.Z }, color);

        }
    }
}

dtPolyRef RecastAdapter::getPoly(const int tileIndex, const int polyIndex) const {
    const dtNavMesh *mesh = sample->getNavMesh();
    if (!mesh) {
        Logger::log(NK_ERROR, "getPoly: NavMesh is null.");
        return 0;
    }

    if (tileIndex < 0 || tileIndex >= mesh->getMaxTiles()) {
        Logger::log(
            NK_ERROR,
            ("getPoly: Invalid tileIndex " + std::to_string(tileIndex) + ". Max tiles: " + std::to_string(
                 mesh->getMaxTiles())).c_str());
        return 0;
    }

    const dtMeshTile *tile = mesh->getTile(tileIndex);
    if (!tile || !tile->header) {
        Logger::log(
            NK_WARN,
            ("getPoly: Tile at index " + std::to_string(tileIndex) + " is not valid or has no header.").c_str());
        return 0;
    }

    if (polyIndex < 0 || static_cast<unsigned int>(polyIndex) >= tile->header->polyCount) {
        Logger::log(
            NK_ERROR,
            ("getPoly: Invalid polyIndex " + std::to_string(polyIndex) + " for tile " + std::to_string(tileIndex) +
             ". Poly count: " + std::to_string(tile->header->polyCount)).c_str());
        return 0;
    }

    return mesh->encodePolyId(tile->salt, tileIndex, polyIndex);
}

dtStatus RecastAdapter::findNearestPoly(const float *recastPos, dtPolyRef *polyRef, float *nearestPt, bool includeExcludedAreas = false) const {
    const dtNavMesh *navMesh = sample->getNavMesh();
    const dtNavMeshQuery *navQuery = sample->getNavMeshQuery();
    if (!navMesh) {
        return DT_FAILURE;
    }
    constexpr float halfExtents[3] = {2, 4, 2};
    if (navQuery) {
        const dtStatus result = navQuery->findNearestPoly(recastPos, halfExtents, includeExcludedAreas ? filterWithExcluded : filter, polyRef, nearestPt);
        return result;
    }
    return DT_FAILURE;
}

void RecastAdapter::findPfSeedPointAreas() {
    pfSeedPointAreas.clear();
    for (const auto &pfSeedPoint: Scene::getInstance().pfSeedPoints) {
        dtPolyRef pfSeedPointRef;
        Vec3 recastPosVec3 = convertFromNavPowerToRecast({pfSeedPoint.pos.x, pfSeedPoint.pos.y, pfSeedPoint.pos.z});
        const float recastPos[3] = {recastPosVec3.X, recastPosVec3.Y, recastPosVec3.Z};
        dtStatus result = findNearestPoly(recastPos, &pfSeedPointRef, nullptr, true);
        if (result == DT_SUCCESS) {
            if (!pfSeedPointRef) {
                Logger::log(
                    NK_INFO,
                    ("Polygon not found for PF Seed Point: " + pfSeedPoint.id).c_str());
                continue;
            }
            Logger::log(
                NK_INFO,
                ("Adding PF Seed Point poly ref: " + std::to_string(pfSeedPointRef)).c_str());
            pfSeedPointAreas.push_back(pfSeedPointRef);
        } else {
            Logger::log(
                NK_ERROR, ("Failed to find polygon for Seed point: " + std::to_string(pfSeedPointRef)).c_str());
        }
    }
}

void RecastAdapter::excludeNonReachableAreas() {
    if (pfSeedPointAreas.empty()) {
        Logger::log(NK_INFO, "No PF Seed Points found. Skipping navmesh pruning.");
        return;
    }
    std::map<dtPolyRef, bool> pathFoundForPoly;
    const dtNavMesh *cmesh = sample->getNavMesh();
    dtNavMesh *mesh = sample->getNavMesh();
    SamplePolyFlags pruneFlagType;
    Navp &navp = Navp::getInstance();
    if (navp.pruningMode == 1.0) {
        Logger::log(NK_INFO, "Using Delete PF Seed Point pruning mode.");
        pruneFlagType = SAMPLE_POLYFLAGS_DISABLED;
    } else {
        Logger::log(NK_INFO, "Using Debug PF Seed Point pruning mode.");
        pruneFlagType = SAMPLE_POLYFLAGS_SWIM;
    }
    if (!mesh) {
        return;
    }
    std::vector<int> tilePolyCounts;
    for (int tileIndex = 0; tileIndex < mesh->getMaxTiles(); ++tileIndex) {
        const dtMeshTile *tile = cmesh->getTile(tileIndex);
        if (!tile || !tile->header || !tile->dataSize) {
            tilePolyCounts.push_back(0);
            continue;
        }
        tilePolyCounts.push_back(tile->header->polyCount);
    }
    int totalAreaCount = 0;
    for (int count: tilePolyCounts) {
        totalAreaCount += count;
    }
    int validPathsFound = 0;
    std::queue<std::pair<dtPolyRef, bool>> polyAndOverrideExcludeQueue;
    for (const dtPolyRef pfSeedPointRef: pfSeedPointAreas) {
        const unsigned int tileIndex = mesh->decodePolyIdTile(pfSeedPointRef);
        const unsigned int polyIndex = mesh->decodePolyIdPoly(pfSeedPointRef);
        const dtMeshTile *tile = cmesh->getTile(tileIndex);
        dtPoly &poly = tile->polys[polyIndex];
        bool overrideExclude = poly.flags == SAMPLE_POLYFLAGS_DISABLED;
        polyAndOverrideExcludeQueue.push({pfSeedPointRef, overrideExclude});
        pathFoundForPoly[pfSeedPointRef] = true;
        poly.flags = SAMPLE_POLYFLAGS_ALL;
    }

    while (!polyAndOverrideExcludeQueue.empty()) {
        dtPolyRef currentPolyRef = polyAndOverrideExcludeQueue.front().first;
        bool overrideExclude = polyAndOverrideExcludeQueue.front().second;
        polyAndOverrideExcludeQueue.pop();
        const unsigned int tileIndex = mesh->decodePolyIdTile(currentPolyRef);
        const unsigned int polyIndex = mesh->decodePolyIdPoly(currentPolyRef);
        const dtMeshTile *tile = cmesh->getTile(tileIndex);
        const dtPoly &poly = tile->polys[polyIndex];
        validPathsFound++;
        if (validPathsFound % 100 == 0) {
            Logger::log(
                NK_INFO,
                ("Found " + std::to_string(validPathsFound) +
                 " areas with valid paths to PF Seed Point areas so far out of " +
                 std::to_string(totalAreaCount) + " total areas.").c_str());
        }
        for (unsigned int linkIndex = poly.firstLink; linkIndex != DT_NULL_LINK;
             linkIndex = tile->links[linkIndex].next) {
            const dtLink &link = tile->links[linkIndex];
            const dtMeshTile *targetTile = nullptr;
            const dtPoly *targetPoly = nullptr;
            mesh->getTileAndPolyByRef(link.ref, &targetTile, &targetPoly);
            if (targetTile && targetPoly) {
                int targetTileIndex = -1;
                for (int i = 0; i < mesh->getMaxTiles(); ++i) {
                    if (cmesh->getTile(i) == targetTile) {
                        targetTileIndex = i;
                        break;
                    }
                }
                if (targetTileIndex != -1) {
                    int targetPolyIndex = -1;
                    for (int i = 0; i < targetTile->header->polyCount; ++i) {
                        if (&targetTile->polys[i] == targetPoly) {
                            targetPolyIndex = i;
                            break;
                        }
                    }
                    if (targetPolyIndex != -1) {
                        const dtPolyRef adjacentPolyRef = mesh->encodePolyId(
                            targetTile->salt, targetTileIndex, targetPolyIndex);
                        if (!pathFoundForPoly.contains(adjacentPolyRef)) {
                            if (overrideExclude || poly.flags != SAMPLE_POLYFLAGS_DISABLED) {
                                pathFoundForPoly[adjacentPolyRef] = true;
                                polyAndOverrideExcludeQueue.push({adjacentPolyRef, overrideExclude});
                            }
                            if (overrideExclude) {
                                const unsigned int adjacentTileIndex = mesh->decodePolyIdTile(adjacentPolyRef);
                                const unsigned int adjacentPolyIndex = mesh->decodePolyIdPoly(adjacentPolyRef);
                                const dtMeshTile *adjacentTile = cmesh->getTile(adjacentTileIndex);
                                dtPoly &adjacentPoly = adjacentTile->polys[adjacentPolyIndex];
                                adjacentPoly.flags = SAMPLE_POLYFLAGS_ALL;
                            }
                        }
                    }
                }
            }
        }
    }
    Logger::log(
        NK_INFO,
        ("Found " + std::to_string(validPathsFound) + " areas with valid paths to PF Seed Point areas in total.").
        c_str());

    int areasPruned = 0;
    for (int tileIndex = 0; tileIndex < cmesh->getMaxTiles(); tileIndex++) {
        const dtMeshTile *tile = cmesh->getTile(tileIndex);
        if (!tile || !tile->header || !tile->dataSize) continue;
        int polyCount = tile->header->polyCount;
        for (int polyIndex = 0; polyIndex < polyCount; ++polyIndex) {
            const dtPolyRef polyRef = mesh->encodePolyId(tile->salt, tileIndex, polyIndex);
            if (!pathFoundForPoly[polyRef]) {
                mesh->setPolyFlags(polyRef, pruneFlagType);
                areasPruned++;
            }
        }
    }
    Logger::log(
        NK_INFO,
        ("Pruned " + std::to_string(areasPruned) + " areas with no path to any PF Seed Point.").
        c_str());
}

void RecastAdapter::save(const std::string &data) const {
    sample->saveAll(data.c_str());
}

std::mutex &RecastAdapter::getLogMutex() const {
    return buildContext->m_log_mutex;
}

int RecastAdapter::getVertCount() const {
    return inputGeom->getMesh()->getVertCount();
}

int RecastAdapter::getTriCount() const {
    return inputGeom->getMesh()->getTriCount();
}

int RecastAdapter::getLogCount() const {
    return buildContext->getLogCount();
}

std::deque<std::string> &RecastAdapter::getLogBuffer() const {
    return buildContext->m_logBuffer;
}

void RecastAdapter::addConvexVolume(ZPathfinding::PfBox &pfBox) const {
    float verts[4 * 3];
    verts[0] = -pfBox.scale.x / 2;
    verts[1] = -pfBox.scale.y / 2;
    verts[2] = -pfBox.scale.z / 2;
    verts[3] = pfBox.scale.x / 2;
    verts[4] = -pfBox.scale.y / 2;
    verts[5] = -pfBox.scale.z / 2;
    verts[6] = pfBox.scale.x / 2;
    verts[7] = pfBox.scale.y / 2;
    verts[8] = -pfBox.scale.z / 2;
    verts[9] = -pfBox.scale.x / 2;
    verts[10] = pfBox.scale.y / 2;
    verts[11] = -pfBox.scale.z / 2;
    Vec3 rotated = Math::rotatePoint(
        {verts[0], verts[1], verts[2]},
        {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w}
    );
    verts[0] = rotated.X;
    verts[1] = rotated.Y;
    verts[2] = rotated.Z;
    rotated = Math::rotatePoint(
        {verts[3], verts[4], verts[5]},
        {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w}
    );
    verts[3] = rotated.X;
    verts[4] = rotated.Y;
    verts[5] = rotated.Z;
    rotated = Math::rotatePoint(
        {verts[6], verts[7], verts[8]},
        {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w}
    );
    verts[6] = rotated.X;
    verts[7] = rotated.Y;
    verts[8] = rotated.Z;
    rotated = Math::rotatePoint(
        {verts[9], verts[10], verts[11]},
        {pfBox.rotation.x, pfBox.rotation.y, pfBox.rotation.z, pfBox.rotation.w}
    );
    verts[9] = rotated.X;
    verts[10] = rotated.Y;
    verts[11] = rotated.Z;

    verts[0] += pfBox.pos.x;
    verts[1] += pfBox.pos.y;
    verts[2] += pfBox.pos.z;
    verts[3] += pfBox.pos.x;
    verts[4] += pfBox.pos.y;
    verts[5] += pfBox.pos.z;
    verts[6] += pfBox.pos.x;
    verts[7] += pfBox.pos.y;
    verts[8] += pfBox.pos.z;
    verts[9] += pfBox.pos.x;
    verts[10] += pfBox.pos.y;
    verts[11] += pfBox.pos.z;

    for (int i = 0; i < 4; i++) {
        float recastY = verts[i * 3 + 2];
        float recastZ = -verts[i * 3 + 1];
        verts[i * 3 + 1] = recastY;
        verts[i * 3 + 2] = recastZ;
    }
    inputGeom->addConvexVolume(verts,
                               4,
                               pfBox.pos.z - pfBox.scale.z / 2 - 0.5,
                               pfBox.pos.z + pfBox.scale.z / 2,
                               1); // areaType 1 is water
}

const ConvexVolume *RecastAdapter::getConvexVolumes() const {
    return inputGeom->getConvexVolumes();
}

int RecastAdapter::getConvexVolumeCount() const {
    return inputGeom->getConvexVolumeCount();
}

void RecastAdapter::clearConvexVolumes() const {
    int count = inputGeom->getConvexVolumeCount();
    for (int i = 0; i < count; i++) {
        inputGeom->deleteConvexVolume(0);
    }
}

dtPolyRef RecastAdapter::getPolyRefForLink(const dtLink &link) const {
    dtNavMesh *mesh = sample->getNavMesh();
    const dtNavMesh *cmesh = sample->getNavMesh();
    const dtMeshTile *targetTile = nullptr;
    const dtPoly *targetPoly = nullptr;

    mesh->getTileAndPolyByRef(link.ref, &targetTile, &targetPoly);
    if (targetTile && targetPoly) {
        int targetTileIndex = -1;
        for (int i = 0; i < mesh->getMaxTiles(); ++i) {
            if (cmesh->getTile(i) == targetTile) {
                targetTileIndex = i;
                break;
            }
        }
        if (targetTileIndex != -1) {
            int targetPolyIndex = -1;
            for (int i = 0; i < targetTile->header->polyCount; ++i) {
                if (&targetTile->polys[i] == targetPoly) {
                    targetPolyIndex = i;
                    break;
                }
            }
            if (targetPolyIndex != -1) {
                return mesh->encodePolyId(
                    targetTile->salt, targetTileIndex, targetPolyIndex);
            }
        }
    }
    return 0;
}

bool RecastAdapter::PFLineBlocked(const Vec3 &recastStart, const Vec3 &recastEnd) const {
    dtNavMesh *mesh = sample->getNavMesh();
    if (!mesh) {
        return true;
    }
    const dtNavMeshQuery *navQuery = sample->getNavMeshQuery();
    if (!navQuery) {
        return true;
    }

    dtPolyRef startRef;
    const float recastStartPos[3] = {recastStart.X, recastStart.Y, recastStart.Z};
    dtStatus result = findNearestPoly(recastStartPos, &startRef, nullptr);
    if (result != DT_SUCCESS || startRef == 0) {
        Logger::log(
            NK_ERROR,
            ("PFLineBlocked: Could not find area for start pos (" + std::to_string(recastStartPos[0]) + ", " +
             std::to_string(recastStartPos[1]) + ", " + std::to_string(recastStartPos[2]) + ")").c_str());
        return true;
    }

    dtPolyRef endRef;
    const float recastEndPos[3] = {recastEnd.X, recastEnd.Y, recastEnd.Z};
    result = findNearestPoly(recastEndPos, &endRef, nullptr);
    if (result != DT_SUCCESS || endRef == 0) {
        Logger::log(
            NK_ERROR,
            ("PFLineBlocked: Could not find area for end pos (" + std::to_string(recastEndPos[0]) + ", " +
             std::to_string(recastEndPos[1]) + ", " + std::to_string(recastEndPos[2]) + ")").c_str());
        return true;
    }

    if (startRef == endRef) {
        return false;
    }

    Vec3 middle = {
        (recastStart.X + recastEnd.X) / 2.0f, (recastStart.Y + recastEnd.Y) / 2.0f, (recastStart.Z + recastEnd.Z) / 2.0f
    };
    dtPolyRef middleRef;
    const float recastMiddlePos[3] = {middle.X, middle.Y, middle.Z};
    result = findNearestPoly(recastMiddlePos, &middleRef, nullptr);
    if (result != DT_SUCCESS) {
        return true;
    }

    const float middlePosRecast[3] = {middle.X, middle.Y, middle.Z};
    const float radius = 2.25f;

    dtPolyRef resultRef[20];
    dtPolyRef resultParent[20];
    float resultCost[20];
    int resultCount;
    constexpr int maxResult = 20;

    navQuery->findPolysAroundCircle(startRef, middlePosRecast, radius, filter, resultRef, resultParent, resultCost,
                                    &resultCount, maxResult);
    std::queue<dtPolyRef> polyQueue;
    std::map<dtPolyRef, bool> pathFoundForPoly;
    polyQueue.push(middleRef);
    pathFoundForPoly[middleRef] = true;
    const dtNavMesh *cmesh = sample->getNavMesh();
    while (!polyQueue.empty()) {
        dtPolyRef currentPolyRef = polyQueue.front();
        polyQueue.pop();
        const unsigned int tileIndex = mesh->decodePolyIdTile(currentPolyRef);
        const unsigned int polyIndex = mesh->decodePolyIdPoly(currentPolyRef);
        const dtMeshTile *tile = cmesh->getTile(tileIndex);
        const dtPoly &poly = tile->polys[polyIndex];
        for (unsigned int linkIndex = poly.firstLink; linkIndex != DT_NULL_LINK;
             linkIndex = tile->links[linkIndex].next) {
            const dtLink &link = tile->links[linkIndex];
            const dtMeshTile *targetTile = nullptr;
            const dtPoly *targetPoly = nullptr;
            mesh->getTileAndPolyByRef(link.ref, &targetTile, &targetPoly);
            if (targetTile && targetPoly) {
                int targetTileIndex = -1;
                for (int i = 0; i < mesh->getMaxTiles(); ++i) {
                    if (cmesh->getTile(i) == targetTile) {
                        targetTileIndex = i;
                        break;
                    }
                }
                if (targetTileIndex != -1) {
                    int targetPolyIndex = -1;
                    for (int i = 0; i < targetTile->header->polyCount; ++i) {
                        if (&targetTile->polys[i] == targetPoly) {
                            targetPolyIndex = i;
                            break;
                        }
                    }
                    if (targetPolyIndex != -1) {
                        const dtPolyRef adjacentPolyRef = mesh->encodePolyId(
                            targetTile->salt, targetTileIndex, targetPolyIndex);
                        if (!pathFoundForPoly.contains(adjacentPolyRef) && poly.flags != SAMPLE_POLYFLAGS_DISABLED) {
                            for (int i = 0; i < resultCount; i++) {
                                if (resultRef[i] == adjacentPolyRef) {
                                    // if (resultCost[i] < radius * 2) {
                                    pathFoundForPoly[adjacentPolyRef] = true;
                                    polyQueue.push(adjacentPolyRef);
                                    break;
                                    // }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (pathFoundForPoly.contains(startRef) && pathFoundForPoly.contains(endRef)
        && pathFoundForPoly[startRef] && pathFoundForPoly[endRef]) {
        return false;
    }
    return true;
}

dtPolyRef RecastAdapter::getAdjacentPoly(const dtPolyRef polyRef, const int edgeIndex) const {
    dtNavMesh *mesh = sample->getNavMesh();
    const dtNavMesh *cmesh = sample->getNavMesh();

    const unsigned int tileIndex = mesh->decodePolyIdTile(polyRef);
    const unsigned int polyIndex = mesh->decodePolyIdPoly(polyRef);
    const dtMeshTile *tile = cmesh->getTile(tileIndex);
    const dtPoly &poly = tile->polys[polyIndex];
    for (unsigned int linkIndex = poly.firstLink; linkIndex != DT_NULL_LINK; linkIndex = tile->links[linkIndex].next) {
        const dtLink &link = tile->links[linkIndex];
        if (link.edge == edgeIndex) {
            return getPolyRefForLink(link);
        }
//        if (link.edge > edgeIndex) { // TODO: Check if this was needed. I think it was just an optimization
//            break;
//        }
    }
    return 0;
}

void RecastAdapter::doHitTest(const int mx, const int my) {
    float rayStart[3];
    float rayEnd[3];
    float hitTime;
    GLdouble x, y, z;
    Renderer &renderer = Renderer::getInstance();
    gluUnProject(mx, my, 0.0f, renderer.modelviewMatrix, renderer.projectionMatrix, renderer.viewport, &x, &y, &z);
    rayStart[0] = (float) x;
    rayStart[1] = (float) y;
    rayStart[2] = (float) z;
    gluUnProject(mx, my, 1.0f, renderer.modelviewMatrix, renderer.projectionMatrix, renderer.viewport, &x, &y, &z);
    rayEnd[0] = (float) x;
    rayEnd[1] = (float) y;
    rayEnd[2] = (float) z;
    int hit = inputGeom->raycastMesh(rayStart, rayEnd, hitTime);
    if (hit != -1) {
        if (SDL_GetModState() & KMOD_CTRL) {
            float pos[3];
            pos[0] = rayStart[0] + (rayEnd[0] - rayStart[0]) * hitTime;
            pos[1] = rayStart[1] + (rayEnd[1] - rayStart[1]) * hitTime;
            pos[2] = rayStart[2] + (rayEnd[2] - rayStart[2]) * hitTime;
            sample->handleClick(rayStart, pos, processHitTestShift);
        } else {
            // Marker
            markerPositionSet = true;
            markerPosition[0] = rayStart[0] + (rayEnd[0] - rayStart[0]) * hitTime;
            markerPosition[1] = rayStart[1] + (rayEnd[1] - rayStart[1]) * hitTime;
            markerPosition[2] = rayStart[2] + (rayEnd[2] - rayStart[2]) * hitTime;
            for (auto [object, vertexRange]: Obj::getInstance().objectTriangleRanges) {
                if (hit >= vertexRange.first && hit < vertexRange.second) {
                    selectedObject = object;
                    break;
                }
            }
            Logger::log(
                NK_INFO,
                ("Selected Object: '" + selectedObject + "' Obj vertex: " + std::to_string(hit) +
                 ". Setting marker position to: " + std::to_string(markerPosition[0]) + ", " +
                 std::to_string(markerPosition[1]) + ", " + std::to_string(markerPosition[2])).c_str());
        }
    } else {
        if (SDL_GetModState()) {
            // Marker
            markerPositionSet = false;
        }
    }
}

Vec3 RecastAdapter::convertFromNavPowerToRecast(Vec3 pos) {
    return {pos.X, pos.Z, -pos.Y};
}

Vec3 RecastAdapter::convertFromRecastToNavPower(Vec3 pos) {
    return {pos.X, -pos.Z, pos.Y};
}

std::vector<Vec3> RecastAdapter::getEdges(const dtPolyRef polyRef) const {
    const dtNavMesh *mesh = sample->getNavMesh();
    unsigned int salt = 0;
    unsigned int tileIndex = 0;
    unsigned int polyIndex = 0;
    mesh->decodePolyId(polyRef, salt, tileIndex, polyIndex);
    const dtMeshTile *tile = mesh->getTile(tileIndex);
    const dtPoly &poly = tile->polys[polyIndex];
    std::vector<Vec3> edges;
    edges.reserve(poly.vertCount);
    for (int vi = 0; vi < poly.vertCount; vi++) {
        Vec3 recastPos = {
            tile->verts[poly.verts[vi] * 3],
            tile->verts[poly.verts[vi] * 3 + 1],
            tile->verts[poly.verts[vi] * 3 + 2]
        };
        edges.push_back(recastPos);
    }
    return edges;
}

Vec3 RecastAdapter::calculateNormal(const dtPolyRef polyRef) const {
    std::vector<Vec3> edges = getEdges(polyRef);
    Vec3 v0 = edges.at(0);
    Vec3 v1 = edges.at(1);
    Vec3 v2 = edges.at(2);

    Vec3 vec1 = v1 - v0;
    Vec3 vec2 = v2 - v0;
    Vec3 cross = vec1.Cross(vec2);
    return cross.GetUnitVec();
}

Vec3 RecastAdapter::calculateCentroid(const dtPolyRef polyRef) const {
    std::vector<Vec3> edges = getEdges(polyRef);
    Vec3 normal = calculateNormal(polyRef);
    Vec3 v0 = edges.at(0);
    Vec3 v1 = edges.at(1);

    Vec3 u = (v1 - v0).GetUnitVec();
    Vec3 v = u.Cross(normal).GetUnitVec();

    std::vector<Vec3> mappedPoints;
    for (auto edge: edges) {
        Vec3 relativePos = edge - v0;
        float uCoord = relativePos.Dot(u);
        float vCoord = relativePos.Dot(v);
        Vec3 uvv = Vec3(uCoord, vCoord, 0.0);
        mappedPoints.push_back(uvv);
    }
    float sum = 0;
    for (int i = 0; i < mappedPoints.size(); i++) {
        int nextI = (i + 1) % mappedPoints.size();
        sum += mappedPoints[i].X * mappedPoints[nextI].Y - mappedPoints[nextI].X * mappedPoints[i].Y;
    }

    float sumX = 0;
    float sumY = 0;
    for (int i = 0; i < mappedPoints.size(); i++) {
        int nextI = (i + 1) % mappedPoints.size();
        float x0 = mappedPoints[i].X;
        float x1 = mappedPoints[nextI].X;
        float y0 = mappedPoints[i].Y;
        float y1 = mappedPoints[nextI].Y;

        float doubleArea = (x0 * y1) - (x1 * y0);
        sumX += (x0 + x1) * doubleArea;
        sumY += (y0 + y1) * doubleArea;
    }

    constexpr float AREA_EPSILON = 1e-9f;
    if (std::abs(sum) < AREA_EPSILON * 2.0f) {
        Logger::log(
            NK_WARN,
            ("calculateCentroid: Polygon " + std::to_string(polyRef) + " has near-zero area (" +
             std::to_string(sum / 2.0f) + "). Returning average of vertices.").c_str());
        Vec3 averagePos(0.0f, 0.0f, 0.0f);
        if (edges.empty()) return averagePos;
        for (const auto &edge: edges) {
            averagePos = averagePos + edge;
        }
        return averagePos / static_cast<float>(edges.size());
    }

    float cu = sumX / (3.0 * sum);
    float cv = sumY / (3.0 * sum);

    Vec3 cucv = Vec3(1, cu, cv);
    Vec3 xuv = Vec3(v0.X, u.X, v.X);
    Vec3 yuv = Vec3(v0.Y, u.Y, v.Y);
    Vec3 zuv = Vec3(v0.Z, u.Z, v.Z);
    float x = xuv.Dot(cucv);
    float y = yuv.Dot(cucv);
    float z = zuv.Dot(cucv);
    return {x, y, z};
}

const dtNavMesh* RecastAdapter::getNavMesh() const {
    return sample->getNavMesh();
}
/**
 * Find up to the maxPolys closest polys within the specified radius of the position which are reachable from the starting area.
 * @param navpowerPos
 * @param start
 * @param maxPolys
 * @return
 */
std::vector<dtPolyRef> RecastAdapter::getClosestReachablePolys(
    dtNavMeshQuery* navQuery,
    Vec3 navpowerPos,
    const dtPolyRef start,
    const int maxPolys) const {
    std::vector<dtPolyRef> polys;
    const dtNavMesh *navMesh = sample->getNavMesh();
    if (!navMesh || !navQuery || start == 0) {
        return polys;
    }

    const float radius = 3.0f;
    const Vec3 recastPos = convertFromNavPowerToRecast(navpowerPos);
    const float centerRecastPos[3] = {recastPos.X, recastPos.Y, recastPos.Z};

    dtPolyRef tempPolys[10];
    int actualPolyCount = 0;
    const float halfExtents[3] = {radius, radius, radius};

    if (const dtStatus status = navQuery->queryPolygons(centerRecastPos, halfExtents, filter, tempPolys,
                                                        &actualPolyCount,
                                                        maxPolys); dtStatusFailed(status)) {
        Logger::log(NK_ERROR, "GetClosestReachableAreas failed to query polygons.");
        return polys;
    }
    if (actualPolyCount == 0) {
        return polys;
    }

    for (int i = 0; i < actualPolyCount; ++i) {
        dtPolyRef endRef = tempPolys[i];
        if (start == endRef) {
            polys.push_back(endRef);
            continue;
        }

        float endRecastPos[3];
        if (dtStatusFailed(navQuery->closestPointOnPoly(endRef, centerRecastPos, endRecastPos, nullptr))) {
            continue;
        }
        float startRecastPos[3];
        if (dtStatusFailed(navQuery->closestPointOnPoly(start, centerRecastPos, startRecastPos, nullptr))) {
            continue;
        }

        int pathCount = 0;
        dtPolyRef path[128];

        if (const dtStatus pathStatus = navQuery->findPath(
                start, endRef, startRecastPos, endRecastPos, filter, path,
                &pathCount, 128);
            dtStatusSucceed(pathStatus) && pathCount > 0 && path[pathCount - 1] == endRef) {
            polys.push_back(endRef);
        }
    }

    return polys;
}

/**
 * Find up to the maxNumPolys closest areas within the specified radius of the position.
 * @param navPowerPos
 * @param maxPolys
 * @return
 */
std::vector<dtPolyRef> RecastAdapter::getClosestPolys(dtNavMeshQuery* navQuery, Vec3 navPowerPos, const int maxPolys) const {
    std::vector<dtPolyRef> polys;
    const dtNavMesh *navMesh = sample->getNavMesh();
    if (!navMesh || !navQuery) {
        return polys;
    }

    const float radius = 1.0f;
    const Vec3 recastPos = convertFromNavPowerToRecast(navPowerPos);
    const float centerRecastPos[3] = {recastPos.X, recastPos.Y, recastPos.Z};

    dtPolyRef tempPolys[20];
    int actualPolyCount = 0;
    const float halfExtents[3] = {radius, radius, radius};

    if (const dtStatus status = navQuery->queryPolygons(centerRecastPos, halfExtents, filter, tempPolys,
                                                        &actualPolyCount,
                                                        maxPolys); dtStatusFailed(status)) {
        Logger::log(NK_ERROR, "getClosestPolys failed to query polygons.");
        return polys;
    }

    for (int i = 0; i < actualPolyCount; ++i) {
        polys.push_back(tempPolys[i]);
    }
    return polys;
}
