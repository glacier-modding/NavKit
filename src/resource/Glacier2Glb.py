import ctypes
import enum
import json
import math
import os
import struct
import sys
from timeit import default_timer as timer

import bmesh
import bpy
import contextlib
import logging
import mathutils
import numpy as np
from bpy.types import (
    Context,
)

glacier2glb_enabled_log_levels = ["ERROR", "WARNING", "INFO"]  # Log levels are "TRACE", "DEBUG", "INFO", "WARNING", "ERROR"
gltf_logger = logging.getLogger("gltf2")
gltf_logger.setLevel(logging.ERROR)
gltf_logger.disabled = True

# General


def log(level: str, msg: str, filter_field: str):
    if level in glacier2glb_enabled_log_levels:  # and filter_field == "007573591BE1BE69":
        # print("[" + str(level) + "] " + str(filter_field) + ": " + str(msg), flush=True)
        if level != "INFO":
            print("[" + str(level) + "] " + str(filter_field) + ": " + str(msg), flush=True)
        else:
            print(str(msg), flush=True)
        try:
            sys.stdout.flush()
            os.fsync(sys.stdout.fileno())
        except OSError:
            pass


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


class PhysicsDataType(enum.IntEnum):
    NONE = 0
    CONVEX_MESH = 1
    TRIANGLE_MESH = 2
    CONVEX_MESH_AND_TRIANGLE_MESH = 3
    PRIMITIVE = 4
    CONVEX_MESH_AND_PRIMITIVE = 5
    TRIANGLE_MESH_AND_PRIMITIVE = 6
    UNKNOWN_129 = 129
    UNKNOWN_130 = 130
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


def load_prim_mesh(prim, prim_name: str, mesh_index: int, load_textures=True):
    """
    Turn the prim data structure into a Blender mesh.
    Returns the generated Mesh
    """
    mesh = bpy.data.meshes.new(name=(str(prim_name) + "_" + str(mesh_index)))

    vert_locs = []
    loop_normals = []
    loop_cols = []

    material_index = prim.header.object_table[mesh_index].prim_object.material_id
    sub_mesh = prim.header.object_table[mesh_index].sub_mesh

    loop_uvs = [[] for _ in range(sub_mesh.num_uvchannels)]

    unique_verts = {}
    remap = []

    for vert in sub_mesh.vertexBuffer.vertices:
        pos = (vert.position[0], vert.position[1], vert.position[2])
        if pos not in unique_verts:
            unique_verts[pos] = len(vert_locs) // 3
            vert_locs.extend(pos)
        remap.append(unique_verts[pos])

    loop_vidxs = [remap[i] for i in sub_mesh.indices]

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

    for index in sub_mesh.indices:
        vert = sub_mesh.vertexBuffer.vertices[index]
        loop_normals.append((vert.normal[0], vert.normal[1], vert.normal[2]))
        if load_textures:
            loop_cols.extend(
                [
                    vert.color[0] / 255,
                    vert.color[1] / 255,
                    vert.color[2] / 255,
                    vert.color[3] / 255,
                ]
            )
            for uv_i in range(sub_mesh.num_uvchannels):
                loop_uvs[uv_i].extend([vert.uv[uv_i][0], 1 - vert.uv[uv_i][1]])

    if load_textures:
        for uv_i in range(sub_mesh.num_uvchannels):
            name = "UVMap" if uv_i == 0 else "UVMap.%03d" % uv_i
            layer = mesh.uv_layers.new(name=name)
            layer.data.foreach_set("uv", loop_uvs[uv_i])

        layer = mesh.vertex_colors.new(name="Col")
        mesh.color_attributes[layer.name].data.foreach_set("color", loop_cols)

    mesh.update()

    mesh.normals_split_custom_set(loop_normals)
    if bpy.app.version < (4, 1, 0):
        mesh.use_auto_smooth = True

    mesh.polygons.foreach_set("use_smooth", [True] * len(mesh.polygons))

    mesh.validate()
    mesh.update()

    # write the additional properties to the blender structure
    prim_mesh_obj = prim.header.object_table[mesh_index].prim_object

    lod = prim_mesh_obj.lodmask
    mask = []
    for bit in range(8):
        mask.append(0 != (lod & (1 << bit)))

    return mesh, material_index


def load_prim(filepath, lod_mask, temp_coll):
    """Imports a mesh from the given path"""

    prim_name = bpy.path.display_name_from_filepath(filepath)
    log("DEBUG", "Started reading PRIM: " + str(prim_name), "load_prim")

    fp = os.fsencode(filepath)
    file = open(fp, "rb")
    br = BinaryReader(file)
    prim = RenderPrimitive()
    prim.read(br)
    br.close()

    submeshes = []
    material_indices = []
    for submesh_index in range(prim.num_objects()):
        prim_submesh_obj = prim.header.object_table[submesh_index].prim_object
        lod = prim_submesh_obj.lodmask

        include_mesh = False
        for bit in range(8):
            bit_value = 0 != (lod & (1 << bit))
            if lod_mask[bit] == '1' and bit_value == 1:
                include_mesh = True
                break
        if not include_mesh:
            continue
        mesh, material_index = load_prim_mesh(prim, prim_name, submesh_index)
        obj = bpy.data.objects.new(mesh.name, mesh)
        temp_coll.objects.link(obj)
        submeshes.append(obj)
        material_indices.append(material_index)

    return submeshes, material_indices


# ALOC
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
    log("TRACE", "Starting to read variable convex mesh hull data. Current offset: " + str(br.tell()), aloc_name)
    convex_mesh.vertex_count = br.readUInt()
    log("TRACE", "Num vertices " + str(convex_mesh.vertex_count), aloc_name)

    grb_flag_and_edge_count = br.readUInt()
    convex_mesh.has_grb_data = 0x8000 & grb_flag_and_edge_count
    log("TRACE", "Has_grb_data " + str(convex_mesh.has_grb_data), aloc_name)

    convex_mesh.edge_count = 0x7FFF & grb_flag_and_edge_count
    log("TRACE", "edge_count " + str(convex_mesh.edge_count), aloc_name)
    convex_mesh.polygon_count = br.readUInt()
    log("TRACE", "polygon_count " + str(convex_mesh.polygon_count), aloc_name)
    convex_mesh.polygons_vertex_count = br.readUInt()
    log("TRACE", "polygons_vertex_count " + str(convex_mesh.polygons_vertex_count), aloc_name)
    vertices = []
    for _vertex_index in range(convex_mesh.vertex_count):
        vertices.append(br.readFloatVec(3))
    convex_mesh.vertices = vertices
    log("TRACE", "Finished reading vertices and metadata. Reading convex main hull data", aloc_name)

    # Unused because Blender can build the convex hull
    hull_polygon_data = 0
    for _polygon_index in range(convex_mesh.polygon_count):
        log("TRACE", "Reading 20 characters of HullPolygonData. Current offset: " + str(br.tell()), aloc_name)
        hull_polygon_data = br.readUByteVec(20)  # HullPolygonData
        log("TRACE", len(hull_polygon_data), aloc_name)
        # TODO: <------------------ Read past EOF Here on second convex mesh!
        if len(hull_polygon_data) < 20:
            break
    if len(hull_polygon_data) < 20:
        return
    _mHullDataVertexData8 = br.readUByteVec(
        convex_mesh.polygons_vertex_count)  # mHullDataVertexData8 for each polygon's vertices
    log("TRACE", "mHullDataVertexData8 " + str(_mHullDataVertexData8), aloc_name)
    _mHullDataFacesByEdges8 = br.readUByteVec(convex_mesh.edge_count * 2)  # mHullDataFacesByEdges8
    log("TRACE", "mHullDataFacesByEdges8 " + str(_mHullDataVertexData8), aloc_name)
    _mHullDataFacesByVertices8 = br.readUByteVec(convex_mesh.vertex_count * 3)  # mHullDataFacesByVertices8
    log("TRACE", "mHullDataFacesByVertices8 " + str(_mHullDataVertexData8), aloc_name)
    if convex_mesh.has_grb_data == 1:
        log("TRACE", "has_grb_data true. Reading edges. Current offset: " + str(br.tell()), aloc_name)
        _mEdges = br.readUByteVec(4 * 2 * convex_mesh.edge_count)  # mEdges
    else:
        log("TRACE", "has_grb_data false. No edges to read. Current offset: " + str(br.tell()), aloc_name)
    log("TRACE",
        "Finished reading main convex hull data. Reading remaining convex hull data. Current offset: " + str(br.tell()),
        aloc_name)
    # ---- End of Variable data for each Convex Mesh Hull

    # Remaining convex hull data
    zero = br.readFloat()  # 0
    log("TRACE", "This should be zero: " + str(zero) + " Current offset: " + str(br.tell()), aloc_name)
    # Local bounds
    bbox_min_x = br.readFloat()  # mHullData.mAABB.getMin(0)
    log("TRACE", "Bbox min x: " + str(bbox_min_x) + " Current offset: " + str(br.tell()), aloc_name)
    _bbox_min_y = br.readFloat()  # mHullData.mAABB.getMin(1)
    _bbox_min_z = br.readFloat()  # mHullData.mAABB.getMin(2)
    _bbox_max_x = br.readFloat()  # mHullData.mAABB.getMax(0)
    _bbox_max_y = br.readFloat()  # mHullData.mAABB.getMax(1)
    _bbox_max_z = br.readFloat()  # mHullData.mAABB.getMax(2)
    # Mass Info
    mass = br.readFloat()  # mMass
    log("TRACE", "Mass: " + str(mass) + " Current offset: " + str(br.tell()), aloc_name)
    br.readFloatVec(9)  # mInertia
    br.readFloatVec(3)  # mCenterOfMass.x
    gauss_map_flag = br.readFloat()
    log("TRACE", "Gauss Flag: " + str(gauss_map_flag), aloc_name)

    if gauss_map_flag == 1.0:
        log("TRACE", "Gauss Flag is 1.0, reading Gauss Data", aloc_name)
        br.readUByteVec(24)  # ICE.SUPM....ICE.GAUS....
        m_subdiv = br.readInt()  # mSVM->mData.m_subdiv
        log("TRACE", "m_subdiv: " + str(m_subdiv), aloc_name)

        num_samples = br.readInt()  # mSVM->mData.mNbSamples
        log("TRACE", "num_samples: " + str(num_samples), aloc_name)
        br.readUByteVec(num_samples * 2)
        br.readUByteVec(4)  # ICE.
        log("TRACE", "Reading VALE: Current offset: " + str(br.tell()), aloc_name)
        vale = br.readString(4)  # VALE
        log("TRACE", "Should say VALE: " + str(vale), aloc_name)
        br.readUByteVec(4)  # ....
        num_svm_verts = br.readInt()  # mSVM->mData.mNbVerts
        log("TRACE", "num_svm_verts: " + str(num_svm_verts), aloc_name)
        num_svm_adj_verts = br.readInt()  # mSVM->mData.mNbAdjVerts
        log("TRACE", "num_svm_adj_verts: " + str(num_svm_adj_verts), aloc_name)
        svm_max_index = br.readInt()  # maxIndex
        log("TRACE", "svm_max_index: " + str(svm_max_index), aloc_name)
        if svm_max_index <= 0xff:
            log("TRACE", "svm_max_index <= 0xff. File offset: " + str(br.tell()), aloc_name)
            br.readUByteVec(num_svm_verts)
        else:
            log("TRACE", "svm_max_index > 0xff. File offset: " + str(br.tell()), aloc_name)
            br.readUByteVec(num_svm_verts * 2)
        br.readUByteVec(num_svm_adj_verts)
        log("TRACE", "Finished Gauss Data. File offset: " + str(br.tell()), aloc_name)
    else:
        log("TRACE", "Gauss Flag is " + str(gauss_map_flag) + " No Gauss Data. File offset: " + str(br.tell()),
            aloc_name)
    _mRadius = br.readFloat()
    _mExtents_0 = br.readFloat()
    _mExtents_1 = br.readFloat()
    _mExtents_2 = br.readFloat()
    log("TRACE", "Finished reading Convex mesh. File offset: " + str(br.tell()), aloc_name)
    return convex_mesh


