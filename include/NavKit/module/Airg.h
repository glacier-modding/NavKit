#pragma once
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

    void drawMenu();

    void finalizeSave();

    void build();

    void renderLayerIndices(int waypointIndex) const;

    void renderCellBitmaps(int waypointIndex, bool selected);

    void renderVisionData(int waypointIndex, bool selected) const;

    void renderAirg();

    void renderAirgForHitTest() const;

    void setSelectedAirgWaypointIndex(int index);

    void connectWaypoints(int startWaypointIndex, int endWaypointIndex) const;

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void handleOpenAirgPressed();

    void handleSaveAirgPressed();

    bool canLoad() const;

    bool canSave() const;

    std::optional<std::jthread> backgroundWorker;
private:
    static char *openSaveAirgFileDialog(char *lastAirgFolder);

    static char *openAirgFileDialog(const char *lastAirgFolder);

    static void saveAirg(Airg *airg, std::string fileName, bool isJson);

    static void loadAirg(Airg *airg, char *fileName, bool isFromJson);
};
