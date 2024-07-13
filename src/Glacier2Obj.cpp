#include "..\include\NavKit\Glacier2Obj.h"
#include <thread>
#include <vector>
using std::string;

void runCommand(string command, BuildContext* context, std::vector<bool>* done) {
	char buffer[100];

	FILE* glacier2Obj = _popen(command.data(), "r");
	FILE* result = fopen("Glacier2Obj.log", "w");

	while (fgets(buffer, sizeof(buffer), glacier2Obj) != NULL)
	{
		context->log(RC_LOG_PROGRESS, buffer);
		fputs(buffer, result);

	}
	fclose(glacier2Obj);
	fclose(result);

	if (done->size() == 0) {
		context->log(RC_LOG_PROGRESS, "Finished extracting scene from game to prims.json.");
		done->push_back(true);
	}
	else {
		done->push_back(true);
		context->log(RC_LOG_PROGRESS, "Finished generating obj from prims.json.");
	}
}

void extractScene(BuildContext* context, char* hitmanFolder, char* outputFolder, std::vector<bool>* done) {
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
	string pfBoxes = "\"";
	pfBoxes += outputFolder;
	pfBoxes += "\\pfBoxes.json\"";
	string runtimeFolder = "\"";
	runtimeFolder += hitmanFolder;
	runtimeFolder += "\\Runtime\"";
	string primFolder = "";
	primFolder += outputFolder;
	primFolder += "\\prim";

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
	//cargo run 
	// <path to a Retail directory> 
	// <game version(H2016 | HM2 | HM3)> 
	// <scenario ioi string or hash> 
	// <path to a hashlist>
	// <path to toFind.json file> 
	// <path to prims.json file> 
	// <path to a Runtime directory> 
	// <path to output prims directory>
	std::thread commandThread(runCommand, command, context, done);
	commandThread.detach();
}

void generateObj(BuildContext* context, char* outputFolder, std::vector<bool>* done) {
	context->log(RC_LOG_PROGRESS, "Generating obj from prims.json.");
	string command = "\"\"C:\\Program Files\\Blender Foundation\\Blender 3.4\\blender.exe\" -b -P glacier2obj.py -- ";
	string prims = "\"";
	prims += outputFolder;
	prims += "\\prims.json\"";
	command += prims;
	command += " \"";
	command += outputFolder;
	command += "\\output.obj\"\"";
	std::thread commandThread(runCommand, command, context, done);
	commandThread.detach();
}