def read_triangle_mesh(aloc_name, br):
    # Start of first triangle mesh, offset = 27
    triangle_mesh = TriangleMesh()
    triangle_mesh.collision_layer = br.readUInt()
    br.readUByteVec(16)  # \0\0\0\0NXS.MESH....\u{15}\0\0\0
    # Offset: 47
    br.readUByteVec(4)  # midPhaseId
    log("TRACE", "Reading serial_flags. Current offset: " + str(br.tell()), aloc_name)

    triangle_mesh.serial_flags = br.readInt()
    # Example Serial Flag: 6 = 00000110: IMSF_FACE_REMAP | IMSF_8BIT_INDICES
    # IMSF_MATERIALS       =    (1 << 0), // ! < if set, the cooked mesh file contains per-triangle material indices
    # IMSF_FACE_REMAP      =    (1 << 1), // ! < if set, the cooked mesh file contains a remap table
    # IMSF_8BIT_INDICES    =    (1 << 2), // ! < if set, the cooked mesh file contains 8bit indices (topology)
    # IMSF_16BIT_INDICES   =    (1 << 3), // ! < if set, the cooked mesh file contains 16bit indices (topology)
    # IMSF_ADJACENCIES     =    (1 << 4), // ! < if set, the cooked mesh file contains adjacency structures
    # IMSF_GRB_DATA        =    (1 << 5)  // ! < if set, the cooked mesh file contains GRB data structures
    log("TRACE", "Reading Vertex_count: Current offset: " + str(br.tell()), aloc_name)
    triangle_mesh.vertex_count = br.readUInt()
    log("TRACE", "vertex_count: " + str(triangle_mesh.vertex_count), aloc_name)
    triangle_mesh.triangle_count = br.readUInt()
    log("TRACE", "triangle_count: " + str(triangle_mesh.triangle_count), aloc_name)
    vertices = []
    for vertex_index in range(triangle_mesh.vertex_count):
        vertex = br.readFloatVec(3)
        log("TRACE", "vertex " + str(vertex_index) + ": " + str(vertex), aloc_name)
        vertices.append(vertex)
    triangle_mesh.vertices = vertices
    # Check serial flag
    triangle_data = []
    log("TRACE", "serial Flags: " + str(triangle_mesh.serial_flags), aloc_name)
    is_8bit = (triangle_mesh.serial_flags >> 2) & 1 == 1
    log("TRACE", "is_8bit: " + str(is_8bit), aloc_name)
    is_16bit = (triangle_mesh.serial_flags >> 3) & 1 == 1
    log("TRACE", "is_16bit: " + str(is_16bit), aloc_name)
    if is_8bit:
        log("TRACE", "is_8bit. Reading triangle bytes", aloc_name)
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            triangle_byte = br.readUByte()
            log("TRACE", "Triangle_byte: " + str(triangle_byte), aloc_name)
            triangle_data.append(triangle_byte)
    elif is_16bit:
        log("TRACE", "is_16bit. Reading triangle shorts", aloc_name)
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            triangle_short = br.readUShort()
            log("TRACE", "Triangle_short: " + str(triangle_short), aloc_name)
            triangle_data.append(triangle_short)
    else:
        log("TRACE", "Not 8 or 16 bit. Reading triangle ints", aloc_name)
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            triangle_int = br.readInt()
            log("TRACE", "Triangle_Int: " + str(triangle_int), aloc_name)
            triangle_data.append(triangle_int)
    triangle_mesh.triangle_data = triangle_data
    material_indices = (triangle_mesh.serial_flags >> 0) & 1 == 1
    log("TRACE", "material_indices: " + str(material_indices), aloc_name)

    if material_indices:
        br.readUByteVec(2 * triangle_mesh.triangle_count)  # material_indices
    face_remap = (triangle_mesh.serial_flags >> 1) & 1 == 1
    log("TRACE", "face_remap: " + str(face_remap), aloc_name)

    if face_remap:
        max_id = br.readInt()
        log("TRACE", "max_id: " + str(max_id), aloc_name)
        if max_id <= 0xff:
            face_remap_val = br.readUByteVec(triangle_mesh.triangle_count)
            log("TRACE", "face_remap_val 8bit: " + str(face_remap_val), aloc_name)

        elif max_id <= 0xffff:
            face_remap_val = br.readUByteVec(triangle_mesh.triangle_count * 2)
            log("TRACE", "face_remap_val 16bit: " + str(face_remap_val), aloc_name)
        else:
            for triangle_index in range(triangle_mesh.triangle_count):
                face_remap_val = br.readInt()
                log("TRACE", "face_remap_val int: " + str(face_remap_val), aloc_name)
    adjacencies = (triangle_mesh.serial_flags >> 4) & 1 == 1
    log("TRACE", "adjacencies: " + str(adjacencies), aloc_name)
    if adjacencies:
        for triangle_index in range(triangle_mesh.triangle_count * 3):
            br.readInt()
    # Write midPhaseStructure. Is it BV4? -> BV4TriangleMeshBuilder::saveMidPhaseStructure
    log("TRACE", "Reading BV4: Current offset: " + str(br.tell()), aloc_name)
    bv4 = br.readString(3)  # "BV4."
    br.readUByte()
    log("TRACE", "Should say BV4: " + str(bv4), aloc_name)
    bv4_version = br.readIntBigEndian()  # Bv4 Structure Version. Is always 1, so the midPhaseStructure will be bigEndian
    log("TRACE", "BV4 version. Should be 1: " + str(bv4_version), aloc_name)
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
    log("TRACE", "Reading mNbNodes: Current offset: " + str(br.tell()), aloc_name)
    m_nb_nodes = br.readIntBigEndian()  # mData.mBV4Tree.mNbNodes
    log("TRACE", "mNbNodes: " + str(m_nb_nodes), aloc_name)

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
    log("TRACE", "mMeshData.mAABB.minimum.x: " + str(bbox_min_x), aloc_name)
    br.readFloat()  # mMeshData.mAABB.minimum.y
    br.readFloat()  # mMeshData.mAABB.minimum.z
    br.readFloat()  # mMeshData.mAABB.maximum.x
    br.readFloat()  # mMeshData.mAABB.maximum.y
    bbox_min_z = br.readFloat()  # mMeshData.mAABB.maximum.z
    log("TRACE", "mMeshData.mAABB.maximum.z: " + str(bbox_min_z), aloc_name)

    # if(mMeshData.mExtraTrigData)
    m_nbv_triangles = br.readInt()  # mMeshData.mNbTriangles
    log("TRACE", "m_nbv_triangles: " + str(m_nbv_triangles), aloc_name)

    br.readUByteVec(m_nbv_triangles)
    # else
    # br.readUByteVec(4)  # 0
    # endif

    # GRB Write
    has_grb = (triangle_mesh.serial_flags >> 5) & 1 == 1
    log("TRACE", "has_grb: " + str(has_grb), aloc_name)

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
        log("TRACE", "Reading BV32: " + str(bv32) + " File Offset: " + str(br.tell()), aloc_name)
        br.readUByteVec(4)  # Bv32 Structure Version. If 1, the midPhaseStructure will be bigEndian
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.x
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.y
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.z
        br.readFloat()  # mData.mBV4Tree.mLocalBounds.mCenter.mExtentsMagnitude
        br.readUByteVec(4)  # mData.mBV4Tree.mInitData
        m_nb_packed_nodes = br.readInt()  # mData.mBV4Tree.m_nb_packed_nodes
        log("TRACE", "m_nb_packed_nodes: " + str(m_nb_packed_nodes), aloc_name)
        m_nb_packed_nodes_be = br.readIntBigEndian()  # mData.mBV4Tree.m_nb_packed_nodes
        log("TRACE", "m_nb_packed_nodes_be: " + str(m_nb_packed_nodes_be), aloc_name)
        for _mNbNodesIndex in range(m_nb_packed_nodes):
            m_nb_nodes = br.readInt(4)  # node.mNbNodes
            log("TRACE", "mNbNodes: " + str(m_nb_nodes), aloc_name)
            m_nb_nodes_be = br.readIntBigEndian(4)  # node.mNbNodes
            log("TRACE", "m_nb_nodes_be: " + str(m_nb_nodes_be), aloc_name)
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
            log("TRACE", "Loading primitive " + str(primitive_index + 1) + " / " + str(
                primitive_count) + " with type: " + primitive_type, aloc_name)
            br.readUByteVec(1)
            if primitive_type == "BOX":
                log("TRACE", "Loading Primitive Box", aloc_name)
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
                log("TRACE",
                    "Primitive Box: Pos: " + str(primitive_box.position[0]) + str(primitive_box.position[1]) + str(
                        primitive_box.position[2]), aloc_name)
                log("TRACE", "Primitive Box: half_extents: " + str(primitive_box.half_extents[0]) + str(
                    primitive_box.half_extents[1]) + str(primitive_box.half_extents[2]), aloc_name)
                log("TRACE",
                    "Primitive Box: rotation: " + str(primitive_box.rotation[0]) + str(primitive_box.rotation[1]) + str(
                        primitive_box.rotation[2]) + str(primitive_box.rotation[3]), aloc_name)
                log("TRACE", "Primitive Box: collision_layer: " + str(primitive_box.collision_layer), aloc_name)
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

        log("TRACE", "Loading aloc file " + aloc_name, aloc_name)
        file_size = os.path.getsize(filepath)
        if file_size == 19:
            log("TRACE", "Skipping header only ALOC: " + aloc_name, aloc_name)
            return -2

        try:
            file = open(os.fsencode(filepath), "rb")
        except OSError as err:
            log("ERROR", "Unable to open ALOC " + aloc_name + ". Error message: " + str(err), aloc_name)
            return -1
        br = BinaryReader(file)
        try:
            return self._read_contents(br, aloc_name)
        except (OSError, struct.error, ValueError, UnicodeDecodeError) as err:
            log("ERROR", "Error reading ALOC " + aloc_name + ". Error message: " + str(err), aloc_name)
            return -1
        finally:
            br.close()

    def _read_contents(self, br, aloc_name):
        # Header + MeshType Header: sizeof = 23
        self.data_type = br.readUInt()
        self.collision_type = br.readUInt()
        br.readUByteVec(11)  # "ID\0\0\0\u{5}PhysX"
        mesh_type = br.readString(3).decode("utf-8")  # Mesh Type ("CVX", "TRI", "ICP", "BCP")
        log("TRACE", "Data type: " + str(PhysicsDataType(self.data_type)), aloc_name)
        log("TRACE", "Current Mesh type: " + mesh_type, aloc_name)
        try:
            br.readUByte()  # .
        except struct.error as err:
            log("ERROR", "Error reading ALOC " + aloc_name + ". Error message: " + str(err), aloc_name)
            br.close()
            return -1

        # End of header. Current offset = 23

        if self.data_type == PhysicsDataType.CONVEX_MESH_AND_TRIANGLE_MESH:
            self.convex_mesh_count = br.readUInt()
            for convex_mesh_index in range(self.convex_mesh_count):
                log("TRACE", "Loading Convex mesh " + str(convex_mesh_index + 1) + " of " + str(self.convex_mesh_count),
                    aloc_name)
                self.convex_meshes.append(read_convex_mesh(br, aloc_name))
            mesh_type = br.readString(3).decode("utf-8")  # Mesh Type ("TRI")
            br.readUByte()  # .
            log("TRACE", "Current Mesh type: " + mesh_type, aloc_name)
            self.triangle_mesh_count = br.readUInt()
            for triangle_mesh_index in range(self.triangle_mesh_count):
                log("TRACE",
                    "Loading Triangle mesh " + str(triangle_mesh_index + 1) + " of " + str(self.triangle_mesh_count),
                    aloc_name)
                mesh = read_triangle_mesh(aloc_name, br)
                if mesh != -1:
                    self.triangle_meshes.append(mesh)
                else:
                    log("ERROR", "Can't continue loading current ALOC " + aloc_name + ". Returning loaded meshes.",
                        aloc_name)
                    self.triangle_mesh_count = triangle_mesh_index
                    br.close()
                    return 0
            br.close()
            return 0
        elif self.data_type == PhysicsDataType.CONVEX_MESH_AND_PRIMITIVE:
            log("TRACE", "Loading Convex Mesh and Primitives for ALOC: " + aloc_name, aloc_name)
            self.convex_mesh_count = br.readUInt()
            for convex_mesh_index in range(self.convex_mesh_count):
                log("TRACE", "Loading Convex mesh " + str(convex_mesh_index + 1) + " of " + str(self.convex_mesh_count),
                    aloc_name)
                self.convex_meshes.append(read_convex_mesh(br, aloc_name))
            br.readString(3).decode("utf-8")  # Mesh Type ("ICP")
            br.readUByte()  # .
            log("TRACE", "Done loading convex meshes. Reading primitive meshes. File offset: " + str(br.tell()),
                aloc_name)
            self.primitive_count = br.readUInt()
            log("TRACE", "Loading Primitive mesh", aloc_name)
            self.read_primitive_mesh(br, self.primitive_count, aloc_name)
            br.close()
            return 0
        elif self.data_type == PhysicsDataType.TRIANGLE_MESH_AND_PRIMITIVE:
            self.triangle_mesh_count = br.readUInt()
            for triangle_mesh_index in range(self.triangle_mesh_count):
                log("TRACE",
                    "Loading Triangle mesh " + str(triangle_mesh_index + 1) + " of " + str(self.triangle_mesh_count),
                    aloc_name)
                self.triangle_meshes.append(read_triangle_mesh(aloc_name, br))
            br.readString(3).decode("utf-8")  # Mesh Type ("ICP")
            br.readUByte()  # .
            self.primitive_count = br.readUInt()
            log("TRACE", "Loading Primitive mesh", aloc_name)
            self.read_primitive_mesh(br, self.primitive_count, aloc_name)
            br.close()
            return 0
        elif self.data_type == PhysicsDataType.SHATTER_LINKED:
            self.shatter_count = br.readUInt()
            br.close()
            return 0
        elif mesh_type == "CVX":
            if self.data_type != PhysicsDataType.CONVEX_MESH:
                log("WARNING", "data_type " + str(PhysicsDataType(
                    self.data_type)) + " does not match magic string " + mesh_type + " for " + aloc_name, aloc_name)
            self.convex_mesh_count = br.readUInt()
            for convex_mesh_index in range(self.convex_mesh_count):
                log("TRACE", "Loading Convex mesh " + str(convex_mesh_index + 1) + " of " + str(self.convex_mesh_count),
                    aloc_name)
                self.convex_meshes.append(read_convex_mesh(br, aloc_name))
            br.close()
            return 0
        elif mesh_type == "TRI":
            if self.data_type != PhysicsDataType.TRIANGLE_MESH:
                log("WARNING", "data_type " + str(PhysicsDataType(
                    self.data_type)) + " does not match magic string " + mesh_type + " for " + aloc_name, aloc_name)
            self.triangle_mesh_count = br.readUInt()
            for triangle_mesh_index in range(self.triangle_mesh_count):
                log("TRACE",
                    "Loading Triangle mesh " + str(triangle_mesh_index + 1) + " of " + str(self.triangle_mesh_count),
                    aloc_name)
                mesh = read_triangle_mesh(aloc_name, br)
                if mesh != -1:
                    self.triangle_meshes.append(mesh)
                else:
                    log("ERROR", "Can't continue loading current ALOC: " + aloc_name + ". Returning loaded meshes.",
                        aloc_name)
                    self.triangle_mesh_count = triangle_mesh_index
                    br.close()
                    return 0
            br.close()
            return 0
        elif mesh_type == "ICP":
            if self.data_type != PhysicsDataType.PRIMITIVE:
                log("WARNING", "data_type " + str(PhysicsDataType(
                    self.data_type)) + " does not match magic string " + mesh_type + " for " + aloc_name, aloc_name)
            log("TRACE", "Found primitive", aloc_name)
            primitive_count = br.readUInt()
            # 27
            self.read_primitive_mesh(br, primitive_count, aloc_name)
            self.primitive_count = self.primitive_capsules_count + self.primitive_boxes_count + self.primitive_spheres_count
            log("TRACE", "mesh_type == ICP ALOC: " + aloc_name, aloc_name)
            # raise(Exception("mesh_type == ICP")) # TODO: Verify that ICP meshes work properly
            br.close()
            return 0
        log("ERROR", "Problem reading aloc: data_type: " + str(self.data_type) + " mesh_type: " + str(mesh_type),
            aloc_name)

        br.close()
        return -1

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
    aloc = Physics()
    return_val = aloc.read(filepath)
    if return_val == -1:
        log("ERROR", "Failed to read ALOC from file: " + filepath + ".", "read_aloc")
        return -1
    log("TRACE", "Finished reading ALOC from file: " + filepath + ".", "read_aloc")

    return aloc


def convex_hull(bm):
    bmesh.ops.convex_hull(bm, input=bm.verts)


def finalize_aloc_mesh(bm, obj):
    bm.to_mesh(obj.data)
    obj.data.polygons.foreach_set("use_smooth", [False] * len(obj.data.polygons))
    bm.free()


def create_new_object(name, collision_type, data_type, temp_coll):
    log("TRACE", "Creating new object for ALOC: " + name + " with collision type: "
        + str(collision_type) + " and data type: " + str(data_type), "create_new_object")
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    temp_coll.objects.link(obj)
    log("TRACE", "Finished Creating new object for ALOC: " + name, "create_new_object")
    return obj


def load_triangle_mesh_objects(aloc, aloc_name, bm):
    for mesh_index in range(aloc.triangle_mesh_count):
        m = aloc.triangle_meshes[mesh_index]
        bmv = []
        if collidable_layer(m.collision_layer):
            log("TRACE", "Adding vertices to TriangleMesh", "load_aloc")
            for v in m.vertices:
                bmv.append(bm.verts.new(v))
            d = m.triangle_data
            log("TRACE", "Adding faces to TriangleMesh", "load_aloc")
            for i in range(0, len(d), 3):
                face = (bmv[d[i]], bmv[d[i + 1]], bmv[d[i + 2]])
                try:
                    bm.faces.new(face)
                except ValueError as err:
                    log("TRACE", "[ERROR] Could not add face to TriangleMesh: " + str(err), "load_aloc")
        else:
            log("TRACE", "Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(
                mesh_index) + " and collision layer type: " + str(m.collision_layer), "load_aloc")


def load_convex_mesh_objects(aloc, aloc_name, bm):
    for mesh_index in range(aloc.convex_mesh_count):
        log("TRACE", " " + aloc_name + " convex mesh " + str(mesh_index) + " / " + str(aloc.convex_mesh_count),
            "load_aloc")
        m = aloc.convex_meshes[mesh_index]
        if collidable_layer(m.collision_layer):
            input_verts = []
            for v in m.vertices:
                input_verts.append(bm.verts.new(v))

            res = bmesh.ops.convex_hull(bm, input=input_verts)
            if 'geom_unused' in res:
                bmesh.ops.delete(bm, geom=res['geom_unused'], context='VERTS')
        else:
            log("TRACE", "Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(
                mesh_index) + " and collision layer type: " + str(m.collision_layer), "load_aloc")


def load_primitive_mesh_objects(aloc, aloc_name, bm):
    log("TRACE", "Primitive Type", "load_aloc")
    log("TRACE", "Primitive count: " + str(aloc.primitive_count), "load_aloc")
    log("TRACE", "Primitive Box count: " + str(aloc.primitive_boxes_count), "load_aloc")
    log("TRACE", "Primitive Spheres count: " + str(aloc.primitive_spheres_count), "load_aloc")
    log("TRACE", "Primitive Capsules count: " + str(aloc.primitive_capsules_count), "load_aloc")
    for mesh_index, box in enumerate(aloc.primitive_boxes):
        if collidable_layer(box.collision_layer):
            log("TRACE", "Primitive Box", "load_aloc")

            loc = mathutils.Vector((box.position[0], box.position[1], box.position[2]))
            if len(box.rotation) == 4:
                rot = mathutils.Quaternion((box.rotation[3], box.rotation[0], box.rotation[1], box.rotation[2]))
            else:
                x = box.rotation[0]
                y = box.rotation[1]
                z = box.rotation[2]
                w = math.sqrt(max(0.0, 1.0 - (x * x + y * y + z * z)))
                rot = mathutils.Quaternion((w, x, y, z))

            mat = mathutils.Matrix.LocRotScale(loc, rot, None)

            bmv = []
            sx = box.half_extents[0]
            sy = box.half_extents[1]
            sz = box.half_extents[2]
            vertices = [
                [sx, sy, -sz],
                [sx, -sy, -sz],
                [-sx, -sy, -sz],
                [-sx, sy, -sz],
                [sx, sy, sz],
                [sx, -sy, sz],
                [-sx, -sy, sz],
                [-sx, sy, sz]
            ]
            for v in vertices:
                bmv.append(bm.verts.new(mat @ mathutils.Vector(v)))
            bm.faces.new((bmv[0], bmv[1], bmv[2], bmv[3]))  # bottom
            bm.faces.new((bmv[4], bmv[5], bmv[6], bmv[7]))  # top
            bm.faces.new((bmv[0], bmv[1], bmv[5], bmv[4]))  # right
            bm.faces.new((bmv[2], bmv[3], bmv[7], bmv[6]))
            bm.faces.new((bmv[0], bmv[3], bmv[7], bmv[4]))
            bm.faces.new((bmv[1], bmv[2], bmv[6], bmv[5]))
        else:
            log("TRACE", "Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(
                mesh_index) + " and collision layer type: " + str(box.collision_layer), "load_aloc")
    for mesh_index, sphere in enumerate(aloc.primitive_spheres):
        if collidable_layer(sphere.collision_layer):
            log("TRACE", "Primitive Sphere", "load_aloc")

            loc = mathutils.Vector((sphere.position[0], sphere.position[1], sphere.position[2]))
            rot = mathutils.Euler((sphere.rotation[0], sphere.rotation[1], sphere.rotation[2])).to_quaternion()
            mat = mathutils.Matrix.LocRotScale(loc, rot, None)

            bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=sphere.radius, matrix=mat)
        else:
            log("TRACE", "Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(
                mesh_index) + " and collision layer type: " + str(sphere.collision_layer), "load_aloc")
    for mesh_index, capsule in enumerate(aloc.primitive_capsules):
        if collidable_layer(capsule.collision_layer):
            log("TRACE", "Primitive Capsule", "load_aloc")

            loc = mathutils.Vector((capsule.position[0], capsule.position[1], capsule.position[2]))
            if len(capsule.rotation) == 4:
                rot = mathutils.Quaternion(
                    (capsule.rotation[3], capsule.rotation[0], capsule.rotation[1], capsule.rotation[2]))
            else:
                x = capsule.rotation[0]
                y = capsule.rotation[1]
                z = capsule.rotation[2]
                w = math.sqrt(max(0.0, 1.0 - (x * x + y * y + z * z)))
                rot = mathutils.Quaternion((w, x, y, z))

            mat = mathutils.Matrix.LocRotScale(loc, rot, None)

            cylinder_height = capsule.length - 2 * capsule.radius
            if cylinder_height < 0:
                cylinder_height = 0
            z_offset = cylinder_height / 2

            bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=capsule.radius,
                                      matrix=mat @ mathutils.Matrix.Translation((0, 0, z_offset)))
            bmesh.ops.create_cone(bm, cap_ends=False, cap_tris=False, segments=32, radius1=capsule.radius,
                                  radius2=capsule.radius, depth=cylinder_height, matrix=mat)
            bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=capsule.radius,
                                      matrix=mat @ mathutils.Matrix.Translation((0, 0, -z_offset)))
        else:
            log("TRACE", "Skipping Non-collidable ALOC mesh: " + aloc_name + " with mesh index: " + str(
                mesh_index) + " and collision layer type: " + str(capsule.collision_layer), "load_aloc")


