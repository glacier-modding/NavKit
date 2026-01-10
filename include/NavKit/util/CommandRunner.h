#pragma once
#include <functional>
#include <map>
#include <string>

class CommandRunner {
public:
    CommandRunner();

    ~CommandRunner();

    static CommandRunner &getInstance() {
        static CommandRunner instance;
        return instance;
    }

    void runCommand(std::string command, std::string logFileName, std::function<void()> callback,
                           std::function<void()> errorCallback);

    bool closing;
    int commandsRun;

private:
    std::map<int, std::vector<HANDLE> > handles;
};
