"""Run with blender -b --factory-startup --python-exit-code 1 -P this_file."""
import importlib.util
import json
import re
from pathlib import Path
import tempfile
import unittest

import bpy


spec = importlib.util.spec_from_file_location(
    "glacier", Path(__file__).resolve().parents[1] / "src/resource/Glacier2Glb.py")
glacier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(glacier)


class CollectionHierarchyTests(unittest.TestCase):
    def setUp(self):
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        for collection in list(bpy.data.collections):
            bpy.data.collections.remove(collection)

    def load_areas(self, parents, count=1):
        glacier.load_volume_boxes({
            "aiAreaWorld": [{"name": "AIWorld", "id": "world"}],
            "volumeBoxes": [], "volumeSpheres": [],
            "aiArea": [{"name": "Area", "id": str(index),
                        "logicalParent": parents, "areaVolumeNames": []}
                       for index in range(count)],
        }, ["aiArea"])

    def test_distinct_parents_with_same_name(self):
        self.load_areas(["Scenario_Falcon (inner)", "Scenario_Falcon (outer)", "AIWorld (world)"])
        world = bpy.data.collections["AIWorld"]
        outer, = world.children
        inner, = outer.children
        area, = inner.children
        self.assertNotEqual(inner, outer)
        self.assertEqual(area.name, "Area")

    def test_shared_parent_is_linked_only_once(self):
        self.load_areas(["Scenario_Falcon (outer)", "AIWorld (world)"], count=2)
        outer, = bpy.data.collections["AIWorld"].children
        self.assertEqual(len(outer.children), 2)

    def test_names_truncated_by_blender_remain_distinct(self):
        name = "Scenario_" + "x" * 80
        self.load_areas([name + " (inner)", name + " (outer)", "AIWorld (world)"])
        outer, = bpy.data.collections["AIWorld"].children
        inner, = outer.children
        self.assertNotEqual(inner, outer)

    def test_parent_name_can_contain_parentheses(self):
        self.load_areas(["Scenario (Falcon) (outer)", "AIWorld (world)"])
        outer, = bpy.data.collections["AIWorld"].children
        self.assertEqual(outer.name, "Scenario (Falcon)")

    def test_parent_name_does_not_reuse_unrelated_collection(self):
        unrelated = bpy.data.collections.new("Scenario_Falcon")
        self.load_areas(["Scenario_Falcon (outer)", "AIWorld (world)"])
        outer, = bpy.data.collections["AIWorld"].children
        self.assertNotEqual(outer, unrelated)
        self.assertEqual(len(unrelated.children), 0)

    def add_box(self, name, collection, minimum, maximum):
        return self.add_boxes(name, collection, [(minimum, maximum)])

    def add_boxes(self, name, collection, boxes):
        """One mesh object made of closed boxes, each given as (minimum corner, maximum corner)."""
        vertices, faces = [], []
        for (x0, y0, z0), (x1, y1, z1) in boxes:
            base = len(vertices)
            vertices += [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
            faces += [tuple(base + index for index in face) for face in
                      ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))]
        data = bpy.data.meshes.new(name)
        data.from_pydata(vertices, [], faces)
        data.update()
        obj = bpy.data.objects.new(name, data)
        collection.objects.link(obj)
        return obj

    def test_map_layer_svg_contains_only_meshes_in_inclusion_volume(self):
        # Floors smaller than GFX_MINIMUM_FLOOR_ISLAND are dropped, so use 4 x 4 m slabs.
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, 0), scale=(4, 4, 1))
        bpy.ops.mesh.primitive_cube_add(size=1, location=(20, 0, 0), scale=(4, 4, 1))
        nav_data = {
            "volumeBoxes": [{
                "id": "map-volume",
                "position": {"x": 0, "y": 0, "z": 0},
                "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                "scale": {"data": {"x": 6, "y": 6, "z": 4}},
            }],
            "mapSpaceConfigs": [{
                "inclusions": {"data": ["map-volume"]},
            }],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.glb"))
            svg = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.003, location=(0, 0, 0))
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.glb"))
            svg_with_speck = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
        # The cube's top is a floor (indoor, as nothing names it outdoor); the cube outside the volume is left out.
        self.assertIn(glacier.GFX_INTERIOR_FLOOR_COLOR, svg)
        self.assertNotIn("20.00", svg)
        self.assertEqual(svg.count("<path"), svg_with_speck.count("<path"))

    def test_map_layer_svg_uses_official_palette_and_keeps_empty_layers_transparent(self):
        outdoor = bpy.data.collections.new("Outdoor")
        interior = bpy.data.collections.new("Rooms")
        # Rooms named after their stairs must not turn the whole room into stairs.
        stairs = bpy.data.collections.new("203_Room_Indoor_StairwayBottom")
        for collection in (outdoor, interior, stairs):
            bpy.context.scene.collection.children.link(collection)
        self.add_box("outside_floor", outdoor, (-4, -2, -0.2), (-1, 2, 0))
        self.add_box("inside_floor", interior, (-1, -2, -0.2), (2, 2, 0))
        self.add_box("inside_wall", interior, (-1, 1.5, 0), (2, 1.8, 3))
        self.add_box("inside_stair_landing", stairs, (2, -2, -0.2), (4, 2, 0))
        # A flight of six 0.2 m steps going 0.3 m each, one metre wide, as one mesh.
        self.add_boxes("inside_stair_flight", stairs, [((2 + 0.3 * step, -0.5, 0), (2.3 + 0.3 * step, 0.5,
                                                                                    0.2 * (step + 1)))
                                                       for step in range(6)])
        nav_data = {
            "volumeBoxes": [{
                "id": "map-volume",
                "position": {"x": 0, "y": 0, "z": 0},
                "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                "scale": {"data": {"x": 8, "y": 4, "z": 4}},
            }],
            "mapSpaceConfigs": [
                {"name": "MapSpaceConfig_00", "inclusions": {"data": ["map-volume"]}},
                {"name": "MapSpaceConfig_01", "inclusions": {"data": []}},
                {"name": "MapSpaceConfig_SAT", "inclusions": {"data": [""]}},
            ],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.glb"))
            first_layer = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
            empty_layer = (Path(output_dir) / "output_01.svg").read_text(encoding="utf-8")
            background = (Path(output_dir) / "output_background.svg").read_text(encoding="utf-8")
            # The satellite config is the background, not a third layer.
            self.assertFalse((Path(output_dir) / "output_02.svg").exists())

        for color in (glacier.GFX_EXTERIOR_FLOOR_COLOR, glacier.GFX_INTERIOR_FLOOR_COLOR, glacier.GFX_STAIRS_COLOR,
                      glacier.GFX_WALL_COLOR):
            self.assertIn('fill="' + color + '"', first_layer)
        # Floors first, then stairs, then the walls as solid shapes (no strokes), like the official maps.
        self.assertLess(first_layer.find(glacier.GFX_EXTERIOR_FLOOR_COLOR), first_layer.find(glacier.GFX_STAIRS_COLOR))
        self.assertLess(first_layer.find(glacier.GFX_STAIRS_COLOR), first_layer.find(glacier.GFX_WALL_COLOR))
        self.assertNotIn("stroke=", first_layer)
        # Only the flight's treads are stairs (canvas y 1.5 to 2.5): the stair room's landing stays floor.
        stair_path = [line for line in first_layer.splitlines() if glacier.GFX_STAIRS_COLOR in line][0]
        stair_y = [float(value) for value in
                   stair_path.split('d="')[1].replace("M", " ").replace("Z", " ").split()[1::2]]
        self.assertGreaterEqual(min(stair_y), 1.3)
        self.assertLessEqual(max(stair_y), 2.7)
        # A layer above shows the floors below it, on a canvas-sized transparent base.
        self.assertIn('fill="#000000" fill-opacity="0"', empty_layer)
        self.assertNotIn(glacier.GFX_STAIRS_COLOR, empty_layer)
        self.assertIn(glacier.GFX_BACKGROUND_COLOR, background)

    def test_background_leaves_flagged_water_out_of_the_land(self):
        rooms = bpy.data.collections.new("Rooms")
        bpy.context.scene.collection.children.link(rooms)
        self.add_box("west_floor", rooms, (-10, -5, -0.2), (0, 5, 0))
        # A water surface over the east half of the canvas, below the floor.
        water_data = bpy.data.meshes.new("00000000000000CC_0")
        water_data.from_pydata([(0, -5, -0.5), (10, -5, -0.5), (10, 5, -0.5), (0, 5, -0.5)], [], [(0, 1, 2, 3)])
        water_data.update()
        water = bpy.data.objects.new("00000000000000CC_0", water_data)
        rooms.objects.link(water)
        nav_data = {
            "volumeBoxes": [{"id": "map-volume", "position": {"x": 0, "y": 0, "z": 0},
                             "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                             "scale": {"data": {"x": 20, "y": 10, "z": 4}}}],
            "mapSpaceConfigs": [{"name": "MapSpaceConfig_00", "inclusions": {"data": ["map-volume"]}},
                                {"name": "MapSpaceConfig_SAT", "inclusions": {"data": ["map-volume"]}}],
        }

        def render():
            with tempfile.TemporaryDirectory() as output_dir:
                glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.gltf"))
                return ((Path(output_dir) / "output_background.svg").read_text(encoding="utf-8"),
                        (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8"))

        def land_x_range(background):
            land_path = [line for line in background.splitlines()
                         if 'fill="' + glacier.GFX_EXTERIOR_FLOOR_COLOR + '"' in line][0]
            numbers = re.findall(r"-?\d+(?:\.\d+)?", land_path.split('d="')[1].split('"')[0])
            xs = [float(value) for value in numbers[0::2]]
            return min(xs), max(xs)

        # Without the flag the surface is plain ground; with it, the land stops where the water starts.
        unflagged_background, _ = render()
        glacier.apply_water_material(water)
        self.assertTrue(glacier.is_water_object(water))
        background, layer = render()
        self.assertGreater(land_x_range(unflagged_background)[1], 15.0)
        self.assertLessEqual(land_x_range(background)[1], 11.0)
        # The water is not drawn as a floor of the layer either.
        self.assertNotIn(glacier.GFX_OTHER_LEVEL_COLOR, layer)

    def test_floor_outside_every_volume_goes_to_the_slice_of_its_height(self):
        # The ground floor's volume covers only the south half of the canvas, while the upper floor's covers
        # all of it: a ground-level floor in the north half is in no volume, but is still on the ground floor.
        rooms = bpy.data.collections.new("Rooms")
        bpy.context.scene.collection.children.link(rooms)
        self.add_box("north_floor", rooms, (-2, 1, -0.2), (2, 5, 0))
        self.add_box("upper_floor", rooms, (-2, -5, 2.8), (2, -1, 3))

        def volume(name, z, depth, south_only):
            return {"id": name, "position": {"x": 0, "y": -3 if south_only else 0, "z": z},
                    "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                    "scale": {"data": {"x": 12, "y": 6 if south_only else 12, "z": depth}}}

        nav_data = {
            "volumeBoxes": [volume("ground", 0, 2, True), volume("upper", 3, 4, False)],
            "mapSpaceConfigs": [{"name": "MapSpaceConfig_F0", "inclusions": {"data": ["ground"]}},
                                {"name": "MapSpaceConfig_F1", "inclusions": {"data": ["upper"]}}],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.glb"))
            ground = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
        floor_path = [line for line in ground.splitlines() if glacier.GFX_INTERIOR_FLOOR_COLOR in line][0]
        floor_y = [float(value) for value in
                   floor_path.split('d="')[1].replace("M", " ").replace("Z", " ").split()[1::2]]
        # Canvas y runs from the north edge (world y 6) down, so the north floor is at canvas y 1 to 5.
        self.assertGreaterEqual(min(floor_y), 0.8)
        self.assertLessEqual(max(floor_y), 5.2)

    def test_tall_ship_over_indoor_rooms_is_not_the_shell(self):
        # Like the Polarbear yacht: one hull mesh whose mast rises far above every map layer. Its deck lies over
        # the indoor rooms in the hull, so it stays a floor. A tall wall with a ledge and no room is still shell.
        rooms = bpy.data.collections.new("Rooms")
        bpy.context.scene.collection.children.link(rooms)
        self.add_boxes("ship_hull", rooms, [((-5, -3, 0), (5, 3, 3)), ((-0.2, -0.2, 3), (0.2, 0.2, 15))])
        self.add_boxes("shell_wall", rooms, [((15, -3, 0), (20, 3, 2)), ((20, -3, 0), (21, 3, 15))])
        nav_data = {
            "volumeBoxes": [{"id": "map-volume", "position": {"x": 8, "y": 0, "z": 3},
                             "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                             "scale": {"data": {"x": 30, "y": 8, "z": 6}}}],
            "mapSpaceConfigs": [{"name": "MapSpaceConfig_Slice0", "inclusions": {"data": ["map-volume"]}}],
            "rooms": [{"name": "101_Room_Indoor_Cabin", "position": {"x": 0, "y": 0, "z": 0},
                       "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                       "roomExtentMin": {"data": {"x": -4, "y": -2, "z": 0}},
                       "roomExtentMax": {"data": {"x": 4, "y": 2, "z": 2.9}}}],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.glb"))
            layer = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
        floor_path = [line for line in layer.splitlines() if glacier.GFX_EXTERIOR_FLOOR_COLOR in line][0]
        floor_x = [float(value) for value in
                   floor_path.split('d="')[1].split('"')[0].replace("M", " ").replace("Z", " ").split()[0::2]]
        # Canvas x is world x + 7: the deck is at canvas x 2 to 12, and the ledge (canvas x 22 to 27) is hidden.
        self.assertLessEqual(min(floor_x), 2.2)
        self.assertGreaterEqual(max(floor_x), 11.8)
        self.assertLess(max(floor_x), 20)

    def test_props_outside_the_shell_do_not_make_it_land(self):
        # Like the Polarbear arena: a ring of tall walls with ledges encloses the floor, and backstage scaffolding
        # stands outside it, high above every map layer. Only the inside of the ring is land.
        rooms = bpy.data.collections.new("Rooms")
        bpy.context.scene.collection.children.link(rooms)
        self.add_box("inside_floor", rooms, (-5, -5, -0.2), (5, 5, 0))
        for side in range(4):
            def turn(x0, y0, x1, y1, z0, z1):
                corners = [(x0, y0), (x1, y1)] if side % 2 == 0 else [(y0, x0), (y1, x1)]
                if side >= 2:
                    corners = [(-x, -y) for x, y in corners]
                (ax, ay), (bx, by) = corners
                return (min(ax, bx), min(ay, by), z0), (max(ax, bx), max(ay, by), z1)
            self.add_boxes("shell_wall_" + str(side), rooms,
                           [turn(17, -22, 20, 22, 0, 1), turn(20, -22, 22, 22, 0, 15)])
        self.add_box("scaffolding", rooms, (38, 38, 20), (42, 42, 22))

        def volume(name, z, size, depth):
            return {"id": name, "position": {"x": 0, "y": 0, "z": z}, "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                    "scale": {"data": {"x": size, "y": size, "z": depth}}}

        nav_data = {
            "volumeBoxes": [volume("background", -10, 100, 2), volume("slice", 3, 60, 6)],
            "mapSpaceConfigs": [{"name": "MapSpaceConfig_Background", "inclusions": {"data": ["background"]}},
                                {"name": "MapSpaceConfig_Slice0", "inclusions": {"data": ["slice"]}}],
            "meshes": [{"alocHash": "", "primHash": "00000000000000AA", "entity": {
                "name": "plywood_support", "position": {"x": 40, "y": 40, "z": 20}}}],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.glb"))
            background = (Path(output_dir) / "output_background.svg").read_text(encoding="utf-8")
        land_path = [line for line in background.splitlines() if glacier.GFX_EXTERIOR_FLOOR_COLOR in line][0]
        land = [float(value) for value in land_path.split('d="')[1].split('"')[0].replace("M", " ")
                .replace("Z", " ").split()]
        # Canvas coordinates are world + 50: the ring's inside is canvas 30 to 70.
        self.assertGreaterEqual(min(land), 28)
        self.assertLessEqual(max(land), 72)


class InstanceBuildCullingTests(unittest.TestCase):
    """The instance build with the include-box filter, using a stand-in for PRIM loading."""

    def setUp(self):
        for obj in list(bpy.data.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        for collection in list(bpy.data.collections):
            bpy.data.collections.remove(collection)
        self.original_load_prim = glacier.load_prim

        def load_unit_cube(filepath, lod_mask, temp_coll):
            name = Path(filepath).stem
            data = bpy.data.meshes.new(name)
            data.from_pydata([(x, y, z) for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)], [],
                             [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)])
            data.update()
            obj = bpy.data.objects.new(name, data)
            temp_coll.objects.link(obj)
            return [obj], [0]

        glacier.load_prim = load_unit_cube

    def tearDown(self):
        glacier.load_prim = self.original_load_prim

    @staticmethod
    def mesh_entry(prim_hash, entity_id, x):
        return {"alocHash": "", "primHash": prim_hash, "roomName": "Room", "roomFolderName": "Folder",
                "entity": {"id": entity_id, "name": prim_hash, "position": {"x": x, "y": 0.0, "z": 0.0},
                           "rotation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                           "scale": {"type": "SVector3", "data": {"x": 1.0, "y": 1.0, "z": 1.0}}}}

    def test_mesh_with_every_instance_culled_is_not_left_at_the_origin(self):
        # Like Hantu Port's backdrop skyscrapers: a mesh whose every instance is far outside the include box.
        nav_data = {
            "pfBoxes": [{"id": "include", "type": {"data": "PFBT_INCLUDE_MESH_COLLISION"},
                         "position": {"x": 100.0, "y": 0.0, "z": 0.0},
                         "rotation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                         "scale": {"data": {"x": 10.0, "y": 10.0, "z": 10.0}}}],
            "meshes": [self.mesh_entry("00000000000000AA", "near", 100.0),
                       self.mesh_entry("00000000000000BB", "far-a", 2000.0),
                       self.mesh_entry("00000000000000BB", "far-b", -2000.0)],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            output_dir = Path(output_dir)
            (output_dir / "prim").mkdir()
            for prim_hash in ("00000000000000AA", "00000000000000BB"):
                (output_dir / "prim" / (prim_hash + ".prim")).write_bytes(b"")
            nav_json = output_dir / "scene.nav.json"
            nav_json.write_text(json.dumps(nav_data), encoding="utf-8")
            self.assertEqual(glacier.glacier2glb(str(nav_json), str(output_dir / "output.glb"), "PRIM", "11111111",
                                                 "instance", True, False, False, False), 0)
            self.assertTrue((output_dir / "output.glb").exists())

        vertices = [obj.matrix_world @ vertex.co for obj in bpy.context.scene.objects if obj.type == "MESH"
                    for vertex in obj.data.vertices]
        self.assertTrue(vertices, "the instance inside the include box should be exported")
        self.assertTrue(all(abs(vertex.x - 100.0) <= 1.0 for vertex in vertices),
                        "only the instance inside the include box should be exported, at its own position")
        self.assertNotIn("00000000000000BB", [obj.name for obj in bpy.context.scene.objects])

    def test_water_in_an_aloc_build_comes_from_its_prim_with_the_water_material(self):
        water = self.mesh_entry("00000000000000CC", "water", 0.0)
        water["isWater"] = True
        nav_data = {"pfBoxes": [], "meshes": [water]}
        with tempfile.TemporaryDirectory() as output_dir:
            output_dir = Path(output_dir)
            (output_dir / "aloc").mkdir()
            (output_dir / "prim").mkdir()
            (output_dir / "prim" / "00000000000000CC.PRIM").write_bytes(b"")
            # A PRIM extracted for an earlier PRIM build that this ALOC build must not pick up.
            (output_dir / "prim" / "00000000000000DD.PRIM").write_bytes(b"")
            nav_json = output_dir / "scene.nav.json"
            nav_json.write_text(json.dumps(nav_data), encoding="utf-8")
            self.assertEqual(glacier.glacier2glb(str(nav_json), str(output_dir / "output.gltf"), "ALOC", "11111111",
                                                 "instance", False, False, False, False), 0)
            gltf = json.loads((output_dir / "output.gltf").read_text(encoding="utf-8"))

        self.assertEqual([material["name"] for material in gltf.get("materials", [])],
                         [glacier.NAVKIT_WATER_MATERIAL])
        self.assertEqual(len(gltf["meshes"]), 1)
        self.assertTrue(all(primitive.get("material") == 0 for primitive in gltf["meshes"][0]["primitives"]))

    def test_rooms_nest_under_their_folder_and_empty_rooms_are_removed(self):
        def entry(prim_hash, entity_id, x, folder, room):
            mesh = self.mesh_entry(prim_hash, entity_id, x)
            mesh["roomFolderName"], mesh["roomName"] = folder, room
            return mesh

        nav_data = {
            "pfBoxes": [{"id": "include", "type": {"data": "PFBT_INCLUDE_MESH_COLLISION"},
                         "position": {"x": 0.0, "y": 0.0, "z": 0.0},
                         "rotation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                         "scale": {"data": {"x": 100.0, "y": 100.0, "z": 100.0}}}],
            "meshes": [entry("00000000000000AA", "a", 0.0, "Rooms_Gardener (f)", "700_Room (a)"),
                       entry("00000000000000AA", "b", 10.0, "Rooms_Gardener (f)", "701_Room (b)"),
                       entry("00000000000000AA", "c", 5000.0, "Rooms_Gardener (f)", "702_Room (c)"),
                       entry("00000000000000AA", "d", 5000.0, "No_Room", "Setpiece (d)")],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            output_dir = Path(output_dir)
            (output_dir / "prim").mkdir()
            (output_dir / "prim" / "00000000000000AA.prim").write_bytes(b"")
            nav_json = output_dir / "scene.nav.json"
            nav_json.write_text(json.dumps(nav_data), encoding="utf-8")
            self.assertEqual(glacier.glacier2glb(str(nav_json), str(output_dir / "output.blend"), "PRIM", "11111111",
                                                 "instance", True, False, False, False), 0)

        environment = bpy.data.collections["Environment"]
        folder, = environment.children
        self.assertEqual(folder.name, "Rooms_Gardener (f)")
        self.assertEqual(sorted(child.name for child in folder.children), ["700_Room (a)", "701_Room (b)"])
        self.assertNotIn("702_Room (c)", bpy.data.collections)
        self.assertNotIn("No_Room", bpy.data.collections)


suite = unittest.TestSuite()
for test_case in (CollectionHierarchyTests, InstanceBuildCullingTests):
    suite.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(test_case))
if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
    raise RuntimeError("Blender collection regression tests failed")