def load_aloc(filepath, temp_coll):
    """Imports an ALOC mesh from the given path"""

    aloc_name = bpy.path.display_name_from_filepath(filepath)
    aloc = read_aloc(filepath)
    if aloc == -1:
        log("ERROR", "Failed to read Mesh ALOC file " + filepath, "load_aloc")
        return -1, []
    if aloc.collision_type == PhysicsCollisionType.RIGIDBODY:
        return aloc, []

    log("TRACE", "Converting ALOC: " + aloc_name + " to blender mesh.", aloc_name)

    aloc_obj = create_new_object(aloc_name + "_original_" + str(aloc.collision_type) + "_" + str(aloc.data_type),
                                 aloc.collision_type, aloc.data_type, temp_coll)
    bm = bmesh.new()

    if aloc.data_type == PhysicsDataType.CONVEX_MESH_AND_TRIANGLE_MESH:
        log("TRACE", "Converting Convex Mesh and Triangle Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        load_convex_mesh_objects(aloc, aloc_name, bm)
        load_triangle_mesh_objects(aloc, aloc_name, bm)
    elif aloc.data_type == PhysicsDataType.CONVEX_MESH:
        log("TRACE", "Converting Convex Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        load_convex_mesh_objects(aloc, aloc_name, bm)
    elif aloc.data_type == PhysicsDataType.TRIANGLE_MESH:
        log("TRACE", "Converting Triangle Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        load_triangle_mesh_objects(aloc, aloc_name, bm)
    elif aloc.data_type == PhysicsDataType.PRIMITIVE:
        log("TRACE", "Converting Primitive Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        load_primitive_mesh_objects(aloc, aloc_name, bm)
    elif aloc.data_type == PhysicsDataType.CONVEX_MESH_AND_PRIMITIVE:
        log("TRACE", "Converting Convex Mesh and Primitive Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        load_convex_mesh_objects(aloc, aloc_name, bm)
        load_primitive_mesh_objects(aloc, aloc_name, bm)
    elif aloc.data_type == PhysicsDataType.TRIANGLE_MESH_AND_PRIMITIVE:
        log("TRACE", "Converting Vertex Mesh and Primitive Mesh ALOC " + aloc_name + " to blender mesh", "load_aloc")
        load_triangle_mesh_objects(aloc, aloc_name, bm)
        load_primitive_mesh_objects(aloc, aloc_name, bm)
    else:
        log("ERROR", "Unknown data type: " + str(aloc.data_type) + " for Mesh ALOC " + aloc_name, "load_aloc")
        return -1, []

    finalize_aloc_mesh(bm, aloc_obj)
    log("TRACE", "Finished converting ALOC: " + aloc_name + " to blender mesh.", aloc_name)
    return aloc, [aloc_obj]


texture_to_material_map = {}


def add_texture(obj, path_to_tga_dir, mati_hash, diffuse_hash, normal_hash, specular_hash):
    diffuse_tga_path = os.path.join(path_to_tga_dir, diffuse_hash + ".TGA")
    normal_tga_path = os.path.join(path_to_tga_dir, normal_hash + ".TGA")
    specular_tga_path = os.path.join(path_to_tga_dir, specular_hash + ".TGA")
    if not os.path.exists(diffuse_tga_path):
        log("ERROR", "Cannot add texture, Diffuse texture file not found at: " + str(diffuse_tga_path), "add_textures")
        return
    if not os.path.exists(normal_tga_path):
        log("TRACE", "Normal texture file not found at: " + str(normal_tga_path), "add_textures")
    else:
        normal_tga_path = os.path.abspath(normal_tga_path)
    if not os.path.exists(specular_tga_path):
        log("TRACE", "Specular texture file not found at: " + str(specular_tga_path), "add_textures")
    else:
        specular_tga_path = os.path.abspath(specular_tga_path)
    diffuse_tga_path = os.path.abspath(diffuse_tga_path)
    log("TRACE", "Diffuse TGA filepath: " + diffuse_tga_path, "add_textures")

    if mati_hash in texture_to_material_map:
        mat = texture_to_material_map[mati_hash]
    else:
        material_name = mati_hash + "_Material"
        mat = bpy.data.materials.new(name=material_name)
        mat.use_nodes = True
        
        # Enable transparency (Updated for Blender 4.3 EEVEE Next)
        # Set the master blend_method first
        if hasattr(mat, "blend_method"):
            mat.blend_method = 'HASHED'

        # Set EEVEE Next specific properties
        if hasattr(mat, "eevee"):
            mat.eevee.transparent_render_method = 'DITHERED'
            # Ensure shadows also respect the alpha mask
            mat.eevee.shadow_method = 'HASHED'
        elif hasattr(mat, "shadow_method"):
            mat.shadow_method = 'HASHED'

        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        nodes.clear()
        node_output = nodes.new(type='ShaderNodeOutputMaterial')
        node_output.location = (700, 0)

        # Select Node Type based on Blender Version (Specular BSDF removed in 4.3)
        is_v40_plus = bpy.app.version >= (4, 0, 0)
        is_v43_plus = bpy.app.version >= (4, 3, 0)
        bsdf_type = 'ShaderNodeBsdfPrincipled' if is_v43_plus else 'ShaderNodeBsdfSpecular'
        invert_type = 'ShaderNodeInvert' if is_v43_plus else 'ShaderNodeInvertColor'

        node_spec_bsdf = nodes.new(type=bsdf_type)
        node_spec_bsdf.name = "Specular BSDF"
        node_spec_bsdf.location = (400, 0)
        links.new(node_spec_bsdf.outputs['BSDF'], node_output.inputs['Surface'])

        # Diffuse Setup
        node_diffuse = nodes.new(type='ShaderNodeTexImage')
        node_diffuse.name = "Diffuse"
        node_diffuse.location = (-400, 350)
        img_diff = bpy.data.images.load(diffuse_tga_path)
        img_diff.name = diffuse_hash
        img_diff.alpha_mode = 'STRAIGHT'
        node_diffuse.image = img_diff

        links.new(node_diffuse.outputs['Color'], node_spec_bsdf.inputs['Base Color'])

        # Transparency/Alpha logic
        if is_v43_plus:
            # Principled BSDF uses 'Alpha' directly (equivalent to Invert -> Transparency)
            links.new(node_diffuse.outputs['Alpha'], node_spec_bsdf.inputs['Alpha'])
        else:
            # Legacy Specular BSDF uses 'Transparency' (Inverted Alpha)
            node_invert_transp = nodes.new(type=invert_type)
            node_invert_transp.location = (50, 300)
            links.new(node_diffuse.outputs['Alpha'], node_invert_transp.inputs['Color'])
            links.new(node_invert_transp.outputs['Color'], node_spec_bsdf.inputs['Transparency'])

        # Specular Setup
        if os.path.exists(specular_tga_path):
            node_spec_tex = nodes.new(type='ShaderNodeTexImage')
            node_spec_tex.name = "Specular"
            node_spec_tex.location = (-400, 0)
            img_spec = bpy.data.images.load(specular_tga_path)
            img_spec.name = specular_hash
            node_spec_tex.image = img_spec
            try:
                node_spec_tex.image.colorspace_settings.name = 'Non-Color'
            except:
                pass

            node_invert_rough = nodes.new(type=invert_type)
            node_invert_rough.location = (50, -50)

            spec_socket = 'Specular Tint' if is_v43_plus else 'Specular'
            links.new(node_spec_tex.outputs['Color'], node_spec_bsdf.inputs[spec_socket])
            links.new(node_spec_tex.outputs['Alpha'], node_invert_rough.inputs['Color'])
            links.new(node_invert_rough.outputs['Color'], node_spec_bsdf.inputs['Roughness'])

        # Normal Setup
        if os.path.exists(normal_tga_path):
            node_norm_tex = nodes.new(type='ShaderNodeTexImage')
            node_norm_tex.name = "Normal"
            node_norm_tex.location = (-400, -350)
            img_norm = bpy.data.images.load(normal_tga_path)
            img_norm.name = normal_hash
            node_norm_tex.image = img_norm
            try:
                node_norm_tex.image.colorspace_settings.name = 'Non-Color'
            except:
                pass

            node_norm_map = nodes.new(type='ShaderNodeNormalMap')
            node_norm_map.location = (50, -350)

            links.new(node_norm_tex.outputs['Color'], node_norm_map.inputs['Color'])
            links.new(node_norm_map.outputs['Normal'], node_spec_bsdf.inputs['Normal'])

        texture_to_material_map[mati_hash] = mat

    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)

    log("TRACE", "Texture " + mati_hash + " applied to " + obj.name, "add_textures")


def get_local_space_bbox_center(obj):
    return 0.125 * sum((mathutils.Vector(ls_corner) for ls_corner in obj.bound_box), mathutils.Vector())


def get_bounding_sphere_data(obj, ws_mat, lsbbox_center=None):
    if lsbbox_center is None:
        lsbbox_center = get_local_space_bbox_center(obj)
    wsbbox_center = ws_mat @ lsbbox_center
    radius = max(
        (mathutils.Vector(ws_mat @ mathutils.Vector(ls_corner)) - wsbbox_center).xy for ls_corner in obj.bound_box)
    return wsbbox_center, radius.length


def create_volume(name, col_name, pos, rot, scale, shape='CUBE'):
    o = bpy.data.objects.new(name, None)
    bpy.data.collections.get(col_name).objects.link(o)
    o.empty_display_type = shape
    o.empty_display_size = 0.5
    o.location = pos
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = rot
    o.scale = scale
    o.show_name = True  # Good idea to show names? Or makes viewport too busy?


def load_volume_boxes(json_data, volume_types):
    volume_boxes_coll = bpy.data.collections.new("Volume boxes")
    bpy.context.scene.collection.children.link(volume_boxes_coll)
    log("INFO", "Loading volume boxes for types:", "glacier2glb")
    for volt in volume_types:
        log("INFO", "   " + volt, "glacier2glb")

    for volt in volume_types:
        log("INFO", "Creating " + volt + " collection.", "glacier2glb")
        if volt == "gates":
            if volt not in bpy.data.collections:
                gates_coll = bpy.data.collections.new(volt)
                volume_boxes_coll.children.link(gates_coll)
            else:
                gates_coll = bpy.data.collections.get(volt)
            for vol in json_data[volt]:
                coords = ["w", "x", "y", "z"]
                pos, rot, scale, center = [
                    mathutils.Vector([vol["position"][coord] for coord in coords[1:]]),
                    mathutils.Quaternion([vol["rotation"][coord] for coord in coords]),
                    mathutils.Vector([vol["bboxHalfSize"][coord] for coord in coords[1:]]) * 2,
                    mathutils.Vector([vol["bboxCenter"][coord] for coord in coords[1:]])
                ]
                pos += center
                create_volume(vol["name"], gates_coll.name, pos, rot, scale)
        elif volt == "rooms":
            if volt not in bpy.data.collections:
                rooms_coll = bpy.data.collections.new(volt)
                volume_boxes_coll.children.link(rooms_coll)
            else:
                rooms_coll = bpy.data.collections.get(volt)
            for vol in json_data[volt]:
                coords = ["w", "x", "y", "z"]
                pos, rot = [
                    mathutils.Vector([vol["position"][coord] for coord in coords[1:]]),
                    mathutils.Quaternion([vol["rotation"][coord] for coord in coords]),
                ]
                scale = mathutils.Vector(
                    [vol["roomExtentMax"]["data"][coord] for coord in coords[1:]]) - mathutils.Vector(
                    [vol["roomExtentMin"]["data"][coord] for coord in coords[1:]])
                center = (mathutils.Vector(
                    [vol["roomExtentMax"]["data"][coord] for coord in coords[1:]]) + mathutils.Vector(
                    [vol["roomExtentMin"]["data"][coord] for coord in coords[1:]])) / 2
                pos += center
                create_volume(vol["name"], rooms_coll.name, pos, rot, scale)
        elif volt == "mapLevel":
            log("INFO", "Handling mapLevel: " + volt, "glacier2glb")
            if volt not in bpy.data.collections:
                map_level_coll = bpy.data.collections.new(volt)
                volume_boxes_coll.children.link(map_level_coll)
                log("INFO", "Creating collection: " + volt, "glacier2glb")
            else:
                log("INFO", "Collection " + volt + " already exists", "glacier2glb")
                map_level_coll = bpy.data.collections.get(volt)

            map_level_volume_box_ids = []
            log("INFO", "Getting mapSpaceConfigs.", "glacier2glb")
            for map_space_config in json_data["mapSpaceConfigs"]:
                log("INFO", "Found a mapSpaceConfig with name: " + map_space_config["name"] + " and id: " + map_space_config["id"], "glacier2glb")
                for map_level_volume_box_id in map_space_config["inclusions"]["data"]:
                    map_level_volume_box_ids.append(map_level_volume_box_id)
            for vol in json_data["volumeBoxes"]:
                if vol["id"] in map_level_volume_box_ids:
                    log("INFO", "Found a map level volume with name: " + vol["name"] + " and id: " + vol["id"], "glacier2glb")
                    coords = ["w", "x", "y", "z"]
                    pos, rot, scale = [
                        mathutils.Vector([vol["position"][coord] for coord in coords[1:]]),
                        mathutils.Quaternion([vol["rotation"][coord] for coord in coords]),
                        mathutils.Vector([vol["scale"]["data"][coord] for coord in coords[1:]]),
                    ]
                    create_volume(vol["name"], map_level_coll.name, pos, rot, scale)
        elif volt == "aiArea":
            # Blender collection names are display labels, not entity identities.
            # Different logical parents can have the same name (or be truncated
            # to the same Blender name), so keep their IDs when resolving them.
            parent_collections = {}
            for ai_area_world in json_data["aiAreaWorld"]:
                area_world_coll = bpy.data.collections.new(ai_area_world["name"])
                volume_boxes_coll.children.link(area_world_coll)
                parent_collections[ai_area_world["id"]] = area_world_coll
            # Go through the volumes and prepare the hashmap
            id_to_trigger_volume = {}
            for vol in json_data["volumeBoxes"]:
                id_to_trigger_volume[vol["id"]] = vol
            for vol in json_data["volumeSpheres"]:
                id_to_trigger_volume[vol["id"]] = vol
            for ai_area in json_data[volt]:
                area_coll = bpy.data.collections.new(ai_area["name"])
                prev_coll = area_coll
                for parent_reference in ai_area["logicalParent"]:
                    parent_coll_name, separator, parent_id = parent_reference.rpartition(" (")
                    if separator and parent_id.endswith(")"):
                        parent_id = parent_id[:-1]
                    else:
                        parent_coll_name = parent_reference
                        parent_id = parent_reference
                    parent_coll = parent_collections.get(parent_id)
                    if parent_coll is None:
                        parent_coll = bpy.data.collections.new(parent_coll_name)
                        parent_collections[parent_id] = parent_coll
                    if prev_coll.name not in parent_coll.children:
                        parent_coll.children.link(prev_coll)
                    prev_coll = parent_coll
                for vol_name in ai_area['areaVolumeNames']:
                    id = vol_name.split("(")[1].split(")")[0]  # name ex: VolumeBox_AIArea_02 (729b0d862bef84a2)
                    vol = id_to_trigger_volume[id]
                    coords = ["w", "x", "y", "z"]
                    pos, rot = [
                        mathutils.Vector([vol["position"][coord] for coord in coords[1:]]),
                        mathutils.Quaternion([vol["rotation"][coord] for coord in coords])
                    ]
                    if "radius" in vol:  # sphere volume
                        try:
                            scale = mathutils.Vector([vol["radius"]["data"] for _ in range(3)])
                        except TypeError:
                            log("WARNING",
                                "Problem reading sphere scale for id: " + id + " value: " + vol["radius"]["data"],
                                "load_volume_boxes")
                            scale = mathutils.Vector([1, 1, 1])
                        create_volume(vol_name, area_coll.name, pos, rot, scale, 'SPHERE')
                    else:  # box volume
                        scale = mathutils.Vector([vol["scale"]["data"][coord] for coord in coords[1:]])
                        create_volume(vol_name, area_coll.name, pos, rot, scale)


# The official map palette: the canvas (also water), floors not on the shown level, outdoor ground, indoor
# floors, walls and solid objects, and stairs.
GFX_BACKGROUND_COLOR = "#1c1e20"
GFX_OTHER_LEVEL_COLOR = "#363a3d"
GFX_EXTERIOR_FLOOR_COLOR = "#bec3c6"
GFX_INTERIOR_FLOOR_COLOR = "#a7adb2"
GFX_WALL_COLOR = "#767f86"
GFX_STAIRS_COLOR = "#d5d8da"
# Like the official map GFX files, the background is drawn as four quadrants with hairline gaps between them,
# which show as a crosshair through the middle of the map.
GFX_CROSSHAIR_GAP = 0.25
# Map layers use one SVG pixel per metre, which matches the scale of the official map GFX files.
# Like the official maps, walls are solid shapes: the cross-section of each wall, grown by this much on every
# side so that thin walls still read, and without the pieces smaller than GFX_MINIMUM_WALL_AREA square metres
# (columns, posts and railings).
GFX_WALL_GROWTH = 0.1
GFX_LINE_WALL_WIDTH = 0.7
GFX_MINIMUM_WALL_AREA = 1.0
# Low objects are drawn as solid blocks, cut halfway up, when their cross-section covers at least this many square
# metres (garden beds, fountains and containers, not boxes and crates).
GFX_MINIMUM_BLOCK_AREA = 6.0
GFX_MINIMUM_BLOCK_HEIGHT = 0.3
# Cross-sections are filled when they are solid (no thicker on average than this many metres, which covers walls
# and blocks); hollow outlines, as of one-sided collision meshes around a room, are drawn as lines.
GFX_MAXIMUM_SOLID_THICKNESS = 4.0
# Outlines are simplified by this many metres (floors and walls), like the clean shapes of the official maps.
GFX_FLOOR_TOLERANCE = 0.25
GFX_WALL_TOLERANCE = 0.15
# Objects whose entities are mostly plants are left out, as the official maps show no vegetation.
GFX_VEGETATION_TOKENS = ("plant", "palm", "bamboo", "bush", "banyan", "tree", "flower", "hedge", "grass", "shrub",
                         "foliage", "vegetation", "fern", "ivy", "leaves", "vine", "reed", "cactus", "boxwood",
                         "plane_bundle")
# Rocks and cliffs (such as the boulders along a river bank) are drawn as ground off the shown level, like the
# band along the river of the official Bangkok map, rather than as walls and floors.
GFX_ROCK_TOKENS = ("rock", "cliff", "boulder")
# Vehicles are left out like the official maps do.
GFX_VEHICLE_TOKENS = ("vehicle", "sedan", "_car_", "car_", "van_", "truck", "_bus_", "wheel", "motorbike", "scooter",
                      "bicycle", "boat", "forklift")
# Indoors, the official maps draw the walls that bound rooms, not the furniture in them: a cut object is kept when
# at least GFX_ROOM_WALL_SHARE of its outline lies within GFX_ROOM_WALL_DISTANCE metres of a room's boundary.
# Outdoors, an object needs an outline at least GFX_MINIMUM_OUTDOOR_OUTLINE metres long (a wall or a building,
# not a parasol or a trunk).
GFX_ROOM_WALL_DISTANCE = 1.0
GFX_ROOM_WALL_SHARE = 0.3
GFX_MINIMUM_OUTDOOR_OUTLINE = 3.0
# Floors on other levels are drawn as smoothed areas of at least this many square metres.
GFX_MINIMUM_OTHER_LEVEL_AREA = 25.0
GFX_OTHER_LEVEL_SMOOTHING = 2.0
# Land and water: the official maps paint the whole canvas as ground except for water. Water is found on a grid of
# GFX_LAND_RESOLUTION metres as the gaps wider than GFX_WATER_MINIMUM_WIDTH between the positions of the scene's
# objects (all of them, including those culled from the build) that lie within the area the objects enclose
# (gaps narrower than GFX_WATER_ENCLOSURE). Only the bodies of water at least GFX_WATER_KEEP_FRACTION as large as
# the largest are kept, which drops the gaps of thinly furnished ground. These settings recover the river of the
# official Bangkok map.
GFX_LAND_RESOLUTION = 1.0
GFX_WATER_MINIMUM_WIDTH = 64.0
GFX_WATER_ENCLOSURE = 400.0
GFX_WATER_KEEP_FRACTION = 0.5
GFX_MINIMUM_WATER_AREA = 4000.0
# Like the ring around the official silo map, the shell ledge within this many metres of the land is drawn as a
# band of indoor floor colour.
GFX_SHELL_BAND = 4.0
# Walls and props are drawn where they cross a horizontal plane this far above the floor they stand on.
GFX_SECTION_HEIGHT = 1.2
# Like the official maps, only walls are drawn: props whose footprint is smaller than this, or whose top is
# less than this high above the floor they stand on (boxes, crates, furniture, leaning panels), are left out.
GFX_MINIMUM_SECTION_FOOTPRINT = 0.75
GFX_MINIMUM_WALL_HEIGHT = 2.6
# Wall segments are only drawn where they border visible floor, so the far side of thick or enclosing
# structures (such as the outer face of a silo wall) does not show.
GFX_WALL_FLOOR_DISTANCE = 0.75
# Objects at least this tall that rise above the top of every map layer are the shell of the level (silo
# walls, cliffs). Their upward faces are ledges of the shell, not floors: they hide the floors below them
# instead of being drawn.
GFX_SHELL_MINIMUM_HEIGHT = 8.0
# Patches of floor smaller than this many square metres that touch no other floor (such as the tops of props
# standing on a shell ledge) are left out.
GFX_MINIMUM_FLOOR_ISLAND = 10.0
# Patches of outdoor floor smaller than this many square metres with indoor floor all around are drawn as
# indoor floor: they are gaps between the room volumes of a building.
GFX_MAXIMUM_ENCLOSED_EXTERIOR = 40.0
# A run of at least this many treads, each rising by GFX_STAIR_RISE and going forward by GFX_STAIR_GOING
# (centre to centre, so a landing between flights still counts) and at least GFX_STAIR_MINIMUM_WIDTH wide,
# rising GFX_STAIR_MINIMUM_SPAN in total, is drawn as stairs.
GFX_STAIR_MINIMUM_TREADS = 5
GFX_STAIR_RISE = (0.08, 0.35)
GFX_STAIR_GOING = (0.1, 1.6)
GFX_STAIR_MINIMUM_WIDTH = 0.6
GFX_STAIR_MINIMUM_SPAN = 0.9
# Floors are merged on a grid of this many metres (or coarser for very large maps) before they are
# traced back to polygons, so that overlapping meshes become one clean outline per colour.
GFX_FLOOR_RESOLUTION = 0.1
GFX_MAXIMUM_FLOOR_GRID = 4096

GFX_FLOOR_NONE = 0
GFX_FLOOR_EXTERIOR = 1
GFX_FLOOR_INTERIOR = 2
GFX_FLOOR_STAIRS = 3
# Classified per map cell from the rooms of the level.
GFX_FLOOR_BY_ROOM = 4
# Hides the floors below it and draws nothing.
GFX_FLOOR_HIDDEN = 5
# A floor that is only on another level (above the shown one, or below every map layer).
GFX_FLOOR_OTHER_LEVEL = 6
GFX_FLOOR_WALL = 7


def is_background_map_space_config(map_space_config):
    """The background (satellite) map space: named "...Background" or "..._SAT" in the game's scenes."""
    name = map_space_config.get("name", "").lower()
    return "background" in name or name == "sat" or name.endswith("_sat")


def rasterize_triangles(triangles, priorities, origin, resolution, shape):
    """Return, for each grid cell, the index of the highest-priority triangle covering its centre (or -1)."""
    height, width = shape
    best = np.full(height * width, -1, dtype=np.int64)
    if len(triangles) == 0:
        return best.reshape(shape)
    cells = (triangles - origin) / resolution - 0.5  # cell (i, j) has its centre at (i, j) in this space
    x0 = np.clip(np.ceil(cells[:, :, 0].min(axis=1)), 0, width).astype(np.int64)
    x1 = np.clip(np.floor(cells[:, :, 0].max(axis=1)) + 1, 0, width).astype(np.int64)
    y0 = np.clip(np.ceil(cells[:, :, 1].min(axis=1)), 0, height).astype(np.int64)
    y1 = np.clip(np.floor(cells[:, :, 1].max(axis=1)) + 1, 0, height).astype(np.int64)
    counts = np.maximum(x1 - x0, 0) * np.maximum(y1 - y0, 0)
    order = np.argsort(priorities, kind="stable")
    rank = np.empty(len(order), dtype=np.int64)
    rank[order] = np.arange(len(order))

    a, b, c = cells[:, 0], cells[:, 1], cells[:, 2]
    area = (b[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1]) - (b[:, 1] - a[:, 1]) * (c[:, 0] - a[:, 0])
    candidates = np.nonzero((counts > 0) & (np.abs(area) > 1e-12))[0]
    chunk_limit = 4_000_000
    start = 0
    while start < len(candidates):
        total = 0
        end = start
        while end < len(candidates) and (total == 0 or total + counts[candidates[end]] <= chunk_limit):
            total += counts[candidates[end]]
            end += 1
        chunk = candidates[start:end]
        start = end
        chunk_counts = counts[chunk]
        owner = np.repeat(chunk, chunk_counts)
        offsets = np.arange(len(owner)) - np.repeat(np.cumsum(chunk_counts) - chunk_counts, chunk_counts)
        box_width = (x1 - x0)[owner]
        px = x0[owner] + offsets % box_width
        py = y0[owner] + offsets // box_width
        sign = np.sign(area[owner])
        inside = np.ones(len(owner), dtype=bool)
        for p, q in ((a, b), (b, c), (c, a)):
            edge = ((q[owner, 0] - p[owner, 0]) * (py - p[owner, 1]) -
                    (q[owner, 1] - p[owner, 1]) * (px - p[owner, 0])) * sign
            inside &= edge >= 0
        np.maximum.at(best, py[inside] * width + px[inside], rank[owner[inside]])
    covered = best >= 0
    best[covered] = order[best[covered]]
    return best.reshape(shape)


def fill_sections(sections, origin, resolution, shape, maximum_half_thickness):
    """Fill the cross-sections of closed objects on a grid by the non-zero winding rule.

    `sections` holds one (N, 2, 2) array of oriented segments per object, in the grid's coordinates (cell (i, j)
    has its centre at origin + ((j, i) + 0.5) * resolution). Each object is filled on its own, inside its own
    bounding box, and a row whose crossings do not balance (an open mesh) is not filled, so no object can spill
    across the map. Collision meshes are often one-sided surfaces rather than solids, which would fill whole
    rooms, so an object is only filled when its area is at most `maximum_half_thickness` (in the grid's units)
    times its outline's length, as for walls and blocks. Returns the filled interiors and, separately, the
    segments drawn as lines.
    """
    filled = np.zeros(shape, dtype=bool)
    lines = np.zeros(shape, dtype=bool)
    height, width = shape
    for segments in sections:
        if len(segments) == 0:
            continue
        cells = (segments - origin) / resolution - 0.5
        x0 = int(np.floor(cells[:, :, 0].min())); x1 = int(np.ceil(cells[:, :, 0].max())) + 1
        y0 = int(np.floor(cells[:, :, 1].min())); y1 = int(np.ceil(cells[:, :, 1].max())) + 1
        if x1 <= 0 or y1 <= 0 or x0 >= width or y0 >= height:
            continue
        box_w, box_h = x1 - x0 + 1, y1 - y0 + 1
        start, end = cells[:, 0] - (x0, y0), cells[:, 1] - (x0, y0)
        low_y, high_y = np.minimum(start[:, 1], end[:, 1]), np.maximum(start[:, 1], end[:, 1])
        first_row = np.ceil(low_y).astype(np.int64)
        last_row = np.ceil(high_y).astype(np.int64) - 1  # rows whose centre lies in [low_y, high_y)
        counts = np.maximum(last_row - first_row + 1, 0)
        winding = np.zeros((box_h, box_w + 1), dtype=np.int32)
        if counts.sum():
            owner = np.repeat(np.arange(len(segments)), counts)
            rows = first_row[owner] + np.arange(int(counts.sum())) - np.repeat(np.cumsum(counts) - counts, counts)
            t = (rows - start[owner, 1]) / (end[owner, 1] - start[owner, 1])
            crossing_x = start[owner, 0] + t * (end[owner, 0] - start[owner, 0])
            columns = np.clip(np.ceil(crossing_x), 0, box_w).astype(np.int64)
            signs = np.where(end[owner, 1] > start[owner, 1], 1, -1)
            np.add.at(winding, (rows, columns), signs)
        balanced = winding.sum(axis=1) == 0
        inside = (np.cumsum(winding, axis=1)[:, :box_w] != 0) & balanced[:, None]
        lengths = np.linalg.norm(end - start, axis=1)
        if np.count_nonzero(inside) > maximum_half_thickness / resolution * lengths.sum():
            inside[:] = False
        # Draw the segments as well, sampled every half cell.
        samples = np.maximum(np.ceil(lengths * 2).astype(np.int64), 1) + 1
        owner = np.repeat(np.arange(len(segments)), samples)
        fraction = (np.arange(int(samples.sum())) - np.repeat(np.cumsum(samples) - samples, samples)) / \
            np.repeat(samples - 1, samples).clip(min=1)
        points = start[owner] + (end[owner] - start[owner]) * fraction[:, None]
        px = np.clip(np.round(points[:, 0]).astype(np.int64), 0, box_w - 1)
        py = np.clip(np.round(points[:, 1]).astype(np.int64), 0, box_h - 1)
        drawn = np.zeros_like(inside)
        drawn[py, px] = True
        gy0, gx0 = max(y0, 0), max(x0, 0)
        gy1, gx1 = min(y0 + box_h, height), min(x0 + box_w, width)
        filled[gy0:gy1, gx0:gx1] |= inside[gy0 - y0:gy1 - y0, gx0 - x0:gx1 - x0]
        lines[gy0:gy1, gx0:gx1] |= drawn[gy0 - y0:gy1 - y0, gx0 - x0:gx1 - x0]
    return filled, lines


def label_regions(mask):
    """Label the 4-connected regions of a boolean grid: (labels with -1 outside the mask, cells per label)."""
    labels = np.full(mask.shape, -1, dtype=np.int64)
    padded = np.pad(mask, ((0, 0), (1, 1)))
    changes = np.diff(padded.astype(np.int8), axis=1)
    run_rows, run_starts = np.nonzero(changes == 1)
    _, run_ends = np.nonzero(changes == -1)  # exclusive; nonzero is row-major, so the runs pair up in order
    if len(run_rows) == 0:
        return labels, np.empty(0)
    parents = list(range(len(run_rows)))

    def find(run):
        while parents[run] != run:
            parents[run] = parents[parents[run]]
            run = parents[run]
        return run

    row_bounds = np.searchsorted(run_rows, np.arange(mask.shape[0] + 1)).tolist()
    starts, ends = run_starts.tolist(), run_ends.tolist()
    for row in range(mask.shape[0] - 1):
        a, a_end = row_bounds[row], row_bounds[row + 1]
        b, b_end = a_end, row_bounds[row + 2]
        while a < a_end and b < b_end:
            if starts[a] < ends[b] and starts[b] < ends[a]:
                root_a, root_b = find(a), find(b)
                if root_a != root_b:
                    parents[root_b] = root_a
            if ends[a] < ends[b]:
                a += 1
            else:
                b += 1
    _, run_labels = np.unique([find(run) for run in range(len(parents))], return_inverse=True)
    lengths = run_ends - run_starts
    run_of_cell = np.repeat(np.arange(len(run_rows)), lengths)
    columns = np.arange(int(lengths.sum())) - np.repeat(np.cumsum(lengths) - lengths, lengths) +         np.repeat(run_starts, lengths)
    labels[run_rows[run_of_cell], columns] = run_labels[run_of_cell]
    return labels, np.bincount(run_labels, weights=lengths)


def remove_small_islands(mask, minimum_cells):
    """Clear the 4-connected regions of a boolean grid that have fewer than `minimum_cells` cells."""
    labels, sizes = label_regions(mask)
    return mask & (labels >= 0) & (sizes[np.maximum(labels, 0)] >= minimum_cells) if len(sizes) else mask


def trace_mask(mask, minimum_cells=4.0, tolerance=0.75):
    """Trace a boolean grid into closed rings (in cell units, y down) that fill the mask with even-odd."""
    padded = np.pad(mask, 1)
    inside = padded[1:-1, 1:-1]
    edges = []
    # Each boundary edge is oriented clockwise around the inside cells (in y-down grid coordinates).
    ys, xs = np.nonzero(inside & ~padded[:-2, 1:-1])
    edges.append(np.stack((xs, ys, xs + 1, ys), axis=1))
    ys, xs = np.nonzero(inside & ~padded[1:-1, 2:])
    edges.append(np.stack((xs + 1, ys, xs + 1, ys + 1), axis=1))
    ys, xs = np.nonzero(inside & ~padded[2:, 1:-1])
    edges.append(np.stack((xs + 1, ys + 1, xs, ys + 1), axis=1))
    ys, xs = np.nonzero(inside & ~padded[1:-1, :-2])
    edges.append(np.stack((xs, ys + 1, xs, ys), axis=1))
    edges = np.concatenate(edges)
    if len(edges) == 0:
        return []
    outgoing = {}
    for index, (sx, sy, _, _) in enumerate(edges.tolist()):
        outgoing.setdefault((sx, sy), []).append(index)
    used = np.zeros(len(edges), dtype=bool)
    edge_list = edges.tolist()
    rings = []
    for first in range(len(edge_list)):
        if used[first]:
            continue
        ring = []
        current = first
        while not used[current]:
            used[current] = True
            sx, sy, ex, ey = edge_list[current]
            ring.append((sx, sy))
            options = [option for option in outgoing[(ex, ey)] if not used[option]]
            if not options:
                break
            if len(options) > 1:
                # At a corner shared by two diagonal cells, turn right to keep the regions separate.
                dx, dy = ex - sx, ey - sy
                options.sort(key=lambda option: (edge_list[option][2] - ex) * dy -
                             (edge_list[option][3] - ey) * dx)
            current = options[0]
        if len(ring) >= 4:
            rings.append(ring)

    simplified = []
    for ring in rings:
        points = np.array(ring, dtype=np.float64)
        area = 0.5 * np.sum(points[:, 0] * np.roll(points[:, 1], -1) - np.roll(points[:, 0], -1) * points[:, 1])
        if abs(area) < minimum_cells:
            continue
        points = simplify_ring(points, tolerance)
        if len(points) >= 3:
            simplified.append(points)
    return simplified


def simplify_ring(points, tolerance):
    """Remove collinear points, then apply Douglas-Peucker to the closed ring."""
    direction = np.roll(points, -1, axis=0) - points
    previous = np.roll(direction, 1, axis=0)
    turning = (direction[:, 0] * previous[:, 1] - direction[:, 1] * previous[:, 0]) != 0
    points = points[turning]
    if len(points) <= 4:
        return points
    distances = np.linalg.norm(points - points[0], axis=1)
    split = int(np.argmax(distances))
    keep = np.zeros(len(points), dtype=bool)
    keep[0] = keep[split] = True
    closed = np.vstack((points, points[:1]))
    stack = [(0, split), (split, len(points))]
    while stack:
        start, end = stack.pop()
        if end - start < 2:
            continue
        a, b = closed[start], closed[end]
        segment = b - a
        length = np.hypot(*segment)
        between = closed[start + 1:end]
        if length < 1e-12:
            offsets = np.linalg.norm(between - a, axis=1)
        else:
            offsets = np.abs(segment[0] * (between[:, 1] - a[1]) - segment[1] * (between[:, 0] - a[0])) / length
        index = int(np.argmax(offsets))
        if offsets[index] > tolerance:
            middle = start + 1 + index
            keep[middle % len(points)] = True
            stack.append((start, middle))
            stack.append((middle, end))
    return points[keep]


def render_map_layers_for_gfx(json_data, output_path):
    """Write one SVG floor plan per map space config, plus a shared background, for the GFX builder.

    The slice SVGs are written as output_NN.svg, numbered in the order of the non-background map space
    configs. The background config (if any) is written as output_background.svg and also defines the
    extent of the map canvas; like the official maps, its fill leaves a crosshair of hairline gaps
    through the middle of the canvas. One SVG unit is one metre.
    """
    volumes = {volume["id"]: volume for volume in json_data.get("volumeBoxes", [])}

    def find_volume(volume_id):
        volume = volumes.get(volume_id)
        if volume is not None:
            return volume
        # Some exports drop the leading digit of entity IDs in references; accept an unambiguous match.
        matches = [volume for candidate_id, volume in volumes.items()
                   if len(candidate_id) > len(volume_id) and candidate_id.endswith(volume_id)]
        if len(matches) == 1:
            log("WARNING", "Map space volume " + volume_id + " matched by suffix to " + matches[0]["id"],
                "render_map_layers_for_gfx")
            return matches[0]
        log("WARNING", "Map space volume not found: " + volume_id, "render_map_layers_for_gfx")
        return None

    def oriented_box(position, rotation, minimum, maximum):
        """An oriented box as (world-to-local 4x4, local min, local max, world AABB min, world AABB max)."""
        rotation = rotation.to_matrix().to_4x4()
        matrix = np.array(mathutils.Matrix.Translation(position) @ rotation)
        corners = np.array([(x, y, z, 1.0) for x in (minimum[0], maximum[0]) for y in (minimum[1], maximum[1])
                            for z in (minimum[2], maximum[2])]) @ matrix.T
        return (np.linalg.inv(matrix), np.array(minimum), np.array(maximum),
                corners[:, :3].min(axis=0), corners[:, :3].max(axis=0))

    def volume_box(volume):
        position = mathutils.Vector([volume["position"][axis] for axis in ("x", "y", "z")])
        rotation = mathutils.Quaternion([volume["rotation"][axis] for axis in ("w", "x", "y", "z")])
        half = np.array([abs(volume["scale"]["data"][axis]) / 2 for axis in ("x", "y", "z")])
        return oriented_box(position, rotation, -half, half)

    def boxes_contain(boxes, points, tolerance=(0.0, 0.0, 0.0), upper_tolerance=None):
        inside = np.zeros(len(points), dtype=bool)
        if len(points) == 0:
            return inside
        homogeneous = np.hstack((points, np.ones((len(points), 1))))
        lower_tolerance = np.array(tolerance)
        upper_tolerance = lower_tolerance if upper_tolerance is None else np.array(upper_tolerance)
        for inverse, minimum, maximum, _, _ in boxes:
            local = (homogeneous @ inverse.T)[:, :3]
            inside |= np.all((local >= minimum - lower_tolerance) & (local <= maximum + upper_tolerance), axis=1)
        return inside

    class Slice:
        def __init__(self, boxes):
            self.boxes = boxes
            self.floor_triangles = []
            self.floor_elevations = []
            self.floor_kinds = []
            # The kind each floor has when it is seen from a higher slice, where stairs are plain floor.
            self.floor_base_kinds = []
            self.section_candidates = []  # (instance min z, instance max z, exterior by name, world triangles)
            self.levels = []
            # One (oriented segments, outdoors) pair per object cut by the section plane.
            self.wall_sections = []
            # The slice's place among the slices that share out the canvas by height (see band_owner).
            self.band_index = -1

        def overlaps(self, lower, upper):
            if any(np.all(lower <= box_max) and np.all(upper >= box_min)
                   for _, _, _, box_min, box_max in self.boxes):
                return True
            if self.band_index < 0:
                return False
            low, high = self.z_range()
            return bool(np.all(lower[:2] <= canvas_max) and np.all(upper[:2] >= canvas_min) and
                        lower[2] <= high and upper[2] >= low)

        def contains(self, points, owner=None):
            inside = boxes_contain(self.boxes, points)
            if self.band_index >= 0:
                inside |= (band_owner(points) if owner is None else owner) == self.band_index
            return inside

        def z_range(self):
            return (min(box[3][2] for box in self.boxes), max(box[4][2] for box in self.boxes))

        def floors(self, base_kinds=False):
            if not self.floor_triangles:
                return np.empty((0, 3, 2)), np.empty(0), np.empty(0, dtype=np.int64)
            return (np.concatenate(self.floor_triangles), np.concatenate(self.floor_elevations),
                    np.concatenate(self.floor_base_kinds if base_kinds else self.floor_kinds))

    slices = []
    background = Slice([])
    for map_space_config in json_data.get("mapSpaceConfigs", []):
        boxes = [volume_box(volume) for volume in map(find_volume, map_space_config["inclusions"]["data"])
                 if volume is not None]
        if is_background_map_space_config(map_space_config):
            background.boxes.extend(boxes)
        else:
            slices.append(Slice(boxes))
    all_slices = [layer for layer in slices + [background] if layer.boxes]

    canvas_boxes = background.boxes or [box for layer in slices for box in layer.boxes]
    if canvas_boxes:
        canvas_min = np.min([box[3] for box in canvas_boxes], axis=0)[:2]
        canvas_max = np.max([box[4] for box in canvas_boxes], axis=0)[:2]
    else:
        canvas_min, canvas_max = np.array([-50.0, -50.0]), np.array([50.0, 50.0])
    canvas_max = np.maximum(canvas_max, canvas_min + 1.0)
    width, height = (float(value) for value in canvas_max - canvas_min)

    # The official maps' slice volumes span the whole map, so that they only split it by height. Where a
    # scene's volumes leave part of the canvas out (a slice volume around just one area), what lies there
    # goes to the slice whose height band holds it, so that a floor still shows on its own level.
    band_slices = [layer for layer in slices if layer.boxes]
    for band_index, layer in enumerate(band_slices):
        layer.band_index = band_index

    def band_owner(points):
        """For each point, the band index of the slice it falls to by height alone: -1 when it is inside any
        slice's own volumes (or off the canvas, or in no slice's height band). Where bands overlap, the slice
        whose band is centred nearest takes it."""
        owner = np.full(len(points), -1, dtype=np.int64)
        if len(points) == 0 or not band_slices:
            return owner
        free = np.all((points[:, :2] >= canvas_min) & (points[:, :2] <= canvas_max), axis=1)
        for layer in band_slices:
            free[free] &= ~boxes_contain(layer.boxes, points[free])
        best = np.full(len(points), np.inf)
        for layer in band_slices:
            low, high = layer.z_range()
            distance = np.abs(points[:, 2] - (low + high) / 2)
            better = free & (points[:, 2] >= low) & (points[:, 2] <= high) & (distance < best)
            owner[better] = layer.band_index
            best[better] = distance[better]
        return owner
    map_top = max((layer.z_range()[1] for layer in slices if layer.boxes), default=np.inf)

    interior_tokens = ("indoor", "interior", "inside")
    exterior_tokens = ("outdoor", "outside", "exterior", "terrain")

    indoor_rooms = []
    for room in json_data.get("rooms", []):
        if any(token in room.get("name", "").lower() for token in exterior_tokens):
            continue
        try:
            indoor_rooms.append(oriented_box(
                mathutils.Vector([room["position"][axis] for axis in ("x", "y", "z")]),
                mathutils.Quaternion([room["rotation"][axis] for axis in ("w", "x", "y", "z")]),
                [room["roomExtentMin"]["data"][axis] for axis in ("x", "y", "z")],
                [room["roomExtentMax"]["data"][axis] for axis in ("x", "y", "z")]))
        except (KeyError, TypeError):
            log("WARNING", "Skipping room with incomplete extents: " + room.get("name", ""),
                "render_map_layers_for_gfx")
    use_rooms = bool(json_data.get("rooms"))

    def room_boundary_distance(points):
        """For each point, its distance to the boundary of the indoor room it is in (inf when in none)."""
        distance = np.full(len(points), np.inf)
        if len(points) == 0:
            return distance
        homogeneous = np.hstack((points, np.ones((len(points), 1))))
        for inverse, minimum, maximum, _, _ in indoor_rooms:
            local = (homogeneous @ inverse.T)[:, :3]
            inside = np.all((local >= minimum - (0, 0, 0.3)) & (local <= maximum + (0, 0, np.inf)), axis=1)
            margin = np.minimum(local[:, :2] - minimum[:2], maximum[:2] - local[:, :2]).min(axis=1)
            distance[inside] = np.minimum(distance[inside], margin[inside])
        return distance

    def interior_points(points):
        """Classify points as indoor floors: inside an indoor room, or above one (the upper floors and roofs
        of a building, and the tops of tall props in a double-height hall)."""
        return boxes_contain(indoor_rooms, points, (0.0, 0.0, 0.3), (0.0, 0.0, np.inf))

    collection_parents = {}
    for collection in bpy.data.collections:
        for child in collection.children:
            collection_parents.setdefault(child.as_pointer(), []).append(collection)

    def object_name_fragments(obj):
        """The object's name followed by its collections, nearest first."""
        names = [obj.name.lower()]
        pending = list(obj.users_collection)
        seen = set()
        while pending:
            next_pending = []
            for collection in pending:
                pointer = collection.as_pointer()
                if pointer in seen:
                    continue
                seen.add(pointer)
                names.append(collection.name.lower())
                next_pending.extend(collection_parents.get(pointer, ()))
            pending = next_pending
        return names

    def exterior_by_name(names):
        for name in names:
            if any(token in name for token in interior_tokens):
                return False
            if any(token in name for token in exterior_tokens):
                return True
        return False

    mesh_cache = {}

    def mesh_data(obj):
        key = (obj.original.name, obj.data.name if obj.data else "")
        if key not in mesh_cache:
            mesh = obj.to_mesh()
            try:
                mesh.calc_loop_triangles()
                vertices = np.empty(len(mesh.vertices) * 3)
                mesh.vertices.foreach_get("co", vertices)
                triangles = np.empty(len(mesh.loop_triangles) * 3, dtype=np.int64)
                mesh.loop_triangles.foreach_get("vertices", triangles)
            finally:
                obj.to_mesh_clear()
            names = object_name_fragments(obj.original)
            mesh_cache[key] = (vertices.reshape(-1, 3), triangles.reshape(-1, 3), exterior_by_name(names))
        return mesh_cache[key]

    def stair_step_mask(triangles, normals, up):
        """The treads of a flight of stairs in the mesh: a run of flat faces rising in even steps.

        Only the treads are marked, so the landings and the rest of the room stay plain floor. Names are
        not used, since whole rooms are often named after their stairs.
        """
        treads = up & (normals[:, 2] >= 0.94)
        if np.count_nonzero(treads) < GFX_STAIR_MINIMUM_TREADS:
            return np.zeros_like(up)
        elevations = triangles[:, :, 2].mean(axis=1)
        values = np.unique(np.round(elevations[treads], 2))
        # Faces of one tread can differ by a centimetre or two; each cluster of elevations is one step.
        cluster_starts = np.concatenate(([True], np.diff(values) > 0.04))
        cluster_of_value = np.cumsum(cluster_starts) - 1
        step_low = values[cluster_starts]
        step_high = np.maximum.reduceat(values, np.nonzero(cluster_starts)[0])
        tread_indices = np.nonzero(treads)[0]
        tread_steps = cluster_of_value[np.searchsorted(values, np.round(elevations[tread_indices], 2))]
        areas = 0.5 * np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]),
                                     axis=1)
        step_count = len(step_low)
        weights = np.bincount(tread_steps, weights=areas[tread_indices], minlength=step_count)
        centres = np.stack([np.bincount(tread_steps, weights=areas[tread_indices] *
                                        triangles[tread_indices, :, axis].mean(axis=1), minlength=step_count)
                            for axis in (0, 1)], axis=1) / np.maximum(weights, 1e-12)[:, None]

        def climbs(step):
            """Whether going from `step` to the next one is a stair step: an even rise, a short way forward,
            and a tread at least GFX_STAIR_MINIMUM_WIDTH wide across the way up (not the top of a beam)."""
            rise = step_low[step + 1] - step_low[step]
            if not GFX_STAIR_RISE[0] <= rise <= GFX_STAIR_RISE[1]:
                return False
            forward = centres[step + 1] - centres[step]
            going = float(np.hypot(*forward))
            if not GFX_STAIR_GOING[0] <= going <= GFX_STAIR_GOING[1]:
                return False
            across = np.array([-forward[1], forward[0]]) / going
            for tread in (step, step + 1):
                corners = triangles[tread_indices[tread_steps == tread], :, :2].reshape(-1, 2) @ across
                if corners.max() - corners.min() < GFX_STAIR_MINIMUM_WIDTH:
                    return False
            return True

        even = [climbs(step) for step in range(step_count - 1)]
        stairs = np.zeros_like(up)
        start = 0
        while start < len(even):
            if not even[start]:
                start += 1
                continue
            end = start
            while end < len(even) and even[end]:
                end += 1
            low, high = step_low[start], step_high[end]
            if end - start >= GFX_STAIR_MINIMUM_TREADS and high - low >= GFX_STAIR_MINIMUM_SPAN:
                stairs |= treads & (elevations >= low - 0.03) & (elevations <= high + 0.03)
            start = end
        return stairs

    # Meshes whose entities are mostly plants, by ALOC/PRIM hash (object names start with the hash).
    hash_names = {}
    for mesh in json_data.get("meshes", []):
        name = (mesh.get("entity") or {}).get("name", "").lower()
        for key in ("alocHash", "primHash"):
            if mesh.get(key):
                hash_names.setdefault(mesh[key].upper(), []).append(name)
    def hashes_named(tokens):
        return {mesh_hash for mesh_hash, names in hash_names.items()
                if sum(any(token in name for token in tokens) for name in names) * 2 > len(names)}

    vegetation_hashes = hashes_named(GFX_VEGETATION_TOKENS) | hashes_named(GFX_VEHICLE_TOKENS)
    rock_hashes = hashes_named(GFX_ROCK_TOKENS) - vegetation_hashes
    slice_bottom = min((layer.z_range()[0] for layer in slices if layer.boxes), default=-np.inf)
    # Floors below every map layer, shown in GFX_OTHER_LEVEL_COLOR (as (2D triangle, elevation) arrays).
    under_floors = []
    # Where the scene has anything at all (object positions and floors), for finding land and water.
    scene_points = [np.array([[(mesh.get("entity") or {}).get("position", {}).get(axis, 0.0) for axis in ("x", "y")]
                              for mesh in json_data.get("meshes", []) if mesh.get("entity")]).reshape(-1, 2)]

    depsgraph = bpy.context.evaluated_depsgraph_get()
    seen_instances = set()
    for instance in depsgraph.object_instances:
        obj = instance.object
        if obj.type != "MESH":
            continue
        if obj.original.name.split("_")[0].upper() in vegetation_hashes:
            continue
        matrix = np.array(instance.matrix_world)
        instance_key = (obj.original.name, matrix.round(5).tobytes())
        if instance_key in seen_instances:
            continue
        seen_instances.add(instance_key)
        local_vertices, triangle_indices, exterior = mesh_data(obj)
        if len(triangle_indices) == 0:
            continue
        local_min, local_max = local_vertices.min(axis=0), local_vertices.max(axis=0)
        corners = np.array([(x, y, z, 1.0) for x in (local_min[0], local_max[0])
                            for y in (local_min[1], local_max[1]) for z in (local_min[2], local_max[2])]) @ matrix.T
        lower, upper = corners[:, :3].min(axis=0), corners[:, :3].max(axis=0)
        matching = [layer for layer in all_slices if layer.overlaps(lower, upper)]
        below = lower[2] < slice_bottom
        if not matching and not below:
            continue

        world_vertices = local_vertices @ matrix[:3, :3].T + matrix[:3, 3]
        triangles = world_vertices[triangle_indices]
        if np.linalg.det(matrix[:3, :3]) < 0:
            triangles = triangles[:, [0, 2, 1]]  # keep the faces pointing outward in a mirrored instance
        cross = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
        lengths = np.linalg.norm(cross, axis=1)
        valid = lengths > 1e-9
        normals = np.zeros_like(cross)
        normals[valid] = cross[valid] / lengths[valid, None]
        up = valid & (normals[:, 2] > 0.85)
        shell = upper[2] > map_top and upper[2] - lower[2] >= GFX_SHELL_MINIMUM_HEIGHT
        stairs = np.zeros_like(up) if shell else stair_step_mask(triangles, normals, up)
        # Every face that is not flat takes part in the cross-sections, so that they close.
        sloped = valid & (np.abs(normals[:, 2]) < 0.99)
        footprint = max(upper[0] - lower[0], upper[1] - lower[1])
        centroids = triangles.mean(axis=1)
        if obj.original.name.split("_")[0].upper() in rock_hashes:
            if np.any(up):
                under_floors.append((triangles[up][:, :, :2], centroids[up, 2]))
            continue
        if not shell and np.any(up):
            scene_points.append(centroids[up, :2])
            if below:
                under = up & (centroids[:, 2] < slice_bottom)
                if np.any(under):
                    under_floors.append((triangles[under][:, :, :2], centroids[under, 2]))
        if shell:
            base_kind = GFX_FLOOR_HIDDEN
        elif use_rooms:
            base_kind = GFX_FLOOR_BY_ROOM
        else:
            base_kind = GFX_FLOOR_EXTERIOR if exterior else GFX_FLOOR_INTERIOR

        owner = band_owner(centroids) if np.any(up) else None
        for layer in matching:
            floor_mask = up & layer.contains(centroids, owner) if owner is not None else up
            if np.any(floor_mask):
                base_kinds = np.full(int(floor_mask.sum()), base_kind)
                kinds = base_kinds.copy()
                kinds[stairs[floor_mask]] = GFX_FLOOR_STAIRS
                layer.floor_triangles.append(triangles[floor_mask][:, :, :2])
                layer.floor_elevations.append(centroids[floor_mask, 2])
                layer.floor_kinds.append(kinds)
                layer.floor_base_kinds.append(base_kinds)
            if footprint >= GFX_MINIMUM_SECTION_FOOTPRINT and np.any(sloped):
                layer.section_candidates.append((float(lower[2]), float(upper[2]), exterior, triangles[sloped]))

    def floor_levels(layer):
        """Elevations that carry a significant share of the slice's walkable floor area."""
        triangles, elevations, kinds = layer.floors()
        walkable = kinds != GFX_FLOOR_HIDDEN
        triangles, elevations = triangles[walkable], elevations[walkable]
        if len(triangles) == 0:
            return []
        areas = 0.5 * np.abs((triangles[:, 1, 0] - triangles[:, 0, 0]) * (triangles[:, 2, 1] - triangles[:, 0, 1]) -
                             (triangles[:, 2, 0] - triangles[:, 0, 0]) * (triangles[:, 1, 1] - triangles[:, 0, 1]))
        bins = np.round(elevations / 0.25).astype(np.int64)
        unique, inverse = np.unique(bins, return_inverse=True)
        totals = np.bincount(inverse, weights=areas)
        levels = []
        for index in np.argsort(-totals):
            if totals[index] < totals.max() * 0.1:
                break
            level = unique[index] * 0.25
            if all(abs(level - existing) > 0.5 for existing in levels):
                levels.append(level)
        return sorted(levels)

    def section_segments(triangles, cut):
        heights = triangles[:, :, 2] - cut
        crossing = (heights.min(axis=1) < 0) & (heights.max(axis=1) > 0)
        if not np.any(crossing):
            return np.empty((0, 2, 2))
        triangles, heights = triangles[crossing], heights[crossing]
        ends, end_heights = np.roll(triangles, -1, axis=1), np.roll(heights, -1, axis=1)
        crosses = (heights < 0) != (end_heights < 0)
        t = np.where(crosses, heights / np.where(crosses, heights - end_heights, 1.0), 0.0)
        points = triangles[:, :, :2] + (ends[:, :, :2] - triangles[:, :, :2]) * t[:, :, None]
        # Every crossing triangle has exactly two crossing edges; a stable sort brings them to the front.
        order = np.argsort(~crosses, axis=1, kind="stable")[:, :2]
        segments = np.take_along_axis(points, order[:, :, None], axis=1)
        # Orient each segment with the outside of the object on its right, for filling by winding.
        normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])[:, :2]
        direction = segments[:, 1] - segments[:, 0]
        flip = direction[:, 1] * normals[:, 0] - direction[:, 0] * normals[:, 1] < 0
        segments[flip] = segments[flip][:, ::-1]
        return segments

    for layer in all_slices:
        layer.levels = floor_levels(layer)
        if not layer.levels:
            continue
        top = layer.z_range()[1]
        for lowest_z, highest_z, exterior, triangles in layer.section_candidates:
            supporting = [level for level in layer.levels if level <= lowest_z + 0.25]
            level = supporting[-1] if supporting else layer.levels[0]
            standing = highest_z - max(level, lowest_z)
            if standing >= GFX_MINIMUM_WALL_HEIGHT:
                cut = min(level + GFX_SECTION_HEIGHT, top - 0.01) + 1e-4
                block = False
            elif standing >= GFX_MINIMUM_BLOCK_HEIGHT:
                cut = (max(level, lowest_z) + highest_z) / 2 + 1e-4
                block = True
            else:
                continue
            found = section_segments(triangles, cut)
            if len(found) == 0:
                continue
            if block:
                # A low object counts only as a large solid block (by the area its oriented outline encloses).
                area = 0.5 * abs(np.sum(found[:, 0, 0] * found[:, 1, 1] - found[:, 1, 0] * found[:, 0, 1]))
                length = np.linalg.norm(found[:, 1] - found[:, 0], axis=1).sum()
                if area < GFX_MINIMUM_BLOCK_AREA or area > GFX_MAXIMUM_SOLID_THICKNESS / 2 * length:
                    continue
            midpoints = np.hstack((found.mean(axis=1), np.full((len(found), 1), cut)))
            inside = layer.contains(midpoints)
            if not np.any(inside):
                continue
            if use_rooms:
                boundary = room_boundary_distance(midpoints[inside])
                outdoors = np.mean(np.isinf(boundary)) > 0.5
                if not outdoors and np.mean(boundary <= GFX_ROOM_WALL_DISTANCE) < GFX_ROOM_WALL_SHARE:
                    continue  # furniture inside a room
            else:
                outdoors = exterior
            if outdoors and np.linalg.norm(found[:, 1] - found[:, 0], axis=1).sum() < GFX_MINIMUM_OUTDOOR_OUTLINE:
                continue
            # Keep the whole outline of an object that is mostly inside the layer, so that it stays closed.
            layer.wall_sections.append((found if np.mean(inside) > 0.5 else found[inside], outdoors))
        layer.section_candidates = []

    def to_canvas(points):
        return np.stack((points[..., 0] - canvas_min[0], canvas_max[1] - points[..., 1]), axis=-1)

    def dilate(mask, reach):
        """Grow a boolean grid by `reach` cells in every direction (a square window)."""
        if reach <= 0:
            return mask
        for axis in (0, 1):
            counts = np.cumsum(np.pad(mask, [(reach + 1, reach) if a == axis else (0, 0) for a in (0, 1)]),
                               axis=axis, dtype=np.int32)
            length = mask.shape[axis]
            upper = np.take(counts, np.arange(2 * reach + 1, 2 * reach + 1 + length), axis=axis)
            lower = np.take(counts, np.arange(0, length), axis=axis)
            mask = upper - lower > 0
        return mask

    def erode(mask, reach):
        return ~dilate(~mask, reach)

    def classify_by_room(visible, winners, lower, resolution, elevations):
        """Resolve GFX_FLOOR_BY_ROOM cells to interior or exterior from the rooms they lie in."""
        rows, columns = np.nonzero(visible == GFX_FLOOR_BY_ROOM)
        if len(rows) == 0:
            return
        owners = winners[rows, columns]
        canvas_x = lower[0] + (columns + 0.5) * resolution
        canvas_y = lower[1] + (rows + 0.5) * resolution
        points = np.stack((canvas_x + canvas_min[0], canvas_max[1] - canvas_y, elevations[owners]), axis=1)
        visible[rows, columns] = np.where(interior_points(points), GFX_FLOOR_INTERIOR, GFX_FLOOR_EXTERIOR)

    def mask_path(mask, lower, resolution, tolerance, minimum_area):
        rings = trace_mask(mask, minimum_cells=minimum_area / resolution ** 2, tolerance=tolerance / resolution)
        return "".join("M" + " ".join("{:.2f} {:.2f}".format(*point) for point in ring * resolution + lower) + "Z"
                       for ring in rings)

    def layer_paths(triangles, elevations, kinds, priorities, other_triangles, other_elevations, other_kinds,
                    wall_sections):
        """The coloured paths of one map layer, in painting order.

        The topmost floor (by `priorities`) wins each cell of a fine grid, and GFX_FLOOR_BY_ROOM cells are
        classified there rather than per triangle, so a large triangle never carries the colour of the room its
        centroid happens to lie in across the rooms around it. Floors that are only on other levels
        (`other_*`: indoor floors above this layer, and floors below every layer) show where this layer has no
        floor. Walls are the filled cross-sections of `wall_sections`, kept where they border floor.
        """
        canvas_triangles = to_canvas(triangles) if len(triangles) else np.empty((0, 3, 2))
        canvas_other = to_canvas(other_triangles) if len(other_triangles) else np.empty((0, 3, 2))
        if len(canvas_triangles) + len(canvas_other) == 0:
            return []
        points = np.concatenate((canvas_triangles.reshape(-1, 2), canvas_other.reshape(-1, 2)))
        lower = np.maximum(points.min(axis=0) - 1.0, 0.0)
        upper = np.minimum(points.max(axis=0) + 1.0, (width, height))
        if np.any(upper <= lower):
            return []
        resolution = max(GFX_FLOOR_RESOLUTION, float(np.max(upper - lower)) / GFX_MAXIMUM_FLOOR_GRID)
        shape = tuple(int(np.ceil(value / resolution)) + 1 for value in (upper - lower)[::-1])

        winners = rasterize_triangles(canvas_triangles, priorities, lower, resolution, shape)
        visible = (np.where(winners >= 0, kinds[np.maximum(winners, 0)], GFX_FLOOR_NONE) if len(kinds)
                   else np.full(shape, GFX_FLOOR_NONE, dtype=np.int64))
        classify_by_room(visible, winners, lower, resolution, elevations)
        # Shell ledges are out of bounds: hide whatever stands on them too, at any height.
        hidden = kinds == GFX_FLOOR_HIDDEN
        out_of_bounds = np.zeros(shape, dtype=bool)
        if np.any(hidden):
            out_of_bounds = rasterize_triangles(canvas_triangles[hidden], elevations[hidden], lower, resolution,
                                                shape) >= 0
            visible[out_of_bounds] = GFX_FLOOR_NONE
        visible[visible == GFX_FLOOR_HIDDEN] = GFX_FLOOR_NONE
        floor = visible != GFX_FLOOR_NONE
        visible[floor & ~remove_small_islands(floor, GFX_MINIMUM_FLOOR_ISLAND / resolution ** 2)] = GFX_FLOOR_NONE
        # Small patches of outdoor floor with indoor floor all around are gaps between room volumes.
        exterior = visible == GFX_FLOOR_EXTERIOR
        labels, sizes = label_regions(exterior)
        if len(sizes):
            pockets = sizes < GFX_MAXIMUM_ENCLOSED_EXTERIOR / resolution ** 2
            open_ground = np.pad(visible == GFX_FLOOR_NONE, 1, constant_values=True)
            pockets[np.unique(labels[dilate(open_ground, 1)[1:-1, 1:-1] & exterior])] = False
            visible[exterior & pockets[np.maximum(labels, 0)]] = GFX_FLOOR_INTERIOR
        floor = visible != GFX_FLOOR_NONE

        # Floors on other levels, where this layer has none.
        if len(canvas_other):
            other_winners = rasterize_triangles(canvas_other, other_elevations, lower, resolution, shape)
            other = np.where(other_winners >= 0, other_kinds[np.maximum(other_winners, 0)], GFX_FLOOR_NONE)
            classify_by_room(other, other_winners, lower, resolution, other_elevations)
            other_level = other != GFX_FLOOR_NONE
            reach = int(np.ceil(GFX_OTHER_LEVEL_SMOOTHING / resolution))
            other_level = erode(dilate(other_level, reach), reach) & ~floor & ~out_of_bounds
            other_level = remove_small_islands(other_level, GFX_MINIMUM_OTHER_LEVEL_AREA / resolution ** 2)
            visible[other_level] = GFX_FLOOR_OTHER_LEVEL

        # Walls: the filled cross-sections, grown a little, where they border this layer's floor.
        solids, lines = fill_sections([to_canvas(segments) for segments in wall_sections], lower, resolution, shape,
                                      GFX_MAXIMUM_SOLID_THICKNESS / 2)
        # Outlines of hollow objects (one-sided walls) are drawn as thick as the official maps' walls.
        walls = dilate(solids | lines, int(round(GFX_WALL_GROWTH / resolution))) |             dilate(lines, int(round(GFX_LINE_WALL_WIDTH / 2 / resolution)))
        walls &= dilate(floor, int(np.ceil(GFX_WALL_FLOOR_DISTANCE / resolution))) & ~out_of_bounds
        walls = remove_small_islands(walls, GFX_MINIMUM_WALL_AREA / resolution ** 2)

        paths = []
        for color, mask, tolerance in (
                (GFX_OTHER_LEVEL_COLOR, visible == GFX_FLOOR_OTHER_LEVEL, GFX_OTHER_LEVEL_SMOOTHING / 4),
                # Paint every floor as exterior first so that interior and stair outlines never leave seams.
                (GFX_EXTERIOR_FLOOR_COLOR, floor, GFX_FLOOR_TOLERANCE),
                (GFX_INTERIOR_FLOOR_COLOR, visible == GFX_FLOOR_INTERIOR, GFX_FLOOR_TOLERANCE),
                (GFX_STAIRS_COLOR, visible == GFX_FLOOR_STAIRS, GFX_FLOOR_TOLERANCE),
                (GFX_WALL_COLOR, walls, GFX_WALL_TOLERANCE)):
            path = mask_path(mask, lower, resolution, tolerance, 4 * resolution ** 2)
            if path:
                paths.append((color, path))
        return paths

    def write_paths(svg, paths):
        for color, path in paths:
            svg.write('<path fill="' + color + '" fill-rule="evenodd" d="' + path + '"/>\n')

    def land_mask():
        """Land on a grid over the canvas: everything but water, and nothing outside an enclosing shell.

        Water is a gap wider than GFX_WATER_MINIMUM_WIDTH between the scene's objects and floors inside the area
        they enclose; further away from the scene the official maps still show land. Where shell ledges (a silo
        wall) enclose the scene, the canvas outside them is not land, and the ledge next to the land is returned
        as a band, as (land, band, resolution).
        """
        resolution = GFX_LAND_RESOLUTION
        shape = (int(np.ceil(height / resolution)), int(np.ceil(width / resolution)))
        points = to_canvas(np.concatenate(scene_points)) if scene_points else np.empty((0, 2))
        cells = np.floor(points / resolution).astype(np.int64)
        inside = (cells[:, 0] >= 0) & (cells[:, 0] < shape[1]) & (cells[:, 1] >= 0) & (cells[:, 1] < shape[0])
        occupied = np.zeros(shape, dtype=bool)
        occupied[cells[inside, 1], cells[inside, 0]] = True
        if not occupied.any():
            return np.ones(shape, dtype=bool), np.zeros(shape, dtype=bool), resolution
        barrier = np.zeros(shape, dtype=bool)
        for layer in all_slices:
            triangles, elevations, kinds = layer.floors()
            hidden = kinds == GFX_FLOOR_HIDDEN
            if np.any(hidden):
                barrier |= rasterize_triangles(to_canvas(triangles[hidden]), elevations[hidden], np.zeros(2),
                                               resolution, shape) >= 0
        void = np.zeros(shape, dtype=bool)
        if barrier.any():
            labels, _ = label_regions(~barrier)
            reached = np.unique(labels[occupied & ~barrier])
            void = barrier | (~barrier & ~np.isin(labels, reached[reached >= 0]))

        def close(mask, width):
            # Past the canvas edge counts as closed, so water may run off the canvas like a river does.
            reach = int(np.ceil(width / 2 / resolution))
            return erode(dilate(mask, reach), reach)

        water = ~close(occupied, GFX_WATER_MINIMUM_WIDTH) & close(occupied, GFX_WATER_ENCLOSURE) & ~void
        labels, sizes = label_regions(water)
        if len(sizes):
            keep = sizes >= max(GFX_WATER_KEEP_FRACTION * sizes.max(), GFX_MINIMUM_WATER_AREA / resolution ** 2)
            water &= keep[np.maximum(labels, 0)]
        land = ~water & ~void
        band = void & dilate(land, int(round(GFX_SHELL_BAND / resolution))) if barrier.any() else barrier
        return land, band, resolution

    def write_svg(path, writer):
        with open(path, "w", encoding="utf-8") as svg:
            svg.write('<?xml version="1.0" encoding="UTF-8"?>\n')
            svg.write('<svg xmlns="http://www.w3.org/2000/svg" width="{0:g}" height="{1:g}" '
                      'viewBox="0 0 {0:g} {1:g}">\n'.format(width, height))
            writer(svg)
            svg.write("</svg>\n")

    output_dir = os.path.abspath(os.path.dirname(output_path))
    os.makedirs(output_dir, exist_ok=True)
    if under_floors:
        under_triangles = np.concatenate([triangles for triangles, _ in under_floors])
        under_elevations = np.concatenate([elevations for _, elevations in under_floors])
    else:
        under_triangles, under_elevations = np.empty((0, 3, 2)), np.empty(0)

    def write_background(svg):
        centre_x, centre_y = width / 2, height / 2
        half_gap = GFX_CROSSHAIR_GAP / 2
        for left, top, right, bottom in ((0.0, 0.0, centre_x - half_gap, centre_y - half_gap),
                                         (centre_x + half_gap, 0.0, width, centre_y - half_gap),
                                         (0.0, centre_y + half_gap, centre_x - half_gap, height),
                                         (centre_x + half_gap, centre_y + half_gap, width, height)):
            svg.write('<rect x="{:g}" y="{:g}" width="{:g}" height="{:g}" fill="{}"/>\n'.format(
                left, top, right - left, bottom - top, GFX_BACKGROUND_COLOR))
        # The official maps paint the ground over the whole canvas, leaving out only the water.
        land, band, land_resolution = land_mask()
        for color, mask in ((GFX_INTERIOR_FLOOR_COLOR, band), (GFX_EXTERIOR_FLOOR_COLOR, land)):
            path = mask_path(mask, np.zeros(2), land_resolution, land_resolution, 4 * land_resolution ** 2)
            if path:
                svg.write('<path fill="' + color + '" fill-rule="evenodd" d="' + path + '"/>\n')
        triangles, elevations, kinds = background.floors()
        write_paths(svg, layer_paths(triangles, elevations, kinds, elevations, np.empty((0, 3, 2)), np.empty(0),
                                     np.empty(0, dtype=np.int64),
                                     [segments for segments, _ in background.wall_sections]))

    background_path = os.path.join(output_dir, "output_background.svg")
    write_svg(background_path, write_background)
    log("INFO", "Rendered map background to " + background_path, "render_map_layers_for_gfx")

    for layer_number, layer in enumerate(slices):
        def write_layer(svg, layer_number=layer_number, layer=layer):
            # A transparent canvas-sized rectangle keeps empty layers importable and pins the shape bounds.
            svg.write('<rect width="{:g}" height="{:g}" fill="#000000" fill-opacity="0"/>\n'.format(
                width, height))
            # Lower slices show through with their outdoor walls, like the official maps: their indoor floors
            # stay dark grey, while their stairs become plain floor.
            triangles, elevations, kinds = layer.floors()
            priorities = elevations
            lower_slices = slices[:layer_number]
            for lower in lower_slices:
                lower_triangles, lower_elevations, lower_kinds = lower.floors(base_kinds=True)
                triangles = np.concatenate((lower_triangles, triangles))
                elevations = np.concatenate((lower_elevations, elevations))
                # Keep lower floors underneath this slice's floors regardless of their elevations.
                priorities = np.concatenate((lower_elevations - 1e6, priorities))
                kinds = np.concatenate((lower_kinds, kinds))
            # Floors only on other levels: those of the slices above, and every floor below all slices.
            other_triangles, other_elevations = [under_triangles], [under_elevations]
            other_kinds = [np.full(len(under_elevations), GFX_FLOOR_OTHER_LEVEL)]
            for upper_slice in slices[layer_number + 1:]:
                upper_triangles, upper_elevations, upper_kinds = upper_slice.floors(base_kinds=True)
                other_triangles.append(upper_triangles)
                other_elevations.append(upper_elevations)
                other_kinds.append(upper_kinds)
            sections = [segments for lower in lower_slices for segments, outdoors in lower.wall_sections
                        if outdoors] + [segments for segments, _ in layer.wall_sections]
            write_paths(svg, layer_paths(triangles, elevations, kinds, priorities,
                                         np.concatenate(other_triangles), np.concatenate(other_elevations),
                                         np.concatenate(other_kinds), sections))

        svg_path = os.path.join(output_dir, "output_" + str(layer_number).zfill(2) + ".svg")
        write_svg(svg_path, write_layer)
        log("INFO", "Rendered map layer " + str(layer_number) + " (floor levels " +
            ", ".join("{:.2f}".format(level) for level in layer.levels) + ") to " + svg_path,
            "render_map_layers_for_gfx")


