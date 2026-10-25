#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "../Render/FrameUniforms.hpp"

namespace bibo
{
	namespace world
	{
		/**
		* The bear and the animals push the grass, the ferns and the bushes aside (Shader/include/vegetation.glsl bends them).
		* Where a body walked it leaves imprints behind: the plants stay pressed down for a moment, then they spring back up
		* and swing past upright a bit. The bodies and the imprints closest to the camera go to the shaders with the frame.
		*/
		class FoliageInteraction
		{
		public:
			/**
			* A body that moves through the plants, e.g. the bear (approximately a sphere)
			*/
			struct Body
			{
				glm::vec3 center;
				float radius;
			};

			/**
			* What pushes the plants aside: a body or where one just was
			*/
			struct Imprint
			{
				glm::vec3 center = glm::vec3(0.0f);
				float radius = 0.0f;
				glm::vec2 motion = glm::vec2(0.0f); //direction of the movement (xz), 1: running
				float age = 0.0f; //seconds since the body left it (negative: the body is here now)
			};

			//How long the plants stay pressed down after a body left, how long they swing back afterwards
			static constexpr float HOLD_TIME = 0.8f;
			static constexpr float SPRING_TIME = 1.8f;
			//Only the plants closer than this to the camera are bent (the grass is not drawn further away)
			static constexpr float RANGE = 60.0f;

			/**
			* Follows the bodies (in the same order every frame) and lets the imprints age
			*/
			void update(float timeDelta, const std::vector<Body>& bodies, glm::vec3 camera);
			/**
			* The selected bodies and imprints into foliageInteractors and foliagePushes, their number into wind.w
			*/
			void fillFrameUniforms(render::FrameUniforms& uniforms) const;
			/**
			* @return The bodies and imprints for the shaders, the most important first (at most MAX_FOLIAGE_INTERACTORS)
			*/
			const std::vector<Imprint>& getSelected() const { return selected; }
			std::size_t getTrailLength() const { return trail.size(); }

			/**
			* How far the plants are pushed aside: 1 while a body is there and for HOLD_TIME after it left, then a damped
			* swing back (a bit past upright), 0 after SPRING_TIME
			* @param age Seconds since the body left (negative: it is there)
			*/
			static float push(float age);

		private:
			struct Tracked
			{
				glm::vec3 position = glm::vec3(0.0f);
				glm::vec3 lastImprint = glm::vec3(0.0f);
				glm::vec2 motion = glm::vec2(0.0f);
				bool valid = false;
			};

			std::vector<Tracked> tracked;
			std::vector<Imprint> trail;
			std::vector<Imprint> selected;
		};
	}
}
