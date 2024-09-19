#include "..\include\NavKit\Renderer.h"

Renderer::Renderer(NavKit* navKit) : navKit(navKit) {
	framebuffer = 0;
	color_rb = 0;
	depth_rb = 0;
	height = 0;
	width = 0;
	window = 0;
	sdlRenderer = 0;

	cameraEulers[0] = 45.0, cameraEulers[1] = 0.0;
	cameraPos[0] = 0, cameraPos[1] = 20, cameraPos[2] = 20;
	camr = 1000;
	origCameraEulers[0] = 0, origCameraEulers[1] = 0; // Used to compute rotational changes across frames.
	prevFrameTime = 0;

	moveFront = 0.0f, moveBack = 0.0f, moveLeft = 0.0f, moveRight = 0.0f, moveUp = 0.0f, moveDown = 0.0f;
}

void Renderer::initFrameBuffer(int width, int height) {
	glewInit();
	// Build the framebuffer.
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	glGenRenderbuffers(1, &color_rb);
	glBindRenderbuffer(GL_RENDERBUFFER, color_rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb);

	glGenRenderbuffers(1, &depth_rb);
	glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Renderer::initWindowAndRenderer() {
	// Init SDL
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
	{
		printf("Could not initialise SDL.\nError: %s\n", SDL_GetError());
		return false;
	}

	// Use OpenGL render driver.
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

	// Enable depth buffer.
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	// Set color channel depth.
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

	// 4x MSAA.
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

	SDL_DisplayMode displayMode;
	SDL_GetCurrentDisplayMode(0, &displayMode);
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
	float aspect = 16.0f / 9.0f;
	width = rcMin(displayMode.w, (int)(displayMode.h * aspect)) - 120;
	height = displayMode.h - 120;
	int errorCode = SDL_CreateWindowAndRenderer(width, height, flags, &window, &sdlRenderer);
	SDL_SetWindowMinimumSize(window, 1024, 768);
	initFrameBuffer(width, height);

	if (errorCode != 0 || !window || !sdlRenderer)
	{
		printf("Could not initialise SDL opengl\nError: %s\n", SDL_GetError());
		return false;
	}

	SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	std::string navKitVersion = NavKit_VERSION_MAJOR "." NavKit_VERSION_MINOR "." NavKit_VERSION_PATCH;
	std::string title = "NavKit v";
	title += navKitVersion;
	SDL_SetWindowTitle(window, title.data());

	if (!imguiRenderGLInit("DroidSans.ttf"))
	{
		printf("Could not init GUI renderer.\n");
		SDL_Quit();
		return false;
	}

	NFD_Init();

	// Fog.
	float fogColor[4] = { 0.32f, 0.31f, 0.30f, 1.0f };
	glEnable(GL_FOG);
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogf(GL_FOG_START, camr * 0.1f);
	glFogf(GL_FOG_END, camr * 1.25f);
	glFogfv(GL_FOG_COLOR, fogColor);

	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	prevFrameTime = SDL_GetTicks();
}

void Renderer::handleResize() {
	width = SDL_GetWindowSurface(window)->w;
	height = SDL_GetWindowSurface(window)->h;
	navKit->log(rcLogCategory::RC_LOG_PROGRESS, ("Window resized. New dimensions: " + std::to_string(width) + "x" + std::to_string(height)).c_str());
}

void Renderer::renderFrame() {
	// Set the viewport.
	glViewport(0, 0, width, height);
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	// Clear the screen
	glClearColor(0.3f, 0.3f, 0.32f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);

	// Compute the projection matrix.
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(50.0f, (float)width / (float)height, 1.0f, camr);
	GLdouble projectionMatrix[16];
	glGetDoublev(GL_PROJECTION_MATRIX, projectionMatrix);

	// Compute the modelview matrix.
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glRotatef(cameraEulers[0], 1, 0, 0);
	glRotatef(cameraEulers[1], 0, 1, 0);
	glTranslatef(-cameraPos[0], -cameraPos[1], -cameraPos[2]);
	GLdouble modelviewMatrix[16];
	glGetDoublev(GL_MODELVIEW_MATRIX, modelviewMatrix);
	Uint32 time = SDL_GetTicks();
	float dt = (time - prevFrameTime) / 1000.0f;
	prevFrameTime = time;

	// Clamp the framerate so that we do not hog all the CPU.
	const float MIN_FRAME_TIME = 1.0f / 40.0f;
	if (dt < MIN_FRAME_TIME)
	{
		int ms = (int)((MIN_FRAME_TIME - dt) * 1000.0f);
		if (ms > 10) ms = 10;
		if (ms >= 0) SDL_Delay(ms);
	}

	// Handle keyboard movement.
	const Uint8* keystate = SDL_GetKeyboardState(NULL);
	moveFront = rcClamp(moveFront + dt * 4 * ((keystate[SDL_SCANCODE_W] || keystate[SDL_SCANCODE_UP]) ? 1 : -1), 0.0f, 1.0f);
	moveLeft = rcClamp(moveLeft + dt * 4 * ((keystate[SDL_SCANCODE_A] || keystate[SDL_SCANCODE_LEFT]) ? 1 : -1), 0.0f, 1.0f);
	moveBack = rcClamp(moveBack + dt * 4 * ((keystate[SDL_SCANCODE_S] || keystate[SDL_SCANCODE_DOWN]) ? 1 : -1), 0.0f, 1.0f);
	moveRight = rcClamp(moveRight + dt * 4 * ((keystate[SDL_SCANCODE_D] || keystate[SDL_SCANCODE_RIGHT]) ? 1 : -1), 0.0f, 1.0f);
	moveUp = rcClamp(moveUp + dt * 4 * ((keystate[SDL_SCANCODE_Q] || keystate[SDL_SCANCODE_PAGEUP]) ? 1 : -1), 0.0f, 1.0f);
	moveDown = rcClamp(moveDown + dt * 4 * ((keystate[SDL_SCANCODE_E] || keystate[SDL_SCANCODE_PAGEDOWN]) ? 1 : -1), 0.0f, 1.0f);
	float movex = (moveRight - moveLeft) * navKit->keybSpeed * dt;
	float movey = (moveBack - moveFront) * navKit->keybSpeed * dt + navKit->scrollZoom * 2.0f;
	navKit->scrollZoom = 0;

	cameraPos[0] += movex * (float)modelviewMatrix[0];
	cameraPos[1] += movex * (float)modelviewMatrix[4];
	cameraPos[2] += movex * (float)modelviewMatrix[8];

	cameraPos[0] += movey * (float)modelviewMatrix[2];
	cameraPos[1] += movey * (float)modelviewMatrix[6];
	cameraPos[2] += movey * (float)modelviewMatrix[10];

	cameraPos[1] += (moveUp - moveDown) * navKit->keybSpeed * dt;

	glFrontFace(GL_CW);
	if (navKit->navp->navpLoaded && navKit->navp->showNavp) {
		navKit->navp->renderNavMesh();
	}
	if (navKit->airg->airgLoaded && navKit->airg->showAirg) {
		navKit->airg->renderAirg();
	}
	if (navKit->obj->objLoaded && navKit->obj->showObj) {
		navKit->obj->renderObj(navKit->geom, &navKit->m_dd);
	}
}

void Renderer::finalizeFrame() {
	glEnable(GL_DEPTH_TEST);
	SDL_GL_SwapWindow(window);
}

int Renderer::hitTestRender(int mx, int my) {
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glClearColor(1.0, 1.0, 1.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			navKit->log(RC_LOG_ERROR, ("FB error, status: 0x" + std::to_string((int)status)).c_str());

			printf("FB error, status: 0x%x\n", status);
			return -1;
		}
		navKit->navp->renderNavMeshForHitTest();

		GLubyte pixel[4];
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(mx, my, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		int selectedArea = int(pixel[1]) * 255 + int(pixel[2]);
		if (selectedArea == 65280) {
			navKit->log(RC_LOG_PROGRESS, "Deselected area.");
			return -1;
		}
		navKit->log(RC_LOG_PROGRESS, ("Selected area: " + std::to_string(selectedArea)).c_str());
		return selectedArea;
	}