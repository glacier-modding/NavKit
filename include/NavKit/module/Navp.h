#pragma once
#include <map>
#include <string>
#include <vector>

#include "../model/ZPathfinding.h"

struct Vec3;

namespace NavPower {
    class Area;

    namespace Binary {
        class Area;
    }

    class NavMesh;
}

class Navp {
public:
    explicit Navp();

    ~Navp();

    void renderPfSeedPoints() const;

    void renderPfSeedPointsForHitTest() const;

    static Navp &getInstance() {
        static Navp instance;
        return instance;
    }

    static Navp &getAirgInstance() {
        static Navp instance;
        return instance;
    }

    void resetDefaults();

    void renderExclusionBoxes() const;

    void renderExclusionBoxesForHitTest() const;

    void renderNavMesh();

    void renderNavMeshForHitTest() const;

    void drawMenu();

    void finalizeBuild();

    void buildAreaMaps();

    void setSelectedNavpAreaIndex(int index);

    void setSelectedPfSeedPointIndex(int index);

    void setSelectedExclusionBoxIndex(int index);

    NavPower::NavMesh *navMesh{};
    void *navMeshFileData{};
    std::map<NavPower::Binary::Area *, NavPower::Area *> binaryAreaToAreaMap{};
    std::map<Vec3, NavPower::Area *> posToAreaMap{};
    int selectedNavpAreaIndex;
    int selectedPfSeedPointIndex;
    int selectedExclusionBoxIndex;
    bool navpLoaded;
    bool showNavp;
    bool showNavpIndices;
    bool showPfExclusionBoxes;
    bool showPfSeedPoints;
    bool showRecastDebugInfo;
    bool doNavpHitTest;
    bool doNavpExclusionBoxHitTest;
    bool doNavpPfSeedPointHitTest;
    int navpScroll;
    bool loading;

    float bBoxPosX;
    float bBoxPosY;
    float bBoxPosZ;
    float bBoxScaleX;
    float bBoxScaleY;
    float bBoxScaleZ;

    float lastBBoxPosX;
    float lastBBoxPosY;
    float lastBBoxPosZ;
    float lastBBoxScaleX;
    float lastBBoxScaleY;
    float lastBBoxScaleZ;

    bool stairsCheckboxValue;

    float bBoxPos[3];
    float bBoxScale[3];
    std::map<NavPower::Binary::Area *, int> binaryAreaToAreaIndexMap;
    float pruningMode;

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void setBBox(float *pos, float *size);

    void updateExclusionBoxConvexVolumes();

    static void loadNavMeshFileData(const std::string &fileName);

    static void loadNavMesh(const std::string &fileName, bool isFromJson, bool isFromBuilding, bool loadAirgNavp);

private:
    static void buildNavp();

    void renderArea(NavPower::Area area, bool selected, int areaIndex);

    static bool areaIsStairs(NavPower::Area area);

    void setStairsFlags() const;

    static char *openLoadNavpFileDialog(char *lastNavpFolder);

    static char *openSaveNavpFileDialog(char *lastNavpFolder);

    std::string loadNavpName;
    std::string lastLoadNavpFile;
    std::string saveNavpName;
    std::string lastSaveNavpFile;
    std::string outputNavpFilename = "output.navp";
    std::vector<bool> navpLoadDone;
    std::vector<bool> navpBuildDone;
    bool building;
};
