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

void SceneExtract::drawMenu() {
	imguiBeginScrollArea("Extract menu", 10, navKit->renderer->height - 225 - 10, 250, 225, &extractScroll);
	imguiLabel("Set Hitman Directory");
	if (imguiButton(hitmanFolderName.c_str())) {
		char* folderName = openHitmanFolderDialog(lastHitmanFolder.data());
		if (folderName) {
			hitmanSet = true;
			lastHitmanFolder = folderName;
			hitmanFolderName = folderName;
			hitmanFolderName = hitmanFolderName.substr(hitmanFolderName.find_last_of("/\\") + 1);
		}
	}
	imguiLabel("Set Output Directory");
	if (imguiButton(outputFolderName.c_str())) {
		char* folderName = openOutputFolderDialog(lastOutputFolder.data());
		if (folderName) {
			outputSet = true;
			lastOutputFolder = folderName;
			outputFolderName = folderName;
			outputFolderName = outputFolderName.substr(outputFolderName.find_last_of("/\\") + 1);
		}
	}
	imguiLabel("Set Blender Executable");
	if (imguiButton(blenderName.c_str())) {
		char* blenderFileName = openSetBlenderFileDialog(lastBlenderFile.data());
		if (blenderFileName) {
			blenderSet = true;
			lastBlenderFile = blenderFileName;
			blenderName = blenderFileName;
			blenderName = blenderName.substr(blenderName.find_last_of("/\\") + 1);
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
	char buffer[100];

	FILE* glacier2Obj = _popen(command.data(), "r");
	FILE* result = fopen("Glacier2Obj.log", "w");

	while (fgets(buffer, sizeof(buffer), glacier2Obj) != NULL)
	{
		sceneExtract->navKit->ctx.log(RC_LOG_PROGRESS, buffer);
		fputs(buffer, result);

	}
	fclose(glacier2Obj);
	fclose(result);

	if (sceneExtract->extractionDone.size() == 0) {
		sceneExtract->navKit->ctx.log(RC_LOG_PROGRESS, "Finished extracting scene from game to prims.json.");
		sceneExtract->extractionDone.push_back(true);
	}
	else {
		sceneExtract->extractionDone.push_back(true);
		sceneExtract->navKit->ctx.log(RC_LOG_PROGRESS, "Finished generating obj from prims.json.");
	}
}

void SceneExtract::extractScene(char* hitmanFolder, char* outputFolder) {
	navKit->ctx.log(RC_LOG_PROGRESS, "Extracting scene from game.");
	std::string retailFolder = "\"";
	retailFolder += hitmanFolder;
	retailFolder += "\\Retail\"";
	std::string gameVersion = "HM3";
	std::string hashList = "\"";
	hashList += hitmanFolder;
	hashList += "\\rpkg\\hash_list.txt\"";
	std::string toFind = "\"";
	toFind += outputFolder;
	toFind += "\\toFind.json\"";
	std::string prims = "\"";
	prims += outputFolder;
	prims += "\\prims.json\"";
	std::string pfBoxes = "\"";
	pfBoxes += outputFolder;
	pfBoxes += "\\pfBoxes.json\"";
	std::string runtimeFolder = "\"";
	runtimeFolder += hitmanFolder;
	runtimeFolder += "\\Runtime\"";
	std::string primFolder = "";
	primFolder += outputFolder;
	primFolder += "\\prim";

	struct stat folderExists;
	int statRC = stat(primFolder.data(), &folderExists);
	if (statRC != 0)
	{
		if (errno == ENOENT) {
			int status = mkdir(primFolder.c_str());
			if (status != 0) {
				navKit->ctx.log(RC_LOG_ERROR, "Error creating prim folder");
			}
		}
	}

	std::string command = "Glacier2Obj.exe ";
	command += retailFolder;
	command += " ";
	command += gameVersion;
	command += " ";
	command += hashList;
	command += " ";
	command += toFind;
	command += " ";
	command += prims;	
	command += " ";
	command += pfBoxes;
	command += " ";
	command += runtimeFolder;
	command += " \"";
	command += primFolder;
	command += "\" 2>&1";
	std::thread commandThread(runCommand, this, command);
	commandThread.detach();
}

void SceneExtract::generateObj(char* blenderPath, char* outputFolder) {
	navKit->ctx.log(RC_LOG_PROGRESS, "Generating obj from prims.json.");
	std::string command = "\"\"";
	command += blenderPath;
	command += "\" -b -P glacier2obj.py -- ";
	std::string prims = "\"";
	prims += outputFolder;
	prims += "\\prims.json\"";
	command += prims;
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
		std::pair<float*, float*> bBox = pfBoxes.getPathfindingBBox();
		navKit->geom = new InputGeom;
		navKit->obj->setBBox(bBox.first, bBox.second);
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
	return FileUtil::openNfdFolderDialog();
}

char* SceneExtract::openOutputFolderDialog(char* lastOutputFolder) {
	return FileUtil::openNfdFolderDialog();
}

char* SceneExtract::openSetBlenderFileDialog(char* lastBlenderFile) {
	nfdu8filteritem_t filters[1] = { { "Exe files", "exe" } };
	return FileUtil::openNfdLoadDialog(filters, 1);
}