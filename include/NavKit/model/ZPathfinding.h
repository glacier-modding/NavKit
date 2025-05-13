#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "../../../extern/simdjson/simdjson.h"

namespace ZPathfinding {
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

        void writeJson(std::ofstream &f) const;
    };

    class Rotation {
    public:
        Rotation(): x(0), y(0), z(0), w(0) {
        } ;

        Rotation(float x, float y, float z, float w): x(x), y(y), z(z), w(w) {
        } ;
        float x;
        float y;
        float z;
        float w;

        void readJson(simdjson::ondemand::object json);

        void writeJson(std::ofstream &f) const;
    };

    class PfBoxType {
    public:
        PfBoxType(): type(""), data("") {
        };
        std::string type{};
        std::string data{};

        void readJson(simdjson::ondemand::object json);

        void writeJson(std::ofstream &f) const;
    };

    class Scale {
    public:
        Scale() {
        };

        Scale(std::string type, Vec3 data) : type(type), data(data) {
        };
        std::string type{};
        Vec3 data{};

        void readJson(simdjson::ondemand::object json);

        void writeJson(std::ofstream &f) const;
    };

    class Entity {
    public:
        Entity() {
        };
        std::string id;
        std::string name;
        std::string tblu;
        Vec3 position;
        Rotation rotation;
        PfBoxType type;
        Scale scale;

        void readJson(simdjson::ondemand::object json);
    };

    class HashAndEntity {
    public:
        HashAndEntity() = default;

        std::string hash;
        Entity entity;

        void readJson(simdjson::ondemand::object json);
    };

    class Aloc {
    public:
        Aloc() = default;

        std::string hash;
        std::string id;
        std::string name;
        std::string tblu;
        Vec3 pos{};
        Scale scale{};
        Rotation rotation{};

        void writeJson(std::ofstream &f) const;
    };

    class Alocs {
    public:
        Alocs() {
        }

        Alocs(std::string fileName);

        std::vector<Aloc> readAlocs() const;

        std::vector<HashAndEntity> hashesAndEntities{};
    };

    class PfBox {
    public:
        PfBox() = default;

        PfBox(const std::string &id, const std::string &name, const std::string &tblu, const Vec3 pos, const Vec3 scale,
              const Rotation rotation, const PfBoxType &type) : id(id), name(name), tblu(tblu), pos(pos), scale(scale),
                                                                rotation(rotation), type(type) {
        }

        std::string id{};
        std::string name{};
        std::string tblu{};
        Vec3 pos{};
        Vec3 scale{};
        Rotation rotation{};
        PfBoxType type{};

        void writeJson(std::ofstream &f) const;
    };

    class PfBoxes {
    public:
        PfBoxes() {
        }

        static inline const std::string INCLUDE_TYPE = "PFBT_INCLUDE_MESH_COLLISION";
        static inline const std::string EXCLUDE_TYPE = "PFBT_EXCLUDE_MESH_COLLISION";

        PfBoxes(std::string fileName);

        void readPathfindingBBoxes();

        std::vector<PfBox> readExclusionBoxes() const;

        std::vector<HashAndEntity> hashesAndEntities{};
    };

    class PfSeedPoint {
    public:
        PfSeedPoint() = default;

        PfSeedPoint(const std::string &id, const std::string &name, const std::string &tblu, const Vec3 pos,
                    Rotation rotation) : id(id), name(name), tblu(tblu), pos(pos), rotation(rotation) {
        }

        std::string id;
        std::string name;
        std::string tblu;
        Vec3 pos{};
        Rotation rotation{};

        void writeJson(std::ofstream &f);
    };

    class PfSeedPoints {
    public:
        PfSeedPoints() {
        }

        PfSeedPoints(std::string fileName);

        std::vector<PfSeedPoint> readPfSeedPoints() const;

        std::vector<HashAndEntity> hashesAndEntities{};
    };
}
