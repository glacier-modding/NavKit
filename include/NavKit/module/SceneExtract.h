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

    void finalizeExtract();

    std::string hitmanFolder;
    std::string outputFolder;
    bool outputSet;
    bool extractingAlocs;
    bool doneExtractingAlocs;
    bool extractingFromGame;
    bool doneExtractingFromGame;
    static const int SCENE_EXTRACT_MENU_HEIGHT;

    void setHitmanFolder(const std::string &folderName);

    void setOutputFolder(const std::string &folderName);

    void handleExtractFromGameClicked();

    bool canExtractFromGame() const;

    bool canExtractFromGameAndBuildObj() const;

    void handleExtractFromGameAndBuildObjClicked();

    int extractScroll;
    bool hitmanSet;
    bool errorExtracting;
    bool alsoBuildObj;

    static char *openHitmanFolderDialog(char *lastHitmanFolder);

    static char *openOutputFolderDialog(char *lastOutputFolder);
    std::optional<std::jthread> backgroundWorker;
    void extractScene();

private:

    void extractAlocs();

    static void extractFromGame(const std::function<void()> &callback, const std::function<void()> &errorCallback);
};
