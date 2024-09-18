#include "..\include\NavKit\Obj.h"

Obj::Obj(NavKit* navKit): navKit(navKit) {
	loadObjName = "Load Obj";
	saveObjName = "Save Obj";
	lastObjFileName = loadObjName;
	lastSaveObjFileName = saveObjName;
	objLoaded = false;
	showObj = true;
	objToLoad = "";
	loadObj = false;
	objScroll = 0;
	bBoxPos[0] = 0;
	bBoxPos[1] = 0;
	bBoxPos[2] = 0;
	bBoxSize[0] = 600;
	bBoxSize[1] = 600;
	bBoxSize[2] = 600;
	navKit->log(RC_LOG_PROGRESS, "Setting bbox to (%f, %f, %f) (%f, %f, %f)", bBoxPos[0], bBoxPos[1], bBoxPos[2], bBoxSize[0], bBoxSize[1], bBoxSize[2]);

}

void Obj::copyObjFile(const std::string& from, BuildContext* ctx, const std::string& to) {
	auto start = std::chrono::high_resolution_clock::now();
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

	std::ifstream is(from, std::ios::in | std::ios::binary);
	std::ofstream os(to, std::ios::out | std::ios::binary);

	std::copy(std::istreambuf_iterator(is), std::istreambuf_iterator<char>(),
		std::ostreambuf_iterator(os));
	is.close();
	os.close();
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	std::string msg = "Finished saving Obj in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	ctx->log(RC_LOG_PROGRESS, msg.data());
}

void Obj::saveObjMesh(char* objToCopy, BuildContext* ctx, char* newFileName) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string msg = "Saving Obj to file at ";
	msg += std::ctime(&start_time);
	ctx->log(RC_LOG_PROGRESS, msg.data());
	CopyFile(objToCopy, newFileName, true);
	//std::thread saveObjThread(copyObjFile, objToCopy, ctx, newFileName);
	//saveObjThread.detach();
}

void Obj::loadObjMesh(Obj* obj) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string msg = "Loading Obj from file at ";
	msg += std::ctime(&start_time);
	obj->navKit->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();

	if (obj->navKit->geom->load(&obj->navKit->ctx, obj->objToLoad)) {
		if (obj->objLoadDone.empty()) {
			
			obj->navKit->geom->m_meshBMin[0] = obj->bBoxPos[0] - obj->bBoxSize[0] / 2;
			obj->navKit->geom->m_meshBMin[1] = obj->bBoxPos[1] - obj->bBoxSize[1] / 2;
			obj->navKit->geom->m_meshBMin[2] = obj->bBoxPos[2] - obj->bBoxSize[2] / 2;
			obj->navKit->geom->m_meshBMax[0] = obj->bBoxPos[0] + obj->bBoxSize[0] / 2;
			obj->navKit->geom->m_meshBMax[1] = obj->bBoxPos[1] + obj->bBoxSize[1] / 2;
			obj->navKit->geom->m_meshBMax[2] = obj->bBoxPos[2] + obj->bBoxSize[2] / 2;
			obj->navKit->log(RC_LOG_PROGRESS, "Setting bbox to (%f, %f, %f) (%f, %f, %f)", obj->bBoxPos[0], obj->bBoxPos[1], obj->bBoxPos[2], obj->bBoxSize[0], obj->bBoxSize[1], obj->bBoxSize[2]);

			obj->objLoadDone.push_back(true);
			auto end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
			msg = "Finished loading Obj in ";
			msg += std::to_string(duration.count());
			msg += " seconds";
			obj->navKit->log(RC_LOG_PROGRESS, msg.data());
		}
	}
	else {
		obj->navKit->log(RC_LOG_ERROR, "Error loading obj.");
	}
	obj->objToLoad.clear();
}

void Obj::setBBox(float* pos, float* size) {
	bBoxPos[0] = pos[0];
	bBoxPos[1] = pos[1];
	bBoxPos[2] = pos[2];
	bBoxSize[0] = size[0];
	bBoxSize[1] = size[1];
	bBoxSize[2] = size[2];
	if (navKit->geom != NULL) {
		navKit->geom->m_meshBMin[0] = bBoxPos[0] - bBoxSize[0] / 2;
		navKit->geom->m_meshBMin[1] = bBoxPos[1] - bBoxSize[1] / 2;
		navKit->geom->m_meshBMin[2] = bBoxPos[2] - bBoxSize[2] / 2;
		navKit->geom->m_meshBMax[0] = bBoxPos[0] + bBoxSize[0] / 2;
		navKit->geom->m_meshBMax[1] = bBoxPos[1] + bBoxSize[1] / 2;
		navKit->geom->m_meshBMax[2] = bBoxPos[2] + bBoxSize[2] / 2;
	}
}

