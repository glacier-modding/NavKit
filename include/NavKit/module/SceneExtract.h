#pragma once
#include <functional>
#include <string>

class SceneExtract {
public:
    SceneExtract();

    ~SceneExtract();

    static SceneExtract &getInstance() {
        static SceneExtract instance;
        return instance;
    }

    void drawMenu();

    void finalizeExtract();

    std::string lastHitmanFolder;
    std::string lastOutputFolder;
    bool outputSet;
    bool extractingAlocs;
    bool doneExtractingAlocs;
    bool extractingFromGame;
    bool doneExtractingFromGame;
    static const int SCENE_EXTRACT_MENU_HEIGHT;

    void setHitmanFolder(const char *folderName);

    void setOutputFolder(const char *folderName);

    int extractScroll;
    std::string hitmanFolderName;
    bool hitmanSet;
    std::string outputFolderName;
    bool errorExtracting;
    bool alsoBuildObj;

    static char *openHitmanFolderDialog(char *lastHitmanFolder);

    static char *openOutputFolderDialog(char *lastOutputFolder);

private:
    void extractScene();

    void extractAlocs();

    static void extractFromGame(const std::function<void()> &callback, const std::function<void()> &errorCallback);
};
