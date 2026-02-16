#pragma once
#include <optional>
#include <string>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <map>
#include <GL/glew.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <windows.h>
#include "../model/ReasoningGrid.h"

struct Vec3;
struct ResourceConverter;
struct ResourceGenerator;
class ReasoningGrid;

enum CellColorDataSource {
    OFF, AIRG_BITMAP, VISION_DATA, LAYER
};

struct AirgVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 color;
};

class Airg {
public:
    explicit Airg();

    ~Airg();

    static GLuint airgLineVao;

    static GLuint airgTriVbo;

    static GLuint airgTriVao;

    static Airg& getInstance() {
        static Airg instance;
        return instance;
    }

    std::string airgName;
    std::string lastLoadAirgFile;
    std::string saveAirgName;
    std::string lastSaveAirgFile;
    bool airgLoaded;
    bool airgLoading;
    bool airgBuilding;
    bool connectWaypointModeEnabled;
    std::vector<bool> airgSaveState;
    bool showAirg;
    bool showAirgIndices;
    bool showRecastDebugInfo;
    CellColorDataSource cellColorSource;
    ResourceConverter* airgResourceConverter;
    ResourceGenerator* airgResourceGenerator;
    ReasoningGrid* reasoningGrid;
    int selectedWaypointIndex;
    bool doAirgHitTest;
    bool buildingVisionAndDeadEndData;

    static void resetDefaults();

    void finalizeSave();

    static int visibilityDataSize(const ReasoningGrid* reasoningGrid, int waypointIndex);

    void build();

    static void addWaypointGeometry(std::vector<AirgVertex>& triVerts, std::vector<AirgVertex>& lineVerts,
                                    const Waypoint& waypoint, bool selected, const glm::vec4& color,
                                    bool forceFan = false);

    void renderLayerIndices(int waypointIndex) const;

    void renderCellBitmaps(int waypointIndex, bool selected);

    void renderVisionData(int waypointIndex, bool selected) const;

    void renderAirg();

    void renderAirgForHitTest() const;

    void setSelectedAirgWaypointIndex(int index);

    void connectWaypoints(int startWaypointIndex, int endWaypointIndex);

    void setLastLoadFileName(const char* fileName);

    void setLastSaveFileName(const char* fileName);

    void handleOpenAirgClicked();

    void handleSaveAirgClicked();

    void loadAirgFromFile(const std::string& fileName);

    static void updateAirgDialogControls(HWND hwnd);

    static INT_PTR CALLBACK extractAirgDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    void showExtractAirgDialog();

    static void extractAirgFromRpkgs(const std::string& hash);

    [[nodiscard]] bool canLoad() const;

    [[nodiscard]] bool canSave() const;

    [[nodiscard]] bool canBuildAirg() const;

    void handleBuildAirgClicked();

    [[nodiscard]] bool canEnterConnectWaypointMode() const;

    void handleConnectWaypointClicked();

    std::optional<std::jthread> backgroundWorker;

    void showAirgDialog();

    static void UpdateDialogControls(HWND hDlg);

    static HWND hAirgDialog;

    std::string loadedAirgText;

    static std::string selectedRpkgAirg;

    static std::map<std::string, std::string> airgHashIoiStringMap;

private:
    static GLuint airgLineVbo;

    static int airgTriCount;

    static int airgLineCount;

    static bool airgDirty;

    static GLuint airgHitTestVao;

    static GLuint airgHitTestVbo;

    static int airgHitTestCount;

    static bool airgHitTestDirty;

    static INT_PTR CALLBACK airgDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    static char* openSaveAirgFileDialog(char* lastAirgFolder);

    static char* openAirgFileDialog(const char* lastAirgFolder);

    static void saveAirg(Airg* airg, const std::string& fileName, bool isJson);

    static void loadAirg(Airg* airg, const std::string& fileName, bool isFromJson);
};
