#pragma once
#include <string>
#include <vector>

class Obj {
    explicit Obj();

public:
    static Obj& getInstance() {
        static Obj instance;
        return instance;
    }
    std::string loadObjName;
    std::string saveObjName;
    std::string lastObjFileName;
    std::string lastSaveObjFileName;
    bool objLoaded;
    bool showObj;
    bool loadObj;
    std::vector<std::string> files;
    const std::string meshesFolder = "Obj";
    std::string objToLoad;
    std::vector<bool> objLoadDone;
    int objScroll;
    float bBoxPos[3];
    float bBoxSize[3];

    static void loadObjMesh(Obj *obj);

    static void copyObjFile(const std::string &from, const std::string &to);

    void saveObjMesh(char *objToCopy, char *newFileName);

    void renderObj();

    void setBBox(float *pos, float *size);

    char *openLoadObjFileDialog(char *lastObjFolder);

    char *openSaveObjFileDialog(char *lastObjFolder);

    void setLastLoadFileName(const char *fileName);

    void setLastSaveFileName(const char *fileName);

    void drawMenu();

    void finalizeLoad();
};
