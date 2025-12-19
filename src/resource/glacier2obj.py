import os
import bpy
import bmesh
import ctypes
import numpy as np
import json
import mathutils
from mathutils import Euler
import math
import enum
import sys
from timeit import default_timer as timer
from enum import IntEnum
import struct
import binascii

from bpy.props import (
    StringProperty,
    BoolProperty,
    BoolVectorProperty,
    CollectionProperty,
    PointerProperty,
    IntProperty,
    EnumProperty,
    FloatProperty,
    FloatVectorProperty,
    IntVectorProperty,
)

from bpy.types import (
    Context,
    Panel,
    Operator,
    PropertyGroup,
)
global glacier2obj_enabled_log_levels
glacier2obj_enabled_log_levels = ["ERROR", "WARNING", "INFO"]  # Log levels are "DEBUG", "INFO", "WARNING", "ERROR"

# General


def log(level, msg, filter_field):
    if level in glacier2obj_enabled_log_levels: # and filter_field == "007573591BE1BE69":
        print("[" + str(level) + "] " + str(filter_field) + ": " + str(msg), flush=True)

class BinaryReader:
    def __init__(self, stream):
        self.file = stream

    def close(self):
        self.file.close()

    def seek(self, position):
        self.file.seek(position)

    def seekBy(self, value):
        self.file.seek(self.file.tell() + value)

    def tell(self):
        return self.file.tell()

    # reading
    def readHex(self, length):
        hexdata = self.file.read(length)
        hexstr = ""
        for h in hexdata:
            hexstr = hex(h)[2:].zfill(2) + hexstr
        return hexstr

    def readInt64(self):
        return struct.unpack("q", self.file.read(8))[0]

    def readIntBigEndian(self):
        return struct.unpack(">i", self.file.read(4))[0]

    def readUInt64(self):
        return struct.unpack("Q", self.file.read(8))[0]

    def readInt(self):
        return struct.unpack("i", self.file.read(4))[0]

    def readUInt(self):
        return struct.unpack("I", self.file.read(4))[0]

    def readUShort(self):
        return struct.unpack("H", self.file.read(2))[0]

    def readShort(self):
        return struct.unpack("h", self.file.read(2))[0]

    def readByte(self):
        return struct.unpack("b", self.file.read(1))[0]

    def readUByte(self):
        return struct.unpack("B", self.file.read(1))[0]

    def readFloat(self):
        return struct.unpack("!f", bytes.fromhex(self.readHex(4)))[0]

    def readShortVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = self.readShort()
        return vec

    def readShortQuantizedVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = self.readShort() / 0x7FFF
        return vec

    def readShortQuantizedVecScaledBiased(self, size, scale, bias):
        vec = [0] * size
        for i in range(size):
            vec[i] = ((self.readShort() * scale[i]) / 0x7FFF) + bias[i]
        return vec

    def readUByteQuantizedVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = ((self.readUByte() * 2) / 255) - 1
        return vec

    def readUByteVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = self.readUByte()
        return vec

    def readUShortToFloatVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = ((self.readUShort() * float(2.0)) / float(65535.0)) - float(1.0)
        return vec

    def readFloatVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = self.readFloat()
        return vec

    def readUBytesToBitBoolArray(self, size):
        bit_array = ""
        bool_array = []
        bytes = self.readUByteVec(size)
        for i in range(size):
            bit_array = bin(bytes[i])[2:].zfill(8) + bit_array
        for i in bit_array[::-1]:
            bool_array.append(True if i == "1" else False)
        return {"count": sum(bool_array), "bones": bool_array}

    def readUIntVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = self.readUInt()
        return vec

    def readIntVec(self, size):
        vec = [0] * size
        for i in range(size):
            vec[i] = self.readInt()
        return vec

    def readString(self, length):
        string = []
        for char in range(length):
            c = self.file.read(1)
            if c != b"\x00":
                string.append(c)
        return b"".join(string)

    def readCString(self):
        string = []
        while True:
            c = self.file.read(1)
            if c == b"\x00":
                return b"".join(string)
            string.append(c)

    # writing
    def align(self, num, bit=0x0):
        padding = (num - (self.tell() % num)) % num
        self.writeHex(bytes([bit] * padding))

    def writeHex(self, bytes):
        self.file.write(bytes)

    def writeInt64(self, val):
        self.file.write(val.to_bytes(8, byteorder="little", signed=True))

    def writeUInt64(self, val):
        self.file.write(val.to_bytes(8, byteorder="little", signed=False))

    def writeInt(self, val):
        self.file.write(val.to_bytes(4, byteorder="little", signed=True))

    def writeUInt(self, val):
        self.file.write(val.to_bytes(4, byteorder="little", signed=False))

    def writeShort(self, val):
        self.file.write(val.to_bytes(2, byteorder="little", signed=True))

    def writeUShort(self, val):
        self.file.write(val.to_bytes(2, byteorder="little", signed=False))

    def writeByte(self, val):
        self.file.write(val.to_bytes(1, byteorder="little", signed=True))

    def writeUByte(self, val):
        self.file.write(val.to_bytes(1, byteorder="little", signed=False))

    def writeFloat(self, val):
        self.file.write(struct.pack("f", val))

    def writeShortVec(self, vec):
        for val in vec:
            self.writeShort(val)

    def writeShortQuantizedVec(self, vec):
        for i in range(len(vec)):
            self.writeShort(int(round(vec[i] * 0x7FFF)))

    def writeShortQuantizedVecScaledBiased(self, vec, scale, bias):
        for i in range(len(vec)):
            value = int(round(((vec[i] - bias[i]) * 0x7FFF) / scale[i]))
            if value > 0x7FFF:
                value = 0x7FFF
            elif value < -0x7FFF:
                value = -0x7FFF
            self.writeShort(value)

    def writeUByteQuantizedVec(self, vec):
        for val in vec:
            self.writeUByte(int(round(((val + 1) * 255) / 2)))

    def writeUByteVec(self, vec):
        for val in vec:
            self.writeUByte(val)

    def writeUShortFromFloatVec(self, vec):
        for val in vec:
            self.writeUShort(int(round(((val + 1) * 0xFFFF) / 2)))

    def writeFloatVec(self, vec):
        for val in vec:
            self.writeFloat(val)

    def writeUBytesFromBitBoolArray(self, vec):
        log("ERROR",
            "Attempted to use writeUBytesFromBitBoolArray, but this function is not implemented yet!",
            "writeUBytesFromBitBoolArray"
            )

    def writeUIntVec(self, vec):
        for val in vec:
            self.writeUInt(val)

    def writeIntVec(self, vec):
        for val in vec:
            self.writeInt(val)

    def writeCString(self, string):
        if len(string) > 0:
            self.writeHex(string)
            self.writeUByte(0)

    def writeString(self, string, length):
        self.writeHex(string)
        self.writeUByteVec([0] * (length - len(string)))

    def IOI_round(self, float):
        # please upgrade to a modulo solution
        byte_const = 1 / 255
        rounded_byte = 0
        byte = int(float / byte_const)
        if abs(float - (byte * byte_const)) > abs(float - ((byte + 1) * byte_const)):
            rounded_byte = byte
        else:
            rounded_byte = byte + 1

        return rounded_byte - 1


# PRIM
import enum
import sys

"""
The RenderPrimitive format:

RenderPrimitive ↴
    PrimObjectHeader
    Objects ↴
        PrimMesh ↴
            PrimObject
            PrimSubMesh ↴
                PrimObject
        ...
"""


class PrimObjectSubtype(enum.IntEnum):
    """
    Enum defining a subtype. All objects inside a prim have a subtype
    The StandarduvX types are not used within the H2016, H2 and H3 games,
    a probable cause for this is the introduction of a num_uvchannels variable.
    """

    Standard = 0
    Linked = 1
    Weighted = 2
    Standarduv2 = 3
    Standarduv3 = 4
    Standarduv4 = 5


class PrimType(enum.IntEnum):
    """
    A type property attached to all headers found within the prim format
    """

    Unknown = 0
    ObjectHeader = 1
    Mesh = 2
    Decal = 3
    Sprites = 4
    Shape = 5
    Unused = 6


class PrimMeshClothId:
    """Bitfield defining properties of the cloth data. Most bits are unknown."""

    def __init__(self, value):
        self.bitfield = value

    def isSmoll(self):  # thank PawRep for this amazing name :)
        return self.bitfield & 0x80 == 0x80

    def write(self, br):
        br.writeUInt(self.bitfield)


class PrimObjectHeaderPropertyFlags:
    """Global properties defined in the main header of a RenderPrimitive."""

    def __init__(self, val: int):
        self.bitfield = val

    def hasBones(self):
        return self.bitfield & 0b1 == 1

    def hasFrames(self):
        return self.bitfield & 0b10 == 2

    def isLinkedObject(self):
        return self.bitfield & 0b100 == 4

    def isWeightedObject(self):
        return self.bitfield & 0b1000 == 8

    def useBounds(self):
        return self.bitfield & 0b100000000 == 128

    def hasHighResolution(self):
        return self.bitfield & 0b1000000000 == 256

    def write(self, br):
        br.writeUInt(self.bitfield)

    def toString(self):
        return (
                "Object Header property flags:\n"
                + "\thas bones:\t\t"
                + str(self.hasBones())
                + "\n"
                + "\thas frames:\t\t"
                + str(self.hasFrames())
                + "\n"
                + "\tis linked object:\t"
                + str(self.isLinkedObject())
                + "\n"
                + "\tis weighted object:\t"
                + str(self.isWeightedObject())
                + "\n"
                + "\tuse bounds:\t\t"
                + str(self.useBounds())
                + "\n"
                + "\thas high resolution:\t"
                + str(self.hasHighResolution())
                + "\n"
        )


class PrimObjectPropertyFlags:
    """Mesh specific properties, used in Mesh and SubMesh."""

    def __init__(self, value: int):
        self.bitfield = value

    def isXaxisLocked(self):
        return self.bitfield & 0b1 == 1

    def isYaxisLocked(self):
        return self.bitfield & 0b10 == 2

    def isZaxisLocked(self):
        return self.bitfield & 0b100 == 4

    def isHighResolution(self):
        return self.bitfield & 0b1000 == 8

    def hasPs3Edge(self):
        return self.bitfield & 0b10000 == 16

    def useColor1(self):
        return self.bitfield & 0b100000 == 32

    def hasNoPhysicsProp(self):
        return self.bitfield & 0b1000000 == 64

    def setXaxisLocked(self):
        self.bitfield |= 0b1

    def setYaxisLocked(self):
        self.bitfield |= 0b10

    def setZaxisLocked(self):
        self.bitfield |= 0b100

    def setHighResolution(self):
        self.bitfield |= 0b1000

    def setColor1(self):
        self.bitfield |= 0b100000

    def setNoPhysics(self):
        self.bitfield |= 0b1000000

    def write(self, br):
        br.writeUByte(self.bitfield)

    def toString(self):
        return (
                "Object property flags:\n"
                + "\tX axis locked:\t\t"
                + str(self.isXaxisLocked())
                + "\n"
                + "\tY axis locked:\t\t"
                + str(self.isYaxisLocked())
                + "\n"
                + "\tZ axis locked:\t\t"
                + str(self.isZaxisLocked())
                + "\n"
                + "\tis high resolution:\t"
                + str(self.isHighResolution())
                + "\n"
                + "\thas ps3 edge:\t\t"
                + str(self.hasPs3Edge())
                + "\n"
                + "\tuses color1:\t\t"
                + str(self.useColor1())
                + "\n"
                + "\thas no physics props:\t"
                + str(self.hasNoPhysicsProp())
                + "\n"
        )


class BoxColiEntry:
    """Helper class for BoxColi, defines an entry to store BoxColi"""

    def __init__(self):
        self.min = [0] * 3
        self.max = [0] * 3

    def read(self, br):
        self.min = br.readUByteVec(3)
        self.max = br.readUByteVec(3)

    def write(self, br):
        br.writeUByteVec(self.min)
        br.writeUByteVec(self.max)


class BoxColi:
    """Used to store and array of BoxColi. Used for bullet collision"""

    def __init__(self):
        self.tri_per_chunk = 0x20
        self.box_entries = []

    def read(self, br):
        num_chunks = br.readUShort()
        self.tri_per_chunk = br.readUShort()
        self.box_entries = [-1] * num_chunks
        self.box_entries = [BoxColiEntry() for _ in range(num_chunks)]
        for box_entry in self.box_entries:
            box_entry.read(br)

    def write(self, br):
        br.writeUShort(len(self.box_entries))
        br.writeUShort(self.tri_per_chunk)
        for entry in self.box_entries:
            entry.write(br)
        br.align(4)


class Vertex:
    """A vertex with all field found inside a RenderPrimitive file"""

    def __init__(self):
        self.position = [0] * 4
        self.weight = [[0] * 4 for _ in range(2)]
        self.joint = [[0] * 4 for _ in range(2)]
        self.normal = [1] * 4
        self.tangent = [1] * 4
        self.bitangent = [1] * 4
        self.uv = [[0] * 2]
        self.color = [0xFF] * 4


