#include "..\include\NavKit\Glacier2Obj.h"
using std::string;

void extractScene(BuildContext* context, char* hitmanFolder, char* sceneName, char* outputFolder) {
	context->log(RC_LOG_PROGRESS, "Extracting scene from game.");
	string retailFolder = "\"";
	retailFolder += hitmanFolder;
	retailFolder += "\\Retail\"";
	string gameVersion = "HM3";
	string hashList = "\"";
	hashList += hitmanFolder;
	hashList += "\\rpkg\\hash_list.txt\"";
	string toFind = "\"";
	toFind += outputFolder;
	toFind += "\\toFind.json\"";
	string prims = "\"";
	prims += outputFolder;
	prims += "\\prims.json\"";
	string runtimeFolder = "\"";
	runtimeFolder += hitmanFolder;
	runtimeFolder += "\\Runtime\"";
	string primFolder = "\"";
	primFolder += outputFolder;
	primFolder += "\\prim\"";

	struct stat folderExists;
	int statRC = stat(primFolder.data(), &folderExists);
	if (statRC != 0)
	{
		if (errno == ENOENT) { 
			int status = mkdir(primFolder.c_str());
			if (status != 0) {
				context->log(RC_LOG_ERROR, "Error creating prim folder");
			}

		}
	}
	// Glacier2Obj.exe "C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3\Retail" HM3 [assembly:/_pro/scenes/atomicforce/navptestworld/scenario_navptestworld.entity].pc_entitytemplate "C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3\rpkg\hash_list.txt" "D:\workspace\NavKit\output\toFind.json" D:\workspace\NavKit\output\prims.json "C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3\Runtime" "D:\workspace\NavKit\output\prim" 2>&1

	string command = "Glacier2Obj.exe ";
	command += retailFolder;
	command += " ";
	command += gameVersion;
	command += " ";
	command += sceneName;
	command += " ";
	command += hashList;
	command += " ";
	command += toFind;
	command += " ";
	command += prims;
	command += " ";
	command += runtimeFolder;
	command += " ";
	command += primFolder;
	command += " 2>&1";
	FILE* glacier2ObjSceneExtract = _popen(command.data(), "r");
	FILE* result = fopen("Glacier2Obj.log", "w");

	char buffer[1024];
	//cargo run 
	// <path to a Retail directory> 
	// <game version(H2016 | HM2 | HM3)> 
	// <scenario ioi string or hash> 
	// <path to a hashlist>
	// <path to toFind.json file> 
	// <path to prims.json file> 
	// <path to a Runtime directory> 
	// <path to output prims directory>
	context->resetLog();
	while (fgets(buffer, sizeof(buffer), glacier2ObjSceneExtract)) {
		context->log(RC_LOG_PROGRESS, buffer);

		fputs(buffer, result);
	}
	fclose(glacier2ObjSceneExtract);
	fclose(result);
	context->log(RC_LOG_PROGRESS, "Finished extracting scene from game to prims.json.");

	generateObj(context, prims.data(), outputFolder);
}

void generateObj(BuildContext* context, char* prims, char* outputFolder) {
	context->log(RC_LOG_PROGRESS, "Generating obj from prims.json.");
	string command = "\"\"C:\\Program Files\\Blender Foundation\\Blender 3.4\\blender.exe\" -b -P glacier2obj.py -- ";
	command += prims;
	command += " \"";
	command += outputFolder;
	command += "\\output.obj\"\"";
	FILE* glacier2ObjSceneExtract = _popen(command.data(), "r");
	FILE* result = fopen("Glacier2Obj.log", "w");
	char buffer[1024];
	while (fgets(buffer, sizeof(buffer), glacier2ObjSceneExtract)) {
		context->log(RC_LOG_PROGRESS, buffer);

		fputs(buffer, result);
	}
	fclose(glacier2ObjSceneExtract);
	fclose(result);
	context->log(RC_LOG_PROGRESS, "Finished generating obj from prims.json.");
}
