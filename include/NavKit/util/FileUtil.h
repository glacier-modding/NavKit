#pragma once

#include <nfd.h>

namespace FileUtil {
    char *openNfdLoadDialog(nfdu8filteritem_t *filters, nfdfiltersize_t filterCount);

    char *openNfdSaveDialog(nfdu8filteritem_t *filters, nfdfiltersize_t filterCount, const nfdu8char_t *defaultName);

    char *openNfdFolderDialog(char *defaultPath = nullptr);

    void initNdf();
}
