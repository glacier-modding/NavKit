#pragma once
#include <map>
#include <string>
#include <vector>

#include "../model/PfBoxes.h"

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

    static Navp &getInstance() {
        static Navp instance;
        return instance;
    }

    void resetDefaults();

    void renderExclusionBoxes();

    void renderNavMesh();

    void renderNavMeshForHitTest() const;

    void drawMenu();

    void finalizeBuild();

    void buildAreaMaps();

    void setSelectedNavpAreaIndex(int index);

    NavPower::NavMesh *navMesh{};
    void *navMeshFileData{};
    std::map<NavPower::Binary::Area *, NavPower::Area *> binaryAreaToAreaMap{};
    std::map<Vec3, NavPower::Area *> posToAreaMap{};
    int selectedNavpAreaIndex;
    bool navpLoaded;
    bool showNavp;
    bool showNavpIndices;
    bool showPfExclusionBoxes;
    bool doNavpHitTest{};
    int navpScroll;
    bool loading;

    float bBoxPosX;
    float bBoxPosY;
    float bBoxPosZ;
    float bBoxSizeX;
    float bBoxSizeY;
    float bBoxSizeZ;

    float lastBBoxPosX;
    float lastBBoxPosY;
    float lastBBoxPosZ;
    float lastBBoxSizeX;
    float lastBBoxSizeY;
    float lastBBoxSizeZ;

    bool stairsCheckboxValue;

    float bBoxPos[3];
    float bBoxSize[3];
    std::vector<PfBoxes::PfBox> exclusionBoxes;

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void setBBox(float *pos, float *size);

    void updateExclusionBoxConvexVolumes();

private:
    static void buildNavp();

    void renderArea(NavPower::Area area, bool selected, int areaIndex);

    static bool areaIsStairs(NavPower::Area area);

    void setStairsFlags() const;

    static void loadNavMesh(char *fileName, bool isFromJson, bool isFromBuilding);

    static void loadNavMeshFileData(char *fileName);

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
