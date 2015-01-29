#include "graphics.hpp"

#include "game/world.hpp"
#include "game/chunk.hpp"
#include "stopwatch.hpp"

#include "io/logging.hpp"

#include <cinttypes>

void Graphics::switchToPerspective() {
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixd(perspectiveMatrix);
	glMatrixMode(GL_MODELVIEW);
}

void Graphics::switchToOrthogonal() {
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixd(orthogonalMatrix);
	glMatrixMode(GL_MODELVIEW);
}

void Graphics::render() {

	if (fbo) {
		// render to the fbo and not the screen
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	} else {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDrawBuffer(GL_BACK);
	}

	stopwatch->start(CLOCK_CLR);
	glClear(GL_DEPTH_BUFFER_BIT);
	logOpenGLError();
	stopwatch->stop(CLOCK_CLR);

	glMatrixMode(GL_MODELVIEW);
	switchToPerspective();
	glLoadIdentity();
	texManager.bind(0);

	Player &player = world->getPlayer(localClientID);
	if (player.isValid()) {
		// Render sky
		glRotated(-player.getPitch(), 1, 0, 0);
		renderSky();

		// Render Scene
		glRotatef(-player.getYaw(), 0, 1, 0);
		glRotatef(-90, 1, 0, 0);
		glRotatef(90, 0, 0, 1);
		vec3i64 playerPos = player.getPos();
		int64 m = RESOLUTION * Chunk::WIDTH;
		glTranslatef(
			(float) -((playerPos[0] % m + m) % m) / RESOLUTION,
			(float) -((playerPos[1] % m + m) % m) / RESOLUTION,
			(float) -((playerPos[2] % m + m) % m) / RESOLUTION
		);
		renderScene();

		// copy framebuffer to screen, blend multisampling on the way
		if (fbo) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
			glDrawBuffer(GL_BACK);
			glBlitFramebuffer(
					0, 0, width, height,
					0, 0, width, height,
					GL_COLOR_BUFFER_BIT,
					GL_NEAREST
			);
			logOpenGLError();

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDrawBuffer(GL_BACK);
		}
	}

	// render overlay
	switchToOrthogonal();
	glLoadIdentity();
	texManager.bind(0);

	if (state == PLAYING && player.isValid()) {
		stopwatch->start(CLOCK_HUD);
		renderHud(player);
		if (debugActive)
			renderDebugInfo(player);
		stopwatch->stop(CLOCK_HUD);
	} else if (state == IN_MENU){
		renderMenu();
	}
}

void Graphics::renderSky() {
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);
	glDisable(GL_FOG);

	// sky
	glDepthMask(false);
	glBegin(GL_QUADS);

	glColor3fv(fogColor.ptr());
	glVertex3d(-2, -2, 2);
	glVertex3d(2, -2, 2);
	glVertex3d(2, 0.3, -1);
	glVertex3d(-2, 0.3, -1);

	glVertex3d(-2, 0.3, -1);
	glVertex3d(2, 0.3, -1);
	glColor3fv(skyColor.ptr());
	glVertex3d(2, 2, 2);
	glVertex3d(-2, 2, 2);

	glEnd();
	glDepthMask(true);
}

void Graphics::renderScene() {
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_LIGHTING);
	if (conf.fog != Fog::NONE) glEnable(GL_FOG);

	glLightfv(GL_LIGHT0, GL_POSITION, sunLightPosition.ptr());

	// render chunks
	stopwatch->start(CLOCK_CHR);
	renderChunks();
	renderTarget();
	stopwatch->stop(CLOCK_CHR);

	// render players
	stopwatch->start(CLOCK_PLA);
	renderPlayers();
	stopwatch->stop(CLOCK_PLA);
}

