#include "..\include\NavKit\FileUtil.h"
#include <string>

namespace FileUtil {
	char* openNfdLoadDialog(nfdu8filteritem_t* filters, nfdfiltersize_t filterCount, char* defaultPath) {
		nfdu8char_t* outPath;
		std::string path(defaultPath);
		path = path.substr(0, path.find_last_of("/\\") + 1);

		nfdopendialogu8args_t args = {
			.filterList = filters,
			.filterCount = filterCount,
		};

		if (std::filesystem::exists(path) && !std::filesystem::is_directory(path)) {
			args.defaultPath = path.c_str();
		}
		nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
		if (result == NFD_OKAY)
		{
			return outPath;
			NFD_FreePathU8(outPath);
		}
		else if (result == NFD_CANCEL)
		{
			return nullptr;
		}
		else
		{
			return nullptr;
		}
	}

	char* openNfdSaveDialog(nfdu8filteritem_t* filters, nfdfiltersize_t filterCount, const nfdu8char_t* defaultName, char* defaultPath) {
		nfdu8char_t* outPath;
		std::string path(defaultPath);
		path = path.substr(0, path.find_last_of("/\\") + 1);

		nfdsavedialogu8args_t args = {
			.filterList = filters,
			.filterCount = filterCount,
			.defaultPath = path.c_str(),
			.defaultName = defaultName,
		};
		nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);
		if (result == NFD_OKAY)
		{
			return outPath;
			NFD_FreePathU8(outPath);
		}
		else if (result == NFD_CANCEL)
		{
			return nullptr;
		}
		else
		{
			return nullptr;
		}
	}

	char* openNfdFolderDialog(char* defaultPath) {
		nfdu8char_t* outPath;
		nfdpickfolderu8args_t args = {
			.defaultPath = defaultPath,
		};
		nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);
		if (result == NFD_OKAY)
		{
			return outPath;
			NFD_FreePathU8(outPath);
		}
		else if (result == NFD_CANCEL)
		{
			return nullptr;
		}
		else
		{
			return nullptr;
		}
	}

}