def glacier2glb(path_to_nav_json: str, output_path: str, mesh_type: str, lod_mask: str, build_type: str,
                filter_to_include_box: bool, apply_textures: bool, output_to_blend: bool, should_build_gfx: bool):
    start = timer()
    log("INFO", "Loading scenario.", "glacier2glb")
    log("INFO", "Nav.Json file: " + path_to_nav_json, "glacier2glb")
    f = open(path_to_nav_json, "r")
    data = json.loads(f.read())
    f.close()
    transforms = {}
    room_collections = {}
    room_folder_collections = {}
    mesh_room_collections = {}
    room_color_index = 0
    room_folder_color_index = 0

    temp_coll = bpy.data.collections.new("Temporary")
    bpy.context.scene.collection.children.link(temp_coll)

    if output_to_blend:
        load_volume_boxes(data, ["gates", "rooms", "aiArea", "mapLevel"])

    # Get the "pathfinding include" box
    pf_include_box_info = None
    pf_box_bounding_sphere_center = None
    pf_box_bounding_sphere_radius = None
    sapienza_include_box_id = "48b22d0153942122"
    for pf_box in data['pfBoxes']:
        if pf_box["type"]["data"] == "PFBT_INCLUDE_MESH_COLLISION" or pf_box["id"] == sapienza_include_box_id:
            coords = ["w", "x", "y", "z"]
            pf_include_box_info = [
                mathutils.Vector([pf_box["position"][coord] for coord in coords[1:]]),
                mathutils.Quaternion([pf_box["rotation"][coord] for coord in coords]),
                mathutils.Vector([pf_box["scale"]["data"][coord] for coord in coords[1:]]),
            ]
            break
    if filter_to_include_box and pf_include_box_info is not None:
        bpy.ops.mesh.primitive_cube_add(size=1)
        box = bpy.context.active_object
        box.location = pf_include_box_info[0]
        box.rotation_mode = "QUATERNION"
        box.rotation_quaternion = pf_include_box_info[1]
        box.scale = pf_include_box_info[2]
        bpy.context.view_layer.update()
        pf_box_bounding_sphere_center, pf_box_bounding_sphere_radius = get_bounding_sphere_data(box, box.matrix_world)
        box.select_set(True)
        bpy.context.view_layer.objects.active = box
        bpy.ops.object.delete(use_global=False)

    geo_node_name = "HitmanMapNode"
    if build_type == "instance":
        # link the geonode
        filepath = os.path.join(os.path.dirname(os.path.abspath(__file__)), "res.blend")
        log("DEBUG", "Loading geometry node from the resource blend file: " + filepath, "glacier2glb")
        with bpy.data.libraries.load(filepath, link=True) as (data_from, data_to):
            if geo_node_name in data_from.node_groups:
                data_to.node_groups.append(geo_node_name)
            else:
                log("ERROR", "=========================== Error Loading the resource blend file ================",
                    "glacier2glb")
                assert 0
    matis = {}
    prim_matis = {}
    shading_mat_name = "HitmanTextureNode"
    if apply_textures:
        matis_list = data['matis']
        for mati in matis_list:
            matis[mati["hash"]] = mati
        prim_matis_list = data['primMatis']
        for prim_mati in prim_matis_list:
            prim_matis[prim_mati["primHash"]] = prim_mati
        # link the shading node
        filepath = os.path.join(os.path.dirname(os.path.abspath(__file__)), "res.blend")
        with bpy.data.libraries.load(filepath, link=False) as (data_from, data_to):
            log("DEBUG", "Loading shading material from the resource blend file: " + filepath, "glacier2glb")
            if shading_mat_name in data_from.materials:
                data_to.materials.append(shading_mat_name)
            else:
                log("WARNING", "Shading material " + shading_mat_name + " not found in res.blend", "glacier2glb")
        log("DEBUG", "Adding light", "glacier2glb")

        light_data = bpy.data.lights.new(name="Sun", type='SUN')
        light_object = bpy.data.objects.new(name="Sun", object_data=light_data)
        bpy.context.collection.objects.link(light_object)

        light_data.energy = 2.5
        light_data.angle = math.radians(180)
        light_object.rotation_euler = (0, 0, 0)
    root_coll = bpy.data.collections.get("Collection")
    if root_coll:
        root_coll.name = "Environment"
    else:
        root_coll = bpy.data.collections.new("Environment")
        bpy.context.scene.collection.children.link(root_coll)


    for hash_and_entity in data['meshes']:
        if mesh_type == "ALOC":
            mesh_hash = hash_and_entity['alocHash']
        else:
            mesh_hash = hash_and_entity['primHash']

        room_folder_name = hash_and_entity["roomFolderName"][:63] if hash_and_entity["roomFolderName"] else "Default"
        if room_folder_name not in room_folder_collections:
            coll = bpy.data.collections.new(room_folder_name)
            root_coll.children.link(coll)
            coll.color_tag = "COLOR_0" + str(room_folder_color_index % 8 + 1)
            room_folder_color_index += 1
            log("DEBUG", "Adding new collection for room folder name: " + room_folder_name, "glacier2glb")
            room_folder_collections[room_folder_name] = coll

        room_folder_coll = room_folder_collections[room_folder_name]

        room_name = hash_and_entity["roomName"][:63] if hash_and_entity["roomName"] else "Default"
        room_collection_key = (room_folder_name, room_name)
        if room_collection_key not in room_collections:
            coll = bpy.data.collections.new(room_name)
            room_folder_coll.children.link(coll)
            coll.color_tag = "COLOR_0" + str(room_color_index % 8 + 1)
            room_color_index += 1
            log("DEBUG", "Adding new collection for room name: " + room_name, "glacier2glb")
            room_collections[room_collection_key] = coll

        entity = hash_and_entity['entity']
        transform = {"position": entity["position"], "rotate": entity["rotation"],
                     "scale": entity["scale"]["data"], "id": entity["id"]}

        if mesh_hash not in transforms:
            transforms[mesh_hash] = []
            mesh_room_collections[mesh_hash] = []
        transforms[mesh_hash].append(transform)
        mesh_room_collections[mesh_hash].append(room_collections[room_collection_key])
    output_dir = os.path.abspath(os.path.dirname(output_path))
    path_to_aloc_or_prim_dir = os.path.join(output_dir, mesh_type.lower())
    log("INFO", "Path to " + mesh_type.lower() + " dir:" + path_to_aloc_or_prim_dir, "glacier2glb")
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
    meshes_in_scenario_count = 0
    processed_mesh_hashes = []
    for aloc_or_prim_i in range(0, aloc_or_prim_file_count):
        aloc_or_prim_filename = aloc_or_prim_list[aloc_or_prim_i]
        mesh_hash = aloc_or_prim_filename[:-5]
        if mesh_hash in transforms:
            meshes_in_scenario_count += 1
            mesh_count += len(transforms[aloc_or_prim_filename[:-5]])
            processed_mesh_hashes.append(mesh_hash)

    mesh_i = 0
    current_mesh_in_scene_index = 0
    error_alocs = []
    log("INFO", mesh_type + " count: " + str(meshes_in_scenario_count), "glacier2glb")
    log("INFO", "Mesh count: " + str(mesh_count), "glacier2glb")
    log_threshold = max(1, round(meshes_in_scenario_count / 10, -2))
    for aloc_or_prim_i in range(0, aloc_or_prim_file_count):
        aloc_or_prim_filename = aloc_or_prim_list[aloc_or_prim_i]
        mesh_hash = aloc_or_prim_filename[:-5]
        if mesh_hash not in transforms:
            continue
        current_mesh_in_scene_index += 1
        if mesh_hash in excluded_mesh_hashes:
            log("DEBUG", "Skipping " + mesh_type + " file " + mesh_hash, mesh_hash)
            mesh_i += len(transforms[mesh_hash])
            continue
        aloc_or_prim_path = os.path.join(path_to_aloc_or_prim_dir, aloc_or_prim_filename)

        log("DEBUG", "Loading " + mesh_type + ": " + mesh_hash, "glacier2glb")
        if current_mesh_in_scene_index % log_threshold == 0:
            log("INFO", "Loaded " + str(current_mesh_in_scene_index) + " " + mesh_type + "s...", "glacier2glb")
        collision_type = None
        material_indices = []
        try:
            if mesh_type == "ALOC":
                aloc, submeshes = load_aloc(aloc_or_prim_path, temp_coll)
                if aloc == -2:
                    log("ERROR", "=========================== Problem Loading " + mesh_type + ": " + str(
                        mesh_hash) + " ================", "glacier2glb")
                    error_alocs.append(mesh_hash)
                    continue
                if aloc == -1:
                    log("ERROR", "=========================== Problem Loading " + mesh_type + ": " + str(
                        mesh_hash) + " ================", "glacier2glb")
                    error_alocs.append(mesh_hash)
                    continue
                collision_type = aloc.collision_type
            else:
                submeshes, material_indices = load_prim(aloc_or_prim_path, lod_mask, temp_coll)
        except (struct.error, OSError) as err:
            error_alocs.append("Problem Loading " + mesh_type + ": " + str(mesh_hash) + " Exception: " + str(err))
            log("ERROR", "=========================== Problem Loading " + mesh_type + ": " + str(
                mesh_hash) + " Exception: " + str(err) + " ================", "glacier2glb")
            continue
        mesh_transforms = transforms[mesh_hash]
        mesh_instance_count = len(mesh_transforms)
        if collision_type == PhysicsCollisionType.RIGIDBODY:
            for mesh_index in range(0, mesh_instance_count):
                log("DEBUG", "Skipping RigidBody " + mesh_type + " [" + str(current_mesh_in_scene_index) + "/" + str(
                    meshes_in_scenario_count) + "]: " + mesh_hash + " #" + str(mesh_index) + " Mesh: [" + str(
                    mesh_i + 1) + "/" + str(mesh_count) + "]", "glacier2glb")
                mesh_i += 1
            continue
        if len(submeshes) == 0:
            log("TRACE", "No submeshes for " + str(mesh_hash), "glacier2glb")
            mesh_i += len(transforms[mesh_hash])
            continue
        if not submeshes:
            log("WARNING", "-------------------- Error Loading " + mesh_type + ":" + mesh_hash + " ----------------------",
                "glacier2glb")
            mesh_i += len(transforms[mesh_hash])
            continue
        if collision_type in excluded_collision_types:
            log("TRACE",
                "Skipping Non-collidable " + mesh_type + ": " + mesh_hash + " with collision type: " + str(
                    collision_type), "glacier2glb")
            mesh_i += len(transforms[mesh_hash])
            continue
        if build_type == "instance":
            # different instances transforms
            mesh_positions = [mathutils.Vector(
                (transforms[mesh_hash][i]["position"]["x"], transforms[mesh_hash][i]["position"]["y"],
                 transforms[mesh_hash][i]["position"]["z"])) for i in range(mesh_instance_count)]
            mesh_rotations = [mathutils.Quaternion(
                (transforms[mesh_hash][i]["rotate"]["w"], transforms[mesh_hash][i]["rotate"]["x"],
                 transforms[mesh_hash][i]["rotate"]["y"], transforms[mesh_hash][i]["rotate"]["z"])).to_euler() for i in
                              range(mesh_instance_count)]
            mesh_scales = [mathutils.Vector(
                (transforms[mesh_hash][i]["scale"]["x"], transforms[mesh_hash][i]["scale"]["y"],
                 transforms[mesh_hash][i]["scale"]["z"])) for i in range(mesh_instance_count)]
            for submesh_i, submesh in enumerate(submeshes):
                culled_indices = set()
                # basic culling prepass using bounding spheres
                if filter_to_include_box and pf_box_bounding_sphere_center is not None:
                    lsbbox_center = get_local_space_bbox_center(submesh)
                    for u, (pos, rot, scale) in enumerate(zip(mesh_positions, mesh_rotations, mesh_scales)):
                        inst_mat = mathutils.Matrix.LocRotScale(pos, rot, scale)
                        instance_bbox_center, instance_bbox_radius = get_bounding_sphere_data(submesh, inst_mat,
                                                                                              lsbbox_center)
                        threshold = (instance_bbox_radius + pf_box_bounding_sphere_radius)
                        if (
                                instance_bbox_center - pf_box_bounding_sphere_center).length_squared > threshold * threshold:
                            culled_indices.add(u)

                room_instances = []
                for mesh_index in range(mesh_instance_count):
                    if mesh_index in culled_indices:
                        continue
                    room_collection = mesh_room_collections[mesh_hash][mesh_index]
                    room_group = next(
                        (group for group in room_instances if group[0] is room_collection), None)
                    if room_group is None:
                        room_group = (room_collection, [])
                        room_instances.append(room_group)
                    room_group[1].append(mesh_index)

                if not room_instances:
                    # Every instance is outside the include box. Without a placement object nothing removes this
                    # source object before saving, and glTF export ignores hide_viewport, so it would show up at the
                    # origin.
                    bpy.data.objects.remove(submesh, do_unlink=True)
                    continue

                for room_collection, instance_indices in room_instances:
                    positions = [mesh_positions[i] for i in instance_indices]
                    rotations = [mesh_rotations[i] for i in instance_indices]
                    scales = [mesh_scales[i] for i in instance_indices]

                    # Create a placement mesh for this room's instances.
                    mesh = bpy.data.meshes.new(mesh_hash + "_placement")
                    mesh.from_pydata(positions, [], [])
                    attr = mesh.attributes.new(name="rotation", type='FLOAT_VECTOR', domain='POINT')
                    for mesh_index, rotation in enumerate(rotations):
                        attr.data[mesh_index].vector = rotation
                    attr = mesh.attributes.new(name="scale", type='FLOAT_VECTOR', domain='POINT')
                    for mesh_index, scale in enumerate(scales):
                        attr.data[mesh_index].vector = scale

                    mesh.update()
                    attr_obj = bpy.data.objects.new(mesh_hash + "_placement", mesh)
                    room_collection.objects.link(attr_obj)

                    modifier = attr_obj.modifiers.new(name="GeometryNodes", type='NODES')
                    modifier.node_group = bpy.data.node_groups[geo_node_name]
                    modifier["Socket_2"] = submesh
                    modifier["Socket_3"] = "rotation"
                    modifier["Socket_4"] = "scale"

                submesh.hide_viewport = True

                if mesh_type == "PRIM" and apply_textures and mesh_hash in prim_matis:
                    log("TRACE", "Adding texture", "glacier2glb")
                    try:
                        add_textures(submesh, matis, mesh_hash, output_dir, prim_matis, submesh_i, material_indices[submesh_i])
                    except struct.error as err:
                        log("ERROR", "Error adding texture " + str(err), "glacier2glb")
        else:
            current_submesh = None
            mesh_positions = [mathutils.Vector(
                (transforms[mesh_hash][mesh_index]["position"]["x"], transforms[mesh_hash][mesh_index]["position"]["y"],
                 transforms[mesh_hash][mesh_index]["position"]["z"])) for mesh_index in range(mesh_instance_count)]
            mesh_rotations = [mathutils.Quaternion(
                (transforms[mesh_hash][i]["rotate"]["w"], transforms[mesh_hash][i]["rotate"]["x"],
                 transforms[mesh_hash][i]["rotate"]["y"], transforms[mesh_hash][i]["rotate"]["z"])) for i in
                              range(mesh_instance_count)]
            mesh_scales = [mathutils.Vector(
                (transforms[mesh_hash][i]["scale"]["x"], transforms[mesh_hash][i]["scale"]["y"],
                 transforms[mesh_hash][i]["scale"]["z"])) for i in range(mesh_instance_count)]
            submesh_count = len(submeshes)
            positions = []
            rotations = []
            scales = []
            for submesh_instance_index in range(submesh_count):
                submesh = submeshes[submesh_instance_index]
                culled_indices = set()
                # basic culling prepass using bounding spheres
                if filter_to_include_box and pf_box_bounding_sphere_center is not None:
                    lsbbox_center = get_local_space_bbox_center(submesh)
                    for u, (pos, rot, scale) in enumerate(zip(mesh_positions, mesh_rotations, mesh_scales)):
                        inst_mat = mathutils.Matrix.LocRotScale(pos, rot, scale)
                        instance_bbox_center, instance_bbox_radius = get_bounding_sphere_data(submesh, inst_mat,
                                                                                              lsbbox_center)
                        threshold = (instance_bbox_radius + pf_box_bounding_sphere_radius)
                        if (
                                instance_bbox_center - pf_box_bounding_sphere_center).length_squared > threshold * threshold:
                            culled_indices.add(u)

                positions.append([mesh_positions[u] for u in range(mesh_instance_count) if u not in culled_indices])
                rotations.append([mesh_rotations[u] for u in range(mesh_instance_count) if u not in culled_indices])
                scales.append([mesh_scales[u] for u in range(mesh_instance_count) if u not in culled_indices])
            unlinked = [False for _ in range(submesh_count)]
            for mesh_index in range(mesh_instance_count):
                room_collection = mesh_room_collections[mesh_hash][mesh_index]
                mesh_instance_id = transforms[mesh_hash][mesh_index]["id"]
                log("DEBUG", "Transforming " + mesh_type + " [" + str(current_mesh_in_scene_index) + "/" + str(
                    meshes_in_scenario_count) + "]: " + mesh_hash + " #" + str(mesh_index) + " Mesh: [" + str(
                    mesh_i + 1) + "/" + str(mesh_count) + "] Room name: " + room_collection.name, "glacier2glb")
                mesh_i += 1
                log("TRACE", "Total submeshes: " + str(submesh_count), "glacier2glb")
                for submesh_instance_index in range(submesh_count):
                    submesh = submeshes[submesh_instance_index]
                    if not unlinked[submesh_instance_index]:
                        for collection in list(submesh.users_collection):
                            collection.objects.unlink(submesh)
                        unlinked[submesh_instance_index] = True
                    if mesh_index >= len(positions[submesh_instance_index]):
                        continue
                    if current_submesh is None:
                        current_submesh = submesh
                    else:
                        current_submesh = submesh.copy()
                    room_collection.objects.link(current_submesh)
                    current_submesh.name = mesh_hash + "_" + mesh_instance_id
                    current_submesh.select_set(True)
                    current_submesh.scale = mathutils.Vector(
                        (scales[submesh_instance_index][mesh_index][0], scales[submesh_instance_index][mesh_index][1],
                         scales[submesh_instance_index][mesh_index][2]))
                    current_submesh.rotation_mode = 'QUATERNION'
                    current_submesh.rotation_quaternion = (rotations[submesh_instance_index][mesh_index][0],
                                                           rotations[submesh_instance_index][mesh_index][1],
                                                           rotations[submesh_instance_index][mesh_index][2],
                                                           rotations[submesh_instance_index][mesh_index][3])
                    current_submesh.location = mathutils.Vector((positions[submesh_instance_index][mesh_index][0],
                                                                 positions[submesh_instance_index][mesh_index][1],
                                                                 positions[submesh_instance_index][mesh_index][2]))

                    if mesh_type == "PRIM" and apply_textures:
                        try:
                            add_textures(current_submesh, matis, mesh_hash, output_dir, prim_matis, submesh_instance_index, material_indices[submesh_instance_index])
                        except struct.error as err:
                            log("ERROR", "Error adding texture " + str(err), "glacier2glb")
                    if current_submesh:
                        current_submesh.select_set(False)

    # Rooms whose meshes were all culled, missing or excluded are left empty, and so are folders of such rooms.
    for room_coll in room_collections.values():
        if not room_coll.objects and not room_coll.children:
            bpy.data.collections.remove(room_coll)
    for room_folder_coll in room_folder_collections.values():
        if not room_folder_coll.objects and not room_folder_coll.children:
            bpy.data.collections.remove(room_folder_coll)

    missing_mesh_hashes = transforms.keys() - processed_mesh_hashes
    if len(missing_mesh_hashes) > 0:
        for missing_mesh_hash in missing_mesh_hashes:
            log("ERROR", "Missing one or more Meshes. Missing Mesh hashes:", "glacier2glb")
            log("ERROR", "Missing Mesh: " + missing_mesh_hash, "glacier2glb")

    end = timer()
    if len(error_alocs) > 0:
        log("ERROR", "Problem loading one or more ALOCs. ALOC hashes with problems:", "glacier2glb")
        for error_aloc in error_alocs:
            log("ERROR", "Problem loading ALOC: " + error_aloc, "glacier2glb")
    log("INFO", "Finished loading scenario in " + str(end - start) + " seconds.", "glacier2glb")

    # The map layers are rendered before saving, because saving a glTF bakes every placement's instances into
    # one mesh, and the renderer judges each instance (its size, whether it is stairs or the level's shell) on
    # its own.
    if should_build_gfx:
        start = timer()
        log("INFO", "Rendering map layers for GFX build...", "glacier2glb")
        render_map_layers_for_gfx(data, output_path)
        end = timer()
        log("INFO", "Finished rendering map layers for GFX build in " + str(end - start) + " seconds.", "glacier2glb")

    if output_path.endswith('.both') or output_path.endswith('.bothWithGfx'):
        save_blend_file(output_path[:-4] + "blend")
        save_gltf_file(output_path[:-4] + "gltf")
    elif output_path.endswith('.blend'):
        save_blend_file(output_path)
    else:
        save_gltf_file(output_path)
    return 0


