#pragma once

#include "..\nativefiledialog-extended\nfd.h"
namespace FileUtil {
	char* openNfdLoadDialog(nfdu8filteritem_t* filters, nfdfiltersize_t filterCount, char* defaultPath = NULL);
	char* openNfdSaveDialog(nfdu8filteritem_t* filters, nfdfiltersize_t filterCount, const nfdu8char_t* defaultName, char* defaultPath = NULL);
	char* openNfdFolderDialog(char* defaultPath = NULL);
}