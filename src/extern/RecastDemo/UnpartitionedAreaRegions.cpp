#include "../../../include/RecastDemo/UnpartitionedAreaRegions.h"

#include <vector>

#include "Recast.h"

void assignRegionsToUnpartitionedArea(rcCompactHeightfield& compactHeightfield, const unsigned char area) {
    const int width = compactHeightfield.width;
    std::vector<int> pending;
    for (int y = 0; y < compactHeightfield.height; ++y) {
        for (int x = 0; x < width; ++x) {
            const rcCompactCell& cell = compactHeightfield.cells[x + y * width];
            for (int i = static_cast<int>(cell.index); i < static_cast<int>(cell.index + cell.count); ++i) {
                if (compactHeightfield.areas[i] != area || compactHeightfield.spans[i].reg != 0) {
                    continue;
                }
                if (compactHeightfield.maxRegions + 1 >= RC_BORDER_REG) {
                    return;
                }
                const unsigned short region = ++compactHeightfield.maxRegions;
                compactHeightfield.spans[i].reg = region;
                pending.assign(1, x | y << 16);
                pending.push_back(i);
                while (!pending.empty()) {
                    const int spanIndex = pending.back();
                    pending.pop_back();
                    const int packedCell = pending.back();
                    pending.pop_back();
                    const int spanX = packedCell & 0xffff;
                    const int spanY = packedCell >> 16;
                    const rcCompactSpan& span = compactHeightfield.spans[spanIndex];
                    for (int direction = 0; direction < 4; ++direction) {
                        if (rcGetCon(span, direction) == RC_NOT_CONNECTED) {
                            continue;
                        }
                        const int neighbourX = spanX + rcGetDirOffsetX(direction);
                        const int neighbourY = spanY + rcGetDirOffsetY(direction);
                        const int neighbourIndex =
                            static_cast<int>(compactHeightfield.cells[neighbourX + neighbourY * width].index) +
                            rcGetCon(span, direction);
                        if (compactHeightfield.areas[neighbourIndex] != area ||
                            compactHeightfield.spans[neighbourIndex].reg != 0) {
                            continue;
                        }
                        compactHeightfield.spans[neighbourIndex].reg = region;
                        pending.push_back(neighbourX | neighbourY << 16);
                        pending.push_back(neighbourIndex);
                    }
                }
            }
        }
    }
}
