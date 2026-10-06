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

## Performance and worker threads

Settings > NavKit > Maximum Worker Threads controls the maximum threads per parallel operation. The default is the hardware thread count (at least one); setting it to 1 runs the work sequentially. Apply/OK persists `maxThreads` in the `[NavKit]` INI section. Reset Defaults restores the hardware count. Changes apply to operations started afterwards. This limits AIRG waypoint/visibility generation, triangle-cache preparation, GLB loading, and RPKG indexing. UI, logging, network I/O, and background task coordinators are separate from the worker limit; it is not a process-wide OS thread quota or a limit on external tools.

Visibility indexes occupied cells in sorted rows to skip empty cells, caches collision triangle origins/edges, and generates independent waypoint records with dynamically assigned work. Ray scratch buffers belong to individual threads. Records merge in waypoint order, preserving identical bytes and offsets regardless of thread count. Triangle caching costs 36 bytes per collision triangle; per-waypoint records temporarily coexist with the final output while it is assembled.

`generateVisionData` ray callbacks must support concurrent calls. Progress callbacks are serialized and report completed counts in increasing order. Worker and progress exceptions are propagated after workers join; failed generation preserves the existing visibility data and offsets.
