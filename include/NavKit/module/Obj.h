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
    const std::string meshesFolder = "Obj";
    std::string objToLoad;
    std::vector<bool> objLoadDone;
    bool startedObjGeneration;
    bool blenderObjStarted;
    bool blenderObjGenerationDone;
    bool blendFileOnlyExtract;
    bool glacier2ObjDebugLogsEnabled;
    bool errorBuilding;
    bool errorExtracting;
    bool extractingAlocsOrPrims;
    bool doneExtractingAlocsOrPrims;
    std::map<std::string, std::pair<int, int> > objectTriangleRanges;
    bool doObjHitTest;
    MeshType meshTypeForBuild;
    bool primLods[8];
    static HWND hObjDialog;

    static char *openSetBlenderFileDialog(const char *lastBlenderFile);

    void loadObjMesh();

    static void copyObjFile(const std::string &from, const std::string &to);

    void saveObjMesh(char *objToCopy, char *newFileName);

    void buildObjFromNavp(bool alsoLoadIntoUi);

    void buildObj();

    void finalizeObjBuild();

    static void renderObj();

    static char *openLoadObjFileDialog(const char *lastObjFolder);

    static char *openSaveObjFileDialog(char *lastObjFolder);

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void handleOpenObjClicked();

    void handleSaveObjClicked();

    bool canLoad() const;

    static bool canBuildObjFromNavp();

    bool canBuildObjFromScene() const;

    bool canBuildBlendFromScene() const;

    void handleBuildObjFromSceneClicked();

    void handleBuildBlendFromSceneClicked();

    void handleBuildObjFromNavpClicked();

    void finalizeLoad();

    [[nodiscard]] std::string buildPrimLodsString() const;

    void saveObjSettings() const;

    static INT_PTR ObjSettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    void showObjDialog();

    std::optional<std::jthread> backgroundWorker;

    void finalizeExtractAlocsOrPrims();

private:
    void extractAlocsOrPrims();
};
