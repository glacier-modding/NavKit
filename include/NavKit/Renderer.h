#pragma once

#include "NavKit.h"
#include <FTGL/ftgl.h>

class NavKit;

enum HitTestType {
	NAVMESH_AREA,
	AIRG_WAYPOINT,
	NONE
}; 

class HitTestResult {
public:
	HitTestResult(HitTestType type, int selectedIndex): type(type), selectedIndex(selectedIndex) {}
	HitTestType type;
	int selectedIndex;
};

class Renderer {
public:
	Renderer(NavKit* navKit);
	
	void initFrameBuffer(int width, int height);
	void drawText(std::string text, Vec3 pos, Vec3 color = { 0.0, 0.0, 0.0 }, double size = 32.0);
	bool initWindowAndRenderer();
	void closeWindow() const;
	void initFrameRate(float frameRateValue);
	void updateFrameRate();
	void handleResize();
	void renderFrame();
	void finalizeFrame() const;
	void drawBounds() const;
	void drawAxes();
	HitTestResult hitTestRender(int mx, int my) const;

	GLuint framebuffer;
	GLuint color_rb;
	GLuint depth_rb;
	FTGLPixmapFont* font;
	int width;
	int height;
	float frameRate;
	float initialFrameRate;
	float cameraEulers[2];
	float cameraPos[3];
	float camr;
	float origCameraEulers[2];

	float moveFront;
	float moveBack;
	float moveLeft;
	float moveRight;
	float moveUp;
	float moveDown;
	SDL_Window* window;
	Uint32 prevFrameTime;

private:
	NavKit* navKit;
};