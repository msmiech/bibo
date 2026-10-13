#include "WaterRenderer.hpp"

using namespace bibo;
using namespace bibo::world;

namespace
{
	const float WATER_EXTENT = 2400.0f;
	const float SNOW_BOX_WIDTH = 70.0f, SNOW_BOX_HEIGHT = 40.0f;
	const float SNOW_FALL_SPEED = 1.1f;
}

WaterRenderer::WaterRenderer() : shader(std::make_unique<Shader>("Shader/water.vert", "Shader/water.frag"))
{
	glGenVertexArrays(1, &vertexArray);
}

WaterRenderer::~WaterRenderer()
{
	glDeleteVertexArrays(1, &vertexArray);
}

void WaterRenderer::draw(bool transparent)
{
	shader->enable();
	glUniform1f(shader->getUniformLocation("extent"), WATER_EXTENT);
	glUniform1i(shader->getUniformLocation("transparent"), transparent);
	GLboolean blend = glIsEnabled(GL_BLEND), cullFace = glIsEnabled(GL_CULL_FACE);
	if (transparent)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	glDisable(GL_CULL_FACE); //also seen from below (from under the water)
	glBindVertexArray(vertexArray);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
	blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
	cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

SnowRenderer::SnowRenderer(int flakes) : shader(std::make_unique<Shader>("Shader/snow.vert", "Shader/snow.frag")), flakes(flakes)
{
	glGenVertexArrays(1, &vertexArray);
}

SnowRenderer::~SnowRenderer()
{
	glDeleteVertexArrays(1, &vertexArray);
}

void SnowRenderer::draw()
{
	if (flakes <= 0)
	{
		return;
	}
	shader->enable();
	glUniform3f(shader->getUniformLocation("boxSize"), SNOW_BOX_WIDTH, SNOW_BOX_HEIGHT, SNOW_BOX_WIDTH);
	glUniform1f(shader->getUniformLocation("fallSpeed"), SNOW_FALL_SPEED);
	GLboolean blend = glIsEnabled(GL_BLEND), cullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean depthMask;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);
	glBindVertexArray(vertexArray);
	glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, flakes);
	glBindVertexArray(0);
	glDepthMask(depthMask);
	blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
	cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}
