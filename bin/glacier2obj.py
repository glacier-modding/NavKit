import os
import bpy
import numpy as np
import json
import mathutils
from mathutils import Euler
import math
import enum
import sys
from enum import IntEnum
import struct
import binascii


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
        print(
            "Attempted to use writeUBytesFromBitBoolArray, but this function is not implemented yet!"
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

class BoneDefinition:
    def __init__(self):
        self.center = [0] * 3
        self.prev_bone_nr = -1
        self.size = [0] * 3
        self.name = [0] * 34
        self.body_part = -1

    def read(self, br):
        self.center = br.readFloatVec(3)
        self.prev_bone_nr = br.readInt()
        self.size = br.readFloatVec(3)
        self.name = br.readString(34)
        self.body_part = br.readShort()

    def write(self, br):
        br.writeFloatVec(self.center)
        br.writeInt(self.prev_bone_nr)
        br.writeFloatVec(self.size)
        br.writeString(self.name, 34)
        br.writeShort(self.body_part)


class SVQ:
    def __init__(self):
        self.rotation = [0] * 4
        self.position = [0] * 4

    def read(self, br):
        self.rotation = br.readFloatVec(4)
        self.position = br.readFloatVec(4)

    def write(self, br):
        br.writeFloatVec(self.rotation)
        br.writeFloatVec(self.position)


class Matrix43:
    def __init__(self):
        self.m = [[0] * 3] * 4

    def read(self, br):
        for row_idx in range(len(self.m)):
            self.m[row_idx] = br.readFloatVec(3)

    def write(self, br):
        for row_idx in range(len(self.m)):
            br.writeFloatVec(self.m[row_idx])


class BoneConstrainType(IntEnum):
    LOOKAT = 1
    ROTATION = 2


class BoneConstraints:
    def __init__(self):
        self.bone_constraints = []

    def read(self, br):
        nr_constraints = br.readUInt()

        for constr in range(nr_constraints):
            self.bone_constraints.append(BoneConstraintLookat())
            self.bone_constraints[constr].read(br)

    def write(self, br):
        br.writeUInt(len(self.bone_constraints))
        for constr in self.bone_constraints:
            constr.write(br)

    def nr_constraints(self):
        return len(self.bone_constraints)


class BoneConstraint:
    def __init__(self):
        self.type  # ubyte
        self.bone_index  # ubyte


class BoneConstraintLookat(BoneConstraint):
    def __init__(self):
        super(BoneConstraint, self).__init__()
        self.look_at_axis = 0
        self.up_bone_alignment_axis = 0
        self.look_at_flip = 0
        self.up_flip = 0
        self.upnode_control = 0
        self.up_node_parent_idx = 0
        self.target_parent_idx = [0] * 2
        self.bone_targets_weights = [0] * 2
        self.target_pos = [[0] * 3] * 2
        self.up_pos = [0] * 3

    def read(self, br):
        self.type = BoneConstrainType(br.readUByte())
        self.bone_index = br.readUByte()
        nr_targets = br.readUByte()
        self.look_at_axis = br.readUByte()
        self.up_bone_alignment_axis = br.readUByte()
        self.look_at_flip = br.readUByte()
        self.up_flip = br.readUByte()
        self.upnode_control = br.readUByte()
        self.up_node_parent_idx = br.readUByte()
        self.target_parent_idx = br.readUByteVec(2)
        br.readUByte()  # alignment
        self.bone_targets_weights = br.readFloatVec(2)
        self.target_pos[0] = br.readFloatVec(3)
        self.target_pos[1] = br.readFloatVec(3)
        self.up_pos = br.readFloatVec(3)

        self.target_parent_idx = self.target_parent_idx[:nr_targets]
        self.bone_targets_weights = self.bone_targets_weights[:nr_targets]
        self.target_pos = self.target_pos[:nr_targets]
        self.target_pos = self.target_pos[:nr_targets]

    def write(self, br):
        br.writeUByte(self.type)
        br.writeUByte(self.bone_index)
        br.writeUByte(len(self.target_parent_idx))
        br.writeUByte(self.look_at_axis)
        br.writeUByte(self.up_bone_alignment_axis)
        br.writeUByte(self.look_at_flip)
        br.writeUByte(self.up_flip)
        br.writeUByte(self.upnode_control)
        br.writeUByte(self.up_node_parent_idx)

        while len(self.target_parent_idx) < 2:
            self.target_parent_idx.append(0)

        while len(self.bone_targets_weights) < 2:
            self.bone_targets_weights.append(0)

        while len(self.target_pos) < 2:
            self.target_pos.append([0, 0, 0])

        br.writeUByteVec(self.target_parent_idx)
        br.writeUByte(0)
        br.writeFloatVec(self.bone_targets_weights)
        for pos in self.target_pos:
            br.writeFloatVec(pos)
        br.writeFloatVec(self.up_pos)


# This is a different BoneConstraint type, it's likely an unused leftover from long ago.
# Since no evidence of it being used in recent BORG files has been found it will remain unused here for now
class BoneConstraintRotate(BoneConstraint):
    def __init__(self):
        super(BoneConstraint, self).__init__()
        self.reference_bone_idx
        self.twist_weight

    def read(self, br):
        print("Tried to read a BoneConstraintRotate, that's not supposed to happen")
        self.type = BoneConstrainType(br.readUByte())
        self.bone_index = br.readUByte()
        self.reference_bone_idx = br.readUByte()
        br.readUByte()
        self.twist_weight = br.readFloat()

    def write(self, br):
        print("Tried to write a BoneConstraintRotate, that's not supposed to happen")
        br.writeUByte(self.type)
        br.writeUByte(self.bone_index)
        br.writeUByte(self.reference_bone_idx)
        br.writeUByte(0)
        br.writeFloat(self.twist_weight)


class PoseBoneHeader:
    def __init__(self):
        self.pose_bone_array_offset = 0  # Size=0x4
        self.pose_bone_index_array_offset = 0  # Size=0x4
        self.pose_bone_count_total = 0  # Size=0x4
        self.pose_entry_index_array_offset = 0  # Size=0x4
        self.pose_bone_count_array_offset = 0  # Size=0x4
        self.pose_count = 0  # Size=0x4
        self.names_list_offset = 0  # Size=0x4
        self.names_entry_index_array_offset = 0  # Size=0x4
        self.face_bone_index_array_offset = 0  # Size=0x4
        self.face_bone_count = 0  # Size=0x4

    def read(self, br):
        self.pose_bone_array_offset = br.readUInt()
        self.pose_bone_index_array_offset = br.readUInt()
        self.pose_bone_count_total = br.readUInt()

        self.pose_entry_index_array_offset = br.readUInt()
        self.pose_bone_count_array_offset = br.readUInt()
        self.pose_count = br.readUInt()

        self.names_list_offset = br.readUInt()
        self.names_entry_index_array_offset = br.readUInt()

        self.face_bone_index_array_offset = br.readUInt()
        self.face_bone_count = br.readUInt()

    def write(self, br):
        header_base = br.tell()

        if self.pose_bone_array_offset == header_base:
            self.pose_bone_array_offset = 0

        if self.pose_bone_index_array_offset == header_base:
            self.pose_bone_index_array_offset = 0

        if self.pose_entry_index_array_offset == header_base:
            self.pose_entry_index_array_offset = 0

        if self.pose_bone_count_array_offset == header_base:
            self.pose_bone_count_array_offset = 0

        if self.names_list_offset == header_base:
            self.names_list_offset = 0

        if self.names_entry_index_array_offset == header_base:
            self.names_entry_index_array_offset = 0

        if self.face_bone_index_array_offset == header_base:
            self.face_bone_index_array_offset = 0

        br.writeUInt(self.pose_bone_array_offset)
        br.writeUInt(self.pose_bone_index_array_offset)
        br.writeUInt(self.pose_bone_count_total)
        br.writeUInt(self.pose_entry_index_array_offset)
        br.writeUInt(self.pose_bone_count_array_offset)
        br.writeUInt(self.pose_count)
        br.writeUInt(self.names_list_offset)
        br.writeUInt(self.names_entry_index_array_offset)
        br.writeUInt(self.face_bone_index_array_offset)
        br.writeUInt(self.face_bone_count)


class Pose:
    def __init__(self):
        self.pose_bone = PoseBone()
        self.pose_bone_index = -1


class PoseBone:
    def __init__(self):
        self.quat = [0] * 4
        self.pos = [0] * 4
        self.scale = [0] * 4

    def read(self, br):
        self.quat = br.readFloatVec(4)
        self.pos = br.readFloatVec(4)
        self.scale = br.readFloatVec(4)

    def write(self, br):
        br.writeFloatVec(self.quat)
        br.writeFloatVec(self.pos)
        br.writeFloatVec(self.scale)


class BoneRig:
    def __init__(self):
        self.bone_definitions = []
        self.bind_poses = []
        self.inv_global_mats = []
        self.pose_bones = []
        self.pose_bone_indices = []
        self.pose_entry_index = []
        self.pose_bone_count_array = []
        self.names_list = []
        self.face_bone_indices = []
        self.bone_constraints = []

    def read(self, br):
        br.seek(br.readUInt64())

        number_of_bones = br.readUInt()
        number_of_animated_bones = br.readUInt()
        bone_definitions_offset = br.readUInt()
        bind_pose_offset = br.readUInt()
        bind_pose_inv_global_mats_offset = br.readUInt()
        bone_constraints_header_offset = br.readUInt()
        pose_bone_header_offset = br.readUInt()

        # invert_global_bones and bone_map are both unused (0) pointers likely leftover from an old version of the BoneRig
        invert_global_bones_offset = br.readUInt()
        bone_map_offset = br.readUInt64()

        # reading data from the offsets
        br.seek(bone_definitions_offset)
        for bone_idx in range(number_of_bones):
            self.bone_definitions.append(BoneDefinition())
            self.bone_definitions[bone_idx].read(br)

        br.seek(bind_pose_offset)
        for bind_pose_idx in range(number_of_bones):
            self.bind_poses.append(SVQ())
            self.bind_poses[bind_pose_idx].read(br)

        br.seek(bind_pose_inv_global_mats_offset)
        for mat_idx in range(number_of_bones):
            self.inv_global_mats.append(Matrix43())
            self.inv_global_mats[mat_idx].read(br)

        br.seek(bone_constraints_header_offset)
        self.bone_constraints = BoneConstraints()
        self.bone_constraints.read(br)

        # read the pose_bone
        br.seek(pose_bone_header_offset)
        pose_bone_header = PoseBoneHeader()
        pose_bone_header.read(br)

        br.seek(pose_bone_header.pose_bone_array_offset)
        for pose_bone in range(pose_bone_header.pose_bone_count_total):
            self.pose_bones.append(PoseBone())
            self.pose_bones[pose_bone].read(br)

        br.seek(pose_bone_header.pose_bone_index_array_offset)
        self.pose_bone_indices = br.readUIntVec(pose_bone_header.pose_bone_count_total)

        br.seek(pose_bone_header.pose_entry_index_array_offset)
        self.pose_entry_index = br.readUIntVec(pose_bone_header.pose_count)

        br.seek(pose_bone_header.pose_bone_count_array_offset)
        self.pose_bone_count_array = br.readUIntVec(pose_bone_header.pose_count)

        # read names
        names_entry_index_array = []
        br.seek(pose_bone_header.names_entry_index_array_offset)
        for entry_idx in range(pose_bone_header.pose_count):
            names_entry_index_array.append(br.readUInt())

        for name_idx in range(pose_bone_header.pose_count):
            br.seek(
                pose_bone_header.names_list_offset + names_entry_index_array[name_idx]
            )
            self.names_list.append(br.readCString())

        # read face bone indices
        br.seek(pose_bone_header.face_bone_index_array_offset)
        self.face_bone_indices = br.readUIntVec(pose_bone_header.face_bone_count)

    def write(self, br):
        br.writeUInt64(420)  # PLACEHOLDER
        br.writeUInt64(0)  # padding

        pose_bone_header = PoseBoneHeader()
        pose_bone_header.pose_bone_array_offset = br.tell()
        for pose_bone in self.pose_bones:
            pose_bone.write(br)

        pose_bone_header.pose_bone_index_array_offset = br.tell()
        br.writeUIntVec(self.pose_bone_indices)
        br.align(16)

        pose_bone_header.pose_entry_index_array_offset = br.tell()
        br.writeUIntVec(self.pose_entry_index)
        br.align(16)

        pose_bone_header.pose_bone_count_array_offset = br.tell()
        br.writeUIntVec(self.pose_bone_count_array)
        br.align(16)

        pose_bone_header.names_list_offset = br.tell()
        for name in self.names_list:
            br.writeCString(name)
        br.align(16)

        pose_bone_header.names_entry_index_array_offset = br.tell()
        name_offset = 0
        for name in self.names_list:
            br.writeUInt(name_offset)
            name_offset = name_offset + len(name) + 1
        br.align(16)

        pose_bone_header.face_bone_index_array_offset = br.tell()
        br.writeUIntVec(self.face_bone_indices)
        br.align(16)

        pose_bone_header.pose_bone_count_total = len(self.pose_bones)
        pose_bone_header.pose_count = len(self.pose_entry_index)
        pose_bone_header.face_bone_count = len(self.face_bone_indices)

        pose_bone_header_offset = br.tell()
        pose_bone_header.write(br)
        br.align(16)

        bone_definitions_offset = br.tell()
        for bone in self.bone_definitions:
            bone.write(br)
        br.align(16)

        bind_pose_offset = br.tell()
        for pose in self.bind_poses:
            pose.write(br)
        br.align(16)

        bind_pose_inv_global_mats_offset = br.tell()
        for mat in self.inv_global_mats:
            mat.write(br)
        br.align(16)

        bone_constraints_header_offset = br.tell()
        self.bone_constraints.write(br)
        br.align(16)

        header_offset = br.tell()
        br.writeUInt(len(self.bone_definitions))  # number_of_bones
        br.writeUInt(
            len(self.bone_definitions) - self.bone_constraints.nr_constraints()
        )  # number_of_animated_bones
        br.writeUInt(bone_definitions_offset)
        br.writeUInt(bind_pose_offset)
        br.writeUInt(bind_pose_inv_global_mats_offset)
        br.writeUInt(bone_constraints_header_offset)
        br.writeUInt(pose_bone_header_offset)
        br.writeUInt(0)  # invert_global_bones_offset
        br.writeUInt64(0)  # bone_map_offset
        br.align(16)

        br.seek(0)
        br.writeUInt64(header_offset)

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

def load_prim(operator, context, collection, filepath, use_rig, rig_filepath):
    """Imports a mesh from the given path"""

    prim_name = bpy.path.display_name_from_filepath(filepath)
    print("Started reading: " + str(prim_name) + "\n")

    fp = os.fsencode(filepath)
    file = open(fp, "rb")
    br = BinaryReader(file)
    prim = RenderPrimitive()
    prim.read(br)
    br.close()

    if prim.header.bone_rig_resource_index == 0xFFFFFFFF:
        collection.prim_collection_properties.bone_rig_resource_index = -1
    else:
        collection.prim_collection_properties.bone_rig_resource_index = (
            prim.header.bone_rig_resource_index
        )
    collection.prim_collection_properties.has_bones = (
        prim.header.property_flags.hasBones()
    )
    collection.prim_collection_properties.has_frames = (
        prim.header.property_flags.hasFrames()
    )
    collection.prim_collection_properties.is_weighted = (
        prim.header.property_flags.isWeightedObject()
    )
    collection.prim_collection_properties.is_linked = (
        prim.header.property_flags.isLinkedObject()
    )

    borg = None
    if use_rig:
        borg_name = bpy.path.display_name_from_filepath(filepath)
        print("Started reading: " + str(borg_name) + "\n")
        fp = os.fsencode(rig_filepath)
        file = open(fp, "rb")
        br = BinaryReader(file)
        borg = BoneRig()
        borg.read(br)

    objects = []
    for meshIndex in range(prim.num_objects()):
        mesh = load_prim_mesh(prim, borg, prim_name, meshIndex)
        obj = bpy.data.objects.new(mesh.name, mesh)
        objects.append(obj)

        # coli testing
        # load_prim_coli(prim, prim_name, meshIndex)

    return objects


def load_prim_coli(prim, prim_name: str, mesh_index: int):
    """Testing class for the prim BoxColi"""
    for boxColi in prim.header.object_table[mesh_index].sub_mesh.collision.box_entries:
        x, y, z = boxColi.min
        x1, y1, z1 = boxColi.max

        bb_min = prim.header.object_table[mesh_index].prim_object.min
        bb_max = prim.header.object_table[mesh_index].prim_object.max

        x = (x / 255) * (bb_max[0] - bb_min[0])
        y = (y / 255) * (bb_max[1] - bb_min[1])
        z = (z / 255) * (bb_max[2] - bb_min[2])

        x1 = (x1 / 255) * (bb_max[0] - bb_min[0])
        y1 = (y1 / 255) * (bb_max[1] - bb_min[1])
        z1 = (z1 / 255) * (bb_max[2] - bb_min[2])

        box_x = (x1 + x) / 2 + bb_min[0]
        box_y = (y1 + y) / 2 + bb_min[1]
        box_z = (z1 + z) / 2 + bb_min[2]

        scale_x = (x1 - x) / 2
        scale_y = (y1 - y) / 2
        scale_z = (z1 - z) / 2

        bpy.ops.mesh.primitive_cube_add(
            scale=(scale_x, scale_y, scale_z),
            calc_uvs=True,
            align="WORLD",
            location=(box_x, box_y, box_z),
        )
        ob = bpy.context.object
        me = ob.data
        ob.name = str(prim_name) + "_" + str(mesh_index) + "_Coli"
        me.name = "CUBEMESH"


def load_prim_mesh(prim, borg, prim_name: str, mesh_index: int):
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
    loop_uvs = [[]]
    loop_cols = []

    num_joint_sets = 0

    if prim.header.property_flags.isWeightedObject() and use_rig:
        num_joint_sets = 2

    sub_mesh = prim.header.object_table[mesh_index].sub_mesh

    vert_joints = [
        [[0] * 4 for _ in range(len(sub_mesh.vertexBuffer.vertices))]
        for _ in range(num_joint_sets)
    ]
    vert_weights = [
        [[0] * 4 for _ in range(len(sub_mesh.vertexBuffer.vertices))]
        for _ in range(num_joint_sets)
    ]

    loop_vidxs.extend(sub_mesh.indices)

    for i, vert in enumerate(sub_mesh.vertexBuffer.vertices):
        vert_locs.extend([vert.position[0], vert.position[1], vert.position[2]])

        for j in range(num_joint_sets):
            vert_joints[j][i] = vert.joint[j]
            vert_weights[j][i] = vert.weight[j]

    for index in sub_mesh.indices:
        vert = sub_mesh.vertexBuffer.vertices[index]
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

    for uv_i in range(sub_mesh.num_uvchannels):
        name = "UVMap" if uv_i == 0 else "UVMap.%03d" % uv_i
        layer = mesh.uv_layers.new(name=name)
        layer.data.foreach_set("uv", loop_uvs[uv_i])

    # Skinning
    ob = bpy.data.objects.new("temp_obj", mesh)
    if num_joint_sets and use_rig:
        for bone in borg.bone_definitions:
            ob.vertex_groups.new(name=bone.name.decode("utf-8"))

        vgs = list(ob.vertex_groups)

        for i in range(num_joint_sets):
            js = vert_joints[i]
            ws = vert_weights[i]
            for vi in range(len(vert_locs) // 3):
                w0, w1, w2, w3 = ws[vi]
                j0, j1, j2, j3 = js[vi]
                if w0 != 0:
                    vgs[j0].add((vi,), w0, "REPLACE")
                if w1 != 0:
                    vgs[j1].add((vi,), w1, "REPLACE")
                if w2 != 0:
                    vgs[j2].add((vi,), w2, "REPLACE")
                if w3 != 0:
                    vgs[j3].add((vi,), w3, "REPLACE")
    bpy.data.objects.remove(ob)

    layer = mesh.vertex_colors.new(name="Col")
    mesh.color_attributes[layer.name].data.foreach_set("color", loop_cols)

    mesh.validate()
    mesh.update()

    # write the additional properties to the blender structure
    prim_mesh_obj = prim.header.object_table[mesh_index].prim_object
    prim_sub_mesh_obj = prim.header.object_table[mesh_index].sub_mesh.prim_object

    lod = prim_mesh_obj.lodmask
    mask = []
    for bit in range(8):
        mask.append(0 != (lod & (1 << bit)))
    mesh.prim_properties.lod = mask

    mesh.prim_properties.material_id = prim_mesh_obj.material_id
    mesh.prim_properties.prim_type = str(prim_mesh_obj.prims.prim_header.type)
    mesh.prim_properties.prim_sub_type = str(prim_mesh_obj.sub_type)

    mesh.prim_properties.axis_lock = [
        prim_mesh_obj.properties.isXaxisLocked(),
        prim_mesh_obj.properties.isYaxisLocked(),
        prim_mesh_obj.properties.isZaxisLocked(),
    ]
    mesh.prim_properties.no_physics = prim_mesh_obj.properties.hasNoPhysicsProp()

    mesh.prim_properties.variant_id = prim_sub_mesh_obj.variant_id
    mesh.prim_properties.z_bias = prim_mesh_obj.zbias
    mesh.prim_properties.z_offset = prim_mesh_obj.zoffset
    mesh.prim_properties.use_mesh_color = prim_sub_mesh_obj.properties.useColor1()
    mesh.prim_properties.mesh_color = [
        prim_sub_mesh_obj.color1[0] / 255,
        prim_sub_mesh_obj.color1[1] / 255,
        prim_sub_mesh_obj.color1[2] / 255,
        prim_sub_mesh_obj.color1[3] / 255,
    ]

    return mesh


def load_scenario(context, collection, path_to_prims_json):
    f = open(path_to_prims_json, "r")
    data = json.loads(f.read())
    f.close()
    transforms = {}
    for hash_and_entity in data['entities']:
        prim_hash = hash_and_entity['primHash']
        entity = hash_and_entity['entity']
        transform = {"position": entity["position"], "rotate": entity["rotation"],
                     "scale": entity["scale"]["data"]}

        if prim_hash not in transforms:
            transforms[prim_hash] = []
        transforms[prim_hash].append(transform)

    path_to_prim_dir = "%s\\%s" % (os.path.dirname(path_to_prims_json), "prim")
    print("Path to prim dir:")
    print(path_to_prim_dir)
    file_list = sorted(os.listdir(path_to_prim_dir))
    prim_list = [item for item in file_list if item.lower().endswith('.prim')]
    for prim_filename in prim_list:
        prim_hash = prim_filename[:-5]
        if prim_hash not in transforms:
            continue
        prim_path = os.path.join(path_to_prim_dir, prim_filename)

        print("Loading prim:")
        print(prim_hash)
        objects = load_prim(
            None, context, collection, prim_path, False, None
        )
        if not objects:
            print("Error Loading prim:")
            print(prim_hash)
            return 1
        highest_lod = -1
        for obj in objects:
            for j in range(0, 8):
                if obj.data['prim_properties']['lod'][j] == 1 and highest_lod < j:
                    highest_lod = j

        t = transforms[prim_hash]
        t_size = len(t)
        for i in range(0, t_size):
            transform = transforms[prim_hash][i]
            p = transform["position"]
            r = transform["rotate"]
            s = transform["scale"]
            r["roll"] = math.pi * 2 - r["roll"]
            print("Transforming prim:" + prim_hash + " #" + str(i))
            for obj in objects:
                if obj.data['prim_properties']['lod'][highest_lod] == 0:
                    continue
                if i != 0:
                    cur = obj.copy()
                else:
                    cur = obj
                collection.objects.link(cur)
                cur.select_set(True)
                cur.scale = mathutils.Vector((s["x"], s["y"], s["z"]))
                cur.rotation_euler = Euler((-r["yaw"], -r["pitch"], -r["roll"]), 'XYZ')
                cur.location = mathutils.Vector((p["x"], p["y"], p["z"]))
                cur.select_set(False)

    return 0


def main():
    print("Usage: blender -b -P glacier2obj.py -- <prims.json path> <output.obj path>")
    argv = sys.argv
    argv = argv[argv.index("--") + 1:]
    print(argv)  # --> ['example', 'args', '123']
    scenario_path = argv[0]
    output_path = argv[1]
    collection = bpy.data.collections.new(
        bpy.path.display_name_from_filepath(scenario_path)
    )
    bpy.context.scene.collection.children.link(collection)

    scenario = load_scenario(bpy.context, collection, scenario_path)
    if scenario == 1:
        print('Failed to import scenario "%s"' % scenario_path, "Importing error", "ERROR")
        return
    bpy.ops.export_scene.obj(filepath=output_path, use_selection=False)


if __name__ == "__main__":
    main()