class PrimMesh:
    """A subMesh wrapper class, used to store information about a mesh, as well as the mesh itself (called sub_mesh)"""

    def __init__(self):
        self.prim_object = PrimObject(2)
        self.pos_scale = [
                             1.0
                         ] * 4  # TODO: Remove this, should be calculated when exporting
        self.pos_bias = [0.0] * 4  # TODO: No need to keep these around
        self.tex_scale_bias = [1.0, 1.0, 0.0, 0.0]  # TODO: This can also go
        self.cloth_id = PrimMeshClothId(0)
        self.sub_mesh = PrimSubMesh()

    def read(self, br, flags):
        self.prim_object.read(br)

        # this will point to a table of submeshes, this is not really usefull since this table will always contain a
        # single pointer if the table were to contain multiple pointer we'd have no way of knowing since the table
        # size is never defined. to improve readability sub_mesh_table is not an array
        sub_mesh_table_offset = br.readUInt()

        self.pos_scale = br.readFloatVec(4)
        self.pos_bias = br.readFloatVec(4)
        self.tex_scale_bias = br.readFloatVec(4)

        self.cloth_id = PrimMeshClothId(br.readUInt())

        old_offset = br.tell()
        br.seek(sub_mesh_table_offset)
        sub_mesh_offset = br.readUInt()
        br.seek(sub_mesh_offset)
        self.sub_mesh.read(br, self, flags)
        br.seek(
            old_offset
        )  # reset offset to end of header, this is required for WeightedPrimMesh

    def write(self, br, flags):
        self.update()

        sub_mesh_offset = self.sub_mesh.write(br, self, flags)

        header_offset = br.tell()

        self.prim_object.write(br)

        br.writeUInt(sub_mesh_offset)

        br.writeFloatVec(self.pos_scale)
        br.writeFloatVec(self.pos_bias)
        br.writeFloatVec(self.tex_scale_bias)

        self.cloth_id.write(br)

        br.align(16)

        return header_offset

    def update(self):
        bb = self.sub_mesh.calc_bb()
        bb_min = bb[0]
        bb_max = bb[1]

        # set bounding box
        self.prim_object.min = bb_min
        self.prim_object.max = bb_max

        # set position scale
        self.pos_scale[0] = (bb_max[0] - bb_min[0]) * 0.5
        self.pos_scale[1] = (bb_max[1] - bb_min[1]) * 0.5
        self.pos_scale[2] = (bb_max[2] - bb_min[2]) * 0.5
        self.pos_scale[3] = 0.5
        for i in range(3):
            self.pos_scale[i] = 0.5 if self.pos_scale[i] <= 0.0 else self.pos_scale[i]

        # set position bias
        self.pos_bias[0] = (bb_max[0] + bb_min[0]) * 0.5
        self.pos_bias[1] = (bb_max[1] + bb_min[1]) * 0.5
        self.pos_bias[2] = (bb_max[2] + bb_min[2]) * 0.5
        self.pos_bias[3] = 1

        bb_uv = self.sub_mesh.calc_UVbb()
        bb_uv_min = bb_uv[0]
        bb_uv_max = bb_uv[1]

        # set UV scale
        self.tex_scale_bias[0] = (bb_uv_max[0] - bb_uv_min[0]) * 0.5
        self.tex_scale_bias[1] = (bb_uv_max[1] - bb_uv_min[1]) * 0.5
        for i in range(2):
            self.tex_scale_bias[i] = (
                0.5 if self.tex_scale_bias[i] <= 0.0 else self.tex_scale_bias[i]
            )

        # set UV bias
        self.tex_scale_bias[2] = (bb_uv_max[0] + bb_uv_min[0]) * 0.5
        self.tex_scale_bias[3] = (bb_uv_max[1] + bb_uv_min[1]) * 0.5


class PrimMeshWeighted(PrimMesh):
    """A different variant of PrimMesh. In addition to PrimMesh it also stores bone data"""

    def __init__(self):
        super().__init__()
        self.prim_mesh = PrimMesh()
        self.num_copy_bones = 0
        self.copy_bones = 0
        self.bone_indices = BoneIndices()
        self.bone_info = BoneInfo()

    def read(self, br, flags):
        super().read(br, flags)
        self.num_copy_bones = br.readUInt()
        copy_bones_offset = br.readUInt()

        bone_indices_offset = br.readUInt()
        bone_info_offset = br.readUInt()

        br.seek(copy_bones_offset)
        self.copy_bones = 0  # empty, because unknown

        br.seek(bone_indices_offset)
        self.bone_indices.read(br)

        br.seek(bone_info_offset)
        self.bone_info.read(br)

    def write(self, br, flags):
        sub_mesh_offset = self.sub_mesh.write(br, self.prim_mesh, flags)

        bone_info_offset = br.tell()
        self.bone_info.write(br)

        bone_indices_offset = br.tell()
        self.bone_indices.write(br)

        header_offset = br.tell()

        bb = self.sub_mesh.calc_bb()

        # set bounding box
        self.prim_object.min = bb[0]
        self.prim_object.max = bb[1]

        self.update()
        self.prim_object.write(br)

        br.writeUInt(sub_mesh_offset)

        br.writeFloatVec(self.pos_scale)
        br.writeFloatVec(self.pos_bias)
        br.writeFloatVec(self.tex_scale_bias)

        self.cloth_id.write(br)

        br.writeUInt(self.num_copy_bones)
        br.writeUInt(0)  # copy_bones offset PLACEHOLDER

        br.writeUInt(bone_indices_offset)
        br.writeUInt(bone_info_offset)

        br.align(16)
        return header_offset


class VertexBuffer:
    """A helper class used to store and manage the vertices found inside a PrimSubMesh"""

    def __init__(self):
        self.vertices = []

    def read(
            self,
            br,
            num_vertices: int,
            num_uvchannels: int,
            mesh: PrimMesh,
            sub_mesh_color1: [],
            sub_mesh_flags: PrimObjectPropertyFlags,
            flags: PrimObjectHeaderPropertyFlags,
    ):
        self.vertices = [Vertex() for _ in range(num_vertices)]

        for vertex in self.vertices:
            if mesh.prim_object.properties.isHighResolution():
                vertex.position[0] = (
                                             br.readFloat() * mesh.pos_scale[0]
                                     ) + mesh.pos_bias[0]
                vertex.position[1] = (
                                             br.readFloat() * mesh.pos_scale[1]
                                     ) + mesh.pos_bias[1]
                vertex.position[2] = (
                                             br.readFloat() * mesh.pos_scale[2]
                                     ) + mesh.pos_bias[2]
                vertex.position[3] = 1
            else:
                vertex.position = br.readShortQuantizedVecScaledBiased(
                    4, mesh.pos_scale, mesh.pos_bias
                )

        if flags.isWeightedObject():
            for vertex in self.vertices:
                vertex.weight[0][0] = br.readUByte() / 255
                vertex.weight[0][1] = br.readUByte() / 255
                vertex.weight[0][2] = br.readUByte() / 255
                vertex.weight[0][3] = br.readUByte() / 255

                vertex.joint[0][0] = br.readUByte()
                vertex.joint[0][1] = br.readUByte()
                vertex.joint[0][2] = br.readUByte()
                vertex.joint[0][3] = br.readUByte()

                vertex.weight[1][0] = br.readUByte() / 255
                vertex.weight[1][1] = br.readUByte() / 255
                vertex.weight[1][2] = 0
                vertex.weight[1][3] = 0

                vertex.joint[1][0] = br.readUByte()
                vertex.joint[1][1] = br.readUByte()
                vertex.joint[1][2] = 0
                vertex.joint[1][3] = 0

        for vertex in self.vertices:
            vertex.normal = br.readUByteQuantizedVec(4)
            vertex.tangent = br.readUByteQuantizedVec(4)
            vertex.bitangent = br.readUByteQuantizedVec(4)
            vertex.uv = [0] * num_uvchannels
            for uv in range(num_uvchannels):
                vertex.uv[uv] = br.readShortQuantizedVecScaledBiased(
                    2, mesh.tex_scale_bias[0:2], mesh.tex_scale_bias[2:4]
                )

        if not mesh.prim_object.properties.useColor1() or flags.isWeightedObject():
            if not sub_mesh_flags.useColor1():
                for vertex in self.vertices:
                    vertex.color = br.readUByteVec(4)
            else:
                for vertex in self.vertices:
                    vertex.color[0] = sub_mesh_color1[0]
                    vertex.color[1] = sub_mesh_color1[1]
                    vertex.color[2] = sub_mesh_color1[2]
                    vertex.color[3] = sub_mesh_color1[3]

    def write(
            self,
            br,
            mesh,
            sub_mesh_flags: PrimObjectPropertyFlags,
            flags: PrimObjectHeaderPropertyFlags,
    ):
        if len(self.vertices) > 0:
            num_uvchannels = len(self.vertices[0].uv)
        else:
            num_uvchannels = 0
        # positions
        for vertex in self.vertices:
            if mesh.prim_object.properties.isHighResolution():
                br.writeFloat(
                    (vertex.position[0] - mesh.pos_bias[0]) / mesh.pos_scale[0]
                )
                br.writeFloat(
                    (vertex.position[1] - mesh.pos_bias[1]) / mesh.pos_scale[1]
                )
                br.writeFloat(
                    (vertex.position[2] - mesh.pos_bias[2]) / mesh.pos_scale[2]
                )
            else:
                br.writeShortQuantizedVecScaledBiased(
                    vertex.position, mesh.pos_scale, mesh.pos_bias
                )

        # joints and weights
        if flags.isWeightedObject():
            for vertex in self.vertices:
                br.writeUByte(br.IOI_round(vertex.weight[0][0]))
                br.writeUByte(br.IOI_round(vertex.weight[0][1]))
                br.writeUByte(br.IOI_round(vertex.weight[0][2]))
                br.writeUByte(br.IOI_round(vertex.weight[0][3]))

                br.writeUByte(vertex.joint[0][0])
                br.writeUByte(vertex.joint[0][1])
                br.writeUByte(vertex.joint[0][2])
                br.writeUByte(vertex.joint[0][3])

                br.writeUByte(br.IOI_round(vertex.weight[1][0]))
                br.writeUByte(br.IOI_round(vertex.weight[1][1]))
                br.writeUByte(vertex.joint[1][0])
                br.writeUByte(vertex.joint[1][1])

        # ntb + uv
        for vertex in self.vertices:
            br.writeUByteQuantizedVec(vertex.normal)
            br.writeUByteQuantizedVec(vertex.tangent)
            br.writeUByteQuantizedVec(vertex.bitangent)
            for uv in range(num_uvchannels):
                br.writeShortQuantizedVecScaledBiased(
                    vertex.uv[uv], mesh.tex_scale_bias[0:2], mesh.tex_scale_bias[2:4]
                )

        # color
        if not mesh.prim_object.properties.useColor1() or flags.isWeightedObject():
            if not sub_mesh_flags.useColor1() or flags.isWeightedObject():
                for vertex in self.vertices:
                    br.writeUByteVec(vertex.color)


