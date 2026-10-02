#pragma once
#include <mutex>
#include <mutex>
#include <string>
#include <thread>

class UpdateChecker {
public:
    explicit UpdateChecker();

    ~UpdateChecker();

    UpdateChecker(const UpdateChecker&) = delete;

    UpdateChecker& operator=(const UpdateChecker&) = delete;

    static UpdateChecker& getInstance() {
        static UpdateChecker instance;
        return instance;
    }

    void openUpdateDialog(const std::string& message);

    static bool isVersionGreaterThan(const std::string& v1, const std::string& v2);

    void startUpdateCheck();

    void waitForUpdateCheck();

    void renderUpdatePopup();

private:
    static void performUpdateCheck();

    void performUpdate() const;

    std::thread updateThread;
    std::mutex mutex;
    std::string responseBody;
    bool updateCheckCompleted;
    bool isUpdateAvailable;
    std::string latestVersion;
    std::string updateUrl;
};