def add_textures(obj, matis, mesh_hash, output_dir, prim_matis, submesh_i, material_i):
    log("TRACE", "Adding texture to prim hash: " + mesh_hash, "add_textures")
    log("TRACE", "Prim Mati: " + str(prim_matis[mesh_hash]), "add_textures")
    log("TRACE", "Mati hashes: " + str(prim_matis[mesh_hash]["matiHashes"]), "add_textures")
    log("TRACE", "Prim Submesh index: " + str(submesh_i), "add_texturess")
    log("TRACE", "Material index: " + str(material_i), "add_texturess")
    if material_i >= len(prim_matis[mesh_hash]["matiHashes"]):
        log("ERROR", "Prim index out of range: " + str(material_i), "add_textures")
        return
    log("TRACE", "Mati hash: " + str(prim_matis[mesh_hash]["matiHashes"][material_i]), "add_texturess")
    mati_hash = prim_matis[mesh_hash]["matiHashes"][material_i]
    if mati_hash not in matis:
        log("ERROR", "Mati hash not found in matis: " + mati_hash, "add_textures")
        return
    mati = matis[mati_hash]
    log("TRACE", "Diffuse hash: " + mati["diffuse"], "add_textures")
    diffuse_hash = mati["diffuse"]
    normal_hash = mati["normal"]
    specular_hash = mati["specular"]
    path_to_tga_dir = os.path.join(output_dir, "tga")
    add_texture(obj, path_to_tga_dir, mati_hash, diffuse_hash, normal_hash, specular_hash)


