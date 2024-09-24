#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdlib.h>
#include "..\..\extern\simdjson\simdjson.h"

namespace PfBoxes {
    class Vec3 {
    public:
        Vec3() {};
        Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
        float x;
        float y;
        float z;
        void readJson(simdjson::ondemand::object json);
    };
    class Rotation {
    public:
        Rotation() {};
        float x;
        float y;
        float z;
        float w;
        void readJson(simdjson::ondemand::object json);
    };
    class PfBoxType {
    public:
        PfBoxType() {};
        std::string type;
        std::string data;
        void readJson(simdjson::ondemand::object json);
    };
    class Size {
    public:
        Size() {};
        std::string type;
        Vec3 data;
        void readJson(simdjson::ondemand::object json);
    };
    class Entity {
    public:
        Entity() {};
        std::string id;
        std::string name;
        Vec3 position;
        Rotation rotation;
        PfBoxType type;
        Size size;
        void readJson(simdjson::ondemand::object json);
    };
    class HashAndEntity {
    public:
        HashAndEntity() {};
        std::string hash;
        Entity entity;
        void readJson(simdjson::ondemand::object json);
    };
    class PfBox {
    public:
        PfBox() {};
        PfBox(Vec3 pos, Vec3 size) : pos(pos), size(size) {}
        Vec3 pos;
        Vec3 size;
    };
    class PfBoxes {
    public:
        static inline const std::string INCLUDE_TYPE = "PFBT_INCLUDE_MESH_COLLISION";
        PfBoxes(char* fileName);
        PfBox getPathfindingBBox();
        std::vector<HashAndEntity> hashesAndEntities;
    };
    
}