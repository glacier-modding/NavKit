#include "..\include\NavKit\SceneExtract.h"

SceneExtract::SceneExtract(NavKit* navKit): navKit(navKit) {
	lastBlenderFile = "\"C:\\Program Files\\Blender Foundation\\Blender 3.4\\blender.exe\"";
	blenderName = "Choose Blender app";
	blenderSet = false;
	extractionDone;
	startedObjGeneration = false;
	hitmanFolderName = "Choose Hitman folder";
	lastHitmanFolder = hitmanFolderName;
	hitmanSet = false;
	outputFolderName = "Choose Output folder";
	lastOutputFolder = outputFolderName;
	outputSet = false;
	extractScroll = 0;
}

void SceneExtract::setHitmanFolder(const char* folderName) {
	if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
		hitmanSet = true;
		lastHitmanFolder = folderName;
		hitmanFolderName = folderName;
		hitmanFolderName = hitmanFolderName.substr(hitmanFolderName.find_last_of("/\\") + 1);
		navKit->log(rcLogCategory::RC_LOG_PROGRESS, ("Setting Hitman folder to: " + lastHitmanFolder).c_str());
		navKit->ini.SetValue("Paths", "hitman", folderName);
		navKit->ini.SaveFile("NavKit.ini");
	} else {
		navKit->log(rcLogCategory::RC_LOG_WARNING, ("Could not find Hitman folder: " + lastHitmanFolder).c_str());
	}
}

void SceneExtract::setOutputFolder(const char* folderName) {
	if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
		outputSet = true;
		lastOutputFolder = folderName;
		outputFolderName = folderName;
		outputFolderName = outputFolderName.substr(outputFolderName.find_last_of("/\\") + 1);
		navKit->log(rcLogCategory::RC_LOG_PROGRESS, ("Setting output folder to: " + lastOutputFolder).c_str());
		navKit->ini.SetValue("Paths", "output", folderName);
		navKit->ini.SaveFile("NavKit.ini");
	} else {
		navKit->log(rcLogCategory::RC_LOG_WARNING, ("Could not find output folder: " + lastOutputFolder).c_str());
	}
}

void SceneExtract::setBlenderFile(const char* filename) {
	if (std::filesystem::exists(filename) && !std::filesystem::is_directory(filename)) {
		blenderSet = true;
		lastBlenderFile = filename;
		blenderName = filename;
		blenderName = blenderName.substr(blenderName.find_last_of("/\\") + 1);
		navKit->log(rcLogCategory::RC_LOG_PROGRESS, ("Setting Blender exe to: " + lastBlenderFile).c_str());
		navKit->ini.SetValue("Paths", "blender", filename);
		navKit->ini.SaveFile("NavKit.ini");
	} else {
		navKit->log(rcLogCategory::RC_LOG_WARNING, ("Could not find Blender exe: " + lastBlenderFile).c_str());
	}
}

void SceneExtract::drawMenu() {
	imguiBeginScrollArea("Extract menu", navKit->renderer->width - 250 - 10, navKit->renderer->height - 10 - 205 - 205 - 225 - 15, 250, 225, &extractScroll);
	imguiLabel("Set Hitman Directory");
	if (imguiButton(hitmanFolderName.c_str())) {
		char* folderName = openHitmanFolderDialog(lastHitmanFolder.data());
		if (folderName) {
			setHitmanFolder(folderName);
		}
	}
	imguiLabel("Set Output Directory");
	if (imguiButton(outputFolderName.c_str())) {
		char* folderName = openOutputFolderDialog(lastOutputFolder.data());
		if (folderName) {
			setOutputFolder(folderName);
		}
	}
	imguiLabel("Set Blender Executable");
	if (imguiButton(blenderName.c_str())) {
		char* blenderFileName = openSetBlenderFileDialog(lastBlenderFile.data());
		if (blenderFileName) {
			setBlenderFile(blenderFileName);
		}
	}
	imguiLabel("Extract from game");
	if (imguiButton("Extract", hitmanSet && outputSet && blenderSet && extractionDone.empty())) {
		navKit->gui->showLog = true;
		extractScene(lastHitmanFolder.data(), lastOutputFolder.data());// , & pfBoxes);
	}
	imguiEndScrollArea();
}

void SceneExtract::runCommand(SceneExtract* sceneExtract, std::string command) {
	char buffer[400];

	FILE* glacier2Obj = _popen(command.data(), "r");
	FILE* result = fopen("Glacier2Obj.log", "w");

	while (fgets(buffer, sizeof(buffer), glacier2Obj) != NULL)
	{
		sceneExtract->navKit->log(RC_LOG_PROGRESS, buffer);
		fputs(buffer, result);

	}
	fclose(glacier2Obj);
	fclose(result);

	if (sceneExtract->extractionDone.size() == 0) {
		sceneExtract->navKit->log(RC_LOG_PROGRESS, "Finished extracting scene from game to alocs.json.");
		sceneExtract->extractionDone.push_back(true);
	}
	else {
		sceneExtract->extractionDone.push_back(true);
		sceneExtract->navKit->log(RC_LOG_PROGRESS, "Finished generating obj from alocs.json.");
	}
}

