#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <string>
#include <string_view>

// Builds and checks FFDec XML for a layered map GFX in the format the game's own map GFX files use (for example
// polarbear005.gfx): every map layer is a DefineSprite with a frame count of 0 and no ShowFrame of its own, placing
// its shapes at increasing depths with an identity matrix. The sprite is bound through SymbolClass to the template's
// ActionScript class "<package>.SLICE_NN_ZMapView", which the matching "<package>.SLICE_NN" class embeds.
namespace GfxMapLayerSwf {
    // Character IDs used for map layer `layerIndex`. Each layer defines its own background and map shape directly
    // before its sprite, like the game's map GFX files do.
    unsigned int backgroundShapeId(size_t layerIndex);
    unsigned int layerShapeId(size_t layerIndex);
    unsigned int spriteId(size_t layerIndex);

    // "SLICE_NN", with at least two digits.
    std::string sliceName(size_t layerIndex);

    // Map-space configs whose name contains "background", or is or ends with "_SAT" (a satellite background), make
    // the shared background, not a layer (case-insensitive).
    bool isBackgroundMapSpaceName(std::string_view name);

    // Turns FFDec XML of a template map GFX into the XML of a GFX with `layerCount` empty map layers whose shapes
    // FFDec's -importShapes then fills from <shape id>.svg files. FFDec fits each SVG into the shape's existing
    // bounds, so the shapes are made the size of the map canvas (`canvasWidth` x `canvasHeight` units, one per
    // metre) for the map to keep its scale and proportions.
    std::string createMapLayerSwfXml(
        std::string_view templateXml, size_t layerCount, double canvasWidth, double canvasHeight);

    // The width and height of an SVG document (from its viewBox, or else its width and height attributes).
    std::optional<std::pair<double, double>> svgCanvasSize(std::string_view svg);

    // Checks that FFDec XML of a built map GFX has `layerCount` map layers in the game's format. Returns an empty
    // string when it does, or a description of the first problem found.
    std::string validateMapLayerSwfXml(std::string_view gfxXml, size_t layerCount);
} // namespace GfxMapLayerSwf
