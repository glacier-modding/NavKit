#pragma once

#include <nfd.h>

namespace FileUtil {
	char* openNfdLoadDialog(nfdu8filteritem_t* filters, int filterCount, char* defaultPath = NULL);
	char* openNfdSaveDialog(nfdu8filteritem_t* filters, int filterCount, const nfdu8char_t* defaultName, char* defaultPath = NULL);
	char* openNfdFolderDialog(char* defaultPath = NULL);
}