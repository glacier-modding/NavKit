#pragma once

#include <nfd.h>
#include <filesystem>
#include <string>

namespace FileUtil {
    char* openNfdLoadDialog(nfdu8filteritem_t* filters, nfdfiltersize_t filterCount);

    char* openNfdSaveDialog(nfdu8filteritem_t* filters, nfdfiltersize_t filterCount, const nfdu8char_t* defaultName);

    char* openNfdFolderDialog(char* defaultPath = nullptr);

    void initNdf();

    /// Absolute path of the running executable.
    std::string getExecutablePath();

    std::filesystem::path getApplicationResourcePath(const std::filesystem::path& filename);

    std::filesystem::path getUserDataDirectory();
} // namespace FileUtil
