#pragma once

#include "NavKit.h"
#include "PfBoxes.h"

class NavKit;

class SceneExtract {
public:
	SceneExtract(NavKit* navKit);

	void drawMenu();
	void finalizeExtract();
	std::string lastBlenderFile;
	std::string lastHitmanFolder;
	std::string lastOutputFolder;
	void setHitmanFolder(const char* folderName);
	void setOutputFolder(const char* folderName);
	void setBlenderFile(const char* fileName);

private:
	NavKit* navKit;

	std::string blenderName;
	bool blenderSet;
	int extractScroll;
	bool startedObjGeneration;
	std::vector<bool> extractionDone;
	std::string hitmanFolderName;
	bool hitmanSet;
	std::string outputFolderName;
	bool outputSet;

	static void runCommand(SceneExtract* sceneExtract, std::string command);

	char* openSetBlenderFileDialog(char* lastBlenderFile);
	char* openHitmanFolderDialog(char* lastHitmanFolder);
	char* openOutputFolderDialog(char* lastOutputFolder);
	void extractScene(char* hitmanFolder, char* outputFolder);
	void generateObj(char* blenderPath, char* outputFolder);
};