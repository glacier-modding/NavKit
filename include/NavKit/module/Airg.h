#pragma once
#include <optional>
#include <string>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct Vec3;
struct ResourceConverter;
struct ResourceGenerator;
class ReasoningGrid;

enum CellColorDataSource {
    OFF, AIRG_BITMAP, VISION_DATA, LAYER
};

class Airg {
public:
    explicit Airg();

    ~Airg();

    static Airg &getInstance() {
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
    ResourceConverter *airgResourceConverter;
    ResourceGenerator *airgResourceGenerator;
    ReasoningGrid *reasoningGrid;
    int airgScroll;
    int selectedWaypointIndex;
    bool doAirgHitTest;
    bool buildingVisionAndDeadEndData;
    static const int AIRG_MENU_HEIGHT;

    static void resetDefaults();

    void finalizeSave();

    void build();

    void renderLayerIndices(int waypointIndex) const;

    void renderCellBitmaps(int waypointIndex, bool selected);

    void renderVisionData(int waypointIndex, bool selected) const;

    void renderAirg();

    void renderAirgForHitTest() const;

    void setSelectedAirgWaypointIndex(int index);

    void connectWaypoints(int startWaypointIndex, int endWaypointIndex);

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void handleOpenAirgClicked();

    void handleSaveAirgClicked();

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

private:
    static INT_PTR CALLBACK AirgDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    static char *openSaveAirgFileDialog(char *lastAirgFolder);

    static char *openAirgFileDialog(const char *lastAirgFolder);

    static void saveAirg(Airg *airg, std::string fileName, bool isJson);

    static void loadAirg(Airg *airg, const std::string& fileName, bool isFromJson);
};
