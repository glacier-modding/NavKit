#define WIN32_LEAN_AND_MEAN
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
        for (const HANDLE& handle : handles[commandIndex]) {
            TerminateProcess(handle, 0);
            CloseHandle(handle);
        }
    }
}

void CommandRunner::runCommand(const std::string& command, const std::string& logFileName,
                               const std::function<void()>& callback,
                               const std::function<void()>& errorCallback) {
    int commandIndex = commandsRun;
    commandsRun++;
    handles.emplace(std::pair<int, std::vector<HANDLE>>(commandIndex, {}));
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

    char* commandLineChar = _strdup(command.c_str());

    if (!CreateProcess(nullptr, commandLineChar, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
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
    handles[commandIndex].push_back(hReadPipe);
    handles[commandIndex].push_back(pi.hProcess);
    handles[commandIndex].push_back(pi.hThread);

    while (true) {
        if (closing) {
            return;
        }
        if (!(ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0)) {
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
            pos++;
            start = pos;
        }
        if (size_t found = outputString.find("panic"); found != std::string::npos) {
            Logger::log(NK_ERROR,
                        "Error extracting resources from Rpkg files. Please report this to AtomicForce.");
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            handles[commandIndex].pop_back();
            handles[commandIndex].pop_back();
            handles[commandIndex].pop_back();
            errorCallback();
            return;
        }
        if (outputString.find("Error") != std::string::npos) {
            Logger::log(
                NK_ERROR,
                "Error building obj or blend file. The blender python script threw an unhandled exception. Please report this to AtomicForce.");
            errorCallback();
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            handles[commandIndex].pop_back();
            handles[commandIndex].pop_back();
            handles[commandIndex].pop_back();
            std::string errorMessage;
            if (!lastOutput.empty()) {
                errorMessage.append(lastOutput.begin(), lastOutput.end());
            }
            errorMessage += outputString;
            errorMessage +=
                "Error building obj or blend file. The blender python script threw an unhandled exception. Please report this to AtomicForce.";
            ErrorHandler::openErrorDialog(errorMessage);
            return;
        }
        if (start > 0) {
            lastOutput.assign(output.begin(), output.begin() + start);
            output.erase(output.begin(), output.begin() + start);
        } else {
            lastOutput.clear();
        }
    }

    // Process any remaining output
    if (!output.empty()) {
        std::string outputString(output.begin(), output.end());
        size_t start = 0;
        size_t pos = 0;
        while ((pos = outputString.find_first_of("\r\n\0", pos)) != std::string::npos) {
            std::string line = outputString.substr(start, pos - start);
            Logger::log(NK_INFO, line.c_str());
            pos++;
            start = pos;
        }
        if (start < outputString.size()) {
            std::string line = outputString.substr(start);
            Logger::log(NK_INFO, line.c_str());
        }
    }

    if (closing) {
        return;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    handles[commandIndex].pop_back();
    handles[commandIndex].pop_back();
    handles[commandIndex].pop_back();

    callback();
}