class PrimSubMesh:
    """Stores the mesh data. as well as the BoxColi and ClothData"""

    def __init__(self):
        self.prim_object = PrimObject(0)
        self.num_vertices = 0
        self.num_indices = 0
        self.num_additional_indices = 0
        self.num_uvchannels = 1
        self.dummy1 = bytes([0, 0, 0])
        self.vertexBuffer = VertexBuffer()
        self.indices = [0] * 0
        self.collision = BoxColi()
        self.cloth = -1

    def read(self, br, mesh: PrimMesh, flags: PrimObjectHeaderPropertyFlags):
        self.prim_object.read(br)

        num_vertices = br.readUInt()
        vertices_offset = br.readUInt()
        num_indices = br.readUInt()
        num_additional_indices = br.readUInt()
        indices_offset = br.readUInt()
        collision_offset = br.readUInt()
        cloth_offset = br.readUInt()
        num_uvchannels = br.readUInt()

        # detour for vertices
        br.seek(vertices_offset)
        self.vertexBuffer.read(
            br,
            num_vertices,
            num_uvchannels,
            mesh,
            self.prim_object.color1,
            self.prim_object.properties,
            flags,
        )

        # detour for indices
        br.seek(indices_offset)
        self.indices = [-1] * (num_indices + num_additional_indices)
        for index in range(num_indices + num_additional_indices):
            self.indices[index] = br.readUShort()

        # detour for collision info
        br.seek(collision_offset)
        self.collision.read(br)

        # optional detour for cloth data,
        # !locked because the format is not known enough!
        if cloth_offset != 0 and self.cloth != -1:
            br.seek(cloth_offset)
            self.cloth.read(br, mesh, self)
        else:
            self.cloth = -1

    def write(self, br, mesh, flags: PrimObjectHeaderPropertyFlags):
        index_offset = br.tell()
        for index in self.indices:
            br.writeUShort(index)

        br.align(16)
        vert_offset = br.tell()
        self.vertexBuffer.write(br, mesh, self.prim_object.properties, flags)

        br.align(16)
        coll_offset = br.tell()
        self.collision.write(br)

        br.align(16)

        if self.cloth != -1:
            cloth_offset = br.tell()
            self.cloth.write(br, mesh)
            br.align(16)
        else:
            cloth_offset = 0

        header_offset = br.tell()
        # IOI uses a cleared primMesh object. so let's clear it here as well
        self.prim_object.lodmask = 0x0
        self.prim_object.wire_color = 0x0

        # TODO: optimze this away
        bb = self.calc_bb()
        self.prim_object.min = bb[0]
        self.prim_object.max = bb[1]
        self.prim_object.write(br)

        num_vertices = len(self.vertexBuffer.vertices)
        br.writeUInt(num_vertices)
        br.writeUInt(vert_offset)
        br.writeUInt(len(self.indices))
        br.writeUInt(
            0
        )  # additional indices kept at 0, because we do not know their purpose
        br.writeUInt(index_offset)
        br.writeUInt(coll_offset)
        br.writeUInt(cloth_offset)

        if num_vertices > 0:
            num_uvchannels = len(self.vertexBuffer.vertices[0].uv)
        else:
            num_uvchannels = 0

        br.writeUInt(num_uvchannels)

        br.align(16)

        obj_table_offset = br.tell()
        br.writeUInt(header_offset)
        br.writeUInt(0)  # padding
        br.writeUInt64(0)  # padding
        return obj_table_offset

    def calc_bb(self):
        bb_max = [-sys.float_info.max] * 3
        bb_min = [sys.float_info.max] * 3
        for vert in self.vertexBuffer.vertices:
            for axis in range(3):
                if bb_max[axis] < vert.position[axis]:
                    bb_max[axis] = vert.position[axis]

                if bb_min[axis] > vert.position[axis]:
                    bb_min[axis] = vert.position[axis]
        return [bb_min, bb_max]

    def calc_UVbb(self):
        bb_max = [-sys.float_info.max] * 3
        bb_min = [sys.float_info.max] * 3
        layer = 0
        for vert in self.vertexBuffer.vertices:
            for axis in range(2):
                if bb_max[axis] < vert.uv[layer][axis]:
                    bb_max[axis] = vert.uv[layer][axis]

                if bb_min[axis] > vert.uv[layer][axis]:
                    bb_min[axis] = vert.uv[layer][axis]
        return [bb_min, bb_max]


class PrimObject:
    """A header class used to store information about PrimMesh and PrimSubMesh"""

    def __init__(self, type_preset: int):
        self.prims = Prims(type_preset)
        self.sub_type = PrimObjectSubtype(0)
        self.properties = PrimObjectPropertyFlags(0)
        self.lodmask = 0xFF
        self.variant_id = 0
        self.zbias = 0
        self.zoffset = 0
        self.material_id = 0
        self.wire_color = 0xFFFFFFFF
        self.color1 = [
                          0xFF
                      ] * 4  # global color used when useColor1 is set. only works inside PrimSubMesh
        self.min = [0] * 3
        self.max = [0] * 3

    def read(self, br):
        self.prims.read(br)
        self.sub_type = PrimObjectSubtype(br.readUByte())
        self.properties = PrimObjectPropertyFlags(br.readUByte())
        self.lodmask = br.readUByte()
        self.variant_id = br.readUByte()
        self.zbias = br.readUByte()  # draws mesh in front of others
        self.zoffset = (
            br.readUByte()
        )  # will move the mesh towards the camera depending on the distance to it
        self.material_id = br.readUShort()
        self.wire_color = br.readUInt()

        # global color used when useColor1 is set. will only work when defined inside PrimSubMesh
        self.color1 = br.readUByteVec(4)

        self.min = br.readFloatVec(3)
        self.max = br.readFloatVec(3)

    def write(self, br):
        self.prims.write(br)
        br.writeUByte(self.sub_type)
        self.properties.write(br)
        br.writeUByte(self.lodmask)
        br.writeUByte(self.variant_id)
        br.writeUByte(self.zbias)
        br.writeUByte(self.zoffset)
        br.writeUShort(self.material_id)
        br.writeUInt(self.wire_color)
        br.writeUByteVec(self.color1)

        br.writeFloatVec(self.min)
        br.writeFloatVec(self.max)


class BoneAccel:
    def __init__(self):
        self.offset = 0
        self.num_indices = 0

    def read(self, br):
        self.offset = br.readUInt()
        self.num_indices = br.readUInt()

    def write(self, br):
        br.writeUInt(self.offset)
        br.writeUInt(self.num_indices)


class BoneInfo:
    def __init__(self):
        self.total_size = 0
        self.bone_remap = [0xFF] * 255
        self.pad = 0
        self.accel_entries = []

    def read(self, br):
        self.total_size = br.readUShort()
        num_accel_entries = br.readUShort()
        self.bone_remap = [0] * 255
        for i in range(255):
            self.bone_remap[i] = br.readUByte()
        self.pad = br.readUByte()

        self.accel_entries = [BoneAccel() for _ in range(num_accel_entries)]
        for accel_entry in self.accel_entries:
            accel_entry.read(br)

    def write(self, br):
        br.writeUShort(self.total_size)
        br.writeUShort(len(self.accel_entries))
        for i in range(255):
            br.writeUByte(self.bone_remap[i])
        br.writeUByte(self.pad)

        for entry in self.accel_entries:
            entry.write(br)

        br.align(16)


class BoneIndices:
    def __init__(self):
        self.bone_indices = []

    def read(self, br):
        num_indices = br.readUInt()
        self.bone_indices = [0] * num_indices
        br.seek(
            br.tell() - 4
        )  # aligns the data to match the offset defined in the BonAccel entries
        for i in range(num_indices):
            self.bone_indices[i] = br.readUShort()

    def write(self, br):
        for index in self.bone_indices:
            br.writeUShort(index)

        br.align(16)


# needs additional research
class ClothData:
    """Class to store data about cloth"""

    def __init__(self):
        self.size = 0
        self.cloth_data = [0] * self.size

    def read(self, br, mesh: PrimMesh, sub_mesh: PrimSubMesh):
        if mesh.cloth_id.isSmoll():
            self.size = br.readUInt()
        else:
            self.size = 0x14 * sub_mesh.num_vertices
        self.cloth_data = [0] * self.size
        for i in range(self.size):
            self.cloth_data[i] = br.readUByte()

    def write(self, br, mesh):
        if mesh.cloth_id.isSmoll():
            br.writeUInt(len(self.cloth_data))
        for b in self.cloth_data:
            br.writeUByte(b)


class PrimHeader:
    """Small header class used by other header classes"""

    def __init__(self, type_preset):
        self.draw_destination = 0
        self.pack_type = 0
        self.type = PrimType(type_preset)

    def read(self, br):
        self.draw_destination = br.readUByte()
        self.pack_type = br.readUByte()
        self.type = PrimType(br.readUShort())

    def write(self, br):
        br.writeUByte(self.draw_destination)
        br.writeUByte(self.pack_type)
        br.writeUShort(self.type)


class Prims:
    """
    Wrapper class for PrimHeader.
    I'm not quite sure why it exists, but here it is :)
    """

    def __init__(self, type_preset: int):
        self.prim_header = PrimHeader(type_preset)

    def read(self, br):
        self.prim_header.read(br)

    def write(self, br):
        self.prim_header.write(br)


class PrimObjectHeader:
    """Global RenderPrimitive header. used by all objects defined"""

    def __init__(self):
        self.prims = Prims(1)
        self.property_flags = PrimObjectHeaderPropertyFlags(0)
        self.bone_rig_resource_index = 0xFFFFFFFF
        self.min = [sys.float_info.max] * 3
        self.max = [-sys.float_info.max] * 3
        self.object_table = []

    def read(self, br):
        self.prims.read(br)
        self.property_flags = PrimObjectHeaderPropertyFlags(br.readUInt())
        self.bone_rig_resource_index = br.readUInt()
        num_objects = br.readUInt()
        object_table_offset = br.readUInt()

        self.min = br.readFloatVec(3)
        self.max = br.readFloatVec(3)

        br.seek(object_table_offset)
        object_table_offsets = [-1] * num_objects
        for obj in range(num_objects):
            object_table_offsets[obj] = br.readInt()

        self.object_table = [-1] * num_objects
        for obj in range(num_objects):
            br.seek(object_table_offsets[obj])
            if self.property_flags.isWeightedObject():
                self.object_table[obj] = PrimMeshWeighted()
                self.object_table[obj].read(br, self.property_flags)
            else:
                self.object_table[obj] = PrimMesh()
                self.object_table[obj].read(br, self.property_flags)

    def write(self, br):
        obj_offsets = []
        for obj in self.object_table:
            if obj is not None:
                obj_offsets.append(obj.write(br, self.property_flags))

                if self.property_flags.isWeightedObject():
                    self.append_bb(
                        obj.prim_mesh.prim_object.min, obj.prim_mesh.prim_object.max
                    )
                else:
                    self.append_bb(obj.prim_object.min, obj.prim_object.max)

        obj_table_offset = br.tell()
        for offset in obj_offsets:
            br.writeUInt(offset)

        br.align(16)

        header_offset = br.tell()
        self.prims.write(br)
        self.property_flags.write(br)

        if self.bone_rig_resource_index < 0:
            br.writeUInt(0xFFFFFFFF)
        else:
            br.writeUInt(self.bone_rig_resource_index)

        br.writeUInt(len(obj_offsets))
        br.writeUInt(obj_table_offset)

        br.writeFloatVec(self.min)
        br.writeFloatVec(self.max)

        if br.tell() % 8 != 0:
            br.writeUInt(0)
        return header_offset

    def append_bb(self, bb_min: [], bb_max: []):
        for axis in range(3):
            if bb_min[axis] < self.min[axis]:
                self.min[axis] = bb_min[axis]
            if bb_max[axis] > self.max[axis]:
                self.max[axis] = bb_max[axis]


def readHeader(br):
    """ "Global function to read only the header of a RenderPrimitive, used to fast file identification"""
    offset = br.readUInt()
    br.seek(offset)
    header_values = PrimObjectHeader()
    header_values.prims.read(br)
    header_values.property_flags = PrimObjectHeaderPropertyFlags(br.readUInt())
    header_values.bone_rig_resource_index = br.readUInt()
    br.readUInt()
    br.readUInt()
    header_values.min = br.readFloatVec(3)
    header_values.max = br.readFloatVec(3)
    return header_values


class RenderPrimitive:
    """
    RenderPrimitive class, represents the .prim file format.
    It contains a multitude of meshes and properties.
    The RenderPrimitive format has built-in support for: armatures, bounding boxes, collision and cloth physics.
    """

    def __init__(self):
        self.header = PrimObjectHeader()

    def read(self, br):
        offset = br.readUInt()
        br.seek(offset)
        self.header.read(br)

    def write(self, br):
        br.writeUInt64(420)  # PLACEHOLDER
        br.writeUInt64(0)  # padding
        header_offset = self.header.write(br)
        br.seek(0)
        br.writeUInt64(header_offset)

    def num_objects(self):
        num = 0
        for obj in self.header.object_table:
            if obj is not None:
                num = num + 1

        return num


