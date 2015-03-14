#ifndef SHADERS_HPP
#define SHADERS_HPP

#include <GL/glew.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

enum ShaderProgram {
	DEFAULT_PROGRAM,
	BLOCK_PROGRAM,
	HUD_PROGRAM,
};
static const int NUM_PROGRAMS = 3;

class Shaders {

	// program locations
	GLuint programLocations[NUM_PROGRAMS];

	// uniform locations
	GLint blockAmbientColorLoc;
	GLint blockDiffColorLoc;
	GLint blockDiffDirLoc;
	GLint blockModelMatLoc;
	GLint blockViewMatLoc;
	GLint blockProjMatLoc;

	GLint defaultAmbientColorLoc;
	GLint defaultDiffColorLoc;
	GLint defaultDiffDirLoc;
	GLint defaultModelMatLoc;
	GLint defaultViewMatLoc;
	GLint defaultProjMatLoc;

	GLint hudProjMatLoc;

	// uniforms
	glm::vec3 ambientColor;
	glm::vec3 diffuseColor;
	glm::vec3 diffuseDirection;

	glm::mat4 modelMatrix;
	glm::mat4 viewMatrix;
	glm::mat4 projectionMatrix;

	glm::mat4 hudProjectionMatrix;

	// uniform up-to-dateness
	bool blockAmbientColorUp = false;
	bool blockDiffColorUp = false;
	bool blockDiffDirUp = false;
	bool blockModelMatUp = false;
	bool blockViewMatUp = false;
	bool blockProjMatUp = false;

	bool defaultAmbientColorUp = false;
	bool defaultDiffColorUp = false;
	bool defaultDiffDirUp = false;
	bool defaultModelMatUp = false;
	bool defaultViewMatUp = false;
	bool defaultProjMatUp = false;

	bool hudProjMatUp = false;

	// active program
	ShaderProgram activeProgram;

public:
	Shaders();
	~Shaders();

	void setDiffuseLightColor(const glm::vec3 &color);
	void setDiffuseLightDirection(const glm::vec3 &direction);
	void setAmbientLightColor(const glm::vec3 &color);

	void setModelMatrix(const glm::mat4 &matrix);
	void setViewMatrix(const glm::mat4 &matrix);
	void setProjectionMatrix(const glm::mat4 &matrix);

	void setHudProjectionMatrix(const glm::mat4 &matrix);

	void prepareProgram(ShaderProgram program);
private:
	void buildShader(GLuint shaderLoc, const char* fileName);
	void buildProgram(GLuint programLoc, GLuint *shaders, int numShaders);
};

#endif /* SHADERS_HPP */