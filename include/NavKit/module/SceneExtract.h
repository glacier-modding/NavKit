#pragma once
#include <functional>

class SceneExtract {
public:
    SceneExtract();

    ~SceneExtract();

    static SceneExtract &getInstance() {
        static SceneExtract instance;
        return instance;
    }

    bool extractingFromGame;
    bool doneExtractingFromGame;

    void handleExtractFromGameClicked();

    bool canExtractFromGame() const;

    bool canExtractFromGameAndBuildObj() const;

    bool canExtractFromGameAndBuildAll() const;

    void handleExtractFromGameAndBuildObjClicked();

    void handleExtractFromGameAndBuildAllClicked();

    bool alsoBuildObj;
    bool alsoBuildAll;

    static char *openHitmanFolderDialog(char *lastHitmanFolder);

    static char *openOutputFolderDialog(char *lastOutputFolder);

    std::optional<std::jthread> backgroundWorker;

    void extractScene();

    void finalizeExtractScene();

private:
    static void extractFromGame(const std::function<void()> &callback, const std::function<void()> &errorCallback);
};
