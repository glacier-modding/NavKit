#pragma once
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>


class Obj {
    explicit Obj();

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
    int objScroll;
    bool startedObjGeneration;
    bool blenderObjStarted;
    bool blenderObjGenerationDone;
    bool glacier2ObjDebugLogsEnabled;
    std::string blenderName;
    bool blenderSet;
    bool errorBuilding;
    std::string lastBlenderFile;
    std::map<std::string, std::pair<int, int> > objectTriangleRanges;
    bool doObjHitTest;

    static const int OBJ_MENU_HEIGHT;

    static char *openSetBlenderFileDialog(const char *lastBlenderFile);

    void loadObjMesh();

    static void copyObjFile(const std::string &from, const std::string &to);

    void saveObjMesh(char *objToCopy, char *newFileName);

    void setBlenderFile(const char *fileName);

    static void buildObjFromNavp(bool alsoLoadIntoUi);

    void buildObj(const char *blenderPath, const char *sceneFilePath, const char *outputFolder);

    void finalizeObjBuild();

    static void renderObj();

    static char *openLoadObjFileDialog(const char *lastObjFolder);

    static char *openSaveObjFileDialog(char *lastObjFolder);

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void handleOpenObjPressed();

    void handleSaveObjPressed();

    bool canLoad() const;

    void drawMenu();

    void finalizeLoad();

    std::optional<std::jthread> backgroundWorker;
};
