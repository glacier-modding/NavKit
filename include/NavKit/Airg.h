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
	std::vector<bool> airgLoadDone;
	bool showAirg;
	ResourceConverter* airgResourceConverter;
	ResourceGenerator* airgResourceGenerator;
	ReasoningGrid* reasoningGrid;
	int airgScroll;

	void drawMenu();
	void finalizeLoad();
	void renderAirg();

private:
	NavKit* navKit;
	char* openSaveAirgFileDialog(char* lastAirgFolder);
	char* openAirgFileDialog(char* lastAirgFolder);
	void saveAirg(char* fileName);
	static void loadAirg(Airg* airg, char* fileName, bool isFromJson);
};