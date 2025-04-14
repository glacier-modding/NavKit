#include <windows.h>
#include <filesystem>

#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/ErrorHandler.h"

CommandRunner::CommandRunner() {
    closing = false;
    commandsRun = 0;
}

CommandRunner::~CommandRunner() {
    closing = true;
    for (int commandIndex = 0; commandIndex < commandsRun; commandIndex++) {
        for (const HANDLE &handle: handles[commandIndex]) {
            TerminateProcess(handle, 0);
            CloseHandle(handle);
        }
    }
}

void CommandRunner::runCommand(std::string command, std::string logFileName, std::function<void()> callback,
                               std::function<void()> errorCallback) {
    CommandRunner &commandRunner = getInstance();
    int commandIndex = commandRunner.commandsRun;
    commandRunner.commandsRun++;
    commandRunner.handles.emplace(std::pair<int, std::vector<HANDLE> >(commandIndex, {}));
    SECURITY_ATTRIBUTES saAttr = {sizeof(saAttr), nullptr, TRUE};
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0)) {
        Logger::log(NK_ERROR,
                    ("Error creating pipe to command: " + command + ".").
                    c_str());
        errorCallback();
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
                     ".").c_str());
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        errorCallback();
        return;
    }

    CloseHandle(hWritePipe);
    std::vector<char> lastOutput;
    std::vector<char> output;
    char buffer[4096];
    DWORD bytesRead;
    commandRunner.handles[commandIndex].push_back(hReadPipe);
    commandRunner.handles[commandIndex].push_back(pi.hProcess);
    commandRunner.handles[commandIndex].push_back(pi.hThread);

    while (true) {
        if (commandRunner.closing) {
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
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            commandRunner.handles[commandIndex].pop_back();
            commandRunner.handles[commandIndex].pop_back();
            commandRunner.handles[commandIndex].pop_back();
            errorCallback();
            return;
        }
        bool pythonError = false;
        std::string outputErrorCheck = output.data();
        if (size_t found = outputErrorCheck.find("Error"); found != std::string::npos) {
            pythonError = true;
        }
        if (pythonError) {
            Logger::log(
                NK_ERROR,
                "Error extracting scene from game. The blender python script threw an unhandled exception. Please report this to AtomicForce.");
            errorCallback();
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            commandRunner.handles[commandIndex].pop_back();
            commandRunner.handles[commandIndex].pop_back();
            commandRunner.handles[commandIndex].pop_back();
            std::string errorMessage = lastOutput.data();
            errorMessage += output.data();
            errorMessage +=
                    "Error extracting scene from game. The blender python script threw an unhandled exception. Please report this to AtomicForce.";
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

    if (commandRunner.closing) {
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    commandRunner.handles[commandIndex].pop_back();
    commandRunner.handles[commandIndex].pop_back();
    commandRunner.handles[commandIndex].pop_back();

    callback();
}
