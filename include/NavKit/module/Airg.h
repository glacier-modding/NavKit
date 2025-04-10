#pragma once
#include <string>
#include <vector>

struct Vec3;
struct ResourceConverter;
struct ResourceGenerator;
class ReasoningGrid;

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
    bool buildingVisionAndDeadEndData;
    bool connectWaypointModeEnabled;
    std::vector<bool> airgLoadState;
    std::vector<bool> airgSaveState;
    std::vector<bool> visionDataBuildState;
    bool showAirg;
    bool showAirgIndices;
    float cellColorSource;
    ResourceConverter *airgResourceConverter;
    ResourceGenerator *airgResourceGenerator;
    ReasoningGrid *reasoningGrid;
    int airgScroll;
    int selectedWaypointIndex;
    bool doAirgHitTest;

    void resetDefaults();

    void drawMenu();

    void finalizeLoad();

    void finalizeSave();

    void finalizeBuildVisionAndDeadEndData();

    void renderLayerIndices(int waypointIndex, bool selected);

    void renderCellBitmaps(int waypointIndex, bool selected);

    void renderVisionData(int waypointIndex, bool selected);

    void renderAirg();

    void renderAirgForHitTest();

    void setSelectedAirgWaypointIndex(int index);

    void saveTolerance(float tolerance);

    void saveZSpacing(float zSpacing);

    void saveZTolerance(float zTolerance);

    void connectWaypoints(int startWaypointIndex, int endWaypointIndex);

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

private:
    char *openSaveAirgFileDialog(char *lastAirgFolder);

    char *openAirgFileDialog(char *lastAirgFolder);

    static void saveAirg(Airg *airg, std::string fileName, bool isJson);

    static void loadAirg(Airg *airg, char *fileName, bool isFromJson);

    float tolerance;
    float zSpacing;
    float zTolerance;
};
