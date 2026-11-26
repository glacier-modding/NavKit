#pragma once
#include <string>

struct FileSource {
    enum class Kind { None, Disk, Rpkg, Generated };

    Kind kind = Kind::None;
    std::string path;
    std::string hash;

    static FileSource disk(const std::string& path) {
        return {Kind::Disk, path, {}};
    }

    static FileSource rpkg(const std::string& hash) {
        return {Kind::Rpkg, {}, hash};
    }

    static FileSource generated() {
        return {Kind::Generated, {}, {}};
    }

    void markEdited() {
        if (kind == Kind::Rpkg) {
            *this = generated();
        }
    }

    bool operator==(const FileSource&) const = default;
};
