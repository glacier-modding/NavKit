#pragma once
#include <deque>
#include <mutex>
#include <string>

struct ConvexVolume;

namespace PfBoxes {
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

    void log(int category, const std::string &message) const;

    void drawInputGeom() const;

    [[nodiscard]] bool loadInputGeom(const std::string & fileName);

    void setMeshBBox(const float * bBoxMin, const float * bBoxMax) const;
    [[nodiscard]] const float* getBBoxMin() const;
    [[nodiscard]] const float* getBBoxMax() const;

    [[nodiscard]] std::pair<int, int> getGridSize() const;

    void handleMeshChanged() const;
    [[nodiscard]] bool handleBuild() const;

    void handleCommonSettings() const;

    void resetCommonSettings() const;

    void save(const std::string &data) const;

    [[nodiscard]] std::mutex &getLogMutex() const;

    [[nodiscard]] int getVertCount() const;

    [[nodiscard]] int getTriCount() const;

    [[nodiscard]] int getLogCount() const;

    std::deque<std::string> &getLogBuffer() const;

    void addConvexVolume(PfBoxes::PfBox &pfBox);

    [[nodiscard]] const ConvexVolume *getConvexVolumes() const;

    int getConvexVolumeCount() const;

    void clearConvexVolumes() const;

    Sample *sample;
    BuildContext *buildContext;
    InputGeom *inputGeom;
    DebugDrawGL *debugDraw;
};
