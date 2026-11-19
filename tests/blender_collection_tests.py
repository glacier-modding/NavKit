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
        self.assertIn("#59646d", svg)
        self.assertNotIn("20.000", svg)
        self.assertTrue(bpy.context.scene.render.film_transparent)
        self.assertEqual(bpy.context.scene.camera.data.type, "ORTHO")
        self.assertEqual(sum(material.name.startswith("GFX ") for material in bpy.data.materials), 3)


suite = unittest.defaultTestLoader.loadTestsFromTestCase(CollectionHierarchyTests)
if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
    raise RuntimeError("Blender collection regression tests failed")
