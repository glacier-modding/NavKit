#pragma once
#include <string>
#include <vector>

class SceneExtract {
public:
    SceneExtract();

    ~SceneExtract();

    static SceneExtract & getInstance() {
        static SceneExtract instance;
        return instance;
    }

    void drawMenu();

    void finalizeExtract();

    void finalizeObjBuild();

    std::string lastBlenderFile;
    std::string lastHitmanFolder;
    std::string lastOutputFolder;
    bool glacier2ObjDebugLogsEnabled;

    void setHitmanFolder(const char *folderName);

    void setOutputFolder(const char *folderName);

    void setBlenderFile(const char *fileName);

private:
    std::string blenderName;
    bool blenderSet;
    int extractScroll;
    bool startedObjGeneration;
    std::vector<bool> extractionDone;
    bool blenderObjGenerationDone;
    bool blenderObjStarted;
    bool buildDone2;
    std::string hitmanFolderName;
    bool hitmanSet;
    std::string outputFolderName;
    bool outputSet;
    bool errorExtracting;
    bool alsoBuildObj;
    std::vector<HANDLE> handles;
    bool closing;

    static void runCommand(SceneExtract *sceneExtract, std::string command, std::string logFileName, bool extracting);

    char *openSetBlenderFileDialog(char *lastBlenderFile);

    char *openHitmanFolderDialog(char *lastHitmanFolder);

    char *openOutputFolderDialog(char *lastOutputFolder);

    void extractScene(char *hitmanFolder, char *outputFolder);

    void buildObj(char *blenderPath, char *outputFolder);
};
