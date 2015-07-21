﻿#include "gl3_renderer.hpp"

#include <SDL2/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/logging.hpp"
#include "engine/math.hpp"

#include "chunk_renderer.hpp"
#include "gl3_debug_renderer.hpp"
#include "graphics.hpp"
#include "menu.hpp"
#include "stopwatch.hpp"

#include "game/world.hpp"

using namespace gui;

GL3Renderer::GL3Renderer(
	Client *client,
	Graphics *graphics,
	SDL_Window *window)
	:
	client(client),
	graphics(graphics),
	conf(*client->getConf()),
	window(window),
	shaderManager(),
	fontTimes(&shaderManager.getFontShader()),
	fontDejavu(&shaderManager.getFontShader()),
	chunkRenderer(client, this, &shaderManager),
	debugRenderer(client, this, &shaderManager, graphics),
	menuRenderer(client, this, &shaderManager, graphics),
	skyRenderer(client, this, &shaderManager, graphics)
{
	makeMaxFOV();
	makePerspectiveMatrix();
	makeOrthogonalMatrix();

	// light
	auto &defaultShader = shaderManager.getDefaultShader();
	defaultShader.setAmbientLightColor(ambientColor);
	defaultShader.setDiffuseLightDirection(diffuseDirection);
	defaultShader.setDiffuseLightColor(diffuseColor);

	auto &blockShader = shaderManager.getBlockShader();
	blockShader.setAmbientLightColor(ambientColor);
	blockShader.setDiffuseLightDirection(diffuseDirection);
	blockShader.setDiffuseLightColor(diffuseColor);

	// fog
	auto endFog = (conf.render_distance - 1) * Chunk::WIDTH;
	auto startFog = (conf.render_distance - 1) * Chunk::WIDTH * 1 / 2.0;

	defaultShader.setEndFogDistance(endFog);
	defaultShader.setStartFogDistance(startFog);

	blockShader.setEndFogDistance(endFog);
	blockShader.setStartFogDistance(startFog);

	buildCrossHair();

    // font
	fontTimes.load("fonts/times32.fnt");
	fontTimes.setEncoding(Font::Encoding::UTF8);
	fontDejavu.load("fonts/dejavusansmono16.fnt");
	fontDejavu.setEncoding(Font::Encoding::UTF8);

	// gl stuff
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_CULL_FACE);
	logOpenGLError();
}

GL3Renderer::~GL3Renderer() {
	LOG(DEBUG, "Destroying GL3 renderer");
}