void Graphics::renderChunks() {
	Player &localPlayer = world->getPlayer(localClientID);
	vec3i64 pc = localPlayer.getChunkPos();
	vec3d lookDir = getVectorFromAngles(localPlayer.getYaw(), localPlayer.getPitch());

	newFaces = 0;
	newChunks = 0;
	int length = conf.render_distance * 2 + 3;

	stopwatch->start(CLOCK_NDL);
	vec3i64 ccc;
	while (newChunks < MAX_NEW_CHUNKS && newFaces < MAX_NEW_QUADS && world->popChangedChunk(&ccc)) {
		Chunk *chunk = world->getChunk(ccc);
		if(chunk) {
			uint index = ((((ccc[2] % length) + length) % length) * length
					+ (((ccc[1] % length) + length) % length)) * length
					+ (((ccc[0] % length) + length) % length);
			if (chunk->pollChanged() || !dlHasChunk[index] || dlChunks[index] != ccc) {
				GLuint lid = firstDL + index;
				faces -= dlFaces[index];
				glNewList(lid, GL_COMPILE);
				dlFaces[index] = renderChunk(*chunk);
				glEndList();
				dlChunks[index] = ccc;
				dlHasChunk[index] = true;
				passThroughs[index] = chunk->getPassThroughs();
				faces += dlFaces[index];
				newFaces += dlFaces[index];
			}
		}
	}
	stopwatch->stop(CLOCK_NDL);

	logOpenGLError();

	fringe[0] = pc;
	indices[0] = ((((pc[2] % length) + length) % length) * length
			+ (((pc[1] % length) + length) % length)) * length
			+ (((pc[0] % length) + length) % length);
	exits[indices[0]] = 0|0b111111;
	visited[indices[0]] = true;
	int fringeSize = 1;
	size_t fringeStart = 0;
	size_t fringeEnd = 1;

	visibleChunks = 0;
	visibleFaces = 0;
	while (fringeSize > 0) {
		visibleChunks++;
		vec3i64 cc = fringe[fringeStart];
		vec3i64 cd = cc - pc;
		int index = indices[fringeStart];
		visited[index] = false;
		fringeStart = (fringeStart + 1) % fringeCapacity;
		fringeSize--;

		if (dlHasChunk[index] && dlChunks[index] == cc) {
			if (dlFaces[index] > 0) {
				visibleFaces += dlFaces[index];
				stopwatch->start(CLOCK_DLC);
				glPushMatrix();
				logOpenGLError();
				glTranslatef(cd[0] * (int) Chunk::WIDTH, cd[1] * (int) Chunk::WIDTH, cd[2] * (int) Chunk::WIDTH);
				glCallList(firstDL + index);
				logOpenGLError();
				glPopMatrix();
				logOpenGLError();
				stopwatch->stop(CLOCK_DLC);
			}

			for (int d = 0; d < 6; d++) {
				if ((exits[index] & (1 << d)) == 0)
					continue;

				vec3i64 ncc = cc + DIRS[d].cast<int64>();
				vec3i64 ncd = ncc - pc;

				if ((uint) abs(ncd[0]) > conf.render_distance + 1
						|| (uint) abs(ncd[1]) > conf.render_distance + 1
						|| (uint) abs(ncd[2]) > conf.render_distance + 1
						|| !inFrustum(ncc, localPlayer.getPos(), lookDir))
					continue;

				int nIndex = ((((ncc[2] % length) + length) % length) * length
						+ (((ncc[1] % length) + length) % length)) * length
						+ (((ncc[0] % length) + length) % length);

				if (!visited[nIndex]) {
					visited[nIndex] = true;
					exits[nIndex] = 0;
					fringe[fringeEnd] = ncc;
					indices[fringeEnd] = nIndex;
					fringeEnd = (fringeEnd + 1) % fringeCapacity;
					fringeSize++;
				}

				if (dlChunks[nIndex] == ncc) {
					int shift = 0;
					int invD = (d + 3) % 6;
					for (int d1 = 0; d1 < invD; d1++) {
						if (ncd[d1 % 3] * (d1 * (-2) + 5) >= 0)
							exits[nIndex] |= ((passThroughs[nIndex] & (1 << (shift + invD - d1 - 1))) > 0) << d1;
						shift += 5 - d1;
					}
					for (int d2 = invD + 1; d2 < 6; d2++) {
						if (ncd[d2 % 3] * (d2 * (-2) + 5) >= 0)
							exits[nIndex] |= ((passThroughs[nIndex] & (1 << (shift + d2 - invD - 1))) > 0) << d2;
					}
				}
			}
		}
	}
}

