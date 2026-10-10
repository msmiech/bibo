#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace glm;
namespace bibo
{
	namespace resources
	{
		struct Material
		{//with default values
			vec3 ambient = vec3(0.6f, 0.6f, 0.6f);
			vec3 diffuse = vec3(0.7f, 0.8f, 0.6f);
			vec3 specular = vec3(0.8f, 0.75f, 0.8f);
			float shininess = 32.0f;
		};
	}
}
