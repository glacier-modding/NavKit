#pragma once
#include <string>

#include "FileSource.h"

struct CrmdSource {
    FileSource file;
    std::string crowdKey;

    bool operator==(const CrmdSource&) const = default;
};
