#pragma once

#include "NavKit.h"

class NavKit;
class ResourceConverter;
class ResourceGenerator;
class ReasoningGrid;

class Airg {
public:
	Airg(NavKit* navKit);
	~Airg();

	std::string airgName;
	std::string lastLoadAirgFile;
	std::string saveAirgName;
	std::string lastSaveAirgFile;
	bool airgLoaded;
	std::vector<bool> airgLoadState;
	std::vector<bool> airgSaveState;
	bool showAirg;
	ResourceConverter* airgResourceConverter;
	ResourceGenerator* airgResourceGenerator;
	ReasoningGrid* reasoningGrid;
	int airgScroll;

	void resetDefaults();
	void drawMenu();
	void finalizeLoad();
	void finalizeSave();
	void renderAirg();
	void saveTolerance(float tolerance);
	void saveSpacing(float spacing);
	void saveZSpacing(float zSpacing);

	void setLastLoadFileName(const char* fileName);
	void setLastSaveFileName(const char* fileName);

private:
	NavKit* navKit;
	char* openSaveAirgFileDialog(char* lastAirgFolder);
	char* openAirgFileDialog(char* lastAirgFolder);
	static void saveAirg(Airg* airg, std::string fileName, bool isJson);
	static void loadAirg(Airg* airg, char* fileName, bool isFromJson);
	float tolerance;
	float spacing;
	float zSpacing;
};