#pragma once

#include <wx/dialog.h>
#include <thread>
#include <mutex>
#include <map>
#include <optional>
#include <GL/glew.h>

class SCrowdMapData;

class Crmd {
public:
    explicit Crmd();

    ~Crmd() = default;

    static Crmd& getInstance() {
        static Crmd instance;
        return instance;
    }

    void renderCrowdMapData() const;
    void loadCrmdFromFile(const std::string& fileName);
    void loadCrowdMapData(const std::string& fileName);
    void handleOpenCrmdClicked();
    void setLastLoadFileName(const char* fileName);
    void clear();
    static void updateCrmdDialogControls(wxDialog* dialog);
    void showExtractCrmdDialog();
    static void extractCrmdFromRpkgs(const std::string& hash);

    std::optional<std::jthread> backgroundWorker;
    SCrowdMapData* crowdMapData{};
    bool crmdLoaded;
    bool loading;
    bool showCrmd;
    std::string loadedCrmdText;
    static std::string selectedRpkgCrmd;
    static std::map<std::string, std::string> crmdHashIoiStringMap;
    static std::mutex crmdHashIoiStringMapMutex;
    static wxDialog* hCrmdDialog;

private:
    static char* openLoadCrmdFileDialog();
    std::string loadCrmdName;
    std::string lastLoadCrmdFile;
};
