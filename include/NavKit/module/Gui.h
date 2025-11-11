#pragma once

class Gui {
    explicit Gui();

public:
    static Gui &getInstance() {
        static Gui instance;
        return instance;
    }

    void drawGui();

    bool mouseOverMenu;
    bool showMenu;
    bool showLog;
    int logScroll;
    int lastLogCount;
};
