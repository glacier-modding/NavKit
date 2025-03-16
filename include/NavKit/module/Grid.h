#pragma once

class Grid {
public:
    Grid();

    void loadBoundsFromNavp();

    void loadBoundsFromAirg();

    void saveSpacing(float newSpacing);

    void renderGrid() const;

    static Grid &getInstance() {
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