void Graphics::renderTarget() {
	Player &localPlayer = world->getPlayer(localClientID);
	vec3i64 pc = localPlayer.getChunkPos();

	vec3i64 tbc;
	vec3i64 tcc;
	vec3ui8 ticc;
	int td;
	bool target = localPlayer.getTargetedFace(&tbc, &td);
	if (target) {
		tcc = bc2cc(tbc);
		ticc = bc2icc(tbc);
	}

	logOpenGLError();

	if (target) {
		glDisable(GL_TEXTURE_2D);
		glBegin(GL_QUADS);
		glColor4d(0.0, 0.0, 0.0, 1.0);
		vec3d pointFive(0.5, 0.5, 0.5);
		for (int d = 0; d < 6; d++) {
			glNormal3d(DIRS[d][0], DIRS[d][1], DIRS[d][2]);
			for (int j = 0; j < 4; j++) {
				vec3d dirOff = DIRS[d].cast<double>() * 0.0005;
				vec3d vOff[4];
				vOff[0] = QUAD_CYCLES_3D[d][j].cast<double>() - pointFive;
				vOff[0][OTHER_DIR_DIMS[d][0]] *= 1.001;
				vOff[0][OTHER_DIR_DIMS[d][1]] *= 1.001;
				vOff[1] = QUAD_CYCLES_3D[d][j].cast<double>() - pointFive;
				vOff[1][OTHER_DIR_DIMS[d][0]] *= 0.97;
				vOff[1][OTHER_DIR_DIMS[d][1]] *= 0.97;
				vOff[2] = QUAD_CYCLES_3D[d][(j + 3) % 4].cast<double>() - pointFive;
				vOff[2][OTHER_DIR_DIMS[d][0]] *= 0.97;
				vOff[2][OTHER_DIR_DIMS[d][1]] *= 0.97;
				vOff[3] = QUAD_CYCLES_3D[d][(j + 3) % 4].cast<double>() - pointFive;
				vOff[3][OTHER_DIR_DIMS[d][0]] *= 1.001;
				vOff[3][OTHER_DIR_DIMS[d][1]] *= 1.001;

				for (int k = 0; k < 4; k++) {
					vec3d vertex = (tbc - pc * Chunk::WIDTH).cast<double>() + dirOff + vOff[k] + pointFive;
					glVertex3d(vertex[0], vertex[1], vertex[2]);
				}
			}
		}
		glEnd();
		glEnable(GL_TEXTURE_2D);
	}
	logOpenGLError();
}

int Graphics::renderChunk(const Chunk &c) {
	if (c.getAirBlocks() == 0)
		return 0;

	newChunks++;
	int faces = 0;

	texManager.bind(0);
	glBegin(GL_QUADS);
	texManager.bind(1);

	uint ds[3];
	vec3ui8 uDirs[3];
	for (uint8 d = 0; d < 3; d++) {
		uDirs[d] = DIRS[d].cast<uint8>();
		ds[d] = Chunk::getBlockIndex(uDirs[d]);
	}

	const uint8 *blocks = c.getBlocks();

	for (uint8 d = 0; d < 3; d++) {
		uint i = 0;
		for (uint8 z = 0; z < Chunk::WIDTH; z++) {
			for (uint8 y = 0; y < Chunk::WIDTH; y++) {
				for (uint8 x = 0; x < Chunk::WIDTH; x++, i++) {
					if ((x == Chunk::WIDTH - 1 && d==0)
							|| (y == Chunk::WIDTH - 1 && d==1)
							|| (z == Chunk::WIDTH - 1 && d==2))
						continue;

					uint ni = i + ds[d];

					uint8 thisType = blocks[i];
					uint8 thatType = blocks[ni];
					if((thisType == 0) != (thatType == 0)) {
						vec3ui8 faceBlock;
						uint8 faceType;
						uint8 faceDir;
						if (thisType == 0) {
							vec3ui8 dir = uDirs[d];
							faceBlock = vec3ui8(x, y, z) + dir;
							faceDir = (uint8) (d + 3);
							faceType = thatType;
						} else {
							faceBlock = vec3ui8(x, y, z);
							faceDir = d;
							faceType = thisType;
						}

						uint8 corners = 0;
						for (int j = 0; j < 8; ++j) {
							vec3i v = EIGHT_CYCLES_3D[faceDir][j];
							vec3i dIcc = faceBlock.cast<int>() + v;
							if (		dIcc[0] < 0 || dIcc[0] >= (int) Chunk::WIDTH
									||	dIcc[1] < 0 || dIcc[1] >= (int) Chunk::WIDTH
									||	dIcc[2] < 0 || dIcc[2] >= (int) Chunk::WIDTH)
								continue;
							if (c.getBlock(dIcc.cast<uint8>())) {
								corners |= 1 << j;
							}
						}

						if (texManager.bind(faceType)) {
							glEnd();
							glBindTexture(GL_TEXTURE_2D, texManager.getTexture());
							glBegin(GL_QUADS);
						}

						vec2f texs[4];
						vec3i64 bc = c.getCC() * c.WIDTH + faceBlock.cast<int64>();
						texManager.getTextureVertices(bc, faceDir, texs);

						vec3f color = {1.0, 1.0, 1.0};

						glNormal3f(DIRS[faceDir][0], DIRS[faceDir][1], DIRS[faceDir][2]);
						for (int j = 0; j < 4; j++) {
							vec2f tex = texs[j];
							glTexCoord2f(tex[0], tex[1]);
							double light = 1.0;
							bool s1 = (corners & FACE_CORNER_MASK[j][0]) > 0;
							bool s2 = (corners & FACE_CORNER_MASK[j][2]) > 0;
							bool m = (corners & FACE_CORNER_MASK[j][1]) > 0;
							if (s1)
								light -= 0.2;
							if (s2)
								light -= 0.2;
							if (m && !(s1 && s2))
								light -= 0.2;
							glColor3f(color[0] * light, color[1] * light, color[2] * light);
							vec3f vertex = (faceBlock.cast<int>() + QUAD_CYCLES_3D[faceDir][j]).cast<float>();
							glVertex3f(vertex[0], vertex[1], vertex[2]);
						}
						faces++;
					}
				}
			}
		}
	}
	glEnd();
	return faces;
}

