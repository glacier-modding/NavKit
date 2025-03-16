#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "../../../extern/simdjson/simdjson.h"

namespace PfBoxes {
    class Vec3 {
    public:
        Vec3(): x(0), y(0), z(0) {
        } ;

        Vec3(const float x, const float y, const float z) : x(x), y(y), z(z) {
        }

        float x;
        float y;
        float z;

        void readJson(simdjson::ondemand::object json);
    };

    class Rotation {
    public:
        Rotation(): x(0), y(0), z(0), w(0) {
        } ;
        float x;
        float y;
        float z;
        float w;

        void readJson(simdjson::ondemand::object json);
    };

    class PfBoxType {
    public:
        PfBoxType(): type(""), data("") {
        };
        std::string type;
        std::string data;

        void readJson(simdjson::ondemand::object json);
    };

    class Size {
    public:
        Size() {
        };
        std::string type;
        Vec3 data;

        void readJson(simdjson::ondemand::object json);
    };

    class Entity {
    public:
        Entity() {
        };
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
        HashAndEntity() = default;
        std::string hash;
        Entity entity;

        void readJson(simdjson::ondemand::object json);
    };

    class PfBox {
    public:
        PfBox() = default;

        PfBox(const Vec3 pos, const Vec3 size) : pos(pos), size(size) {
        }

        PfBox(const Vec3 pos, const Vec3 size, const Rotation rotation) : pos(pos), size(size), rotation(rotation) {
        }

        Vec3 pos{};
        Vec3 size{};
        Rotation rotation{};
    };

    class PfBoxes {
    public:
        PfBoxes() : hashesAndEntities() {
        }

        static inline const std::string INCLUDE_TYPE = "PFBT_INCLUDE_MESH_COLLISION";
        static inline const std::string EXCLUDE_TYPE = "PFBT_EXCLUDE_MESH_COLLISION";

        PfBoxes(const char *fileName);

        PfBox getPathfindingBBox() const;

        std::vector<PfBox> getExclusionBoxes() const;

        std::vector<HashAndEntity> hashesAndEntities;
    };
}
