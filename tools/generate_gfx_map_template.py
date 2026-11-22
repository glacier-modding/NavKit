#!/usr/bin/env python3
"""Writes src/resource/map_template.gfx, the starting point NavKit builds map GFX files from.

Everything in the file is written here, so that NavKit ships no game data. It holds one map layer and the
ActionScript a map needs, assembled to ABC bytecode:

    package common {
        public class BaseControl extends Sprite        // the control interface the game calls on a map layer
    }
    package <PACKAGE> {
        public class SLICE_00_ZMapView extends Sprite  // bound to the layer's sprite
        public class SLICE_00 extends BaseControl {    // shows it, fades with the selected level, draws its bounds
            function setLayer(layer:int, total:int):void
            function setBounds(x:Number, y:Number, width:Number, height:Number):void
            override function onSetVisible(visible:Boolean):void
        }
    }
    public class <PACKAGE>_Main extends Sprite         // the document class

NavKit copies SLICE_00 for each further layer and imports the map's shapes. Usage:
    python tools/generate_gfx_map_template.py [output path]
"""
import struct
import sys
from pathlib import Path

# The identifiers a generated map has always had; the game's map system is not known to need any others.
PACKAGE = "_Scene_Mission_Polarbear_Module_005"
MAIN_CLASS = PACKAGE + "_Main"

PACKAGE_NAMESPACE = 0x16
QNAME = 0x07
TRAIT_SLOT = 0
TRAIT_METHOD = 1
TRAIT_CLASS = 4
ATTRIBUTE_OVERRIDE = 0x20
CLASS_SEALED = 0x01

GETLOCAL0, GETLOCAL1, GETLOCAL2, GETLOCAL3 = 0xD0, 0xD1, 0xD2, 0xD3
GETLOCAL, SETLOCAL = 0x62, 0x63
PUSHSCOPE, POPSCOPE = 0x30, 0x1D
RETURNVOID, RETURNVALUE = 0x47, 0x48
CONSTRUCTSUPER, CONSTRUCTPROP = 0x49, 0x4A
CALLPROPVOID, CALLPROPERTY = 0x4F, 0x46
FINDPROPSTRICT, GETLEX = 0x5D, 0x60
SETPROPERTY, GETPROPERTY, INITPROPERTY = 0x61, 0x66, 0x68
GETSCOPEOBJECT, NEWCLASS = 0x65, 0x58
PUSHNULL, PUSHBYTE, PUSHINT, PUSHDOUBLE = 0x20, 0x24, 0x2D, 0x2F
IFFALSE, IFNE = 0x12, 0x14


