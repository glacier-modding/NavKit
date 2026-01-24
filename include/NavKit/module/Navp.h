#pragma once
#include <map>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/glew.h>

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

    static int navMeshTriCount;

    static GLuint hitTestVBO;

    static GLuint hitTestVAO;

    static GLuint navMeshLineVBO;

    static GLuint navMeshLineVAO;

    static GLuint navMeshTriVBO;

    static GLuint navMeshTriVAO;

    static HWND hNavpDialog;

    static int navMeshLineCount;

    static int hitTestTriCount;

    static bool navMeshDirty;

    static bool hitTestDirty;

    static int lastSelectedArea;

    static void updateNavMeshBuffers(const NavPower::NavMesh* navMesh, int selectedIndex);

    static void updateHitTestBuffers(const NavPower::NavMesh* navMesh);

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

    void renderExclusionBoxes() const;

    void renderExclusionBoxesForHitTest() const;

    void renderNavMesh();

    void renderNavMeshForHitTest() const;

    void finalizeBuild();

    static void updateNavpDialogControls(HWND hwnd);

    static INT_PTR CALLBACK extractNavpDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    void showExtractNavpDialog();

    static void extractNavpFromRpkgs(const std::string& hash);

    void buildAreaMaps();

    void setSelectedNavpAreaIndex(int index);

    void setSelectedPfSeedPointIndex(int index);

    void setSelectedExclusionBoxIndex(int index);

    NavPower::NavMesh *navMesh{};
    std::vector<char> navMeshFileData{};
    std::map<NavPower::Binary::Area *, NavPower::Area *> binaryAreaToAreaMap;
    std::map<Vec3, NavPower::Area *> posToAreaMap;
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
    bool loading;

    bool stairsCheckboxValue;

    std::map<NavPower::Binary::Area *, int> binaryAreaToAreaIndexMap;

    std::string loadedNavpText;

    static std::string selectedRpkgNavp;

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void loadNavpFromFile(const std::string& fileName);

    void handleOpenNavpClicked();

    void saveNavMesh(const std::string &fileName, const std::string &extension);

    void handleSaveNavpClicked();

    bool stairsAreaSelected() const;

    bool canBuildNavp() const;

    bool canSave() const;

    void handleEditStairsClicked() const;

    void handleBuildNavpClicked();

    static void updateExclusionBoxConvexVolumes();

    void loadNavMeshFileData(const std::string &fileName);

    void loadNavMesh(const std::string &fileName, bool isFromJson, bool isFromBuildingNavp, bool isFromBuildingAirg);

    std::atomic<bool> navpBuildDone{false};

    bool building;

    std::optional<std::jthread> backgroundWorker;

    void buildNavp();

    static std::map<std::string, std::string> navpHashIoiStringMap;
private:

    static void renderArea(const NavPower::Area &area, bool selected);

    static bool areaIsStairs(const NavPower::Area &area);

    void setStairsFlags() const;

    static char *openLoadNavpFileDialog();

    static char *openSaveNavpFileDialog();

    std::string loadNavpName;
    std::string lastLoadNavpFile;
    std::string saveNavpName;
    std::string lastSaveNavpFile;
    std::string outputNavpFilename = "output.navp";
};