void Obj::renderObj(InputGeom* m_geom, DebugDrawGL* m_dd) {
	// Draw mesh
	duDebugDrawTriMesh(m_dd, m_geom->getMesh()->getVerts(), m_geom->getMesh()->getVertCount(),
		m_geom->getMesh()->getTris(), m_geom->getMesh()->getNormals(), m_geom->getMesh()->getTriCount(), 0, 1.0f);
	// Draw bounds
	const float* bmin = m_geom->getMeshBoundsMin();
	const float* bmax = m_geom->getMeshBoundsMax();
	duDebugDrawBoxWire(m_dd, bmin[0], bmin[1], bmin[2], bmax[0], bmax[1], bmax[2], duRGBA(255, 255, 255, 128), 1.0f);
}

char* Obj::openLoadObjFileDialog(char* lastObjFolder) {
	nfdu8filteritem_t filters[1] = { { "Obj files", "obj" } };
	return FileUtil::openNfdLoadDialog(filters, 1);
}


char* Obj::openSaveObjFileDialog(char* lastObjFolder) {
	nfdu8filteritem_t filters[1] = { { "Obj files", "obj" } };
	return FileUtil::openNfdSaveDialog(filters, 1, "output");
}

void Obj::drawMenu() {
	if (imguiBeginScrollArea("Obj menu", navKit->renderer->width - 250 - 10, navKit->renderer->height - 10 - 205 - 15 - 200, 250, 205, &objScroll))
		navKit->gui->mouseOverMenu = true;
	if (imguiCheck("Show Obj", showObj))
		showObj = !showObj;
	imguiLabel("Load Obj file");
	if (imguiButton(loadObjName.c_str(), objToLoad.empty())) {
		char* fileName = openLoadObjFileDialog(loadObjName.data());
		if (fileName)
		{
			loadObjName = fileName;
			lastObjFileName = loadObjName;
			loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
			objToLoad = fileName;
			loadObj = true;
		}
	}
	imguiLabel("Save Obj file");
	if (imguiButton(saveObjName.c_str(), objLoaded)) {
		char* fileName = openSaveObjFileDialog(loadObjName.data());
		if (fileName)
		{
			loadObjName = fileName;
			lastSaveObjFileName = fileName;
			saveObjMesh(lastObjFileName.data(), &navKit->ctx, lastSaveObjFileName.data());
			loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
			saveObjName = loadObjName;
		}
	}
	imguiSeparatorLine();
	char polyText[64];
	char voxelText[64];
	if (navKit->geom && objToLoad.empty() && objLoaded)
	{
		snprintf(polyText, 64, "Verts: %.1fk  Tris: %.1fk",
			navKit->geom->getMesh()->getVertCount() / 1000.0f,
			navKit->geom->getMesh()->getTriCount() / 1000.0f);
		const float* bmin = navKit->sample->m_geom->getNavMeshBoundsMin();
		const float* bmax = navKit->sample->m_geom->getNavMeshBoundsMax();
		int gw = 0, gh = 0;
		rcCalcGridSize(bmin, bmax, navKit->sample->m_cellSize, &gw, &gh);
		snprintf(voxelText, 64, "Voxels: %d x %d", gw, gh);
	}
	else {
		snprintf(polyText, 64, "Verts: 0  Tris: 0");
		snprintf(voxelText, 64, "Voxels: 0 x 0");
	}
	imguiValue(polyText);
	imguiValue(voxelText);
	imguiEndScrollArea();
}

void Obj::finalizeLoad() {
	if (loadObj) {
		navKit->geom = new InputGeom();
		lastObjFileName = objToLoad;
		std::string msg = "Loading Obj file: '";
		msg += objToLoad;
		msg += "'...";
		navKit->log(RC_LOG_PROGRESS, msg.data());
		std::thread loadObjThread(&Obj::loadObjMesh, this);
		loadObjThread.detach();
		loadObj = false;
	}

	if (!objLoadDone.empty()) {
		navKit->gui->showLog = true;
		navKit->gui->logScroll = 0;
		navKit->ctx.dumpLog("Geom load log %s:", loadObjName.c_str());

		if (navKit->geom)
		{
			navKit->sample->handleMeshChanged(navKit->geom);
			objLoaded = true;
		}
		objLoadDone.clear();
	}

}