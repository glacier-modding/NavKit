#pragma once
#include <functional>
#include <map>
#include <string>

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
    std::map<int, std::vector<HANDLE>> handles;
};
