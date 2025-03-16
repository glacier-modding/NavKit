#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/RecastDemo/Sample_SoloMesh.h"
#include "../../extern/vcpkg/packages/recastnavigation_x64-windows/include/recastnavigation/RecastDebugDraw.h"

#include "../../include/NavKit/model/PfBoxes.h"
#include "../../include/NavKit/util/Math.h"
#include "../../include/RecastDemo/InputGeom.h"

RecastAdapter::RecastAdapter() {
    buildContext = new BuildContext();
    sample = new Sample_SoloMesh();
    sample->setContext(buildContext);
    debugDraw = new DebugDrawGL();
    inputGeom = new InputGeom();
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

bool RecastAdapter::loadInputGeom(const std::string &fileName) {
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

bool RecastAdapter::handleBuild() const {
    return sample->handleBuild();
}

void RecastAdapter::handleCommonSettings() const {
    sample->handleCommonSettings();
}

void RecastAdapter::resetCommonSettings() const {
    sample->resetCommonSettings();
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

void RecastAdapter::addConvexVolume(PfBoxes::PfBox &pfBox) {
    float verts[4 * 3];
    verts[0] = -pfBox.size.x / 2;
    verts[1] = -pfBox.size.y / 2;
    verts[2] = -pfBox.size.z / 2;
    verts[3] = pfBox.size.x / 2;
    verts[4] = -pfBox.size.y / 2;
    verts[5] = -pfBox.size.z / 2;
    verts[6] = pfBox.size.x / 2;
    verts[7] = pfBox.size.y / 2;
    verts[8] = -pfBox.size.z / 2;
    verts[9] = -pfBox.size.x / 2;
    verts[10] = pfBox.size.y / 2;
    verts[11] = -pfBox.size.z / 2;
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
                               pfBox.pos.z - pfBox.size.z / 2 - 1.5,
                               pfBox.pos.z + pfBox.size.z / 2,
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
