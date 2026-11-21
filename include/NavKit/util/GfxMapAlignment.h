#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Places a map GFX layer image on the scene's geometry. Official map GFX files are drawn at one unit per metre,
// but where their canvas sits in the world comes from scene data that the nav.json does not carry, so the image is
// registered against the geometry instead: the walls the image draws (in the official wall colour) are matched to
// the cross-sections of the scene's geometry at the layer's floor level.
//
// Map plane coordinates: u is the game's x, v is the game's -y (so that v grows down the image, like its rows).
namespace GfxMapAlignment {
    using Segment = std::array<glm::vec2, 2>;

    struct LayerImage {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba; // top row first
        float pixelsPerMetre = 2.0f;
    };

    struct Placement {
        glm::vec2 topLeft{0.0f}; // map plane position of the image's top-left corner
        float score = 0.0f; // mean distance of matched walls, in metres (lower is better)
        float confidence = 0.0f; // how much better than a random placement (1 means no better)
    };

    // Elevations (game z) that carry a large share of the up-facing area of `triangles` (game coordinates) within
    // [minimumZ, maximumZ], ascending. Levels closer than `separation` are merged into the larger one.
    std::vector<float> floorLevels(
        const std::vector<glm::vec3>& triangles, float minimumZ, float maximumZ, float separation);

    // The segments where the faces of `triangles` (game coordinates, three vertices each) cross the plane z = cut,
    // in map plane coordinates.
    std::vector<Segment> sectionSegments(const std::vector<glm::vec3>& triangles, float cut);

    // Pixels of the official wall colour, as a mask of the image's size.
    std::vector<unsigned char> wallMask(const LayerImage& image);

    // Finds where the image lies on the walls `segments`: a coarse search over every overlap on a 2 m grid, by the
    // mean distance between the image's walls and the geometry's walls both ways (computed with FFTs), then a fine
    // search at the image's own resolution. Returns nothing when there is too little to match.
    std::optional<Placement> placeLayer(const LayerImage& image, const std::vector<Segment>& segments);
} // namespace GfxMapAlignment
