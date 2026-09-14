#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/Platform.h"
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <filesystem>
#endif

namespace FileUtil {
    char* openNfdLoadDialog(nfdu8filteritem_t* filters, const nfdfiltersize_t filterCount) {
        nfdu8char_t* outPath;

        const nfdopendialogu8args_t args = {
            .filterList = filters,
            .filterCount = filterCount,
        };

        const nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
        if (result == NFD_OKAY) {
            return outPath;
        }
        if (result == NFD_CANCEL) {
            return nullptr;
        }
        return nullptr;
    }

    char* openNfdSaveDialog(nfdu8filteritem_t* filters, nfdfiltersize_t filterCount, const nfdu8char_t* defaultName) {
        nfdu8char_t* outPath;

        const nfdsavedialogu8args_t args = {
            .filterList = filters,
            .filterCount = filterCount,
            .defaultName = defaultName,
        };
        const nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);
        if (result == NFD_OKAY) {
            return outPath;
        }
        if (result == NFD_CANCEL) {
            return nullptr;
        }
        return nullptr;
    }

    char* openNfdFolderDialog(char* defaultPath) {
        nfdu8char_t* outPath;
        const nfdpickfolderu8args_t args = {
            .defaultPath = defaultPath,
        };
        const nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);
        if (result == NFD_OKAY) {
            return outPath;
        }
        if (result == NFD_CANCEL) {
            return nullptr;
        }
        return nullptr;
    }

    std::string getExecutablePath() {
#ifdef _WIN32
        char buffer[MAX_PATH];
        GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        return buffer;
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector buffer(size + 1, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return "";
        }
        return buffer.data();
#else
        std::error_code error;
        const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? "" : path.string();
#endif
    }
} // namespace FileUtil
