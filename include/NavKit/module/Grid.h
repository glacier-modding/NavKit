#pragma once

class Grid {
public:
    Grid();

    void loadBoundsFromNavp();

    void loadBoundsFromAirg();

    void saveSpacing(float newSpacing);

    void loadSettings();

    void renderGrid() const;

    void renderGridText() const;

    [[nodiscard]] static float defaultSpacing();

    static Grid& getInstance() {
        static Grid instance;
        return instance;
    }

    bool showGrid;

    float spacing;
    float xOffset;
    float yOffset;
    float xMin;
    float yMin;
    float xMax;
    float yMax;
    int gridWidth;
};