def main():
    log("TRACE",
        "Usage: blender -b -P Glacier2Glb.py -- <nav.json path> <output.gltf path> <mesh type (ALOC | PRIM)> <LOD Mask e.g. 11111111> <Build Type (copy | instance)> <Culling enabled (true | false)> <apply textures (true | false)> <export to gfx (true | false)> <debug logs enabled (true | false)>",
        "main")
    argv = sys.argv
    argv = argv[argv.index("--") + 1:]
    log("INFO", "blender.exe called with args: " + str(argv), "main")  # --> ['example', 'args', '123']
    scene_path = argv[0]
    output_path = argv[1]
    mesh_type = argv[2]
    lod_mask = argv[3]
    build_type = argv[4]
    filter_to_include_box = argv[5] == "true"
    apply_textures = argv[6] == "true"
    should_build_gfx = argv[7] == "true"
    if len(argv) > 8 and argv[8] == "true":
        log("INFO", "Enabling debug logs", "main"),
        glacier2glb_enabled_log_levels.append("TRACE")
        glacier2glb_enabled_log_levels.append("TRACE")

    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

    # Ensure the render engine is set to EEVEE so transparency properties are active
    # In 4.2+, EEVEE Next is 'BLENDER_EEVEE_NEXT'
    bpy.context.scene.render.engine = 'BLENDER_EEVEE_NEXT'
    output_to_blend = output_path.endswith('.both') or output_path.endswith('.blend')
    scenario = glacier2glb(scene_path, output_path, mesh_type, lod_mask, build_type, filter_to_include_box,
                             apply_textures, output_to_blend, should_build_gfx)
    if scenario == 1:
        log("INFO", 'Failed to import scenario "%s"' % scene_path, "main")
        return 1

    log("INFO", "Script finished, waiting...", "main")
    try:
        sys.stdout.flush()
        os.fsync(sys.stdout.fileno())
    except OSError:
        pass
    return None


