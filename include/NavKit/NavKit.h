#pragma once

class NavKit {
public:
    static NavKit& getInstance() {
        static NavKit instance;
        return instance;
    }

    [[nodiscard]] static int runProgram();
};