def u30(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


class Abc:
    def __init__(self):
        self.ints, self.doubles, self.strings, self.namespaces, self.multinames = [], [], [], [], []
        self.methods, self.instances, self.classes, self.scripts, self.bodies = [], [], [], [], []

    @staticmethod
    def intern(pool, value):
        if value not in pool:
            pool.append(value)
        return pool.index(value) + 1

    def string(self, text):
        return self.intern(self.strings, text)

    def int(self, value):
        return self.intern(self.ints, value)

    def double(self, value):
        return self.intern(self.doubles, value)

    def name(self, qualified):
        """The index of a public QName, from "package.Name" or "Name"."""
        package, _, name = qualified.rpartition(".")
        namespace = self.intern(self.namespaces, self.string(package))
        return self.intern(self.multinames, (namespace, self.string(name)))

    def method(self, parameters, returns, code, max_stack, locals_extra, scope_depth):
        """Adds a method and its body. `code` is a list of opcodes and (kind, value) operands."""
        index = len(self.methods)
        types = [self.name(parameter) for parameter in parameters]
        self.methods.append((types, self.name(returns) if returns else 0))
        self.bodies.append((index, max_stack, len(parameters) + 1 + locals_extra, scope_depth, scope_depth + 1,
                            self.assemble(code)))
        return index

    def assemble(self, code):
        out = bytearray()
        labels, jumps = {}, []
        for item in code:
            if isinstance(item, int):
                out.append(item)
            elif item[0] == "label":
                labels[item[1]] = len(out)
            elif item[0] == "jump":
                out.append(item[1])
                out += b"\0\0\0"
                jumps.append((len(out), item[2]))
            elif item[0] == "u8":
                out.append(item[1])
            elif item[0] == "u30":
                out += u30(item[1])
            elif item[0] == "name":
                out += u30(self.name(item[1]))
            elif item[0] == "int":
                out += u30(self.int(item[1]))
            elif item[0] == "double":
                out += u30(self.double(item[1]))
            else:
                raise ValueError(item)
        for end, label in jumps:
            out[end - 3:end] = struct.pack("<i", labels[label] - end)[:3]
        return bytes(out)

    def add_class(self, name, base, chain, constructor, methods=(), slots=(), constructor_stack=2):
        """Adds a class in its own script. `chain` is its ancestors from Object down to `base`."""
        depth = len(chain) + 1
        constructor_index = self.method([], None, constructor, constructor_stack, 0, depth + 1)
        class_init = self.method([], None, [GETLOCAL0, PUSHSCOPE, RETURNVOID], 1, 0, depth)
        traits = [(self.name(slot), TRAIT_SLOT, 0) for slot in slots]
        for method_name, parameters, returns, code, max_stack, locals_extra, attributes in methods:
            index = self.method(parameters, returns, code, max_stack, locals_extra, depth + 1)
            traits.append((self.name(method_name), TRAIT_METHOD | attributes, index))
        class_index = len(self.instances)
        self.instances.append((self.name(name), self.name(base), constructor_index, traits))
        self.classes.append(class_init)
        script_code = [GETLOCAL0, PUSHSCOPE, GETSCOPEOBJECT, ("u8", 0)]
        for ancestor in chain:
            script_code += [GETLEX, ("name", ancestor), PUSHSCOPE]
        script_code += [GETLEX, ("name", base), NEWCLASS, ("u30", class_index)]
        script_code += [POPSCOPE] * len(chain)
        script_code += [INITPROPERTY, ("name", name), RETURNVOID]
        script_init = self.method([], None, script_code, 2, 0, 0)
        # The script body's scope reaches past the single level `method` assumes.
        body = self.bodies[-1]
        self.bodies[-1] = (body[0], body[1], body[2], 1, depth + 1, body[5])
        self.scripts.append((script_init, self.name(name), class_index))

    def serialize(self):
        out = bytearray(struct.pack("<HH", 16, 46))
        out += u30(len(self.ints) + 1) if self.ints else u30(0)
        for value in self.ints:
            out += u30(value)
        out += u30(0)  # no unsigned integers
        out += u30(len(self.doubles) + 1) if self.doubles else u30(0)
        for value in self.doubles:
            out += struct.pack("<d", value)
        out += u30(len(self.strings) + 1)
        for text in self.strings:
            encoded = text.encode("utf-8")
            out += u30(len(encoded)) + encoded
        out += u30(len(self.namespaces) + 1)
        for name in self.namespaces:
            out += bytes([PACKAGE_NAMESPACE]) + u30(name)
        out += u30(0)  # no namespace sets
        out += u30(len(self.multinames) + 1)
        for namespace, name in self.multinames:
            out += bytes([QNAME]) + u30(namespace) + u30(name)
        out += u30(len(self.methods))
        for parameters, returns in self.methods:
            out += u30(len(parameters)) + u30(returns)
            for parameter in parameters:
                out += u30(parameter)
            out += u30(0) + b"\0"  # no name, no flags
        out += u30(0)  # no metadata

        def traits(items):
            data = bytearray(u30(len(items)))
            for name, kind, value in items:
                data += u30(name) + bytes([kind])
                if kind & 0x0F == TRAIT_SLOT:
                    data += u30(0) + u30(0) + u30(0)  # automatic slot, any type, no value
                elif kind & 0x0F == TRAIT_CLASS:
                    data += u30(1) + u30(value)
                else:
                    data += u30(0) + u30(value)
            return data

        out += u30(len(self.instances))
        for name, base, constructor, instance_traits in self.instances:
            out += u30(name) + u30(base) + bytes([CLASS_SEALED]) + u30(0) + u30(constructor) + traits(instance_traits)
        for class_init in self.classes:
            out += u30(class_init) + traits([])
        out += u30(len(self.scripts))
        for script_init, name, class_index in self.scripts:
            out += u30(script_init) + traits([(name, TRAIT_CLASS, class_index)])
        out += u30(len(self.bodies))
        for method, max_stack, local_count, init_scope, max_scope, code in self.bodies:
            out += u30(method) + u30(max_stack) + u30(local_count) + u30(init_scope) + u30(max_scope)
            out += u30(len(code)) + code + u30(0) + traits([])
        return bytes(out)


def build_abc():
    abc = Abc()
    sprite_chain = ["Object", "flash.events.EventDispatcher", "flash.display.DisplayObject",
                    "flash.display.InteractiveObject", "flash.display.DisplayObjectContainer",
                    "flash.display.Sprite"]
    enter = [GETLOCAL0, PUSHSCOPE]
    nothing = enter + [RETURNVOID]
    base_control = "common.BaseControl"
    abc.add_class(
        base_control, "flash.display.Sprite", sprite_chain,
        enter + [GETLOCAL0, CONSTRUCTSUPER, ("u30", 0),
                 GETLOCAL0, FINDPROPSTRICT, ("name", "flash.geom.Rectangle"),
                 CONSTRUCTPROP, ("name", "flash.geom.Rectangle"), ("u30", 0), SETPROPERTY, ("name", "bounds"),
                 RETURNVOID],
        methods=[
            ("onAttached", [], "void", nothing, 1, 0, 0),
            ("onChildrenAttached", [], "void",
             enter + [GETLOCAL0, GETLOCAL0, GETLOCAL0, CALLPROPERTY, ("name", "getBounds"), ("u30", 1),
                      SETPROPERTY, ("name", "bounds"), RETURNVOID], 3, 0, 0),
            ("onEditorSelected", ["Boolean", "Number", "Number"], "void", nothing, 1, 0, 0),
            ("clearMatrix3D", [], "void",
             enter + [GETLOCAL0, GETPROPERTY, ("name", "transform"), PUSHNULL,
                      SETPROPERTY, ("name", "perspectiveProjection"),
                      GETLOCAL0, GETPROPERTY, ("name", "transform"), PUSHNULL, SETPROPERTY, ("name", "matrix3D"),
                      RETURNVOID], 2, 0, 0),
            ("getContainer", [], "flash.display.Sprite", enter + [GETLOCAL0, RETURNVALUE], 1, 0, 0),
            ("onSetConstrained", ["Boolean"], "void", nothing, 1, 0, 0),
            ("onSetSize", ["Number", "Number"], "void", nothing, 1, 0, 0),
            ("onSetViewport", ["Number", "Number", "Number"], "void", nothing, 1, 0, 0),
            ("onSetVisible", ["Boolean"], "void", nothing, 1, 0, 0),
            ("onMouseEnabled", ["Boolean"], "void",
             enter + [GETLOCAL0, GETLOCAL1, SETPROPERTY, ("name", "mouseEnabled"),
                      GETLOCAL0, GETLOCAL1, SETPROPERTY, ("name", "mouseChildren"), RETURNVOID], 2, 0, 0),
        ],
        slots=["bounds", "CallEntity"], constructor_stack=3)

    view = PACKAGE + ".SLICE_00_ZMapView"
    abc.add_class(view, "flash.display.Sprite", sprite_chain,
                  enter + [GETLOCAL0, CONSTRUCTSUPER, ("u30", 0), RETURNVOID], constructor_stack=1)

    def set_alpha(when_zero_label, first, second):
        return [GETLOCAL0, *first, SETPROPERTY, ("name", "alpha"), RETURNVOID, ("label", when_zero_label),
                GETLOCAL0, *second, SETPROPERTY, ("name", "alpha"), RETURNVOID]

    opaque = [PUSHBYTE, ("u8", 1)]
    faded = [PUSHDOUBLE, ("double", 0.2)]
    hidden = [PUSHBYTE, ("u8", 0)]
    graphics_local = ("u30", 5)
    abc.add_class(
        PACKAGE + ".SLICE_00", base_control, sprite_chain + [base_control],
        enter + [GETLOCAL0, CONSTRUCTSUPER, ("u30", 0),
                 GETLOCAL0, FINDPROPSTRICT, ("name", view), CONSTRUCTPROP, ("name", view), ("u30", 0),
                 SETPROPERTY, ("name", "mapView"),
                 GETLOCAL0, GETLOCAL0, GETPROPERTY, ("name", "mapView"), CALLPROPVOID, ("name", "addChild"),
                 ("u30", 1),
                 GETLOCAL0, FINDPROPSTRICT, ("name", "flash.display.Shape"),
                 CONSTRUCTPROP, ("name", "flash.display.Shape"), ("u30", 0), SETPROPERTY, ("name", "boundary"),
                 GETLOCAL0, GETLOCAL0, GETPROPERTY, ("name", "boundary"), CALLPROPVOID, ("name", "addChild"),
                 ("u30", 1), RETURNVOID],
        methods=[
            # The selected level is opaque, and the others fade.
            ("setLayer", ["int", "int"], "void",
             enter + [GETLOCAL1, PUSHBYTE, ("u8", 0), ("jump", IFNE, "other")] + set_alpha("other", opaque, faded),
             2, 0, 0),
            ("setBounds", ["Number", "Number", "Number", "Number"], "void",
             enter + [GETLOCAL0, GETPROPERTY, ("name", "boundary"), GETPROPERTY, ("name", "graphics"),
                      SETLOCAL, graphics_local,
                      GETLOCAL, graphics_local, CALLPROPVOID, ("name", "clear"), ("u30", 0),
                      GETLOCAL, graphics_local, PUSHBYTE, ("u8", 1), PUSHINT, ("int", 0x111111),
                      PUSHDOUBLE, ("double", 0.1), CALLPROPVOID, ("name", "lineStyle"), ("u30", 3),
                      GETLOCAL, graphics_local, PUSHBYTE, ("u8", 0), PUSHBYTE, ("u8", 0), GETLOCAL3,
                      GETLOCAL, ("u30", 4), CALLPROPVOID, ("name", "drawRect"), ("u30", 4),
                      GETLOCAL, graphics_local, CALLPROPVOID, ("name", "endFill"), ("u30", 0), RETURNVOID],
             5, 1, 0),
            ("onSetVisible", ["Boolean"], "void",
             enter + [GETLOCAL0, GETLOCAL1, SETPROPERTY, ("name", "visible"),
                      GETLOCAL1, ("jump", IFFALSE, "hide")] + set_alpha("hide", opaque, hidden),
             2, 0, ATTRIBUTE_OVERRIDE),
        ],
        slots=["mapView", "boundary"], constructor_stack=3)

    abc.add_class(MAIN_CLASS, "flash.display.Sprite", sprite_chain,
                  enter + [GETLOCAL0, CONSTRUCTSUPER, ("u30", 0), RETURNVOID], constructor_stack=1)
    return abc.serialize()


class Bits:
    def __init__(self):
        self.bits = []

    def write(self, value, count):
        self.bits += [(value >> (count - 1 - bit)) & 1 for bit in range(count)]

    def bytes(self):
        padded = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(int("".join(map(str, padded[index:index + 8])), 2) for index in range(0, len(padded), 8))


def rect(width, height, bits=16):
    out = Bits()
    out.write(bits, 5)
    for value in (0, width, 0, height):
        out.write(value, bits)
    return out.bytes()


def identity_matrix():
    out = Bits()
    out.write(1, 1)
    out.write(18, 5)
    out.write(0x10000, 18)
    out.write(0x10000, 18)
    out.write(0, 1)
    out.write(1, 5)
    out.write(0, 1)
    out.write(0, 1)
    return out.bytes()


def placeholder_shape(shape_id, size=20):
    """A filled square; NavKit replaces it with the map's shapes."""
    edges = Bits()
    edges.write(1, 4)  # one fill-style bit
    edges.write(0, 4)  # no line-style bits
    edges.write(0, 1)
    edges.write(0b00101, 5)  # style change: fill style 1, and move
    edges.write(1, 5)
    edges.write(0, 1)
    edges.write(0, 1)
    edges.write(1, 1)
    for horizontal, delta in ((True, size), (False, size), (True, -size), (False, -size)):
        edges.write(0b11, 2)  # a straight edge
        edges.write(5, 4)  # 7-bit deltas
        edges.write(0, 1)  # along one axis
        edges.write(0 if horizontal else 1, 1)
        edges.write(delta & 0x7F, 7)
    edges.write(0, 6)  # end of shape
    return (struct.pack("<H", shape_id) + rect(size, size) +
            b"\x01\x00" + bytes((0x1C, 0x1E, 0x20, 0xFF)) + b"\x00" + edges.bytes())


def tag(code, data=b""):
    if len(data) < 0x3F:
        return struct.pack("<H", code << 6 | len(data)) + data
    return struct.pack("<HI", code << 6 | 0x3F, len(data)) + data


def cstring(text):
    return text.encode("utf-8") + b"\0"


def build_gfx():
    slice_view = PACKAGE + ".SLICE_00_ZMapView"
    name = PACKAGE.encode("utf-8")
    exporter = struct.pack("<HIH", 1025, 0, 14) + b"\0" + bytes([len(name)]) + name + b"\0\0"
    placement = bytes([0x06]) + struct.pack("<HH", 1, 1) + identity_matrix()
    sprite = struct.pack("<HH", 2, 0) + tag(26, placement) + tag(0)
    exports = struct.pack("<HH", 1, 2) + cstring(slice_view)
    symbols = struct.pack("<HH", 2, 2) + cstring(slice_view) + struct.pack("<H", 0) + cstring(MAIN_CLASS)
    body = b"".join((
        tag(1000, exporter),
        tag(69, struct.pack("<I", 0x09)),
        tag(9, b"\xff\xff\xff"),
        tag(43, cstring(MAIN_CLASS)),
        tag(32, placeholder_shape(1)),
        tag(39, sprite),
        tag(56, exports),
        tag(82, struct.pack("<I", 1) + cstring("frame1") + build_abc()),
        tag(76, symbols),
        tag(1),
        tag(0),
    ))
    header = rect(10000, 7500) + struct.pack("<HH", 60 << 8, 1)
    return b"GFX\x0e" + struct.pack("<I", 8 + len(header) + len(body)) + header + body


if __name__ == "__main__":
    default = Path(__file__).resolve().parents[1] / "src" / "resource" / "map_template.gfx"
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    data = build_gfx()
    output.write_bytes(data)
    print("Wrote", output, len(data), "bytes")
