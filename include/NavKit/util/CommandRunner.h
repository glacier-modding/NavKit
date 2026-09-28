#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Platform.h"
#ifndef _WIN32
#include <sys/types.h>
#endif

class CommandRunner {
public:
    CommandRunner();

    ~CommandRunner();

    static CommandRunner& getInstance() {
        static CommandRunner instance;
        return instance;
    }

    void runCommand(const std::string& command, const std::string& logFileName, const std::function<void()>& callback,
        const std::function<void()>& errorCallback);

    bool closing;
    int commandsRun;

private:
#ifdef _WIN32
    std::map<int, std::vector<HANDLE>> handles;
#else
    std::map<int, std::vector<pid_t>> childPids;
#endif
};
