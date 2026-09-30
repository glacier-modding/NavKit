#pragma once
#include "Platform.h"
#include <string>

class ErrorHandler {
public:
    static void openErrorDialog(const std::string& message);
};