def load_prim_mesh(prim, borg, prim_name: str, mesh_index: int, lod_mask):
    """
    Turn the prim data structure into a Blender mesh.
    Returns the generated Mesh
    """
    mesh = bpy.data.meshes.new(name=(str(prim_name) + "_" + str(mesh_index)))

    use_rig = False
    if borg is not None:
        use_rig = True

    vert_locs = []
    loop_vidxs = []
    # loop_uvs = [[]]
    loop_cols = []

    # num_joint_sets = 0

    # if prim.header.property_flags.isWeightedObject() and use_rig:
    #     num_joint_sets = 2

    sub_mesh = prim.header.object_table[mesh_index].sub_mesh
    sub_mesh.prim_object.lodmask
    # vert_joints = [
    #     [[0] * 4 for _ in range(len(sub_mesh.vertexBuffer.vertices))]
    #     for _ in range(num_joint_sets)
    # ]
    # vert_weights = [
    #     [[0] * 4 for _ in range(len(sub_mesh.vertexBuffer.vertices))]
    #     for _ in range(num_joint_sets)
    # ]

    loop_vidxs.extend(sub_mesh.indices)

    for i, vert in enumerate(sub_mesh.vertexBuffer.vertices):
        vert_locs.extend([vert.position[0], vert.position[1], vert.position[2]])

        # for j in range(num_joint_sets):
        #     vert_joints[j][i] = vert.joint[j]
        #     vert_weights[j][i] = vert.weight[j]

    # for index in sub_mesh.indices:
    #     vert = sub_mesh.vertexBuffer.vertices[index]
    #     loop_cols.extend(
    #         [
    #             vert.color[0] / 255,
    #             vert.color[1] / 255,
    #             vert.color[2] / 255,
    #             vert.color[3] / 255,
    #             ]
    #     )
    #     for uv_i in range(sub_mesh.num_uvchannels):
    #         loop_uvs[uv_i].extend([vert.uv[uv_i][0], 1 - vert.uv[uv_i][1]])

    mesh.vertices.add(len(vert_locs) // 3)
    mesh.vertices.foreach_set("co", vert_locs)

    mesh.loops.add(len(loop_vidxs))
    mesh.loops.foreach_set("vertex_index", loop_vidxs)

    num_faces = len(sub_mesh.indices) // 3
    mesh.polygons.add(num_faces)

    loop_starts = np.arange(0, 3 * num_faces, step=3)
    loop_totals = np.full(num_faces, 3)
    mesh.polygons.foreach_set("loop_start", loop_starts)
    mesh.polygons.foreach_set("loop_total", loop_totals)

    # for uv_i in range(sub_mesh.num_uvchannels):
    #     name = "UVMap" if uv_i == 0 else "UVMap.%03d" % uv_i
    #     layer = mesh.uv_layers.new(name=name)
    #     layer.data.foreach_set("uv", loop_uvs[uv_i])

    # # Skinning
    # ob = bpy.data.objects.new("temp_obj", mesh)
    # if num_joint_sets and use_rig:
    #     for bone in borg.bone_definitions:
    #         ob.vertex_groups.new(name=bone.name.decode("utf-8"))
    #
    #     vgs = list(ob.vertex_groups)
    #
    #     for i in range(num_joint_sets):
    #         js = vert_joints[i]
    #         ws = vert_weights[i]
    #         for vi in range(len(vert_locs) // 3):
    #             w0, w1, w2, w3 = ws[vi]
    #             j0, j1, j2, j3 = js[vi]
    #             if w0 != 0:
    #                 vgs[j0].add((vi,), w0, "REPLACE")
    #             if w1 != 0:
    #                 vgs[j1].add((vi,), w1, "REPLACE")
    #             if w2 != 0:
    #                 vgs[j2].add((vi,), w2, "REPLACE")
    #             if w3 != 0:
    #                 vgs[j3].add((vi,), w3, "REPLACE")
    # bpy.data.objects.remove(ob)

    # layer = mesh.vertex_colors.new(name="Col")
    # mesh.color_attributes[layer.name].data.foreach_set("color", loop_cols)

    mesh.validate()
    mesh.update()

    # write the additional properties to the blender structure
    prim_mesh_obj = prim.header.object_table[mesh_index].prim_object
    prim_sub_mesh_obj = prim.header.object_table[mesh_index].sub_mesh.prim_object

    lod = prim_mesh_obj.lodmask
    mask = []
    for bit in range(8):
        mask.append(0 != (lod & (1 << bit)))

    return mesh


def load_prim(operator, context, filepath, lod_mask):
    """Imports a mesh from the given path"""

    prim_name = bpy.path.display_name_from_filepath(filepath)
    log("INFO", "Started reading PRIM: " + str(prim_name), "load_prim")

    fp = os.fsencode(filepath)
    file = open(fp, "rb")
    br = BinaryReader(file)
    prim = RenderPrimitive()
    prim.read(br)
    br.close()

    borg = None

    objects = []
    for mesh_index in range(prim.num_objects()):
        prim_mesh_obj = prim.header.object_table[mesh_index].prim_object
        lod = prim_mesh_obj.lodmask

        include_mesh = False
        for bit in range(8):
            bit_value = 0 != (lod & (1 << bit))
            if lod_mask[bit] == '1' and bit_value == 1:
                include_mesh = True
                break
        if not include_mesh:
            continue
        mesh = load_prim_mesh(prim, borg, prim_name, mesh_index, lod_mask)
        obj = bpy.data.objects.new(mesh.name, mesh)
        objects.append(obj)

    return objects

# ALOC
class AlocProperties(PropertyGroup):
    """ "Stored exposed variables relevant to the Physics files"""

    collision_type: BoolVectorProperty(
        name="collision_type_mask",
        description="Set which Collision Types should be shown",
        default=(True, True, True, True, True, True),
        size=6,
        subtype="LAYER",
    )

    aloc_type: EnumProperty(
        name="Type",
        description="The type of the aloc",
        items=[
            ("PhysicsDataType.NONE", "None", ""),
            ("PhysicsDataType.CONVEX_MESH", "Convex Mesh", "The header of an Object"),
            ("PhysicsDataType.TRIANGLE_MESH", "Triangle Mesh", ""),
            ("PhysicsDataType.CONVEX_MESH_AND_TRIANGLE_MESH", "Convex Mesh and Triangle Mesh", ""),
            ("PhysicsDataType.PRIMITIVE", "Primitive", ""),
            ("PhysicsDataType.CONVEX_MESH_AND_PRIMITIVE", "Convex Mesh and Primitive", ""),
            ("PhysicsDataType.TRIANGLE_MESH_AND_PRIMITIVE", "Triangle Mesh and Primitive", ""),
            ("PhysicsDataType.KINEMATIC_LINKED", "Kinematic Linked", ""),
            ("PhysicsDataType.SHATTER_LINKED", "Shatter Linked", ""),
            ("PhysicsDataType.KINEMATIC_LINKED_2", "Kinematic Linked 2", ""),
        ],
        default="PhysicsDataType.NONE",
    )

    aloc_subtype: EnumProperty(
        name="Sub-Type",
        description="The subtype of the aloc",
        items=[
            ("PhysicsCollisionPrimitiveType.BOX", "Box", ""),
            ("PhysicsCollisionPrimitiveType.CAPSULE", "Capsule", ""),
            ("PhysicsCollisionPrimitiveType.SPHERE", "Sphere", ""),
            ("PhysicsCollisionPrimitiveType.NONE", "None", ""),
        ],
        default="PhysicsCollisionPrimitiveType.NONE",
    )


class PhysicsDataType(enum.IntEnum):
    NONE = 0
    CONVEX_MESH = 1
    TRIANGLE_MESH = 2
    CONVEX_MESH_AND_TRIANGLE_MESH = 3
    PRIMITIVE = 4
    CONVEX_MESH_AND_PRIMITIVE = 5
    TRIANGLE_MESH_AND_PRIMITIVE = 6
    KINEMATIC_LINKED = 132
    SHATTER_LINKED = 144
    KINEMATIC_LINKED_2 = 192


class PhysicsCollisionType(enum.IntEnum):
    NONE = 0
    STATIC = 1
    RIGIDBODY = 2
    SHATTER_LINKED = 16
    KINEMATIC_LINKED = 32
    BACKWARD_COMPATIBLE = 2147483647


class PhysicsCollisionLayerType(enum.IntEnum):
    COLLIDE_WITH_ALL = 0
    STATIC_COLLIDABLES_ONLY = 1
    DYNAMIC_COLLIDABLES_ONLY = 2
    STAIRS = 3
    SHOT_ONLY_COLLISION = 4
    DYNAMIC_TRASH_COLLIDABLES = 5
    KINEMATIC_COLLIDABLES_ONLY = 6
    STATIC_COLLIDABLES_ONLY_TRANSPARENT = 7
    DYNAMIC_COLLIDABLES_ONLY_TRANSPARENT = 8
    KINEMATIC_COLLIDABLES_ONLY_TRANSPARENT = 9
    STAIRS_STEPS = 10
    STAIRS_SLOPE = 11
    HERO_PROXY = 12
    ACTOR_PROXY = 13
    HERO_VR = 14
    CLIP = 15
    ACTOR_RAGDOLL = 16
    CROWD_RAGDOLL = 17
    LEDGE_ANCHOR = 18
    ACTOR_DYN_BODY = 19
    HERO_DYN_BODY = 20
    ITEMS = 21
    WEAPONS = 22
    COLLISION_VOLUME_HITMAN_ON = 23
    COLLISION_VOLUME_HITMAN_OFF = 24
    DYNAMIC_COLLIDABLES_ONLY_NO_CHARACTER = 25
    DYNAMIC_COLLIDABLES_ONLY_NO_CHARACTER_TRANSPARENT = 26
    COLLIDE_WITH_STATIC_ONLY = 27
    AI_VISION_BLOCKER = 28
    AI_VISION_BLOCKER_AMBIENT_ONLY = 29
    UNUSED_LAST = 30


class PhysicsCollisionPrimitiveType(enum.IntEnum):
    BOX = 0
    CAPSULE = 1
    SPHERE = 2
    NONE = 3


class PhysicsCollisionSettings(ctypes.Structure):
    _fields_ = [
        ("data_type", ctypes.c_uint32),
        ("collider_type", ctypes.c_uint32),
    ]


class ConvexMesh:
    def __init__(self):
        self.collision_layer = 0
        self.position = [0.0, 0.0, 0.0]
        self.rotation = [0.0, 0.0, 0.0, 0.0]
        self.vertex_count = 0
        self.vertices = []
        self.has_grb_data = 0
        self.edge_count = 0
        self.polygon_count = 0
        self.polygons_vertex_count = 0


class TriangleMesh:
    def __init__(self):
        self.collision_layer = 0
        self.serial_flags = 0
        self.vertex_count = 0
        self.triangle_count = 0
        self.vertices = []
        self.triangle_data = []


class Shatter:
    def __init__(self):
        self.collision_layer = 0
        self.vertex_count = 0
        self.triangle_count = 0


class PrimitiveBox:
    def __init__(self):
        self.half_extents = [0.0, 0.0, 0.0]
        self.collision_layer = 0
        self.position = [0.0, 0.0, 0.0]
        self.rotation = [0.0, 0.0, 0.0, 0.0]


class PrimitiveCapsule:
    def __init__(self):
        self.radius = 0.0
        self.length = 0.0
        self.collision_layer = 0
        self.position = [0.0, 0.0, 0.0]
        self.rotation = [0.0, 0.0, 0.0, 0.0]


class PrimitiveSphere:
    def __init__(self):
        self.radius = 0.0
        self.collision_layer = 0
        self.position = [0.0, 0.0, 0.0]
        self.rotation = [0.0, 0.0, 0.0, 0.0]

        
def read_convex_mesh(br, aloc_name):
    # Start of first convex mesh, offset = 27
    # ---- Fixed header for each Convex Mesh sizeof = 80
    # CollisionLayer, position, rotation: sizeof = 36
    convex_mesh = ConvexMesh()
    convex_mesh.collision_layer = br.readUInt()
    convex_mesh.position = br.readFloatVec(3)
    convex_mesh.rotation = br.readFloatVec(4)
    # "\0\0.?NXS.VCXM\u{13}\0\0\0\0\0\0\0ICE.CLCL\u{8}\0\0\0ICE.CVHL\u{8}\0\0\0" sizeof = 44
    br.readUByteVec(44)
    # For first mesh, current position = 103 = 0x67
    # ---- Variable data for each Convex Mesh Hull
    # sizeof = 16 + 3*vertex_count + 20*polygon_count + polygons_vertex_count + 2*edge_count + 3*vertex_count + (if (has_grb_data) then 8*edge_count else 0)
    # Example: 00C87539307C6091:
    #  vertex_count: 8
    #  has_grb_data: false
    #  edge_count: 12
    #  polygon_count: 6
    #  polygons_vertex_count: 24
    #  sizeof variable data (no grb) = 16 + 24 + 120 + 24 + 24 + 24 + 0 = 232 = 0xE8
    #  sizeof variable data (with grb) = 16 + 24 + 120 + 24 + 24 + 24 + 96 = 328 = 0x148
    #  Total size (no grb) = 103 + 232 = 0x67 + 0xE8 = 335 = 0x14F
    #  Total size (with grb) = 103 + 328 = 0x67 + 0x148 = 431 = 0x1AF
    log("DEBUG", "Starting to read variable convex mesh hull data. Current offset: " + str(br.tell()), aloc_name)
    convex_mesh.vertex_count = br.readUInt()
    log("DEBUG", "Num vertices " + str(convex_mesh.vertex_count), aloc_name)

    grb_flag_and_edge_count = br.readUInt()
    convex_mesh.has_grb_data = 0x8000 & grb_flag_and_edge_count
    log("DEBUG", "Has_grb_data " + str(convex_mesh.has_grb_data), aloc_name)

    convex_mesh.edge_count = 0x7FFF & grb_flag_and_edge_count
    log("DEBUG", "edge_count " + str(convex_mesh.edge_count), aloc_name)
    convex_mesh.polygon_count = br.readUInt()
    log("DEBUG", "polygon_count " + str(convex_mesh.polygon_count), aloc_name)
    convex_mesh.polygons_vertex_count = br.readUInt()
    log("DEBUG", "polygons_vertex_count " + str(convex_mesh.polygons_vertex_count), aloc_name)
    vertices = []
    for _vertex_index in range(convex_mesh.vertex_count):
        vertices.append(br.readFloatVec(3))
    convex_mesh.vertices = vertices
    log("DEBUG", "Finished reading vertices and metadata. Reading convex main hull data", aloc_name)

    # Unused because Blender can build the convex hull
    hull_polygon_data = 0
    for _polygon_index in range(convex_mesh.polygon_count):
        log("DEBUG", "Reading 20 characters of HullPolygonData. Current offset: " + str(br.tell()), aloc_name)
        hull_polygon_data = br.readUByteVec(20)  # HullPolygonData
        log("DEBUG", len(hull_polygon_data), aloc_name)
        # TODO: <------------------ Read past EOF Here on second convex mesh!
        if len(hull_polygon_data) < 20:
            break
    if len(hull_polygon_data) < 20:
        return
    _mHullDataVertexData8 = br.readUByteVec(
        convex_mesh.polygons_vertex_count)  # mHullDataVertexData8 for each polygon's vertices
    log("DEBUG", "mHullDataVertexData8 " + str(_mHullDataVertexData8), aloc_name)
    _mHullDataFacesByEdges8 = br.readUByteVec(convex_mesh.edge_count * 2)  # mHullDataFacesByEdges8
    log("DEBUG", "mHullDataFacesByEdges8 " + str(_mHullDataVertexData8), aloc_name)
    _mHullDataFacesByVertices8 = br.readUByteVec(convex_mesh.vertex_count * 3)  # mHullDataFacesByVertices8
    log("DEBUG", "mHullDataFacesByVertices8 " + str(_mHullDataVertexData8), aloc_name)
    if convex_mesh.has_grb_data == 1:
        log("DEBUG", "has_grb_data true. Reading edges. Current offset: " + str(br.tell()), aloc_name)
        _mEdges = br.readUByteVec(4 * 2 * convex_mesh.edge_count)  # mEdges
    else:
        _ = -1
        log("DEBUG", "has_grb_data false. No edges to read. Current offset: " + str(br.tell()), aloc_name)
    log("DEBUG",  "Finished reading main convex hull data. Reading remaining convex hull data. Current offset: " + str(br.tell()), aloc_name)
    # ---- End of Variable data for each Convex Mesh Hull

    # Remaining convex hull data
    zero = br.readFloat()  # 0
    log("DEBUG", "This should be zero: " + str(zero) + " Current offset: " + str(br.tell()), aloc_name)
    # Local bounds
    bbox_min_x = br.readFloat()  # mHullData.mAABB.getMin(0)
    log("DEBUG", "Bbox min x: " + str(bbox_min_x) + " Current offset: " + str(br.tell()), aloc_name)
    _bbox_min_y = br.readFloat()  # mHullData.mAABB.getMin(1)
    _bbox_min_z = br.readFloat()  # mHullData.mAABB.getMin(2)
    _bbox_max_x = br.readFloat()  # mHullData.mAABB.getMax(0)
    _bbox_max_y = br.readFloat()  # mHullData.mAABB.getMax(1)
    _bbox_max_z = br.readFloat()  # mHullData.mAABB.getMax(2)
    # Mass Info
    mass = br.readFloat()  # mMass
    log("DEBUG", "Mass: " + str(mass) + " Current offset: " + str(br.tell()), aloc_name)
    br.readFloatVec(9)  # mInertia
    br.readFloatVec(3)  # mCenterOfMass.x
    gauss_map_flag = br.readFloat()
    log("DEBUG", "Gauss Flag: " + str(gauss_map_flag), aloc_name)

    if gauss_map_flag == 1.0:
        log("DEBUG", "Gauss Flag is 1.0, reading Gauss Data", aloc_name)
        br.readUByteVec(24)  # ICE.SUPM....ICE.GAUS....
        m_subdiv = br.readInt()  # mSVM->mData.m_subdiv
        log("DEBUG", "m_subdiv: " + str(m_subdiv), aloc_name)

        num_samples = br.readInt()  # mSVM->mData.mNbSamples
        log("DEBUG", "num_samples: " + str(num_samples), aloc_name)
        br.readUByteVec(num_samples * 2)
        br.readUByteVec(4)  # ICE.
        log("DEBUG", "Reading VALE: Current offset: " + str(br.tell()), aloc_name)
        vale = br.readString(4)  # VALE
        log("DEBUG", "Should say VALE: " + str(vale), aloc_name)
        br.readUByteVec(4)  # ....
        num_svm_verts = br.readInt()  # mSVM->mData.mNbVerts
        log("DEBUG", "num_svm_verts: " + str(num_svm_verts), aloc_name)
        num_svm_adj_verts = br.readInt()  # mSVM->mData.mNbAdjVerts
        log("DEBUG", "num_svm_adj_verts: " + str(num_svm_adj_verts), aloc_name)
        svm_max_index = br.readInt()  # maxIndex
        log("DEBUG", "svm_max_index: " + str(svm_max_index), aloc_name)
        if svm_max_index <= 0xff:
            log("DEBUG", "svm_max_index <= 0xff. File offset: " + str(br.tell()), aloc_name)
            br.readUByteVec(num_svm_verts)
        else:
            log("DEBUG", "svm_max_index > 0xff. File offset: " + str(br.tell()), aloc_name)
            br.readUByteVec(num_svm_verts * 2)
        br.readUByteVec(num_svm_adj_verts)
        log("DEBUG", "Finished Gauss Data. File offset: " + str(br.tell()), aloc_name)
    else:
        _ = -1
        log("DEBUG", "Gauss Flag is " + str(gauss_map_flag) + " No Gauss Data. File offset: " + str(br.tell()), aloc_name)
    _mRadius = br.readFloat()
    _mExtents_0 = br.readFloat()
    _mExtents_1 = br.readFloat()
    _mExtents_2 = br.readFloat()
    log("DEBUG", "Finished reading Convex mesh. File offset: " + str(br.tell()), aloc_name)
    return convex_mesh


def read_triangle_mesh(aloc_name, br):
    # Start of first triangle mesh, offset = 27
    triangle_mesh = TriangleMesh()
    triangle_mesh.collision_layer = br.readUInt()
    br.readUByteVec(16)  # \0\0\0\0NXS.MESH....\u{15}\0\0\0
    # Offset: 47
    br.readUByteVec(4)  # midPhaseId
    log("DEBUG", "Reading serial_flags. Current offset: " + str(br.tell()), aloc_name)

    triangle_mesh.serial_flags = br.readInt()
    # Example Serial Flag: 6 = 00000110: IMSF_FACE_REMAP | IMSF_8BIT_INDICES
    # IMSF_MATERIALS       =    (1 << 0), // ! < if set, the cooked mesh file contains per-triangle material indices
    # IMSF_FACE_REMAP      =    (1 << 1), // ! < if set, the cooked mesh file contains a remap table
    # IMSF_8BIT_INDICES    =    (1 << 2), // ! < if set, the cooked mesh file contains 8bit indices (topology)
    # IMSF_16BIT_INDICES   =    (1 << 3), // ! < if set, the cooked mesh file contains 16bit indices (topology)
    # IMSF_ADJACENCIES     =    (1 << 4), // ! < if set, the cooked mesh file contains adjacency structures
    # IMSF_GRB_DATA        =    (1 << 5)  // ! < if set, the cooked mesh file contains GRB data structures
    log("DEBUG", "Reading Vertex_count: Current offset: " + str(br.tell()), aloc_name)
    triangle_mesh.vertex_count = br.readUInt()
    log("DEBUG", "vertex_count: " + str(triangle_mesh.vertex_count), aloc_name)
    triangle_mesh.triangle_count = br.readUInt()
    log("DEBUG", "triangle_count: " + str(triangle_mesh.triangle_count), aloc_name)
    vertices = []
    for vertex_index in range(triangle_mesh.vertex_count):
        vertex = br.readFloatVec(3)
        log("DEBUG", "vertex " + str(vertex_index) + ": " + str(vertex), aloc_name)
        vertices.append(vertex)
    triangle_mesh.vertices = vertices
    # Check serial flag
    triangle_data = []
    log("DEBUG", "serial Flags: " + str(triangle_mesh.serial_flags), aloc_name)
    is_8bit = (triangle_mesh.serial_flags >> 2) & 1 == 1
    log("DEBUG", "is_8bit: " + str(is_8bit), aloc_name)
    is_16bit = (triangle_mesh.serial_flags >> 3) & 1 == 1
    log("DEBUG", "is_16bit: " + str(is_16bit), aloc_name)
    if is_8bit:
        log("DEBUG", "is_8bit. Reading triangle bytes", aloc_name)
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            triangle_byte = br.readUByte()
            log("DEBUG", "Triangle_byte: " + str(triangle_byte), aloc_name)
            triangle_data.append(triangle_byte)
    elif is_16bit:
        log("DEBUG", "is_16bit. Reading triangle shorts", aloc_name)
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            triangle_short = br.readUShort()
            log("DEBUG", "Triangle_short: " + str(triangle_short), aloc_name)
            triangle_data.append(triangle_short)
    else:
        log("DEBUG", "Not 8 or 16 bit. Reading triangle ints", aloc_name)
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            triangle_int = br.readInt()
            log("DEBUG", "Triangle_Int: " + str(triangle_int), aloc_name)
            triangle_data.append(triangle_int)
    triangle_mesh.triangle_data = triangle_data
    material_indices = (triangle_mesh.serial_flags >> 0) & 1 == 1
    log("DEBUG", "material_indices: " + str(material_indices), aloc_name)

    if material_indices:
        br.readUByteVec(2 * triangle_mesh.triangle_count)  # material_indices
    face_remap = (triangle_mesh.serial_flags >> 1) & 1 == 1
    log("DEBUG", "face_remap: " + str(face_remap), aloc_name)

    if face_remap:
        max_id = br.readInt()
        log("DEBUG", "max_id: " + str(max_id), aloc_name)
        if max_id <= 0xff:
            face_remap_val = br.readUByteVec(triangle_mesh.triangle_count)
            log("DEBUG", "face_remap_val 8bit: " + str(face_remap_val), aloc_name)

        elif max_id <= 0xffff:
            face_remap_val = br.readUByteVec(triangle_mesh.triangle_count * 2)
            log("DEBUG", "face_remap_val 16bit: " + str(face_remap_val), aloc_name)
        else:
            for triangle_index in range(triangle_mesh.triangle_count):
                face_remap_val = br.readInt()
                log("DEBUG", "face_remap_val int: " + str(face_remap_val), aloc_name)
    adjacencies = (triangle_mesh.serial_flags >> 4) & 1 == 1
    log("DEBUG", "adjacencies: " + str(adjacencies), aloc_name)
    if adjacencies:
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            br.readInt()
    # Write midPhaseStructure. Is it BV4? -> BV4TriangleMeshBuilder::saveMidPhaseStructure
    log("DEBUG", "Reading BV4: Current offset: " + str(br.tell()), aloc_name)
    bv4 = br.readString(3)  # "BV4."
    br.readUByte()
    log("DEBUG", "Should say BV4: " + str(bv4), aloc_name)
    bv4_version = br.readIntBigEndian()  # Bv4 Structure Version. Is always 1, so the midPhaseStructure will be bigEndian
    log("DEBUG", "BV4 version. Should be 1: " + str(bv4_version), aloc_name)
    if bv4_version != 1:
        log("ERROR", "[ERROR] Error reading triangle mesh: Unexpected BV4 version.", aloc_name)
        # raise ValueError("[ERROR] Error reading triangle mesh " + aloc_name + ": Unexpected BV4 version. File offset: " + str(br.tell()))
        return -1
    br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.x
    br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.y
    br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.z
    br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.mExtentsMagnitude
    br.readUByteVec(4)  # mData.mBV4Tree.mInitData
    # #ifdef GU_BV4_QUANTIZED_TREE
    br.readFloat()  # mData.mBV4Tree.mCenterOrMinCoeff.x
    br.readFloat()  # mData.mBV4Tree.mCenterOrMinCoeff.y
    br.readFloat()  # mData.mBV4Tree.mCenterOrMinCoeff.z
    br.readFloat()  # mData.mBV4Tree.mExtentsOrMaxCoeff.x
    br.readFloat()  # mData.mBV4Tree.mExtentsOrMaxCoeff.y
    br.readFloat()  # mData.mBV4Tree.mExtentsOrMaxCoeff.z
    # endif
    log("DEBUG", "Reading mNbNodes: Current offset: " + str(br.tell()), aloc_name)
    m_nb_nodes = br.readIntBigEndian()  # mData.mBV4Tree.mNbNodes
    log("DEBUG", "mNbNodes: " + str(m_nb_nodes), aloc_name)

    for _mNbNodesIndex in range(m_nb_nodes):
        # #ifdef GU_BV4_QUANTIZED_TREE
        br.readUByteVec(12)  # node.mAABB.mData[0].mExtents
        # else
        # br.readFloatVec(6)  # node.mAABB.mCenter.x
        # endif
        br.readUByteVec(4)  # node.mData
    # End midPhaseStructure

    br.readFloat()  # mMeshData.mGeomEpsilon
    bbox_min_x = br.readFloat()  # mMeshData.mAABB.minimum.x
    log("DEBUG", "mMeshData.mAABB.minimum.x: " + str(bbox_min_x), aloc_name)
    br.readFloat()  # mMeshData.mAABB.minimum.y
    br.readFloat()  # mMeshData.mAABB.minimum.z
    br.readFloat()  # mMeshData.mAABB.maximum.x
    br.readFloat()  # mMeshData.mAABB.maximum.y
    bbox_min_z = br.readFloat()  # mMeshData.mAABB.maximum.z
    log("DEBUG", "mMeshData.mAABB.maximum.z: " + str(bbox_min_z), aloc_name)

    # if(mMeshData.mExtraTrigData)
    m_nbv_triangles = br.readInt()  # mMeshData.mNbTriangles
    log("DEBUG", "m_nbv_triangles: " + str(m_nbv_triangles), aloc_name)

    br.readUByteVec(m_nbv_triangles)
    # else
    # br.readUByteVec(4)  # 0
    # endif

    # GRB Write
    has_grb = (triangle_mesh.serial_flags >> 5) & 1 == 1
    log("DEBUG", "has_grb: " + str(has_grb), aloc_name)

    if has_grb:
        for _triangle_index in range(triangle_mesh.triangle_count * 3):
            if is_8bit:
                br.readUByte()
            elif is_16bit:
                br.readUByteVec(2)
            else:
                br.readInt()
        br.readUIntVec(triangle_mesh.triangle_count * 4)  # mMeshData.mGRB_triAdjacencies
        br.readUIntVec(triangle_mesh.triangle_count)  # mMeshData.mGRB_faceRemap
        # Write midPhaseStructure BV3 -> BV32TriangleMeshBuilder::saveMidPhaseStructure
        bv32 = br.readString(4)  # "BV32"
        log("DEBUG", "Reading BV32: " + str(bv32) + " File Offset: " + str(br.tell()), aloc_name)
        br.readUByteVec(4)  # Bv32 Structure Version. If 1, the midPhaseStructure will be bigEndian
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.x
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.y
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.z
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.mExtentsMagnitude
        br.readUByteVec(4)  # mData.mBV4Tree.mInitData
        m_nb_packed_nodes = br.readInt()  # mData.mBV4Tree.m_nb_packed_nodes
        log("DEBUG", "m_nb_packed_nodes: " + str(m_nb_packed_nodes), aloc_name)
        m_nb_packed_nodes_be = br.readIntBigEndian()  # mData.mBV4Tree.m_nb_packed_nodes
        log("DEBUG", "m_nb_packed_nodes_be: " + str(m_nb_packed_nodes_be), aloc_name)
        for _mNbNodesIndex in range(m_nb_packed_nodes):
            m_nb_nodes = br.readInt(4)  # node.mNbNodes
            log("DEBUG", "mNbNodes: " + str(m_nb_nodes), aloc_name)
            m_nb_nodes_be = br.readIntBigEndian(4)  # node.mNbNodes
            log("DEBUG", "m_nb_nodes_be: " + str(m_nb_nodes_be), aloc_name)
            br.readUByteVec(4 * m_nb_nodes)  # node.mData
            br.readFloatVec(4 * m_nb_nodes)  # node.mCenter[0].x
            br.readFloatVec(4 * m_nb_nodes)  # node.mExtents[0].x
        # End midPhaseStructure
        # End GRB Write
    return triangle_mesh


class Physics:
    def __init__(self):
        self.data_type = PhysicsDataType.NONE
        self.collision_type = PhysicsCollisionType.NONE
        self.convex_meshes = []
        self.convex_mesh_count = 0
        self.triangle_meshes = []
        self.triangle_mesh_count = 0
        self.primitive_count = []
        self.primitive_boxes = []
        self.primitive_boxes_count = 0
        self.primitive_capsules = []
        self.primitive_capsules_count = 0
        self.primitive_spheres = []
        self.primitive_spheres_count = 0
        self.shatters = []
        self.shatter_count = 0
        if not os.environ["PATH"].startswith(
                os.path.abspath(os.path.dirname(__file__))
        ):
            os.environ["PATH"] = (
                    os.path.abspath(os.path.dirname(__file__))
                    + os.pathsep
                    + os.environ["PATH"]
            )
       

    def read_primitive_mesh(self, br, primitive_count, aloc_name):
        for primitive_index in range(primitive_count):  # size of box = 52
            primitive_type = br.readString(3).decode("utf-8")
            log("DEBUG", "Loading primitive " + str(primitive_index + 1) + " / " + str(primitive_count) + " with type: " + primitive_type, aloc_name)
            br.readUByteVec(1)
            if primitive_type == "BOX":
                log("DEBUG", "Loading Primitive Box", aloc_name)
                primitive_box = PrimitiveBox()
                # 31
                primitive_box.half_extents = br.readFloatVec(3)
                # 43
                primitive_box.collision_layer = br.readUInt64()
                # 51
                primitive_box.position = br.readFloatVec(3)
                # 63
                primitive_box.rotation = br.readFloatVec(4)
                # 79
                log("DEBUG", "Primitive Box: Pos: " + str(primitive_box.position[0]) + str(primitive_box.position[1]) + str(primitive_box.position[2]), aloc_name)
                log("DEBUG", "Primitive Box: half_extents: " + str(primitive_box.half_extents[0]) + str(primitive_box.half_extents[1]) + str(primitive_box.half_extents[2]), aloc_name)
                log("DEBUG", "Primitive Box: rotation: " + str(primitive_box.rotation[0]) + str(primitive_box.rotation[1]) + str(primitive_box.rotation[2]) + str(primitive_box.rotation[3]), aloc_name)
                log("DEBUG", "Primitive Box: collision_layer: " + str(primitive_box.collision_layer), aloc_name)
                self.primitive_boxes_count += 1
                self.primitive_boxes.append(primitive_box)

            elif primitive_type == "CAP":
                primitive_capsule = PrimitiveCapsule()
                primitive_capsule.radius = br.readFloat()
                primitive_capsule.length = br.readFloat()
                primitive_capsule.collision_layer = br.readUInt64()
                primitive_capsule.position = br.readFloatVec(3)
                primitive_capsule.rotation = br.readFloatVec(4)
                self.primitive_capsules_count += 1
                self.primitive_capsules.append(primitive_capsule)
            elif primitive_type == "SPH":
                primitive_sphere = PrimitiveSphere()
                primitive_sphere.radius = br.readFloat()
                primitive_sphere.collision_layer = br.readUInt64()
                primitive_sphere.position = br.readFloatVec(3)
                primitive_sphere.rotation = br.readFloatVec(4)
                self.primitive_spheres_count += 1
                self.primitive_spheres.append(primitive_sphere)

    def read(self, filepath):
        aloc_name = bpy.path.display_name_from_filepath(filepath)

        log("INFO", "Loading aloc file " + aloc_name, aloc_name)
        file_size = os.path.getsize(filepath)
        if file_size == 19:
            log("INFO", "[INFO] Skipping header only ALOC: " + aloc_name, aloc_name)
            return -1

        fp = os.fsencode(filepath)
        file = open(fp, "rb")
        br = BinaryReader(file)
        # Header + MeshType Header: sizeof = 23
        self.data_type = br.readUInt()
        self.collision_type = br.readUInt()
        br.readUByteVec(11)  # "ID\0\0\0\u{5}PhysX"
        mesh_type = br.readString(3).decode("utf-8")  # Mesh Type ("CVX", "TRI", "ICP", "BCP")
        log("DEBUG", "Data type: " + str(PhysicsDataType(self.data_type)), aloc_name)
        log("DEBUG", "Current Mesh type: " + mesh_type, aloc_name)
        try:
            br.readUByte()  # .
        except struct.error as err:
            log("ERROR", "Error reading ALOC " + aloc_name + ". Error message: " + str(err), aloc_name)

        # End of header. Current offset = 23

        if self.data_type == PhysicsDataType.CONVEX_MESH_AND_TRIANGLE_MESH:
            self.convex_mesh_count = br.readUInt()
            for convex_mesh_index in range(self.convex_mesh_count):
                log("DEBUG", "Loading Convex mesh " + str(convex_mesh_index + 1) + " of " + str(self.convex_mesh_count), aloc_name)
                self.convex_meshes.append(read_convex_mesh(br, aloc_name))
            mesh_type = br.readString(3).decode("utf-8")  # Mesh Type ("TRI")
            br.readUByte()  # .
            log("DEBUG", "Current Mesh type: " + mesh_type, aloc_name)
            self.triangle_mesh_count = br.readUInt()
            for triangle_mesh_index in range(self.triangle_mesh_count):
                log("DEBUG", "Loading Triangle mesh " + str(triangle_mesh_index + 1) + " of " + str(self.triangle_mesh_count), aloc_name)
                mesh = read_triangle_mesh(aloc_name, br)
                if mesh != -1:
                    self.triangle_meshes.append(mesh)
                else:
                    log("ERROR", "Can't continue loading current ALOC " + aloc_name + ". Returning loaded meshes.", aloc_name)
                    self.triangle_mesh_count = triangle_mesh_index
                    return self.triangle_meshes
        elif self.data_type == PhysicsDataType.CONVEX_MESH_AND_PRIMITIVE:
            log("DEBUG", "Loading Convex Mesh and Primitives for ALOC: " + aloc_name, aloc_name)
            self.convex_mesh_count = br.readUInt()
            for convex_mesh_index in range(self.convex_mesh_count):
                log("DEBUG", "Loading Convex mesh " + str(convex_mesh_index + 1) + " of " + str(self.convex_mesh_count), aloc_name)
                self.convex_meshes.append(read_convex_mesh(br, aloc_name))
            br.readString(3).decode("utf-8")  # Mesh Type ("ICP")
            br.readUByte()  # .
            log("DEBUG", "Done loading convex meshes. Reading primitive meshes. File offset: " + str(br.tell()), aloc_name)
            self.primitive_count = br.readUInt()
            log("DEBUG", "Loading Primitive mesh", aloc_name)
            self.read_primitive_mesh(br, self.primitive_count, aloc_name)
        elif self.data_type == PhysicsDataType.TRIANGLE_MESH_AND_PRIMITIVE:
            self.triangle_mesh_count = br.readUInt()
            for triangle_mesh_index in range(self.triangle_mesh_count):
                log("DEBUG", "Loading Triangle mesh " + str(triangle_mesh_index + 1) + " of " + str(self.triangle_mesh_count), aloc_name)
                self.triangle_meshes.append(read_triangle_mesh(aloc_name, br))
            br.readString(3).decode("utf-8")  # Mesh Type ("ICP")
            br.readUByte()  # .
            self.primitive_count = br.readUInt()
            log("DEBUG", "Loading Primitive mesh", aloc_name)
            self.read_primitive_mesh(br, self.primitive_count, aloc_name)
        elif self.data_type == PhysicsDataType.SHATTER_LINKED:
            self.shatter_count = br.readUInt()
        elif mesh_type == "CVX":
            if self.data_type != PhysicsDataType.CONVEX_MESH:
                _ = -1
                log("WARNING", "data_type " + str(PhysicsDataType(self.data_type)) + " does not match magic string " + mesh_type + " for " + aloc_name, aloc_name)
            self.convex_mesh_count = br.readUInt()
            for convex_mesh_index in range(self.convex_mesh_count):
                log("DEBUG", "Loading Convex mesh " + str(convex_mesh_index + 1) + " of " + str(self.convex_mesh_count), aloc_name)
                self.convex_meshes.append(read_convex_mesh(br, aloc_name))
        elif mesh_type == "TRI":
            if self.data_type != PhysicsDataType.TRIANGLE_MESH:
                _ = -1
                log("WARNING", "data_type " + str(PhysicsDataType(self.data_type)) + " does not match magic string " + mesh_type + " for " + aloc_name, aloc_name)
            self.triangle_mesh_count = br.readUInt()
            for triangle_mesh_index in range(self.triangle_mesh_count):
                log("DEBUG", "Loading Triangle mesh " + str(triangle_mesh_index + 1) + " of " + str(self.triangle_mesh_count), aloc_name)
                mesh = read_triangle_mesh(aloc_name, br)
                if mesh != -1:
                    self.triangle_meshes.append(mesh)
                else:
                    log("ERROR", "Can't continue loading current ALOC: " + aloc_name + ". Returning loaded meshes.", aloc_name)
                    self.triangle_mesh_count = triangle_mesh_index
                    return self.triangle_meshes
        elif mesh_type == "ICP":
            if self.data_type != PhysicsDataType.PRIMITIVE:
                _ = -1
                log("WARNING", "data_type " + str(PhysicsDataType(self.data_type)) + " does not match magic string " + mesh_type + " for " + aloc_name, aloc_name)
            log("DEBUG", "Found primitive", aloc_name)
            primitive_count = br.readUInt()
            # 27
            self.read_primitive_mesh(br, primitive_count, aloc_name)
            self.primitive_count = self.primitive_capsules_count + self.primitive_boxes_count + self.primitive_spheres_count
        br.close()

    def set_collision_settings(self, settings):
        self.lib.SetCollisionSettings(ctypes.byref(settings))

    def add_convex_mesh(self, vertices_list, indices_list, collider_layer):
        vertices = (ctypes.c_float * len(vertices_list))(*vertices_list)
        indices = (ctypes.c_uint32 * len(indices_list))(*indices_list)
        self.lib.AddConvexMesh(
            len(vertices_list),
            vertices,
            int(len(indices_list) / 3),
            indices,
            collider_layer,
        )

    def add_triangle_mesh(self, vertices_list, indices_list, collider_layer):
        vertices = (ctypes.c_float * len(vertices_list))(*vertices_list)
        indices = (ctypes.c_uint32 * len(indices_list))(*indices_list)
        self.lib.AddTriangleMesh(
            len(vertices_list),
            vertices,
            int(len(indices_list) / 3),
            indices,
            collider_layer,
        )

    def add_primitive_box(
            self, half_extents_list, collider_layer, position_list, rotation_list
    ):
        half_extents = (ctypes.c_float * len(half_extents_list))(*half_extents_list)
        position = (ctypes.c_float * len(position_list))(*position_list)
        rotation = (ctypes.c_float * len(rotation_list))(*rotation_list)
        self.lib.AddPrimitiveBox(half_extents, collider_layer, position, rotation)

    def add_primitive_capsule(
            self, radius, length, collider_layer, position_list, rotation_list
    ):
        position = (ctypes.c_float * len(position_list))(*position_list)
        rotation = (ctypes.c_float * len(rotation_list))(*rotation_list)
        self.lib.AddPrimitiveCapsule(radius, length, collider_layer, position, rotation)

    def add_primitive_sphere(
            self, radius, collider_layer, position_list, rotation_list
    ):
        position = (ctypes.c_float * len(position_list))(*position_list)
        rotation = (ctypes.c_float * len(rotation_list))(*rotation_list)
        self.lib.AddPrimitiveSphere(radius, collider_layer, position, rotation)


def read_aloc(filepath):
    fp = os.fsencode(filepath)
    file = open(fp, "rb")
    br = BinaryReader(file)
    aloc = Physics()
    return_val = aloc.read(filepath)
    br.close()
    if return_val == -1:
        return -1
    log("DEBUG", "Finished reading ALOC from file: " + filepath + ".", "read_aloc")

    return aloc


def convex_hull(bm):
    ch = bmesh.ops.convex_hull(bm, input=bm.verts)


def to_mesh(bm, mesh, obj, collection, context):
    bm.to_mesh(mesh)
    obj.data = mesh
    bm.free()
    collection.objects.link(obj)
    context.view_layer.objects.active = obj
    obj.select_set(True)


def set_mesh_aloc_properties(mesh, collision_type, data_type, sub_data_type):
    if bpy.app.version_string[0] == "3":
        mask = []
        log("DEBUG", "Setting Mesh ALOC properties. Sub-data type: " + str(sub_data_type), "set_mesh_aloc_properties")
        for col_type in PhysicsCollisionType:
            mask.append(collision_type == col_type.value)
        log("DEBUG", "Setting Collision type", "set_mesh_aloc_properties")
        mesh.aloc_properties.collision_type = mask
        log("DEBUG", "Setting Data type", "set_mesh_aloc_properties")
        mesh.aloc_properties.aloc_type = str(PhysicsDataType(data_type))
        log("DEBUG", "Setting Sub-data type", "set_mesh_aloc_properties")
        mesh.aloc_properties.aloc_subtype = str(PhysicsCollisionPrimitiveType(sub_data_type))
        log("DEBUG", "Finished setting Mesh ALOC properties.", "set_mesh_aloc_properties")


def create_new_object(aloc_name, collision_type, data_type):
    log("DEBUG", "Creating new object for ALOC: " + aloc_name + " with collision type: "
        + str(collision_type) + " and data type: " + str(data_type), "create_new_object")
    mesh = bpy.data.meshes.new(aloc_name)
    set_mesh_aloc_properties(mesh, collision_type, data_type, PhysicsCollisionPrimitiveType.NONE)
    obj = bpy.data.objects.new(aloc_name, mesh)
    log("DEBUG", "Finished Creating new object for ALOC: " + aloc_name, "create_new_object")
    return obj


def link_new_object(aloc_name, context):
    obj = bpy.context.active_object
    obj.name = aloc_name
    mesh = obj.data
    mesh.name = aloc_name
    context.view_layer.objects.active = obj
    obj.select_set(True)


def collidable_layer(collision_layer):
    excluded_collision_layer_types = [
        # PhysicsCollisionLayerType.COLLIDE_WITH_ALL,
        # PhysicsCollisionLayerType.STATIC_COLLIDABLES_ONLY,
        # PhysicsCollisionLayerType.DYNAMIC_COLLIDABLES_ONLY,
        # PhysicsCollisionLayerType.STAIRS,
        # PhysicsCollisionLayerType.SHOT_ONLY_COLLISION,
        # PhysicsCollisionLayerType.DYNAMIC_TRASH_COLLIDABLES,
        # PhysicsCollisionLayerType.KINEMATIC_COLLIDABLES_ONLY,
        # PhysicsCollisionLayerType.STATIC_COLLIDABLES_ONLY_TRANSPARENT,
        # PhysicsCollisionLayerType.DYNAMIC_COLLIDABLES_ONLY_TRANSPARENT,
        # PhysicsCollisionLayerType.KINEMATIC_COLLIDABLES_ONLY_TRANSPARENT,
        # PhysicsCollisionLayerType.STAIRS_STEPS,
        # PhysicsCollisionLayerType.STAIRS_SLOPE,
        # PhysicsCollisionLayerType.HERO_PROXY,
        # PhysicsCollisionLayerType.ACTOR_PROXY,
        # PhysicsCollisionLayerType.HERO_VR,
        # PhysicsCollisionLayerType.CLIP,
        # PhysicsCollisionLayerType.ACTOR_RAGDOLL,
        # PhysicsCollisionLayerType.CROWD_RAGDOLL,
        # PhysicsCollisionLayerType.LEDGE_ANCHOR,
        # PhysicsCollisionLayerType.ACTOR_DYN_BODY,
        # PhysicsCollisionLayerType.HERO_DYN_BODY,
        # PhysicsCollisionLayerType.ITEMS,
        # PhysicsCollisionLayerType.WEAPONS,
        # PhysicsCollisionLayerType.COLLISION_VOLUME_HITMAN_ON,
        # PhysicsCollisionLayerType.COLLISION_VOLUME_HITMAN_OFF,
        # PhysicsCollisionLayerType.DYNAMIC_COLLIDABLES_ONLY_NO_CHARACTER,
        # PhysicsCollisionLayerType.DYNAMIC_COLLIDABLES_ONLY_NO_CHARACTER_TRANSPARENT,
        # PhysicsCollisionLayerType.COLLIDE_WITH_STATIC_ONLY,
        # PhysicsCollisionLayerType.AI_VISION_BLOCKER,
        # PhysicsCollisionLayerType.AI_VISION_BLOCKER_AMBIENT_ONLY,
        # PhysicsCollisionLayerType.UNUSED_LAST
    ]
    return collision_layer not in excluded_collision_layer_types


def load_aloc(operator, context, filepath, include_non_collidable_layers):
    """Imports an ALOC mesh from the given path"""

    aloc_name = bpy.path.display_name_from_filepath(filepath)
    aloc = read_aloc(filepath)
    if aloc == -1:
        return -1, -1
    log("DEBUG", "Converting ALOC: " + aloc_name + " to blender mesh.", aloc_name)

    collection = context.scene.collection
    objects = []
    log("DEBUG", "Initialized collection and objects", aloc_name)
    log("DEBUG", "Collision type: " + str(aloc.collision_type), aloc_name)
    log("DEBUG", "Data type: " + str(aloc.data_type), aloc_name)

    if aloc.collision_type == PhysicsCollisionType.RIGIDBODY:
        log("DEBUG", "Skipping RigidBody ALOC " + aloc_name, "load_aloc")
        return PhysicsCollisionType.RIGIDBODY, objects
    if aloc.data_type == PhysicsDataType.CONVEX_MESH_AND_TRIANGLE_MESH:
        log("DEBUG", "Converting Convex Mesh and Triangle Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        for mesh_index in range(aloc.convex_mesh_count):
            log("DEBUG", " " + aloc_name + " convex mesh " + str(mesh_index) + " / " + str(aloc.convex_mesh_count),
                "load_aloc")
            obj = create_new_object(aloc_name, aloc.collision_type, aloc.data_type)
            bm = bmesh.new()
            m = aloc.convex_meshes[mesh_index]
            if include_non_collidable_layers or collidable_layer(m.collision_layer):
                for v in m.vertices:
                    bm.verts.new(v)
            else:
                _ = -1
                log("DEBUG",
                    "+++++++++++++++++++++ Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(
                        mesh_index) + " and collision layer type: " + str(m.collision_layer) + " +++++++++++++",
                    "load_aloc")
            mesh = obj.data
            bm.from_mesh(mesh)
            convex_hull(bm)
            to_mesh(bm, mesh, obj, collection, context)
            objects.append(obj)
        for mesh_index in range(aloc.triangle_mesh_count):
            obj = create_new_object(aloc_name, aloc.collision_type, aloc.data_type)
            bm = bmesh.new()
            m = aloc.triangle_meshes[mesh_index]
            bmv = []
            if include_non_collidable_layers or collidable_layer(m.collision_layer):
                for v in m.vertices:
                    bmv.append(bm.verts.new(v))
                d = m.triangle_data
                for i in range(0, len(d), 3):
                    face = (bmv[d[i]], bmv[d[i + 1]], bmv[d[i + 2]])
                    try:
                        bm.faces.new(face)
                    except ValueError as err:
                        _ = -1
                        log("DEBUG", "[ERROR] Could not add face to TriangleMesh: " + str(err), "load_aloc")
            else:
                _ = -1
                log("DEBUG",
                    "+++++++++++++++++++++ Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(
                        mesh_index) + " and collision layer type: " + str(m.collision_layer) + " +++++++++++++",
                    "load_aloc")

            mesh = obj.data
            to_mesh(bm, mesh, obj, collection, context)

            objects.append(obj)

    elif aloc.data_type == PhysicsDataType.CONVEX_MESH:
        log("DEBUG", "Converting Convex Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        for mesh_index in range(aloc.convex_mesh_count):
            log("DEBUG", " " + aloc_name + " convex mesh " + str(mesh_index) + " / " + str(aloc.convex_mesh_count), "load_aloc")
            obj = create_new_object(aloc_name, aloc.collision_type, aloc.data_type)
            bm = bmesh.new()
            m = aloc.convex_meshes[mesh_index]
            if include_non_collidable_layers or collidable_layer(m.collision_layer):
                for v in m.vertices:
                    bm.verts.new(v)
            else:
                _ = -1
                log("DEBUG", "+++++++++++++++++++++ Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(mesh_index) + " and collision layer type: " + str(m.collision_layer) + " +++++++++++++", "load_aloc")
            mesh = obj.data
            bm.from_mesh(mesh)
            convex_hull(bm)
            to_mesh(bm, mesh, obj, collection, context)
            objects.append(obj)
    elif aloc.data_type == PhysicsDataType.TRIANGLE_MESH:
        log("DEBUG", "Converting Triangle Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        log("DEBUG", "triangle_mesh_count: " + str(aloc.triangle_mesh_count), "load_aloc")
        for mesh_index in range(aloc.triangle_mesh_count):
            obj = create_new_object(aloc_name, aloc.collision_type, aloc.data_type)
            bm = bmesh.new()
            m = aloc.triangle_meshes[mesh_index]
            bmv = []
            if include_non_collidable_layers or collidable_layer(m.collision_layer):
                for v in m.vertices:
                    bmv.append(bm.verts.new(v))
                d = m.triangle_data
                for i in range(0, len(d), 3):
                    face = (bmv[d[i]], bmv[d[i + 1]], bmv[d[i + 2]])
                    try:
                        bm.faces.new(face)
                    except ValueError as err:
                        _ = -1
                        log("DEBUG", "[ERROR] Could not add face to TriangleMesh: " + str(err), "load_aloc")
            else:
                _ = -1
                log("DEBUG", "+++++++++++++++++++++ Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(mesh_index) + " and collision layer type: " + str(m.collision_layer) + " +++++++++++++", "load_aloc")

            mesh = obj.data
            to_mesh(bm, mesh, obj, collection, context)

            objects.append(obj)
    elif aloc.data_type == PhysicsDataType.PRIMITIVE:
        log("DEBUG", "Converting Primitive ALOC " + aloc_name + " to blender mesh", "load_aloc")
        log("DEBUG", "Primitive Type", "load_aloc")
        log("DEBUG", "Primitive count: " + str(aloc.primitive_count), "load_aloc")
        log("DEBUG", "Primitive Box count: " + str(aloc.primitive_boxes_count), "load_aloc")
        log("DEBUG", "Primitive Spheres count: " + str(aloc.primitive_spheres_count), "load_aloc")
        log("DEBUG", "Primitive Capsules count: " + str(aloc.primitive_capsules_count), "load_aloc")
        for mesh_index, box in enumerate(aloc.primitive_boxes):
            if include_non_collidable_layers or collidable_layer(box.collision_layer):
                log("DEBUG", "Primitive Box", "load_aloc")
                obj = create_new_object(aloc_name, aloc.collision_type, aloc.data_type)
                bm = bmesh.new()
                bmv = []
                x = box.position[0]
                y = box.position[1]
                z = box.position[2]
                rx = box.rotation[0]
                ry = box.rotation[1]
                rz = box.rotation[2]
                if rx != 0 or ry != 0 or rz != 0:
                    log("DEBUG", "Box has rotation value. Hash: " + aloc_name, "load_aloc")
                sx = box.half_extents[0]
                sy = box.half_extents[1]
                sz = box.half_extents[2]
                vertices = [
                    [x + sx, y + sy, z - sz],
                    [x + sx, y - sy, z - sz],
                    [x - sx, y - sy, z - sz],
                    [x - sx, y + sy, z - sz],
                    [x + sx, y + sy, z + sz],
                    [x + sx, y - sy, z + sz],
                    [x - sx, y - sy, z + sz],
                    [x - sx, y + sy, z + sz]
                ]
                for v in vertices:
                    bmv.append(bm.verts.new(v))
                bm.faces.new((bmv[0], bmv[1], bmv[2], bmv[3]))  # bottom
                bm.faces.new((bmv[4], bmv[5], bmv[6], bmv[7]))  # top
                bm.faces.new((bmv[0], bmv[1], bmv[5], bmv[4]))  # right
                bm.faces.new((bmv[2], bmv[3], bmv[7], bmv[6]))
                bm.faces.new((bmv[0], bmv[3], bmv[7], bmv[4]))
                bm.faces.new((bmv[1], bmv[2], bmv[6], bmv[5]))
                mesh = obj.data
                to_mesh(bm, mesh, obj, collection, context)
                objects.append(obj)
            else:
                _ = -1
                log("DEBUG", "+++++++++++++++++++++ Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(mesh_index) + " and collision layer type: " + str(box.collision_layer) + " +++++++++++++", "load_aloc")

        for mesh_index, sphere in enumerate(aloc.primitive_spheres):
            if include_non_collidable_layers or collidable_layer(sphere.collision_layer):
                log("DEBUG", "Primitive Sphere", "load_aloc")
                bpy.ops.mesh.primitive_ico_sphere_add(
                    subdivisions=2,
                    radius=sphere.radius,
                    location=(sphere.position[0], sphere.position[1], sphere.position[2]),
                    rotation=(sphere.rotation[0], sphere.rotation[1], sphere.rotation[2]),
                )
                link_new_object(aloc_name, context)
                obj = bpy.context.active_object
                set_mesh_aloc_properties(obj.data, aloc.collision_type, aloc.data_type, PhysicsCollisionPrimitiveType.SPHERE)
                objects.append(obj)
            else:
                _ = -1
                log("DEBUG", "+++++++++++++++++++++ Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(mesh_index) + " and collision layer type: " + str(sphere.collision_layer) + " +++++++++++++", "load_aloc")
        for mesh_index, capsule in enumerate(aloc.primitive_capsules):
            if include_non_collidable_layers or collidable_layer(capsule.collision_layer):
                log("DEBUG", "Primitive Capsule", "load_aloc")
                bpy.ops.mesh.primitive_ico_sphere_add(
                    subdivisions=2,
                    radius=capsule.radius,
                    location=(capsule.position[0], capsule.position[1], capsule.position[2] + capsule.length),
                    rotation=(capsule.rotation[0], capsule.rotation[1], capsule.rotation[2]),
                )
                link_new_object(aloc_name + "_top", context)
                obj = bpy.context.active_object
                set_mesh_aloc_properties(obj.data, aloc.collision_type, aloc.data_type, PhysicsCollisionPrimitiveType.CAPSULE)
                objects.append(obj)
                bpy.ops.mesh.primitive_cylinder_add(
                    radius=capsule.radius,
                    depth=capsule.length,
                    end_fill_type='NOTHING',
                    location=(capsule.position[0], capsule.position[1], capsule.position[2]),
                    rotation=(capsule.rotation[0], capsule.rotation[1], capsule.rotation[2])
                )
                link_new_object(aloc_name + "_cylinder", context)
                obj = bpy.context.active_object
                set_mesh_aloc_properties(obj.data, aloc.collision_type, aloc.data_type, PhysicsCollisionPrimitiveType.CAPSULE)
                objects.append(obj)
                bpy.ops.mesh.primitive_ico_sphere_add(
                    subdivisions=2,
                    radius=capsule.radius,
                    location=(capsule.position[0], capsule.position[1], capsule.position[2] - capsule.length),
                    rotation=(capsule.rotation[0], capsule.rotation[1], capsule.rotation[2]),
                )
                link_new_object(aloc_name + "_bottom", context)
                obj = bpy.context.active_object
                set_mesh_aloc_properties(obj.data, aloc.collision_type, aloc.data_type, PhysicsCollisionPrimitiveType.CAPSULE)
                objects.append(obj)
            else:
                _ = -1
                log("DEBUG", "+++++++++++++++++++++ Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(mesh_index) + " and collision layer type: " + str(capsule.collision_layer) + " +++++++++++++", "load_aloc")
    log("DEBUG", "Finished converting ALOC: " + aloc_name + " to blender mesh.", aloc_name)

    return aloc.collision_type, objects


def load_scenario(context, collection, path_to_nav_json, path_to_output_obj_file, mesh_type, lod_mask):
    start = timer()
    log("INFO", "Loading scenario.", "load_scenario")
    log("INFO", "Nav.Json file: " + path_to_nav_json, "load_scenario")
    f = open(path_to_nav_json, "r")
    data = json.loads(f.read())
    f.close()
    transforms = {}
    room_names = {}
    roomColorIndex = 0
    roomFolderColorIdx = 0    
    for hash_and_entity in data['meshes']:
        if mesh_type == "ALOC":
            mesh_hash = hash_and_entity['alocHash']
        else:
            mesh_hash = hash_and_entity['primHash']
            
        room_folder_name = hash_and_entity["roomFolderName"]
        if room_folder_name not in bpy.data.collections:
            coll = bpy.data.collections.new(room_folder_name)
            bpy.context.scene.collection.children.link(coll)
            coll.color_tag = "COLOR_0" + str(roomFolderColorIdx%8 + 1)
            roomFolderColorIdx += 1
        room_folder_coll = bpy.data.collections.get(room_folder_name)
        
        room_name = hash_and_entity["roomName"]
        if room_name not in bpy.data.collections:
            coll = bpy.data.collections.new(room_name)
            room_folder_coll.children.link(coll)
            coll.color_tag = "COLOR_0" + str(roomColorIndex%8 + 1)
            roomColorIndex += 1
            
        entity = hash_and_entity['entity']
        transform = {"position": entity["position"], "rotate": entity["rotation"],
                     "scale": entity["scale"]["data"], "id": entity["id"]}
        
        if mesh_hash not in transforms:
            transforms[mesh_hash] = []
            room_names[mesh_hash] = []
        transforms[mesh_hash].append(transform)
        room_names[mesh_hash].append(room_name)
    output_dir = os.path.dirname(path_to_output_obj_file)
    path_to_aloc_or_prim_dir = "%s\\%s" % (output_dir, mesh_type.lower())
    log("INFO", "Path to " + mesh_type.lower() + " dir:" + path_to_aloc_or_prim_dir, "load_scenario")
    file_list = sorted(os.listdir(path_to_aloc_or_prim_dir))
    aloc_or_prim_list = [item for item in file_list if item.lower().endswith('.' + mesh_type.lower())]

    excluded_collision_types = [
        # PhysicsCollisionType.NONE,
        # PhysicsCollisionType.RIGIDBODY,
        # PhysicsCollisionType.KINEMATIC_LINKED,
        # PhysicsCollisionType.SHATTER_LINKED
    ]
    excluded_mesh_hashes = [
        # "00C47B7553348F32"
    ]
    aloc_or_prim_file_count = len(aloc_or_prim_list)
    mesh_count = 0
    for aloc_or_prim_i in range(0, aloc_or_prim_file_count):
        aloc_or_prim_filename = aloc_or_prim_list[aloc_or_prim_i]
        mesh_hash = aloc_or_prim_filename[:-5]
        if mesh_hash in transforms:
            mesh_count += len(transforms[aloc_or_prim_filename[:-5]])

    mesh_i = 0
    meshes_in_scenario_count = len(transforms)
    current_mesh_in_scene_index = 0
    for aloc_or_prim_i in range(0, aloc_or_prim_file_count):
        aloc_or_prim_filename = aloc_or_prim_list[aloc_or_prim_i]
        mesh_hash = aloc_or_prim_filename[:-5]
        if mesh_hash not in transforms:
            continue
        if mesh_hash in excluded_mesh_hashes:
            log("INFO", "Skipping " + mesh_type + " file " + mesh_hash, mesh_hash)
            continue
        aloc_or_prim_path = os.path.join(path_to_aloc_or_prim_dir, aloc_or_prim_filename)

        log("INFO", "Loading " + mesh_type + ": " + mesh_hash, "load_scenario")
        collision_type = None
        try:
            if mesh_type == "ALOC":
                collision_type, objects = load_aloc(
                    None, context, aloc_or_prim_path, False
                )
                if collision_type == -1 and objects == -1:
                    continue
            else:
                objects = load_prim(None, context, aloc_or_prim_path, lod_mask)
        except struct.error as err:
            log("DEBUG", "=========================== Error Loading " + mesh_type + ": " + str(mesh_hash) + " Exception: " + str(err) + " ================", "load_scenario")
            continue

        if len(objects) == 0:
            log("DEBUG", "No collidable objects for " + str(mesh_hash), "load_scenario")
            continue
        if not objects:
            log("INFO", "-------------------- Error Loading " + mesh_type + ":" + mesh_hash + " ----------------------", "load_scenario")
            continue
        if collision_type in excluded_collision_types:
            log("DEBUG", "+++++++++++++++++++++ Skipping Non-collidable " + mesh_type + ": " + mesh_hash + " with collision type: " + str(collision_type) + " +++++++++++++", "load_scenario")
            continue
        t = transforms[mesh_hash]
        current_mesh_in_scene_index += 1
        t_size = len(t)
        for i in range(0, t_size):
            mesh_transform = transforms[mesh_hash][i]
            room_name = room_names[mesh_hash][i]
            p = mesh_transform["position"]
            r = mesh_transform["rotate"]
            s = mesh_transform["scale"]
            log("INFO", "Transforming " + mesh_type + " [" + str(current_mesh_in_scene_index) + "/" + str(meshes_in_scenario_count) + "]: " + mesh_hash + " #" + str(i) + " Mesh: [" + str(mesh_i + 1) + "/" + str(mesh_count) + "]", "load_scenario")
            mesh_i += 1
            for obj in objects:
                if i != 0:
                    cur = obj.copy()
                else:
                    cur = obj
                bpy.data.collections.get(room_name).objects.link(cur)
                cur.select_set(True)
                cur.name = mesh_hash + " " + mesh_transform["id"]
                cur.scale = mathutils.Vector((s["x"], s["y"], s["z"]))
                cur.rotation_mode = 'QUATERNION'
                cur.rotation_quaternion = (r["w"], r["x"], r["y"], r["z"])
                cur.location = mathutils.Vector((p["x"], p["y"], p["z"]))
                cur.select_set(False)

    end = timer()
    log("INFO", "Finished loading scenario in " + str(end - start) + " seconds.", "load_scenario")
    return 0


def main():
    log("DEBUG", "Usage: blender -b -P glacier2obj.py -- <nav.json path> <output.obj path> <mesh type (ALOC | PRIM)> <LOD Mask e.g. 11111111> <debug logs enabled>", "main")
    argv = sys.argv
    argv = argv[argv.index("--") + 1:]
    log("INFO", "blender.exe called with args: " + str(argv), "main")  # --> ['example', 'args', '123']
    scene_path = argv[0]
    output_path = argv[1]
    mesh_type = argv[2]
    lod_mask = argv[3]
    global glacier2obj_enabled_log_levels
    if len(argv) > 4 and argv[4] == True:
        log("INFO", "Enabling debug logs", "main"),
        glacier2obj_enabled_log_levels.append("DEBUG")

    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

    collection = bpy.data.collections.new(
        bpy.path.display_name_from_filepath(scene_path)
    )
    bpy.context.scene.collection.children.link(collection)

    scenario = load_scenario(bpy.context, collection, scene_path, output_path, mesh_type, lod_mask)
    if scenario == 1:
        log("INFO", 'Failed to import scenario "%s"' % scene_path   , "main")
        return 1
    if(output_path[-6:]=='.blend'):
        log("INFO", "Attempting to save blender file to :" + output_path, "main")
        bpy.ops.wm.save_as_mainfile(filepath=output_path)
    else:
        log("INFO", "Attempting to save obj file to :" + output_path, "main")
        if bpy.app.version_string[0] == "3":
            bpy.ops.export_scene.obj(filepath=output_path, use_selection=False)
        else:
            bpy.ops.wm.obj_export(filepath=output_path)  # Export the entire scene

    return None


if __name__ == "__main__":
    classes = [
        AlocProperties,
    ]
    for c in classes:
        bpy.utils.register_class(c)
    if bpy.app.version_string[0] == "3":
        bpy.types.Mesh.aloc_properties = PointerProperty(type=AlocProperties)
    main()