void Graphics::renderPlayers() {
	glBindTexture(GL_TEXTURE_2D, 0);
	glBegin(GL_QUADS);
	for (uint i = 0; i < MAX_CLIENTS; i++) {
		if (i == localClientID)
			continue;
		Player &player = world->getPlayer(i);
		if (!player.isValid())
			continue;
		vec3i64 pos = player.getPos();
		for (int d = 0; d < 6; d++) {
			vec3i8 dir = DIRS[d];
			glColor3d(0.8, 0.2, 0.2);

			glNormal3d(dir[0], dir[1], dir[2]);
			for (int j = 0; j < 4; j++) {
				vec3i off(
					(QUAD_CYCLES_3D[d][j][0] * 2 - 1) * Player::RADIUS,
					(QUAD_CYCLES_3D[d][j][1] * 2 - 1) * Player::RADIUS,
					QUAD_CYCLES_3D[d][j][2] * Player::HEIGHT - Player::EYE_HEIGHT
				);
				glTexCoord2d(QUAD_CYCLE_2D[j][0], QUAD_CYCLE_2D[j][1]);
				vec3d vertex = (pos + off.cast<int64>()).cast<double>() * (1.0 / RESOLUTION);
				glVertex3d(vertex[0], vertex[1], vertex[2]);
			}
		}
	}
	glEnd();
}

void Graphics::renderHud(const Player &player) {
	glDisable(GL_LIGHTING);
	glDisable(GL_FOG);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	glColor4d(0, 0, 0, 0.5);
	glBegin(GL_QUADS);
	glVertex2d(-20, -2);
	glVertex2d(20, -2);
	glVertex2d(20, 2);
	glVertex2d(-20, 2);

	glVertex2d(-2, -20);
	glVertex2d(2, -20);
	glVertex2d(2, 20);
	glVertex2d(-2, 20);
	glEnd();

	glEnable(GL_TEXTURE_2D);

	vec2f texs[4];
	texManager.bind(player.getBlock());
	glBindTexture(GL_TEXTURE_2D, texManager.getTexture());
	texManager.getTextureVertices(texs);

	glColor4f(1, 1, 1, 1);

	float d = (width < height ? width : height) * 0.05;
	glPushMatrix();
	glTranslatef(-drawWidth * 0.48, -drawHeight * 0.48, 0);
	glBegin(GL_QUADS);
		glTexCoord2f(texs[0][0], texs[0][1]); glVertex2f(0, 0);
		glTexCoord2f(texs[1][0], texs[1][1]); glVertex2f(d, 0);
		glTexCoord2f(texs[2][0], texs[2][1]); glVertex2f(d, d);
		glTexCoord2f(texs[3][0], texs[3][1]); glVertex2f(0, d);
	glEnd();
	glPopMatrix();
}

