#include "../../include/NavKit/util/FileUtil.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <wx/stdpaths.h>

namespace FileUtil {
    namespace {
        std::string toUtf8(const wxString& value) {
            const wxScopedCharBuffer utf8 = value.ToUTF8();
            return utf8 ? utf8.data() : "";
        }

        struct DialogFilters {
            std::vector<std::string> specs;
            std::vector<nfdu8filteritem_t> items;

            DialogFilters(const nfdu8filteritem_t* filters, const nfdfiltersize_t filterCount) {
#ifdef __APPLE__
                // nfd's Cocoa backend maps every extension to a UTType and asserts if a filter list
                // yields none. Compound extensions such as "nav.json" have no UTType, so keep only
                // the final component of each extension ("nav.json" -> "json").
                specs.reserve(filterCount);
                items.reserve(filterCount);
                for (nfdfiltersize_t i = 0; i < filterCount; ++i) {
                    std::string spec;
                    std::stringstream specStream(filters[i].spec);
                    std::string extension;
                    while (std::getline(specStream, extension, ',')) {
                        if (const size_t lastDot = extension.find_last_of('.'); lastDot != std::string::npos) {
                            extension.erase(0, lastDot + 1);
                        }
                        if (extension.empty()) {
                            continue;
                        }
                        if (!spec.empty()) {
                            spec += ',';
                        }
                        spec += extension;
                    }
                    if (spec.empty()) {
                        continue;
                    }
                    specs.push_back(std::move(spec));
                    items.push_back({.name = filters[i].name, .spec = specs.back().c_str()});
                }
#else
                items.assign(filters, filters + filterCount);
#endif
            }

            nfdu8filteritem_t* data() {
                return items.empty() ? nullptr : items.data();
            }
            nfdfiltersize_t count() const {
                return static_cast<nfdfiltersize_t>(items.size());
            }
        };
    } // namespace

    char* openNfdLoadDialog(const nfdu8filteritem_t* filters, const nfdfiltersize_t filterCount) {
        nfdu8char_t* outPath;

        DialogFilters dialogFilters(filters, filterCount);
        const nfdopendialogu8args_t args = {
            .filterList = dialogFilters.data(),
            .filterCount = dialogFilters.count(),
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

    char* openNfdSaveDialog(
        const nfdu8filteritem_t* filters, nfdfiltersize_t filterCount, const nfdu8char_t* defaultName) {
        nfdu8char_t* outPath;

        DialogFilters dialogFilters(filters, filterCount);
        const nfdsavedialogu8args_t args = {
            .filterList = dialogFilters.data(),
            .filterCount = dialogFilters.count(),
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
