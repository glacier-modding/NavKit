#pragma once

#include "../../nativefiledialog-extended/nfd.h"

namespace FileUtil {
    char *openNfdLoadDialog(nfdu8filteritem_t *filters, nfdfiltersize_t filterCount, const char *defaultPath = nullptr);

    char *openNfdSaveDialog(nfdu8filteritem_t *filters, nfdfiltersize_t filterCount, const nfdu8char_t *defaultName,
                            char *defaultPath = nullptr);

    char *openNfdFolderDialog(char *defaultPath = nullptr);

    void initNdf();
}
