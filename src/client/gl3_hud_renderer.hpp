#ifndef GL3_HUD_RENDERER_HPP_
#define GL3_HUD_RENDERER_HPP_

#include "client/client.hpp"
#include "client/graphics.hpp"
#include "engine/macros.hpp"

class GL3Renderer;
class ShaderManager;

class GL3HudRenderer {
public:
	GL3HudRenderer(Client *client, GL3Renderer *renderer, ShaderManager *shaderManager);
	~GL3HudRenderer() = default;

	void render();

private:
	Client *client = nullptr;
	GL3Renderer *renderer = nullptr;
	ShaderManager *shaderManager = nullptr;

	GLuint vao;
	GLuint vbo;
};

#endif //GL3_HUD_RENDERER_HPP_