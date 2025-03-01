#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/RecastDemo/Sample_SoloMesh.h"
#include "../../extern/vcpkg/packages/recastnavigation_x64-windows/include/recastnavigation/RecastDebugDraw.h"
#include "../../include/RecastDemo/InputGeom.h"

RecastAdapter::RecastAdapter() {
    buildContext = new BuildContext();
    sample = new Sample_SoloMesh();
    sample->setContext(buildContext);
    debugDraw = new DebugDrawGL();
    inputGeom = nullptr;
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
    delete inputGeom;
    inputGeom = new InputGeom();
    return inputGeom->load(buildContext, fileName);
}

void RecastAdapter::resetInputGeom() {
    delete inputGeom;
    inputGeom = new InputGeom();
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

int RecastAdapter::getLogCount() const{
    return buildContext->getLogCount();
}

std::deque<std::string> &RecastAdapter::getLogBuffer() const {
    return buildContext->m_logBuffer;
}
