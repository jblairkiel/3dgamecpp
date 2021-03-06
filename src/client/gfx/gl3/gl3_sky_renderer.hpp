#ifndef GL3_SYK_RENDERER_HPP_
#define GL3_SYK_RENDERER_HPP_

#include "shared/engine/macros.hpp"
#include "shared/engine/vmath.hpp"
#include "client/client.hpp"
#include "client/gfx/graphics.hpp"
#include "client/gfx/component_renderer.hpp"

class GL3Renderer;
class ShaderManager;

class GL3SkyRenderer : public ComponentRenderer {
public:
	GL3SkyRenderer(Client *client, GL3Renderer *renderer);

	void render() override;

private:
	Client *client = nullptr;
	GL3Renderer *renderer = nullptr;

	vec3f skyColor{ 0.15f, 0.15f, 0.9f };
	vec3f fogColor{ 0.6f, 0.6f, 0.8f };

	GLuint vao;
	GLuint vbo;

	PACKED(
	struct VertexData {
		GLfloat xyz[3];
		GLfloat rgba[4];
	});
};

#endif //GL3_SYK_RENDERER_HPP_