void Graphics::renderDebugInfo(const Player &player) {
	vec3i64 playerPos = player.getPos();
	vec3d playerVel = player.getVel();
	uint32 windowFlags = SDL_GetWindowFlags(window);

	glDisable(GL_TEXTURE_2D);
	glPushMatrix();
	glColor3f(1.0f, 1.0f, 1.0f);
	glTranslatef(-drawWidth / 2 + 3, drawHeight / 2, 0);
	char buffer[1024];
	#define RENDER_LINE(...) sprintf(buffer, __VA_ARGS__);\
			glTranslatef(0, -16, 0);\
			font->Render(buffer)

	RENDER_LINE("fps: %d", fpsSum);
	RENDER_LINE("new faces: %d", newFaces);
	RENDER_LINE("faces: %d", faces);
	RENDER_LINE("x: %" PRId64 "(%" PRId64 ")", playerPos[0], (int64) floor(playerPos[0] / (double) RESOLUTION));
	RENDER_LINE("y: %" PRId64 " (%" PRId64 ")", playerPos[1], (int64) floor(playerPos[1] / (double) RESOLUTION));
	RENDER_LINE("z: %" PRId64 " (%" PRId64 ")", playerPos[2],
			(int64) floor((playerPos[2] - Player::EYE_HEIGHT - 1) / (double) RESOLUTION));
	RENDER_LINE("yaw:   %6.1f", player.getYaw());
	RENDER_LINE("pitch: %6.1f", player.getPitch());
	RENDER_LINE("xvel: %8.1f", playerVel[0]);
	RENDER_LINE("yvel: %8.1f", playerVel[1]);
	RENDER_LINE("zvel: %8.1f", playerVel[2]);
	RENDER_LINE("chunks loaded: %" PRIuPTR "", world->getNumChunks());
	RENDER_LINE("chunks visible: %d", visibleChunks);
	RENDER_LINE("faces visible: %d", visibleFaces);
	RENDER_LINE("block: %d", player.getBlock());
	if ((SDL_WINDOW_FULLSCREEN & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_FULLSCREEN");
	if ((SDL_WINDOW_FULLSCREEN_DESKTOP & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_FULLSCREEN_DESKTOP");
	if ((SDL_WINDOW_OPENGL & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_OPENGL");
	if ((SDL_WINDOW_SHOWN & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_SHOWN");
	if ((SDL_WINDOW_HIDDEN & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_HIDDEN");
	if ((SDL_WINDOW_BORDERLESS & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_BORDERLESS");
	if ((SDL_WINDOW_RESIZABLE & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_RESIZABLE");
	if ((SDL_WINDOW_MINIMIZED & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_MINIMIZED");
	if ((SDL_WINDOW_MAXIMIZED & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_MAXIMIZED");
	if ((SDL_WINDOW_INPUT_GRABBED & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_INPUT_GRABBED");
	if ((SDL_WINDOW_INPUT_FOCUS & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_INPUT_FOCUS");
	if ((SDL_WINDOW_MOUSE_FOCUS & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_MOUSE_FOCUS");
	if ((SDL_WINDOW_FOREIGN & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_FOREIGN");
	if ((SDL_WINDOW_ALLOW_HIGHDPI & windowFlags) > 0)
		glColor3f(1.0f, 0.0f, 0.0f);
	else
		glColor3f(1.0f, 1.0f, 1.0f);
	RENDER_LINE("SDL_WINDOW_ALLOW_HIGHDPI");
//	if ((SDL_WINDOW_MOUSE_CAPTURE & windowFlags) > 0)
//		glColor3f(1.0f, 0.0f, 0.0f);
//	else
//		glColor3f(1.0f, 1.0f, 1.0f);
//	RENDER_LINE("SDL_WINDOW_MOUSE_CAPTURE");

	glPopMatrix();

	if (stopwatch)
		renderPerformance();
}

void Graphics::renderPerformance() {
	const char *rel_names[] = {
		"CLR",
		"NDL",
		"DLC",
		"CHL",
		"CHR",
		"PLA",
		"HUD",
		"FLP",
		"TIC",
		"NET",
		"SYN",
		"FSH",
		"ALL"
	};

	vec3f rel_colors[] {
		{0.6f, 0.6f, 1.0f},
		{0.0f, 0.0f, 0.8f},
		{0.6f, 0.0f, 0.8f},
		{0.0f, 0.6f, 0.8f},
		{0.4f, 0.4f, 0.8f},
		{0.7f, 0.7f, 0.0f},
		{0.8f, 0.8f, 0.3f},
		{0.0f, 0.8f, 0.0f},
		{0.0f, 0.4f, 0.0f},
		{0.7f, 0.1f, 0.7f},
		{0.7f, 0.7f, 0.4f},
		{0.2f, 0.6f, 0.6f},
		{0.8f, 0.0f, 0.0f},
	};

	float rel = 0.0;
	float cum_rels[CLOCK_ID_NUM + 1];
	cum_rels[0] = 0;
	float center_positions[CLOCK_ID_NUM];

	glPushMatrix();
	glTranslatef(+drawWidth / 2.0, -drawHeight / 2, 0);
	glScalef(10.0, drawHeight, 1.0);
	glTranslatef(-1, 0, 0);
	glBegin(GL_QUADS);
	for (uint i = 0; i < CLOCK_ID_NUM; ++i) {
		glColor3f(rel_colors[i][0], rel_colors[i][1], rel_colors[i][2]);
		glVertex2f(0, rel);
		glVertex2f(1, rel);
		rel += stopwatch->getRel(i);
		cum_rels[i + 1] = rel;
		center_positions[i] = (cum_rels[i] + cum_rels[i + 1]) / 2.0;
		glVertex2f(1, rel);
		glVertex2f(0, rel);
	}
	glEnd();
	glPopMatrix();

	static const float REL_THRESHOLD = 0.001;
	uint labeled_ids[CLOCK_ID_NUM];
	int num_displayed_labels = 0;
	for (int i = 0; i < CLOCK_ID_NUM; ++i) {
		if (stopwatch->getRel(i) > REL_THRESHOLD)
			labeled_ids[num_displayed_labels++] = i;
	}

	float used_positions[CLOCK_ID_NUM + 2];
	used_positions[0] = 0.0;
	for (int i = 0; i < num_displayed_labels; ++i) {
		int id = labeled_ids[i];
		used_positions[i + 1] = center_positions[id];
	}
	used_positions[num_displayed_labels + 1] = 1.0;

	for (int iteration = 0; iteration < 3; ++iteration)
	for (int i = 1; i < num_displayed_labels + 1; ++i) {
		float d1 = used_positions[i] - used_positions[i - 1];
		float d2 = used_positions[i + 1] - used_positions[i];
		float diff = 2e-4 * (1.0 / d1 - 1.0 / d2);
		used_positions[i] += clamp((double) diff, -0.02, 0.02);
	}

	glPushMatrix();
	glTranslatef(+drawWidth / 2.0, -drawHeight / 2, 0);
	glTranslatef(-15, 0, 0);
	glRotatef(90.0, 0.0, 0.0, 1.0);
	for (int i = 0; i < num_displayed_labels; ++i) {
		int id = labeled_ids[i];
		glPushMatrix();
		glTranslatef(used_positions[i + 1] * drawHeight - 14, 0, 0);
		char buffer[1024];
		sprintf(buffer, "%s", rel_names[id]);
		auto color = rel_colors[id];
		glColor3f(color[0], color[1], color[2]);
		font->Render(buffer);
		glPopMatrix();
	}
	glPopMatrix();
}

bool Graphics::inFrustum(vec3i64 cc, vec3i64 pos, vec3d lookDir) {
	double chunkDia = sqrt(3) * Chunk::WIDTH * RESOLUTION;
	vec3d cp = (cc * Chunk::WIDTH * RESOLUTION - pos).cast<double>();
	double chunkLookDist = lookDir * cp + chunkDia;
	if (chunkLookDist < 0)
		return false;
	vec3d orthoChunkPos = cp - lookDir * chunkLookDist;
	double orthoChunkDist = std::max(0.0, orthoChunkPos.norm() - chunkDia);
	return atan(orthoChunkDist / chunkLookDist) <= maxFOV / 2;
}
