#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "SceneObject.hpp"

using namespace glm;

namespace bibo
{
	namespace scene
	{
		class Camera : public SceneObject
		{
		public:
			Camera(vec3 position, vec3 direction, vec3 up, Shader* shader, GLFWwindow& window);
			~Camera();

			void draw(Shader* shader) override {};
			void update(float time_delta) override;
			vec3 getPosition() override { return position; };

			void setTarget(vec3 targetPosition);
			void setTarget(vec3 targetPosition, vec3 targetDirection);
			void setTarget(SceneObject* targetObject);

			mat4 getViewMatrix();
			mat4 getProjectionMatrix();
			/**
			* Size of the image in pixels, determines the aspect ratio of the projection
			*/
			void setViewportSize(int width, int height);

			bool isLocked();

		private:
			void onMouseMoved(double mouseX, double mouseY, float time_delta);

			Shader* shader;
			int windowWidth, windowHeight;

			vec3 position, direction, right, up;
			float horizontalAngle = 3.14f; //yaw
			float verticalAngle = 0.0f; //pitch
			float speed = 3.0f;
			float aperture = 60.0f;
			float zNear = 0.1f;
			float zFar = 200.0f;

			bool locked = true;
		};
	}
}
