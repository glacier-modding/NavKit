#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace GfxMapLayerSwf {
    std::string createMapLayerSwfXml(std::string_view templateXml, size_t layerCount);
}
