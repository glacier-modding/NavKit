#include <windows.h>
#include "../../include/NavKit/module/SceneExtract.h"
#include <direct.h>
#include <filesystem>
#include <string>
#include <vector>

#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/model/PfBoxes.h"
#include "../../include/NavKit/util/ErrorHandler.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/RecastDemo/imgui.h"

SceneExtract::SceneExtract() {
    lastBlenderFile = "\"C:\\Program Files\\Blender Foundation\\Blender 3.4\\blender.exe\"";
    blenderName = "Choose Blender app";
    blenderSet = false;
    startedObjGeneration = false;
    hitmanFolderName = "Choose Hitman folder";
    lastHitmanFolder = hitmanFolderName;
    hitmanSet = false;
    outputFolderName = "Choose Output folder";
    lastOutputFolder = outputFolderName;
    outputSet = false;
    extractScroll = 0;
    errorExtracting = false;
    closing = false;
    glacier2ObjDebugLogsEnabled = false;
}

SceneExtract::~SceneExtract() {
    closing = true;
    for (HANDLE handle: handles) {
        TerminateProcess(handle, 0);
        CloseHandle(handle);
    }
}

void SceneExtract::setHitmanFolder(const char *folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        hitmanSet = true;
        lastHitmanFolder = folderName;
        hitmanFolderName = folderName;
        hitmanFolderName = hitmanFolderName.substr(hitmanFolderName.find_last_of("/\\") + 1);
        Logger::log(NK_INFO, ("Setting Hitman folder to: " + lastHitmanFolder).c_str());
        Settings::setValue("Paths", "hitman", folderName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find Hitman folder: " + lastHitmanFolder).c_str());
    }
}

void SceneExtract::setOutputFolder(const char *folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        outputSet = true;
        lastOutputFolder = folderName;
        outputFolderName = folderName;
        outputFolderName = outputFolderName.substr(outputFolderName.find_last_of("/\\") + 1);
        Logger::log(NK_INFO, ("Setting output folder to: " + lastOutputFolder).c_str());
        Settings::setValue("Paths", "output", folderName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find output folder: " + lastOutputFolder).c_str());
    }
}

void SceneExtract::setBlenderFile(const char *filename) {
    if (std::filesystem::exists(filename) && !std::filesystem::is_directory(filename)) {
        blenderSet = true;
        lastBlenderFile = filename;
        blenderName = filename;
        blenderName = blenderName.substr(blenderName.find_last_of("/\\") + 1);
        Logger::log(NK_INFO, ("Setting Blender exe to: " + lastBlenderFile).c_str());
        Settings::setValue("Paths", "blender", filename);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find Blender exe: " + lastBlenderFile).c_str());
    }
}

void SceneExtract::drawMenu() {
    Renderer& renderer = Renderer::getInstance();
    int sceneExtractHeight = 238;
    if (imguiBeginScrollArea("Extract menu", renderer.width - 250 - 10,
                         renderer.height - 10 - 205 - 417 - sceneExtractHeight - 10, 250, sceneExtractHeight, &extractScroll)) {
        Gui::getInstance().mouseOverMenu = true;
    }
    imguiLabel("Set Hitman Directory");
    if (imguiButton(hitmanFolderName.c_str())) {
        char *folderName = openHitmanFolderDialog(lastHitmanFolder.data());
        if (folderName) {
            setHitmanFolder(folderName);
        }
    }
    imguiLabel("Set Output Directory");
    if (imguiButton(outputFolderName.c_str())) {
        char *folderName = openOutputFolderDialog(lastOutputFolder.data());
        if (folderName) {
            setOutputFolder(folderName);
        }
    }
    imguiLabel("Set Blender Executable");
    if (imguiButton(blenderName.c_str())) {
        char *blenderFileName = openSetBlenderFileDialog(lastBlenderFile.data());
        if (blenderFileName) {
            setBlenderFile(blenderFileName);
        }
    }
    if (imguiButton("Extract from game", hitmanSet && outputSet && extractionDone.empty())) {
        Gui& gui = Gui::getInstance();
        gui.showLog = true;
        alsoBuildObj = false;
        extractScene(lastHitmanFolder.data(), lastOutputFolder.data());
    }
    if (imguiButton("Build obj", outputSet && blenderSet && extractionDone.empty() && !blenderObjStarted && !blenderObjGenerationDone)) {
        Gui& gui = Gui::getInstance();
        gui.showLog = true;
        Obj::getInstance().objLoaded = false;
        buildObj(lastBlenderFile.data(), lastOutputFolder.data());
    }
    if (imguiButton("Extract from game and build obj", hitmanSet && blenderSet && outputSet && extractionDone.empty() && !blenderObjStarted && !blenderObjGenerationDone)) {
        Gui& gui = Gui::getInstance();
        gui.showLog = true;
        alsoBuildObj = true;
        Obj::getInstance().objLoaded = false;
        extractScene(lastHitmanFolder.data(), lastOutputFolder.data());
    }
    imguiEndScrollArea();
}