void GL3Renderer::buildCrossHair() {
	glGenVertexArrays(1, &crossHairVAO);
	glGenBuffers(1, &crossHairVBO);
	glBindVertexArray(crossHairVAO);
	glBindBuffer(GL_ARRAY_BUFFER, crossHairVBO);

	HudVertexData vertexData[12] = {
			// x        y       r     g     b     a
			{{-20.0f,  -2.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{ 20.0f,  -2.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{ 20.0f,   2.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{ 20.0f,   2.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{-20.0f,   2.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{-20.0f,  -2.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{ -2.0f, -20.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{  2.0f, -20.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{  2.0f,  20.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{  2.0f,  20.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{ -2.0f,  20.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
			{{ -2.0f, -20.0f}, {0.0f, 0.0f, 0.0f, 0.5f}},
	};

	glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, 0);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, (void *) 8);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void GL3Renderer::resize() {
	makePerspectiveMatrix();
	makeOrthogonalMatrix();
}

void GL3Renderer::makePerspectiveMatrix() {
	double normalRatio = DEFAULT_WINDOWED_RES[0] / (double) DEFAULT_WINDOWED_RES[1];
	double currentRatio = graphics->getWidth() / (double) graphics->getHeight();
	double angle;

	float yfov = conf.fov / normalRatio * TAU / 360.0;
	if (currentRatio > normalRatio)
		angle = atan(tan(yfov / 2) * normalRatio / currentRatio) * 2;
	else
		angle = yfov;

	float zFar = Chunk::WIDTH * sqrt(3 * (conf.render_distance + 1) * (conf.render_distance + 1));
	glm::mat4 perspectiveMatrix = glm::perspective((float) angle,
			(float) currentRatio, ZNEAR, zFar);
	auto &defaultShader = shaderManager.getDefaultShader();
	defaultShader.setProjectionMatrix(perspectiveMatrix);
	auto &blockShader = shaderManager.getBlockShader();
	blockShader.setProjectionMatrix(perspectiveMatrix);
}

void GL3Renderer::makeOrthogonalMatrix() {
	float normalRatio = DEFAULT_WINDOWED_RES[0] / (double) DEFAULT_WINDOWED_RES[1];
	float currentRatio = graphics->getWidth() / (double) graphics->getHeight();
	glm::mat4 hudMatrix;
	if (currentRatio > normalRatio)
		hudMatrix = glm::ortho(-DEFAULT_WINDOWED_RES[0] / 2.0f, DEFAULT_WINDOWED_RES[0] / 2.0f, -DEFAULT_WINDOWED_RES[0]
				/ currentRatio / 2.0f, DEFAULT_WINDOWED_RES[0] / currentRatio / 2.0f, 1.0f, -1.0f);
	else
		hudMatrix = glm::ortho(-DEFAULT_WINDOWED_RES[1] * currentRatio / 2.0f, DEFAULT_WINDOWED_RES[1]
				* currentRatio / 2.0f, -DEFAULT_WINDOWED_RES[1] / 2.0f,
				DEFAULT_WINDOWED_RES[1] / 2.0f, 1.0f, -1.0f);
    shaderManager.getHudShader().setHudProjectionMatrix(hudMatrix);
    shaderManager.getFontShader().setFontProjectionMatrix(hudMatrix);
}

void GL3Renderer::makeMaxFOV() {
	float ratio = (float) DEFAULT_WINDOWED_RES[0] / DEFAULT_WINDOWED_RES[1];
	float yfov = conf.fov / ratio * TAU / 360.0;
	if (ratio < 1.0)
		maxFOV = yfov;
	else
		maxFOV = atan(ratio * tan(yfov / 2)) * 2;
}

void GL3Renderer::setConf(const GraphicsConf &conf) {
	GraphicsConf old_conf = this->conf;
	this->conf = conf;

	if (conf.render_distance != old_conf.render_distance) {
		makePerspectiveMatrix();

		auto endFog = (conf.render_distance - 1) * Chunk::WIDTH;
		auto startFog = (conf.render_distance - 1) * Chunk::WIDTH * 1 / 2.0;

		auto &defaultShader = shaderManager.getDefaultShader();
		defaultShader.setEndFogDistance(endFog);
		defaultShader.setStartFogDistance(startFog);

		auto &blockShader = shaderManager.getBlockShader();
		blockShader.setEndFogDistance(endFog);
		blockShader.setStartFogDistance(startFog);
	}

	if (conf.fov != old_conf.fov) {
		makePerspectiveMatrix();
		makeMaxFOV();
	}
}

void GL3Renderer::tick() {
	render();

	SDL_GL_SwapWindow(window);

	if (getCurrentTime() - lastStopWatchSave > millis(200)) {
		lastStopWatchSave = getCurrentTime();
		client->getStopwatch()->stop(CLOCK_ALL);
		client->getStopwatch()->save();
		client->getStopwatch()->start(CLOCK_ALL);
	}

	while (getCurrentTime() - lastFPSUpdate > millis(50)) {
		lastFPSUpdate += millis(50);
		fpsSum -= prevFPS[fpsIndex];
		fpsSum += fpsCounter;
		prevFPS[fpsIndex] = fpsCounter;
		fpsCounter = 0;
		fpsIndex = (fpsIndex + 1) % 20;
	}
	fpsCounter++;
}

void GL3Renderer::render() {
    logOpenGLError();

	skyRenderer.render();
	logOpenGLError();

	glClear(GL_DEPTH_BUFFER_BIT);
	logOpenGLError();
	
	chunkRenderer.render();
	renderTarget();
	renderPlayers();

	Player &player = client->getWorld()->getPlayer(client->getLocalClientId());
	// render overlay
	glDepthMask(false);
	glDisable(GL_DEPTH_TEST);
	if (client->getState() == Client::State::PLAYING) {
		renderHud(player);
		if (client->isDebugOn())
			debugRenderer.render();
	} else if (client->getState() == Client::State::IN_MENU){
		menuRenderer.render();
	}
	glDepthMask(true);
	glEnable(GL_DEPTH_TEST);
}

void GL3Renderer::renderHud(const Player &player) {
	glBindVertexArray(crossHairVAO);
	shaderManager.getHudShader().useProgram();
	glDrawArrays(GL_TRIANGLES, 0, 12);

	/*vec2f texs[4];
	texManager.bind(player.getBlock());
	glBindTexture(GL_TEXTURE_2D, texManager.getTexture());
	texManager.getTextureVertices(texs);

	glColor4f(1, 1, 1, 1);

	float d = (graphics->getWidth() < graphics->getHeight() ? graphics->getWidth() : graphics->getHeight()) * 0.05;
	glPushMatrix();
	glTranslatef(-graphics->getDrawWidth() * 0.48, -graphics->getDrawHeight() * 0.48, 0);
	glBegin(GL_QUADS);
		glTexCoord2f(texs[0][0], texs[0][1]); glVertex2f(0, 0);
		glTexCoord2f(texs[1][0], texs[1][1]); glVertex2f(d, 0);
		glTexCoord2f(texs[2][0], texs[2][1]); glVertex2f(d, d);
		glTexCoord2f(texs[3][0], texs[3][1]); glVertex2f(0, d);
	glEnd();
	glPopMatrix();*/
}

void GL3Renderer::renderTarget() {
	Player &player = client->getWorld()->getPlayer(client->getLocalClientId());
	if (!player.isValid())
		return;
	// view matrix for scene
	glm::mat4 viewMatrix = glm::rotate(glm::mat4(1.0f), (float) (-player.getPitch() / 360.0 * TAU), glm::vec3(1.0f, 0.0f, 0.0f));
	viewMatrix = glm::rotate(viewMatrix, (float) (-player.getYaw() / 360.0 * TAU), glm::vec3(0.0f, 1.0f, 0.0f));
	viewMatrix = glm::rotate(viewMatrix, (float) (-TAU / 4.0), glm::vec3(1.0f, 0.0f, 0.0f));
	viewMatrix = glm::rotate(viewMatrix, (float) (TAU / 4.0), glm::vec3(0.0f, 0.0f, 1.0f));
	vec3i64 playerPos = player.getPos();
	int64 m = RESOLUTION * Chunk::WIDTH;
	viewMatrix = glm::translate(viewMatrix, glm::vec3(
		(float) -((playerPos[0] % m + m) % m) / RESOLUTION,
		(float) -((playerPos[1] % m + m) % m) / RESOLUTION,
		(float) -((playerPos[2] % m + m) % m) / RESOLUTION)
	);
	auto &defaultShader = shaderManager.getDefaultShader();
	defaultShader.setViewMatrix(viewMatrix);
	defaultShader.useProgram();
	// TODO
}

void GL3Renderer::renderPlayers() {

}
