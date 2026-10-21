#pragma once
#include <cstdint>
#include <random>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace bibo
{
	namespace scene
	{
		/**
		* The beaver at the start of the race track, who offers the race to the bear (RaceQuest). It sits upright at the side of
		* the track, turns to the bear when it comes close and looks at it. While it talks its mouth moves with every sound
		* it makes, it nods and gestures with its arms; between the conversations it nibbles at something and slaps its tail
		* on the ground every now and then. Only the behavior, the scene draws it (FurryRenderer with the BeaverModel).
		*/
		class Beaver
		{
		public:
			//Size of the model (BeaverModel) in the world
			static constexpr float SCALE = 2.3f;
			//It notices the bear within this distance and turns to it
			static constexpr float ATTENTION_DISTANCE = 14.0f;

			/**
			* @param position On the ground
			* @param direction Where it looks when the bear is not close (horizontal)
			*/
			Beaver(glm::vec3 position, glm::vec3 direction, std::uint32_t seed = 4242u);

			/**
			* @param bear Where the head of the bear is (it turns to it when the bear is close, its head and eyes look there)
			*/
			void update(float timeDelta, glm::vec3 bear);
			/**
			* Back to how it was at the start
			*/
			void reset();
			/**
			* Talking (the dialog is open): it gestures with its arms and does not nibble
			*/
			void setTalking(bool talking);
			/**
			* One sound while talking: the mouth opens and closes, the head nods
			*/
			void speak();
			/**
			* Happy: it hops, raises its arms and slaps its tail on the ground
			*/
			void cheer();

			glm::vec3 getPosition() const { return position; }
			/**
			* Where the exclamation mark floats (above its head), it bobs up and down
			*/
			glm::vec3 getMarkerPosition() const;
			/**
			* The marker turns slowly around the vertical axis
			*/
			glm::quat getMarkerRotation() const;
			/**
			* Its collision sphere (the bear cannot walk through it)
			*/
			glm::vec3 getColliderCenter() const;
			float getColliderRadius() const;
			/**
			* The point the camera looks at in a conversation (its face)
			*/
			glm::vec3 getHeadPosition() const;
			/**
			* Between its eyes (world space), e.g. for the bear to look at
			*/
			glm::vec3 getFacePosition() const;
			/**
			* @return 0 (closed) to 1 (wide open)
			*/
			float getMouthOpen() const { return mouth; }
			bool isTalking() const { return talking; }

			/**
			* Model matrices of the parts of the BeaverModel (with the scale), indexed by BeaverModel::Part
			*/
			std::vector<glm::mat4> getPose() const;

		private:
			float uniform(float from, float to);

			glm::vec3 home;
			glm::vec3 homeDirection;
			std::uint32_t seed;
			std::mt19937 random;

			glm::vec3 position;
			float yaw = 0.0f;
			float headYaw = 0.0f, headPitch = 0.0f;
			float eyeYaw = 0.0f, eyePitch = 0.0f; //relative to the head
			float time = 0.0f;
			bool talking = false;
			float talkBlend = 0.0f; //0 to 1: blends the gestures in and out
			float mouth = 0.0f;
			float mouthTime = -1.0f; //since the last sound started, -1 if the mouth is closed
			float nod = 0.0f;
			float nibbleTime = -1.0f, nextNibble = 3.0f;
			float cheerTime = -1.0f;
			float slapTime = -1.0f, nextSlap = 8.0f;
		};
	}
}