void SceneExtract::runCommand(SceneExtract *sceneExtract, std::string command, std::string logFileName, bool extracting) {
    SECURITY_ATTRIBUTES saAttr = {sizeof(saAttr), nullptr, TRUE};
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        Logger::log(NK_ERROR,
                                  ("Error creating pipe to command: " + command + " while extracting scene from game.").
                                  c_str());
        sceneExtract->errorExtracting = true;
        return;
    }
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi, sizeof(pi));

    FILE *logFile = fopen(logFileName.c_str(), "w");
    char *commandLineChar = _strdup(command.c_str());

    if (!CreateProcess(NULL, commandLineChar, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        Logger::log(NK_ERROR,
                                  ("Error creating process for command: " + command +
                                   " while extracting scene from game.").c_str());
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        sceneExtract->errorExtracting = true;
        return;
    }

    CloseHandle(hWritePipe);
    std::vector<char> lastOutput;
    std::vector<char> output;
    char buffer[4096];
    DWORD bytesRead;
    sceneExtract->handles.push_back(hReadPipe);
    sceneExtract->handles.push_back(pi.hProcess);
    sceneExtract->handles.push_back(pi.hThread);

    while (true) {
        if (sceneExtract->closing) {
            return;
        }
        if (!(ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)) {
            break;
        }
        output.insert(output.end(), buffer, buffer + bytesRead);

        // Check if the output size exceeds the threshold
        //if (output.size() >= 1024) {
        // Process and clear the output
        std::string outputString(output.begin(), output.end());
        size_t start = 0;
        size_t pos = 0;
        while ((pos = outputString.find_first_of("\r\n\0", pos)) != std::string::npos) {
            std::string line = outputString.substr(start, pos - start);
            Logger::log(NK_INFO, line.c_str());
            fputs(line.c_str(), logFile);
            fputc('\n', logFile);
            pos++;
            start = pos;
        }
        if (size_t found = outputString.find("panic"); found != std::string::npos) {
            Logger::log(NK_ERROR,
                                      "Error extracting scene from game. Make sure Hitman is running with ZHMModSDK installed.");
            sceneExtract->errorExtracting = true;
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            sceneExtract->handles.pop_back();
            sceneExtract->handles.pop_back();
            sceneExtract->handles.pop_back();
            return;
        }
        bool pythonError = false;
        std::string outputErrorCheck = output.data();
        if (size_t found = outputErrorCheck.find("Error"); found != std::string::npos) {
            pythonError = true;
        }
        if (pythonError) {
            Logger::log(NK_ERROR, "Error extracting scene from game. The blender python script threw an unhandled exception. Please report this to AtomicForce.");
            sceneExtract->errorExtracting = true;
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            sceneExtract->handles.pop_back();
            sceneExtract->handles.pop_back();
            sceneExtract->handles.pop_back();
            std::string errorMessage = lastOutput.data();
            errorMessage += output.data();
            errorMessage += "Error extracting scene from game. The blender python script threw an unhandled exception. Please report this to AtomicForce.";
            ErrorHandler::openErrorDialog(errorMessage);
            return;
        }
        lastOutput.resize(output.size());
        std::ranges::copy(output, lastOutput.begin());
        output.clear();
        //}
    }

    // Process any remaining output
    if (!output.empty()) {
        std::string outputString(output.begin(), output.end());
        size_t start = 0;
        size_t pos = 0;
        while ((pos = outputString.find_first_of("\r\n\0", pos)) != std::string::npos) {
            std::string line = outputString.substr(start, pos - start);
            Logger::log(NK_INFO, line.c_str());
            fputs(line.c_str(), logFile);
            fputc('\n', logFile);
            pos++;
            start = pos;
        }
    }

    if (sceneExtract->closing) {
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    sceneExtract->handles.pop_back();
    sceneExtract->handles.pop_back();
    sceneExtract->handles.pop_back();

    if (sceneExtract->extractionDone.size() == 1 && extracting) {
        Logger::log(NK_INFO, "Finished extracting scene from game to nav.json file.");
        sceneExtract->extractionDone.push_back(true);
    }
    if (sceneExtract->blenderObjStarted && !extracting) {
        Logger::log(NK_INFO, "Finished generating obj from nav.json file.");
        Obj& obj = Obj::getInstance();
        obj.objLoaded = false;
        sceneExtract->blenderObjGenerationDone = true;
    }
}

void SceneExtract::extractScene(char *hitmanFolder, char *outputFolder) {
    Logger::log(NK_INFO, "Extracting scene from game.");
    std::string retailFolder = "\"";
    retailFolder += hitmanFolder;
    retailFolder += "\\Retail\"";
    std::string gameVersion = "HM3";
    std::string navJsonFilePath = "\"";
    navJsonFilePath += outputFolder;
    navJsonFilePath += "\\output.nav.json\"";
    std::string runtimeFolder = "\"";
    runtimeFolder += hitmanFolder;
    runtimeFolder += "\\Runtime\"";
    std::string alocFolder = "";
    alocFolder += outputFolder;
    alocFolder += "\\aloc";

    struct stat folderExists;
    int statRC = stat(alocFolder.data(), &folderExists);
    if (statRC != 0) {
        if (errno == ENOENT) {
            int status = mkdir(alocFolder.c_str());
            if (status != 0) {
                Logger::log(NK_ERROR, "Error creating prim folder");
            }
        }
    }

    std::string command = "Glacier2Obj.exe ";
    command += retailFolder;
    command += " ";
    command += gameVersion;
    command += " ";
    command += navJsonFilePath;
    command += " ";
    command += runtimeFolder;
    command += " \"";
    command += alocFolder;
    command += "\"";
    extractionDone.push_back(true);
    std::thread commandThread(runCommand, this, command, "Glacier2ObjExtract.log", true);
    commandThread.detach();
}

void SceneExtract::buildObj(char *blenderPath, char *outputFolder) {
    startedObjGeneration = true;
    Logger::log(NK_INFO, "Generating obj from nav.json file.");
    std::string command = "\"";
    command += blenderPath;
    command += "\" -b --factory-startup -P glacier2obj.py -- "; //--debug-all
    std::string alocs = "\"";
    alocs += outputFolder;
    alocs += "\\output.nav.json\"";
    command += alocs;
    command += " \"";
    command += outputFolder;
    command += "\\output.obj\"";
    if (glacier2ObjDebugLogsEnabled) {
        command += " True";
    }
    blenderObjStarted = true;

    std::thread commandThread(runCommand, this, command, "Glacier2ObjBlender.log", false);
    commandThread.detach();
}


void SceneExtract::finalizeExtract() {
    if (extractionDone.size() == 2) {
        std::string pfBoxesFile = lastOutputFolder.data();
        pfBoxesFile += "\\output.nav.json";
        PfBoxes::PfBoxes pfBoxes;
        try {
            pfBoxes = PfBoxes::PfBoxes(pfBoxesFile.data());
        } catch (...) {
            return;
        }
        Navp& navp = Navp::getInstance();
        PfBoxes::PfBox bBox = pfBoxes.getPathfindingBBox();
        if (bBox.size.x != -1) {

            // Swap Y and Z to go from Hitman's Z+ = Up coordinates to Recast's Y+ = Up coordinates
            // Negate Y position to go from Hitman's Z+ = North to Recast's Y- = North
            float pos[3] = {bBox.pos.x, bBox.pos.z, -bBox.pos.y};
            float size[3] = {bBox.size.x, bBox.size.z, bBox.size.y};
            navp.setBBox(pos, size);
        }
        navp.exclusionBoxes = pfBoxes.getExclusionBoxes();
        extractionDone.clear();
        if (alsoBuildObj && !startedObjGeneration) {
            buildObj(lastBlenderFile.data(), lastOutputFolder.data());
        }
    }
    if (errorExtracting) {
        errorExtracting = false;
        startedObjGeneration = false;
        extractionDone.clear();
    }
}

void SceneExtract::finalizeObjBuild() {
    if (blenderObjGenerationDone) {
        Obj& obj = Obj::getInstance();
        startedObjGeneration = false;
        obj.objToLoad = lastOutputFolder;
        obj.objToLoad += "\\output.obj";
        obj.loadObj = true;
        obj.lastObjFileName = lastOutputFolder.data();
        obj.lastObjFileName += "output.obj";
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
    }
    if (errorExtracting) {
        errorExtracting = false;
        startedObjGeneration = false;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
    }
}

char *SceneExtract::openHitmanFolderDialog(char *lastHitmanFolder) {
    return FileUtil::openNfdFolderDialog(lastHitmanFolder);
}

char *SceneExtract::openOutputFolderDialog(char *lastOutputFolder) {
    return FileUtil::openNfdFolderDialog(lastOutputFolder);
}

char *SceneExtract::openSetBlenderFileDialog(char *lastBlenderFile) {
    nfdu8filteritem_t filters[1] = {{"Exe files", "exe"}};
    return FileUtil::openNfdLoadDialog(filters, 1, lastBlenderFile);
}
