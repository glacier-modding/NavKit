#pragma once
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <wx/dialog.h>
#include <set>
#include <GL/glew.h>

#include "../render/Model.h"
#include "../render/Shader.h"
#include "../model/Json.h"

enum MeshType { ALOC, PRIM };

enum SceneMeshBuildType { INSTANCE, COPY };

class SceneMesh {
    explicit SceneMesh();

    static GLuint tileTextureId;

    static void loadTileTexture();

    static void updateSceneMeshDialogControls(wxDialog* dialog);

public:
    static SceneMesh& getInstance() {
        static SceneMesh instance;
        return instance;
    }

    std::string loadGlbName;
    std::string saveGlbName;
    std::string lastGlbFileName;
    std::string lastSaveGlbFileName;
    std::string generatedGlbName;
    bool glbLoaded;
    bool showGlb;
    bool loadGlb;
    std::vector<std::string> files;
    std::string glbToLoad;
    std::vector<bool> glbLoadDone;
    bool startedSceneMeshGeneration;
    bool blenderSceneMeshBuildStarted;
    bool blenderSceneMeshGenerationDone;
    bool blendFileOnlyBuild;
    bool blendFileAndGlbBuild;
    bool filterToIncludeBox;
    bool errorBuilding;
    bool skipExtractingAlocsOrPrims;
    bool errorExtracting;
    bool extractingResources;
    bool doneExtractingAlocsOrPrims;
    std::map<std::string, std::pair<int, int>> objectTriangleRanges;
    bool doGlbHitTest;
    MeshType meshTypeForBuild;
    SceneMeshBuildType sceneMeshBuildType;
    bool onlyCollidable = true;
    bool primLods[8];
    bool blendFileBuilt;
    bool extractTextures;
    bool applyTextures;
    static wxDialog* hSceneMeshDialog;
    Model model;

    static char* openSetBlenderFileDialog();

    void loadSettings();

    void loadGlbMesh();

    void handleBuildBlendAndGlbFromSceneClicked();

    static void copyFile(const std::string& from, const std::string& to, const std::string& filetype);

    void saveGlbMesh(char* glbToCopy, char* newFileName);

    void saveBlendMesh(std::string blendToCopy, std::string newFileName);

    void buildGlbFromNavp(bool alsoLoadIntoUi);

    void buildSceneMeshFromScene();

    void finalizeSceneMeshBuild();

    void renderGlb() const;

    static void renderGlbUsingRecast();

    static char* openLoadGlbFileDialog();

    static char* openSaveGlbFileDialog();

    static char* openSaveBlendFileDialog();

    void setLastLoadFileName(const char* fileName);

    void setLastSaveFileName(const char* fileName);

    void handleOpenGlbClicked();

    void handleSaveGlbClicked();

    void handleSaveBlendClicked();

    [[nodiscard]] bool canLoad() const;

    static bool canBuildGlbFromNavp();

    [[nodiscard]] bool canBuildGlbFromScene() const;

    bool canSaveBlend() const;

    [[nodiscard]] bool canBuildBlendFromScene() const;

    [[nodiscard]] bool canBuildBlendAndGlbFromScene() const;

    void handleBuildGlbFromSceneClicked();

    void handleBuildBlendFromSceneClicked();

    void handleBuildGlbFromNavpClicked();

    void finalizeLoad();

    [[nodiscard]] std::string buildPrimLodsString() const;

    void saveSceneMeshSettings() const;

    void resetDefaults();

    void showSceneMeshDialog();

    std::optional<std::jthread> backgroundWorker;

    void finalizeExtractResources();

    bool shouldExtractTextures() const;

    void extractResourcesAndStartSceneMeshBuild();
};
