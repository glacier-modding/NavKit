#include "..\include\NavKit\FileUtil.h"

namespace FileUtil {
	char* openNfdLoadDialog(nfdu8filteritem_t* filters, int filterCount, char* defaultPath) {
		nfdu8char_t* outPath;
		nfdopendialogu8args_t args = { 0 };
		args.filterList = filters;
		args.filterCount = filterCount;
		//args.defaultPath = defaultPath;
		nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
		if (result == NFD_OKAY)
		{
			return outPath;
			NFD_FreePathU8(outPath);
		}
		else if (result == NFD_CANCEL)
		{
			return NULL;
		}
		else
		{
			return NULL;
		}
	}

	char* openNfdSaveDialog(nfdu8filteritem_t* filters, int filterCount, const nfdu8char_t* defaultName, char* defaultPath) {
		nfdu8char_t* outPath;
		nfdsavedialogu8args_t args = { 0 };
		args.filterList = filters;
		args.filterCount = filterCount;
		args.defaultName = defaultName;
		args.defaultPath = defaultPath;
		nfdresult_t result = NFD_SaveDialogU8_With(&outPath, &args);
		if (result == NFD_OKAY)
		{
			return outPath;
			NFD_FreePathU8(outPath);
		}
		else if (result == NFD_CANCEL)
		{
			return NULL;
		}
		else
		{
			return NULL;
		}
	}

	char* openNfdFolderDialog(char* defaultPath) {
		nfdu8char_t* outPath;
		nfdpickfolderu8args_t args = { 0 };
		args.defaultPath = defaultPath;
		nfdresult_t result = NFD_PickFolderU8_With(&outPath, &args);
		if (result == NFD_OKAY)
		{
			return outPath;
			NFD_FreePathU8(outPath);
		}
		else if (result == NFD_CANCEL)
		{
			return NULL;
		}
		else
		{
			return NULL;
		}
	}

}