def bake_geometry_node_instances():
    node_group_copies = {}
    geometry_node_objects = []
    source_objects = set()
    for obj in bpy.context.scene.objects:
        for modifier in obj.modifiers:
            if modifier.type != 'NODES' or modifier.node_group is None:
                continue
            geometry_node_objects.append(obj)
            source_object = modifier.get('Socket_2')
            if source_object is not None:
                source_objects.add(source_object)
            original_group = modifier.node_group
            node_group = node_group_copies.get(original_group)
            if node_group is None:
                node_group = original_group.copy()
                node_group_copies[original_group] = node_group
                for output_node in (node for node in node_group.nodes if node.bl_idname == 'NodeGroupOutput'):
                    geometry_input = output_node.inputs.get('Geometry')
                    if geometry_input is None:
                        continue
                    for link in list(geometry_input.links):
                        if link.from_node.bl_idname == 'GeometryNodeRealizeInstances':
                            continue
                        from_socket = link.from_socket
                        to_socket = link.to_socket
                        node_group.links.remove(link)
                        realize = node_group.nodes.new('GeometryNodeRealizeInstances')
                        node_group.links.new(from_socket, realize.inputs['Geometry'])
                        node_group.links.new(realize.outputs['Geometry'], to_socket)
            modifier.node_group = node_group

    if not geometry_node_objects:
        return

    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in geometry_node_objects:
        evaluated_obj = obj.evaluated_get(depsgraph)
        baked_mesh = bpy.data.meshes.new_from_object(evaluated_obj, depsgraph=depsgraph)
        obj.data = baked_mesh
        for modifier in list(obj.modifiers):
            obj.modifiers.remove(modifier)

    for source_object in source_objects:
        bpy.data.objects.remove(source_object, do_unlink=True)


def save_gltf_file(output_path: str):
    log("INFO", "Attempting to save glTF file to :" + output_path, "main")
    bake_geometry_node_instances()
    with open(os.devnull, "w") as devnull, \
            contextlib.redirect_stdout(devnull), \
            contextlib.redirect_stderr(devnull):
        bpy.ops.export_scene.gltf(filepath=output_path, export_format='GLTF_SEPARATE', export_apply=True)


def save_blend_file(output_path: str):
    log("INFO", "Attempting to save blender file to :" + output_path, "main")
    bpy.ops.wm.save_as_mainfile(filepath=output_path, relative_remap=False)


if __name__ == "__main__":
    main()
