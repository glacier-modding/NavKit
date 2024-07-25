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
        float x;
        float y;
        float z;
        void readJson(simdjson::ondemand::object json);
    };
    class Rotation {
    public:
        float yaw;
        float pitch;
        float roll;
        void readJson(simdjson::ondemand::object json);
    };
    class PfBoxType {
    public:
        std::string type;
        std::string data;
        void readJson(simdjson::ondemand::object json);
    };
    class Size {
    public:
        std::string type;
        Vec3 data;
        void readJson(simdjson::ondemand::object json);
    };
    class Entity {
    public:
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
        std::string hash;
        Entity entity;
        void readJson(simdjson::ondemand::object json);
    };
    class PfBoxes {
    public:
        static inline const std::string INCLUDE_TYPE = "PFBT_INCLUDE_MESH_COLLISION";
        PfBoxes(char* fileName);
        std::pair<float*, float*> getPathfindingBBox();
        std::vector<HashAndEntity> hashesAndEntities;
    };
}