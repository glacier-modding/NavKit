# AIRG vision generation

Build AIRG with the scene GLB loaded. The GLB must contain collision geometry exported by Glacier2Glb from ALOC files. Vision uses the main Recast scene input, while waypoint generation uses the separate AIRG navmesh input. All exported triangles block vision; Glacier2Glb has already applied its collision layer/type filters. No material or transparency filter is applied during baking.

The generation policy is explicit:

- Game coordinates have Z up; collision rays convert to Recast/glTF coordinates `(x, z, -y)`.
- High visibility is channel 0 at 1.6 m above both waypoint positions; low visibility is channel 1 at 0.6 m. These are configurable arguments to `ReasoningGrid::generateVisionData`, and are chosen defaults rather than confirmed engine constants.
- Cells use `floor((position - grid minimum) / spacing)`, matching the reader. Actual waypoint positions supply ray endpoints. The first waypoint in list order represents duplicate destinations in the same cell and layer.
- Each source includes its own layer implicitly, followed by sorted signed 16-bit IDs of other layers with waypoints inside the square neighborhood. Empty cells remain false. The square includes its corners; no radial cutoff is applied.
- Collision triangles are tested on both sides using the existing chunky triangle tree. Hits within 0.1 mm of either endpoint are ignored. Zero-length rays are clear.
- Each record starts with a little-endian 16-bit count of **other** layers, then their little-endian IDs, followed by contiguous bit planes. Bits use least-significant-bit-first ordering and index `xOffset + width * (yOffset + width * (channel + 2 * layerIndex))`. Only the end of the entire bit array is padded to a byte boundary.
- Visibility is generated directionally. It is normally symmetric for unique cells because the same heights are used at both ends, but symmetry is not enforced for duplicate cells.

Generation checks grid settings, coordinates, layer IDs, and 32-bit buffer limits. New offsets and bytes are published only after generation succeeds. The global low/high bit arrays are cleared because this implementation stores visibility in per-waypoint records. Dead-end data remains handled by the existing generation path.

The reasoning grid tests exercise channel heights, LSB packing, negative layer IDs, square corners, absent/out-of-range cells, duplicate representatives, failure atomicity, malformed record bounds, and binary save/load. Verify the chosen heights against intended game behavior before producing a final bake.
