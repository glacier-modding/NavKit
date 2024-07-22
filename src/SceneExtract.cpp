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
	imguiBeginScrollArea("Extract menu", 10, navKit->height - 225 - 10, 250, 225, &extractScroll);
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
		navKit->showLog = true;
		extractScene(&navKit->ctx, lastHitmanFolder.data(), lastOutputFolder.data(), &extractionDone);// , & pfBoxes);
	}
	imguiEndScrollArea();
}

void SceneExtract::finalizeExtract() {
	if (extractionDone.size() == 1 && !startedObjGeneration) {
		startedObjGeneration = true;
		generateObj(&navKit->ctx, lastBlenderFile.data(), lastOutputFolder.data(), &extractionDone);
	}
	if (extractionDone.size() == 2) {
		startedObjGeneration = false;
		extractionDone.clear();
		navKit->obj->objToLoad = lastOutputFolder;
		navKit->obj->objToLoad += "\\output.obj";
		navKit->obj->loadObj = true;
		navKit->obj->lastObjFileName = lastOutputFolder.data();
		navKit->obj->lastObjFileName += "output.obj";
		navKit->geom = new InputGeom;
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