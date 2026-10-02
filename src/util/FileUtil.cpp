#include "../../include/NavKit/util/FileUtil.h"

#include <filesystem>
#include <wx/stdpaths.h>

namespace FileUtil {
    namespace {
        std::string toUtf8(const wxString& value) {
            const wxScopedCharBuffer utf8 = value.ToUTF8();
            return utf8 ? utf8.data() : "";
        }
    } // namespace

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
        return toUtf8(wxStandardPaths::Get().GetExecutablePath());
    }

    std::filesystem::path getApplicationResourcePath(const std::filesystem::path& filename) {
#ifdef __APPLE__
        const std::filesystem::path resourceDirectory =
            std::filesystem::u8path(toUtf8(wxStandardPaths::Get().GetResourcesDir()));
#else
        const std::filesystem::path resourceDirectory = std::filesystem::u8path(getExecutablePath()).parent_path();
#endif
        return resourceDirectory / filename;
    }

    std::filesystem::path getUserDataDirectory() {
        return std::filesystem::u8path(toUtf8(wxStandardPaths::Get().GetUserDataDir()));
    }
} // namespace FileUtil
