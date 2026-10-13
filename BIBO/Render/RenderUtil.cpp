#include "RenderUtil.hpp"
#include <string>

using namespace bibo;

bool render::isSoftwareRenderer()
{
	const GLubyte* name = glGetString(GL_RENDERER);
	if (!name)
	{
		return false;
	}
	std::string renderer(reinterpret_cast<const char*>(name));
	for (const char* software : { "llvmpipe", "softpipe", "SwiftShader", "Software Rasterizer", "Basic Render" })
	{
		if (renderer.find(software) != std::string::npos)
		{
			return true;
		}
	}
	return false;
}

render::FullscreenTriangle::FullscreenTriangle()
{
	glGenVertexArrays(1, &vertexArray);
}

render::FullscreenTriangle::~FullscreenTriangle()
{
	glDeleteVertexArrays(1, &vertexArray);
}

void render::FullscreenTriangle::draw() const
{
	glBindVertexArray(vertexArray);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

render::StateGuard::StateGuard()
{
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
	depthTest = glIsEnabled(GL_DEPTH_TEST);
	blend = glIsEnabled(GL_BLEND);
	cullFace = glIsEnabled(GL_CULL_FACE);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
}

render::StateGuard::~StateGuard()
{
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
	glUseProgram(static_cast<GLuint>(program));
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glBindVertexArray(static_cast<GLuint>(vertexArray));
	glActiveTexture(static_cast<GLenum>(activeTexture));
	depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
	blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
	cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
	glDepthMask(depthMask);
}
