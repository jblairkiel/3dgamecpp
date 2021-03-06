#include "gl3_target_renderer.hpp"

#define GLM_FORCE_RADIANS

#include <glm/gtc/matrix_transform.hpp>

#include "shared/engine/logging.hpp"
#include "shared/engine/math.hpp"
#include "client/config.hpp"

#include "gl3_renderer.hpp"

GL3TargetRenderer::GL3TargetRenderer(Client *client, GL3Renderer *renderer) :
	client(client),
	renderer(renderer)
{
	GL(GenVertexArrays(1, &vao));
	GL(GenBuffers(1, &vbo));
	GL(BindVertexArray(vao));
	GL(BindBuffer(GL_ARRAY_BUFFER, vbo));
	GL(VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, 0));
	GL(VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, (void *) 12));
	GL(VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, (void *) 24));
	GL(EnableVertexAttribArray(0));
	GL(EnableVertexAttribArray(1));
	GL(EnableVertexAttribArray(2));
	GL(BindVertexArray(0));

	VertexData vertexData[144];
	int vertexIndices[6] = {0, 1, 2, 0, 2, 3};
	vec3f pointFive(0.5, 0.5, 0.5);
	int index = 0;
	for (int d = 0; d < 6; d++) {
		vec3f normal(0.0);
		normal[d % 3] = DIRS[d][d % 3];
		for (int i = 0; i < 4; i++) {
			vec3f vertices[4];
			vec3f dirOff = DIRS[d].cast<float>() * 0.0005f;
			vec3f vOff[4];
			vOff[0] = DIR_QUAD_CORNER_CYCLES_3D[d][i].cast<float>() - pointFive;
			vOff[0][OTHER_DIR_DIMS[d][0]] *= 1.001f;
			vOff[0][OTHER_DIR_DIMS[d][1]] *= 1.001f;
			vOff[1] = DIR_QUAD_CORNER_CYCLES_3D[d][i].cast<float>() - pointFive;
			vOff[1][OTHER_DIR_DIMS[d][0]] *= 0.97f;
			vOff[1][OTHER_DIR_DIMS[d][1]] *= 0.97f;
			vOff[2] = DIR_QUAD_CORNER_CYCLES_3D[d][(i + 3) % 4].cast<float>() - pointFive;
			vOff[2][OTHER_DIR_DIMS[d][0]] *= 0.97f;
			vOff[2][OTHER_DIR_DIMS[d][1]] *= 0.97f;
			vOff[3] = DIR_QUAD_CORNER_CYCLES_3D[d][(i + 3) % 4].cast<float>() - pointFive;
			vOff[3][OTHER_DIR_DIMS[d][0]] *= 1.001f;
			vOff[3][OTHER_DIR_DIMS[d][1]] *= 1.001f;

			for (int j = 0; j < 4; j++) {
				vertices[j] = dirOff + vOff[j] + pointFive;
			}

			for (int j = 0; j < 6; j++) {
				vertexData[index].xyz[0] = vertices[vertexIndices[j]][0];
				vertexData[index].xyz[1] = vertices[vertexIndices[j]][1];
				vertexData[index].xyz[2] = vertices[vertexIndices[j]][2];
				vertexData[index].nxyz[0] = normal[0];
				vertexData[index].nxyz[1] = normal[1];
				vertexData[index].nxyz[2] = normal[2];
				vertexData[index].rgba[0] = targetColor[0];
				vertexData[index].rgba[1] = targetColor[1];
				vertexData[index].rgba[2] = targetColor[2];
				vertexData[index].rgba[3] = 1.0f;
				index++;
			}
		}
	}

	GL(BufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW));
	GL(BindBuffer(GL_ARRAY_BUFFER, 0));
}

GL3TargetRenderer::~GL3TargetRenderer() {
	GL(DeleteVertexArrays(1, &vao));
	GL(DeleteBuffers(1, &vbo));
}

void GL3TargetRenderer::render() {
	Character &character = client->getLocalCharacter();
	if (!character.isValid())
		return;

	vec3i64 tbc;
	int td;
	bool target = character.getTargetedFace(&tbc, &td);
	if (target) {
		glm::mat4 viewMatrix = glm::rotate(glm::mat4(1.0f), (float) (-character.getPitch() / 36000.0f * TAU), glm::vec3(1.0f, 0.0f, 0.0f));
		viewMatrix = glm::rotate(viewMatrix, (float) (-character.getYaw() / 36000.0f * TAU), glm::vec3(0.0f, 1.0f, 0.0f));
		viewMatrix = glm::rotate(viewMatrix, (float) (-TAU / 4.0), glm::vec3(1.0f, 0.0f, 0.0f));
		viewMatrix = glm::rotate(viewMatrix, (float) (TAU / 4.0), glm::vec3(0.0f, 0.0f, 1.0f));

		vec3i64 diff = tbc * RESOLUTION - character.getPos();
		glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(
			(float) diff[0] / RESOLUTION,
			(float) diff[1] / RESOLUTION,
			(float) diff[2] / RESOLUTION)
		);

		auto &defaultShader = ((GL3Renderer *) renderer)->getShaderManager()->getDefaultShader();

		defaultShader.setViewMatrix(viewMatrix);
		defaultShader.setModelMatrix(modelMatrix);
		defaultShader.setLightEnabled(true);
		defaultShader.setFogEnabled(client->getConf().fog != Fog::NONE);
		defaultShader.useProgram();

		GL(BindVertexArray(vao));
		GL(DrawArrays(GL_TRIANGLES, 0, 144));
		GL(BindVertexArray(0));
	}
}
