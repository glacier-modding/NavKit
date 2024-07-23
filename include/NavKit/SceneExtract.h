#pragma once

#include "NavKit.h"

class NavKit;

class SceneExtract {
public:
	SceneExtract(NavKit* navKit);

	void drawMenu();
	void finalizeExtract();

private:
	NavKit* navKit;

	std::string lastBlenderFile;
	std::string blenderName;
	bool blenderSet;
	int extractScroll;
	bool startedObjGeneration;
	std::vector<bool> extractionDone;
	std::string hitmanFolderName;
	std::string lastHitmanFolder;
	bool hitmanSet;
	std::string outputFolderName;
	std::string lastOutputFolder;
	bool outputSet;

	static void runCommand(SceneExtract* sceneExtract, std::string command);

	char* openSetBlenderFileDialog(char* lastBlenderFile);
	char* openHitmanFolderDialog(char* lastHitmanFolder);
	char* openOutputFolderDialog(char* lastOutputFolder);
	void extractScene(char* hitmanFolder, char* outputFolder);
	void generateObj(char* blenderPath, char* outputFolder);
};