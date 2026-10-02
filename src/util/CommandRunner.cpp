#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/ErrorHandler.h"

#include <wx/process.h>
#include <wx/utils.h>

#include <array>
#include <vector>

namespace {
    enum class CommandError { None, ResourceExtraction, Blender };

    constexpr const char* BLENDER_ERROR =
        "Error building obj or blend file. The blender python script threw an unhandled exception. Please report "
        "this to AtomicForce.";

    bool appendAvailableOutput(wxInputStream* stream, std::vector<char>& output) {
        if (stream == nullptr || !stream->CanRead()) {
            return false;
        }

        std::array<char, 4096> buffer{};
        stream->Read(buffer.data(), buffer.size());
        const size_t bytesRead = stream->LastRead();
        output.insert(output.end(), buffer.begin(), buffer.begin() + bytesRead);
        return bytesRead > 0;
    }
} // namespace

CommandRunner::CommandRunner() : closing(false), commandsRun(0) {}

CommandRunner::~CommandRunner() {
    closing = true;
    for (const auto& [commandIndex, pid] : childPids) {
        (void)commandIndex;
        if (pid > 0 && wxProcess::Exists(static_cast<int>(pid))) {
            wxProcess::Kill(static_cast<int>(pid), wxSIGTERM);
        }
    }
    for (const auto& [commandIndex, pid] : childPids) {
        (void)commandIndex;
        while (pid > 0 && wxProcess::Exists(static_cast<int>(pid))) {
            wxMilliSleep(10);
        }
    }
}

void CommandRunner::runCommand(const std::string& command, const std::string& logFileName,
    const std::function<void()>& callback, const std::function<void()>& errorCallback) {
    (void)logFileName;
    const int commandIndex = commandsRun++;
    childPids.emplace(commandIndex, 0);

    wxProcess process;
    process.Redirect();
    const long pid = wxExecute(wxString::FromUTF8(command.c_str()), wxEXEC_ASYNC, &process);
    if (pid == 0) {
        Logger::log(NK_ERROR, ("Error creating process for command: " + command + ".").c_str());
        errorCallback();
        return;
    }
    childPids[commandIndex] = pid;

    std::vector<char> output;
    std::vector<char> previousOutput;
    CommandError commandError = CommandError::None;

    const auto processOutput = [&] {
        if (output.empty()) {
            return;
        }

        const std::string outputString(output.begin(), output.end());
        size_t start = 0;
        size_t pos = 0;
        while ((pos = outputString.find_first_of("\r\n", pos)) != std::string::npos) {
            Logger::log(NK_INFO, outputString.substr(start, pos - start).c_str());
            start = ++pos;
        }

        if (commandError == CommandError::None && outputString.find("panic") != std::string::npos) {
            Logger::log(NK_ERROR, "Error extracting resources from Rpkg files. Please report this to AtomicForce.");
            commandError = CommandError::ResourceExtraction;
        } else if (commandError == CommandError::None && outputString.find("Error") != std::string::npos) {
            Logger::log(NK_ERROR, BLENDER_ERROR);
            commandError = CommandError::Blender;
        }

        if (start > 0) {
            previousOutput.assign(output.begin(), output.begin() + start);
            output.erase(output.begin(), output.begin() + start);
        } else {
            previousOutput.clear();
        }
    };

    while (wxProcess::Exists(static_cast<int>(pid))) {
        if (closing) {
            process.Detach();
            return;
        }
        while (true) {
            const bool readOutput = appendAvailableOutput(process.GetInputStream(), output);
            const bool readError = appendAvailableOutput(process.GetErrorStream(), output);
            processOutput();
            if (!readOutput && !readError) {
                break;
            }
        }
        wxMilliSleep(10);
    }

    appendAvailableOutput(process.GetInputStream(), output);
    appendAvailableOutput(process.GetErrorStream(), output);
    processOutput();

    if (!output.empty()) {
        Logger::log(NK_INFO, std::string(output.begin(), output.end()).c_str());
    }

    childPids[commandIndex] = 0;

    if (commandError == CommandError::ResourceExtraction) {
        errorCallback();
        return;
    }
    if (commandError == CommandError::Blender) {
        errorCallback();
        std::string errorMessage(previousOutput.begin(), previousOutput.end());
        errorMessage.append(output.begin(), output.end());
        errorMessage += BLENDER_ERROR;
        ErrorHandler::openErrorDialog(errorMessage);
        return;
    }

    if (!closing) {
        callback();
    }
}
