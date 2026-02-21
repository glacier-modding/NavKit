#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <string_view>
#include "../../../extern/simdjson/simdjson.h"

namespace Json {
    template <typename t> struct SimdJsonTypeMap;

    template <> struct SimdJsonTypeMap<float> { using type = double; };
    template <> struct SimdJsonTypeMap<double> { using type = double; };
    template <> struct SimdJsonTypeMap<int> { using type = int64_t; };
    template <> struct SimdJsonTypeMap<bool> { using type = bool; };
    template <> struct SimdJsonTypeMap<std::string> { using type = std::string_view; };
    std::string toString(double val);
    std::string toString(int64_t val);
    std::string toString(bool val);
    std::string toString(std::string_view val);
    struct JsonValueProxy {
        simdjson::ondemand::object json;
        std::string field;
        template <typename t, typename = typename SimdJsonTypeMap<t>::type>
        operator t();
    };
    JsonValueProxy getValue(simdjson::ondemand::object json, const std::string& field);

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

        void writeJson(std::ostream &f, const std::string& propertyName = "position") const;
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

        void writeJson(std::ostream &f) const;
    };

    class PfBoxType {
    public:
        PfBoxType(): type(""), data("") {
        };
        std::string type{};
        std::string data{};

        void readJson(simdjson::ondemand::object json);

        void writeJson(std::ostream &f) const;
    };

    class Vec3Wrapped {
    public:
        Vec3Wrapped() {
        };

        Vec3Wrapped(const std::string& type, const Vec3 data) : type(type), data(data) {
        };
        std::string type{};
        Vec3 data{};

        void readJson(simdjson::ondemand::object json);

        void writeJson(std::ostream& f, std::string propertyName = "scale") const;
    };

    class Entity {
    public:
        Entity() {
        }
        std::string id;
        std::string name;
        std::string tblu;
        Vec3 position;
        Rotation rotation;
        PfBoxType type;
        Vec3Wrapped scale;

        void readJson(simdjson::ondemand::object json);

        void writeJson(std::ostream &f) const;
    };

    class HashesAndEntity {
    public:
        HashesAndEntity() = default;

        std::string alocHash;
        std::string primHash;
        std::string roomName;
        std::string roomFolderName;
        Entity entity;

        void readJson(simdjson::ondemand::object json);
    };

    class Mesh {
    public:
        Mesh() = default;

        std::string alocHash;
        std::string primHash;
        std::string roomName;
        std::string roomFolderName;
        std::string id;
        std::string name;
        std::string tblu;
        Vec3 pos{};
        Vec3Wrapped scale{};
        Rotation rotation{};

        void writeJson(std::ostream &f) const;
    };

    class Meshes {
    public:
        Meshes() {
        }

        Meshes(simdjson::ondemand::array alocs);

        std::vector<Mesh> readMeshes() const;

        std::vector<HashesAndEntity> hashesAndEntities{};
    };

    class PfBox {
    public:
        PfBox() = default;

        PfBox(const std::string &id, const std::string &name, const std::string &tblu, const Vec3 pos,
            const Vec3 scale, const Rotation rotation, const PfBoxType &type) :
            id(id), name(name), tblu(tblu), pos(pos), scale(scale), rotation(rotation), type(type) {
        }

        std::string id{};
        std::string name{};
        std::string tblu{};
        Vec3 pos{};
        Vec3 scale{};
        Rotation rotation{};
        PfBoxType type{};

        void writeJson(std::ostream &f) const;
    };

    class PfBoxes {
    public:
        PfBoxes() {
        }

        static inline const std::string INCLUDE_TYPE = "PFBT_INCLUDE_MESH_COLLISION";
        static inline const std::string EXCLUDE_TYPE = "PFBT_EXCLUDE_MESH_COLLISION";

        static inline const std::string NO_INCLUDE_BOX_FOUND = "NO_INCLUDE_BOX_FOUND";

        PfBoxes(simdjson::ondemand::array);

        void readPathfindingBBoxes();

        std::vector<Entity> entities{};
    };

    class Gate {
    public:
        Gate() = default;

        Gate(const std::string &id, const std::string &name, const std::string &tblu, const Vec3 position,
                    Rotation rotation, const Vec3 bboxCenter, const Vec3 bboxHalfSize) : id(id), name(name), tblu(tblu), position(position), rotation(rotation),
                    bboxCenter(bboxCenter), bboxHalfSize(bboxHalfSize) {
        }

        std::string id;
        std::string name;
        std::string tblu;
        Vec3 position{};
        Rotation rotation{};
        Vec3 bboxCenter{};
        Vec3 bboxHalfSize{};

        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class Gates {
    public:
        Gates() = default;

        explicit Gates(simdjson::ondemand::array gatesJson);

        std::vector<Gate> gates{};
    };

    class Room {
    public:
        Room() = default;

        Room(const std::string &id, const std::string &name, const std::string &tblu, const Vec3 position,
                    Rotation rotation, const Vec3Wrapped& roomExtentMin, const Vec3Wrapped& roomExtentMax) : id(id), name(name), tblu(tblu), position(position), rotation(rotation),
                    roomExtentMin(roomExtentMin), roomExtentMax(roomExtentMax) {
        }

        std::string id;
        std::string name;
        std::string tblu;
        Vec3 position{};
        Rotation rotation{};
        Vec3Wrapped roomExtentMin{};
        Vec3Wrapped roomExtentMax{};

        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class Rooms {
    public:
        Rooms() = default;

        explicit Rooms(simdjson::ondemand::array roomsJson);

        std::vector<Room> rooms{};
    };

    class AiAreaWorld {
    public:
        AiAreaWorld() = default;

        AiAreaWorld(const std::string &id, const std::string &name, const std::string &tblu,
                    const Rotation rotation) : id(id), name(name), tblu(tblu), rotation(rotation) {
        }

        std::string id;
        std::string name;
        std::string tblu;
        Rotation rotation{};

        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class AiAreaWorlds {
    public:
        AiAreaWorlds() = default;

        explicit AiAreaWorlds(simdjson::ondemand::array aiAreaWorldsJson);

        std::vector<AiAreaWorld> aiAreaWorlds{};
    };

    class ParentData {
    public:
        ParentData() = default;

        ParentData(const std::string &id, const std::string &name, const std::string &source, const std::string &tblu, const std::string &type) : id(id), name(name), source(source), tblu(tblu), type(type) {
        }

        std::string id;
        std::string name;
        std::string source;
        std::string tblu;
        std::string type;

        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class Parent {
    public:
        Parent() = default;

        Parent(const std::string &type, const ParentData &data) : type(type), data(data) {
        }

        std::string type;
        ParentData data{};

        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class AiArea {
    public:
        AiArea() = default;

        AiArea(const std::string &id, const std::string &name, const std::string &tblu,
                    const Rotation rotation, Parent parent) : id(id), name(name), tblu(tblu),
                    rotation(rotation), parent(parent) {
        }

        std::string id;
        std::string name;
        std::string tblu;
        Rotation rotation{};
        std::vector<std::string> logicalParents;
        std::vector<std::string> areaVolumeNames;
        Parent parent{};

        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class AiAreas {
    public:
        AiAreas() = default;

        explicit AiAreas(simdjson::ondemand::array aiAreasJson);

        std::vector<AiArea> aiAreas{};
    };

    class VolumeBoxes {
    public:
        VolumeBoxes() = default;
        explicit VolumeBoxes(simdjson::ondemand::array volumeBoxesJson);
        std::vector<Entity> volumeBoxes{};
    };

    class Radius {
    public:
        Radius() = default;

        Radius(const std::string &type, const float data) : type(type), data(data) {
        }

        std::string type;
        float data;
        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class VolumeSphere {
    public:
        VolumeSphere() = default;

        VolumeSphere(const std::string &id, const std::string &name, const Vec3 &position,
                    const Rotation &rotation, const Radius &radius) : id(id), name(name), position(position),
                    rotation(rotation), radius(radius) {
        }

        std::string id;
        std::string name;
        Vec3 position{};
        Rotation rotation{};
        Radius radius{};
        void writeJson(std::ostream &f) const;

        void readJson(simdjson::ondemand::object json);
    };

    class VolumeSpheres {
    public:
        VolumeSpheres() = default;
        explicit VolumeSpheres(simdjson::ondemand::array volumeSpheresJson);
        std::vector<VolumeSphere> volumeSpheres{};
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

        void writeJson(std::ostream &f) const;
    };

    class PfSeedPoints {
    public:
        PfSeedPoints() {
        }

        PfSeedPoints(simdjson::ondemand::array pfSeedPoints);

        std::vector<PfSeedPoint> readPfSeedPoints() const;

        std::vector<Entity> entities{};
    };

    class Mati {
    public:
        Mati() = default;

        void readJsonFromMatiFile(simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument);

        void readJsonFromScene(simdjson::ondemand::object jsonDocument);

        void writeJson(std::ostream& f) const;

        std::string hash;
        std::string className;
        std::string diffuse;
        std::string normal;
        std::string specular;
    };

    class Matis {
    public:
        Matis() {
        }

        Matis(simdjson::ondemand::array matisJson);

        std::vector<Mati> matis{};
    };

    class PrimMati {
    public:
        PrimMati() {

        }
        std::string primHash;
        std::vector<std::string> matiHashes{};
        void readJson(simdjson::ondemand::object jsonDocument);

        void writeJson(std::ostream& f) const;
    };

    class PrimMatis {
    public:
        PrimMatis() {
        }

        PrimMatis(simdjson::ondemand::array primMatisJson);

        std::vector<PrimMati> primMatis{};
    };
}
