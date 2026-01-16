#pragma once
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum MeshType {
    ALOC,
    PRIM
};

enum SceneMeshBuildType {
    INSTANCE,
    COPY
};

class Obj {
    explicit Obj();

    static void updateObjDialogControls(HWND hDlg);

public:
    static Obj &getInstance() {
        static Obj instance;
        return instance;
    }

    std::string loadObjName;
    std::string saveObjName;
    std::string lastObjFileName;
    std::string lastSaveObjFileName;
    std::string generatedObjName;
    bool objLoaded;
    bool showObj;
    bool loadObj;
    std::vector<std::string> files;
    std::string objToLoad;
    std::vector<bool> objLoadDone;
    bool startedObjGeneration;
    bool blenderObjStarted;
    bool blenderObjGenerationDone;
    bool blendFileOnlyBuild;
    bool blendFileAndObjBuild;
    bool filterToIncludeBox;
    bool glacier2ObjDebugLogsEnabled;
    bool errorBuilding;
    bool skipExtractingAlocsOrPrims;
    bool errorExtracting;
    bool extractingAlocsOrPrims;
    bool doneExtractingAlocsOrPrims;
    std::map<std::string, std::pair<int, int> > objectTriangleRanges;
    bool doObjHitTest;
    MeshType meshTypeForBuild;
    SceneMeshBuildType sceneMeshBuildType;
    bool primLods[8];
    bool blendFileBuilt;
    static HWND hObjDialog;
    static std::string gameVersion;

    static char *openSetBlenderFileDialog(const char *lastBlenderFile);

    void loadSettings();

    void loadObjMesh();

    void handleBuildBlendAndObjFromSceneClicked();

    static void copyFile(const std::string &from, const std::string &to, const std::string& filetype);

    void saveObjMesh(char *objToCopy, char *newFileName);

    void saveBlendMesh(std::string objToCopy, std::string newFileName);

    void buildObjFromNavp(bool alsoLoadIntoUi);

    void buildObjFromScene();

    void finalizeObjBuild();

    static void renderObj();

    static char *openLoadObjFileDialog();

    static char *openSaveObjFileDialog();

    static char* openSaveBlendFileDialog();

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void handleOpenObjClicked();

    void handleSaveObjClicked();
    void handleSaveBlendClicked();

    [[nodiscard]] bool canLoad() const;

    static bool canBuildObjFromNavp();

    [[nodiscard]] bool canBuildObjFromScene() const;
    bool canSaveBlend() const;

    [[nodiscard]] bool canBuildBlendFromScene() const;

    [[nodiscard]] bool canBuildBlendAndObjFromScene() const;

    void handleBuildObjFromSceneClicked();

    void handleBuildBlendFromSceneClicked();

    void handleBuildObjFromNavpClicked();

    void finalizeLoad();

    [[nodiscard]] std::string buildPrimLodsString() const;

    void saveObjSettings() const;

    void resetDefaults();

    static INT_PTR ObjSettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    void showObjDialog();

    std::optional<std::jthread> backgroundWorker;

    void finalizeExtractAlocsOrPrims();

    void extractAlocsOrPrimsAndStartObjBuild();
};
