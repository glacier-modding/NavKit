#pragma once;

#include "NavKit.h"

class NavKit;

class Renderer {
public:
	Renderer(NavKit* navKit);
	
	void initFrameBuffer(int width, int height);
	bool initWindowAndRenderer();
	void renderFrame();
	void finalizeFrame();
	int hitTestRender(int mx, int my);

	GLuint framebuffer;
	GLuint color_rb;
	GLuint depth_rb;
	int width;
	int height;
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
	SDL_Renderer* sdlRenderer;
	Uint32 prevFrameTime;

private:
	NavKit* navKit;
};