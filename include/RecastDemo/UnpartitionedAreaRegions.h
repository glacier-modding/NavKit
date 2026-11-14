#pragma once

struct rcCompactHeightfield;

// Gives each connected patch of the area that region filtering left without a region its own new region
void assignRegionsToUnpartitionedArea(rcCompactHeightfield& compactHeightfield, unsigned char area);
