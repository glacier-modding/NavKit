"""Run with blender -b --factory-startup --python-exit-code 1 -P this_file."""
import importlib.util
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

    def test_map_layer_svg_contains_only_meshes_in_inclusion_volume(self):
        bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 0))
        bpy.ops.mesh.primitive_cube_add(size=2, location=(20, 0, 0))
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
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.gltf"))
            svg = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=0.003, location=(0, 0, 0))
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.gltf"))
            svg_with_speck = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
        self.assertIn("#404040", svg)
        self.assertNotIn("20.000", svg)
        self.assertEqual(svg.count("<path"), svg_with_speck.count("<path"))
        self.assertTrue(bpy.context.scene.render.film_transparent)
        self.assertEqual(bpy.context.scene.camera.data.type, "ORTHO")
        self.assertEqual(sum(material.name.startswith("GFX ") for material in bpy.data.materials), 4)

    def test_map_layer_svg_uses_grayscale_palette_and_keeps_empty_layers_transparent(self):
        outdoor = bpy.data.collections.new("Outdoor")
        interior = bpy.data.collections.new("Rooms")
        stairs = bpy.data.collections.new("203_Room_Indoor_StairwayBottom")
        for collection in (outdoor, interior, stairs):
            bpy.context.scene.collection.children.link(collection)

        for name, collection, location in (
                ("outside_floor", outdoor, (-2, 0, 0)),
                ("inside_floor", interior, (0, 0, 0)),
                ("stair_mesh", stairs, (2, 0, 0))):
            bpy.ops.mesh.primitive_cube_add(size=1, location=location)
            obj = bpy.context.object
            obj.name = name
            for linked_collection in list(obj.users_collection):
                linked_collection.objects.unlink(obj)
            collection.objects.link(obj)
        vertices = []
        faces = []
        for step in range(4):
            x0, x1 = step, step + 1
            z0, z1 = 0, 0.4 * (step + 1)
            base = len(vertices)
            vertices.extend([
                (x0, 0, z0), (x1, 0, z0), (x1, 1, z0), (x0, 1, z0),
                (x0, 0, z1), (x1, 0, z1), (x1, 1, z1), (x0, 1, z1),
            ])
            faces.extend([
                (base, base + 3, base + 2, base + 1), (base + 4, base + 5, base + 6, base + 7),
                (base, base + 1, base + 5, base + 4), (base + 1, base + 2, base + 6, base + 5),
                (base + 2, base + 3, base + 7, base + 6), (base + 3, base, base + 4, base + 7),
            ])
        stair_data = bpy.data.meshes.new("GFX test stairs")
        stair_data.from_pydata(vertices, [], faces)
        stair_data.update()
        stair_object = bpy.data.objects.new("GFX test stair flight", stair_data)
        stairs.objects.link(stair_object)
        light = bpy.data.lights.new("GFX map test light", "POINT")
        bpy.context.scene.collection.objects.link(bpy.data.objects.new("GFX map test light", light))

        nav_data = {
            "volumeBoxes": [{
                "id": "map-volume",
                "position": {"x": 0, "y": 0, "z": 0},
                "rotation": {"w": 1, "x": 0, "y": 0, "z": 0},
                "scale": {"data": {"x": 8, "y": 4, "z": 4}},
            }],
            "mapSpaceConfigs": [
                {"inclusions": {"data": ["map-volume"]}},
                {"inclusions": {"data": []}},
            ],
        }
        with tempfile.TemporaryDirectory() as output_dir:
            glacier.render_map_layers_for_gfx(nav_data, str(Path(output_dir) / "output.gltf"))
            first_layer = (Path(output_dir) / "output_00.svg").read_text(encoding="utf-8")
            empty_layer = (Path(output_dir) / "output_01.svg").read_text(encoding="utf-8")

        for color in ("#ffffff", "#c0c0c0", "#808080", "#404040"):
            self.assertIn(color, first_layer)
        last_floor = max(first_layer.rfind("#c0c0c0"), first_layer.rfind("#808080"))
        first_wall = first_layer.find("#404040")
        first_stair = first_layer.find("#ffffff")
        self.assertGreater(first_wall, last_floor)
        self.assertGreater(first_stair, first_wall)
        self.assertGreater(first_layer.count('fill="#ffffff"'), 0)
        self.assertLessEqual(first_layer.count('fill="#ffffff"'), 4)
        self.assertIn('stroke-width="2.4"', first_layer)
        self.assertIn('fill="#000000" fill-opacity="0"', empty_layer)
        self.assertEqual(bpy.context.scene.render.engine, "BLENDER_EEVEE_NEXT")
        self.assertFalse(light.use_shadow)
        for material in bpy.data.materials:
            if material.name.startswith("GFX "):
                node_types = {node.type for node in material.node_tree.nodes}
                self.assertIn("EMISSION", node_types)
                self.assertNotIn("BSDF_PRINCIPLED", node_types)


suite = unittest.defaultTestLoader.loadTestsFromTestCase(CollectionHierarchyTests)
if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
    raise RuntimeError("Blender collection regression tests failed")
