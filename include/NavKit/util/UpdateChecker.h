#pragma once
#include <mutex>
#include <SDL_syswm.h>
#include <string>

class UpdateChecker {
public:
    explicit UpdateChecker();

    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    static INT_PTR CALLBACK updateDialogHandler(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    static UpdateChecker &getInstance() {
        static UpdateChecker instance;
        return instance;
    }

    void openUpdateDialog(const std::string &message);

    static bool isVersionGreaterThan(const std::string& v1, const std::string& v2);
    void startUpdateCheck();
    void renderUpdatePopup();
private:
    static void performUpdateCheck();
    void performUpdate() const;

    std::thread updateThread;
    std::mutex mutex;
    std::string updateMessage;
    std::string responseBody;
    bool updateCheckCompleted;
    bool isUpdateAvailable;
    std::string latestVersion;
    std::string msiUrl;
};