void SceneExtract::extractScene(char* hitmanFolder, char* outputFolder) {
	navKit->log(RC_LOG_PROGRESS, "Extracting scene from game.");
	std::string retailFolder = "\"";
	retailFolder += hitmanFolder;
	retailFolder += "\\Retail\"";
	std::string gameVersion = "HM3";
	std::string alocs = "\"";
	alocs += outputFolder;
	alocs += "\\alocs.json\"";
	std::string pfBoxes = "\"";
	pfBoxes += outputFolder;
	pfBoxes += "\\pfBoxes.json\"";
	std::string runtimeFolder = "\"";
	runtimeFolder += hitmanFolder;
	runtimeFolder += "\\Runtime\"";
	std::string alocFolder = "";
	alocFolder += outputFolder;
	alocFolder += "\\aloc";

	struct stat folderExists;
	int statRC = stat(alocFolder.data(), &folderExists);
	if (statRC != 0)
	{
		if (errno == ENOENT) {
			int status = mkdir(alocFolder.c_str());
			if (status != 0) {
				navKit->log(RC_LOG_ERROR, "Error creating prim folder");
			}
		}
	}

	std::string command = "Glacier2Obj.exe ";
	command += retailFolder;
	command += " ";
	command += gameVersion;
	command += " ";
	command += alocs;	
	command += " ";
	command += pfBoxes;
	command += " ";
	command += runtimeFolder;
	command += " \"";
	command += alocFolder;
	command += "\" 2>&1";
	std::thread commandThread(runCommand, this, command);
	commandThread.detach();
}

void SceneExtract::generateObj(char* blenderPath, char* outputFolder) {
	navKit->log(RC_LOG_PROGRESS, "Generating obj from alocs.json.");
	std::string command = "\"\"";
	command += blenderPath;
	command += "\" -b --factory-startup -P glacier2obj.py -- ";
	std::string alocs = "\"";
	alocs += outputFolder;
	alocs += "\\alocs.json\"";
	command += alocs;
	std::string pfBoxes = " \"";
	pfBoxes += outputFolder;
	pfBoxes += "\\pfBoxes.json\"";
	command += pfBoxes;
	command += " \"";
	command += outputFolder;
	command += "\\output.obj\"\"";
	std::thread commandThread(runCommand, this, command);
	commandThread.detach();
}


void SceneExtract::finalizeExtract() {
	if (extractionDone.size() == 1 && !startedObjGeneration) {
		startedObjGeneration = true;
		generateObj(lastBlenderFile.data(), lastOutputFolder.data());
	}
	if (extractionDone.size() == 2) {
		std::string pfBoxesFile = lastOutputFolder.data();
		pfBoxesFile += "\\pfBoxes.json";
		PfBoxes::PfBoxes pfBoxes = PfBoxes::PfBoxes(pfBoxesFile.data());
		PfBoxes::PfBox bBox = pfBoxes.getPathfindingBBox();
		if (bBox.size.x != -1 ) {
			navKit->geom = new InputGeom;

			// Swap Y and Z to go from Hitman's Z+ = Up coordinates to Recast's Y+ = Up coordinates
			// Negate Y position to go from Hitman's Z+ = North to Recast's Y- = North
			float pos[3] = { bBox.pos.x, bBox.pos.z, -bBox.pos.y };
			float size[3] = { bBox.size.x, bBox.size.z, bBox.size.y };
			navKit->obj->setBBox(pos, size);
		}
		startedObjGeneration = false;
		extractionDone.clear();
		navKit->obj->objToLoad = lastOutputFolder;
		navKit->obj->objToLoad += "\\output.obj";
		navKit->obj->loadObj = true;
		navKit->obj->lastObjFileName = lastOutputFolder.data();
		navKit->obj->lastObjFileName += "output.obj";

	}
}

char* SceneExtract::openHitmanFolderDialog(char* lastHitmanFolder) {
	return FileUtil::openNfdFolderDialog(lastHitmanFolder);
}

char* SceneExtract::openOutputFolderDialog(char* lastOutputFolder) {
	return FileUtil::openNfdFolderDialog(lastOutputFolder);
}

char* SceneExtract::openSetBlenderFileDialog(char* lastBlenderFile) {
	nfdu8filteritem_t filters[1] = { { "Exe files", "exe" } };
	return FileUtil::openNfdLoadDialog(filters, 1, lastBlenderFile);
}