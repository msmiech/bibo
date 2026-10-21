#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Dialog.hpp"
#include "GameRules.hpp"
#include "../World/TrackPath.hpp"

namespace bibo
{
	namespace scene
	{
		/**
		* The race around the track as a quest of the beaver at the start: the player talks to the beaver, who asks whether the
		* bear would like to give the race track a shot. Yes: the bear walks up to the starting line (a short cutscene, the scene
		* moves it), a countdown 3, 2, 1 starts the timer. The lap is complete at the finish line after all checkpoints in their
		* order. It fails when the lap takes too long, the bear leaves the track or falls into the water; the beaver offers the
		* race again then. After a completed lap the beaver offers to improve the best time. While the bear is on another quest
		* (e.g. fishing, SalmonQuest) the beaver does not offer the race, it only says that the bear looks busy.
		*
		* Only the rules and the conversation, the scene tells it where the bear is and moves the bear and the camera.
		*/
		class RaceQuest
		{
		public:
			enum class Stage
			{
				IDLE, //the bear roams freely, the beaver offers the race
				TALKING, //conversation with the beaver
				WALKING_TO_START, //cutscene: the bear walks up to the starting line
				COUNTDOWN, //3, 2, 1
				RACING //the timer runs
			};
			/**
			* How the last race ended
			*/
			enum class Outcome { NONE, COMPLETED, TOO_SLOW, LEFT_TRACK, FELL_INTO_WATER };
			/**
			* Something happened (for the sounds and the animation of the beaver), see takeEvents
			*/
			enum class Event { BEAVER_SOUND, ACCEPTED, DECLINED, COUNTDOWN_TICK, START, CHECKPOINT, COMPLETED, FAILED };

			//The name of the beaver
			static constexpr const char* SPEAKER = "Bober";
			//The lap must be done within this time (seconds)
			static constexpr float TIME_LIMIT = 120.0f;
			static constexpr int COUNTDOWN_SECONDS = 3;
			//The player can talk to the beaver within this distance (horizontally from the beaver to the center of the bear)
			static constexpr float TALK_DISTANCE = 5.5f;
			//The cutscene ends after this time even if the bear did not reach the starting line (e.g. it was knocked over)
			static constexpr float WALK_TIMEOUT = 12.0f;
			//How long "GO!" is shown after the countdown
			static constexpr float GO_DURATION = 1.0f;
			//The bear left the track when it is this far beside the floor of the track (e.g. up the walls of the canyon into the forest)
			static constexpr float OFF_TRACK_MARGIN = 6.0f;

			/**
			* @return true if the position is on the race track (on its floor or close to it, e.g. on the slope of its walls)
			*/
			static bool isOnTrack(const world::TrackPath& path, glm::vec3 position);

			/**
			* What the bear does in this frame
			*/
			struct Situation
			{
				glm::vec3 bear = glm::vec3(0.0f); //center of the bear
				bool nearBeaver = false; //within TALK_DISTANCE, and the bear can talk (not tumbling)
				bool swimming = false;
				bool onTrack = true;
				bool atStart = false; //at the starting line, looking along the track (end of the cutscene)
				bool interact = false; //the player wants to talk (pressed in this frame)
				bool busy = false; //on another quest, the race is not offered
				DialogInput dialog;
			};

			/**
			* @param finishLine Start and finish of the lap
			* @param startDirection Driving direction at the start (horizontal)
			*/
			RaceQuest(const rules::FinishLine& finishLine, glm::vec3 startDirection);

			void update(float timeDelta, const Situation& situation);
			/**
			* Ends a race or a conversation without a result (e.g. when the game is restarted), the best time is kept
			*/
			void abort();

			Stage getStage() const { return stage; }
			/**
			* @return true if the player moves the bear (not in a conversation or the cutscene)
			*/
			bool isPlayerInControl() const { return stage == Stage::IDLE || stage == Stage::RACING; }
			/**
			* @return true if the beaver offers the race (the exclamation mark above it)
			*/
			bool isOffered() const { return stage == Stage::IDLE && !busy; }
			/**
			* @return true if the player can start a conversation now (near the beaver, it may still say something in passing)
			*/
			bool canTalk() const { return stage == Stage::IDLE && nearBeaver && (!dialog.isOpen() || dialog.isPassing()); }
			const Dialog& getDialog() const { return dialog; }

			/**
			* Where the bear stands at the starting line (on the ground plane, y is the height of the finish line) and where it looks
			*/
			glm::vec3 getStartPosition() const { return startPosition; }
			glm::vec3 getStartDirection() const { return startDirection; }
			const rules::FinishLine& getFinishLine() const { return finishLine; }

			/**
			* @return Seconds left in the countdown (COUNTDOWN_SECONDS to 0)
			*/
			float getCountdown() const { return countdown; }
			/**
			* @return The number of the countdown that is shown (3, 2, 1), 0 for "GO!", -1 if nothing is shown
			*/
			int getCountdownNumber() const;
			/**
			* @return Seconds since the start of the race (also after it ended: the time of the last race)
			*/
			float getRaceTime() const { return raceTime; }
			std::size_t getCheckpointsPassed() const { return nextCheckpoint; }
			std::size_t getCheckpointCount() const { return rules::CHECKPOINT_COUNT; }
			/**
			* Seconds since the stage changed
			*/
			float getStageTime() const { return stageTime; }

			Outcome getOutcome() const { return outcome; }
			/**
			* @return Seconds since the last race ended (for showing the result for a while)
			*/
			float getOutcomeAge() const { return outcomeAge; }
			bool hasCompleted() const { return completions > 0; }
			int getCompletions() const { return completions; }
			/**
			* @return The best time of a completed lap (seconds), 0 if none
			*/
			float getBestTime() const { return bestTime; }
			/**
			* @return true if the last race was completed with a new best time
			*/
			bool isNewBest() const { return newBest; }

			/**
			* @return What happened since the last call
			*/
			std::vector<Event> takeEvents();

			/**
			* @return The time as minutes, seconds and hundredths, e.g. "1:23.45"
			*/
			static std::string formatTime(float seconds);

		private:
			void setStage(Stage newStage);
			void startConversation();
			void fail(Outcome reason);

			rules::FinishLine finishLine;
			glm::vec3 startDirection;
			glm::vec3 startPosition;
			Dialog dialog;
			Stage stage = Stage::IDLE;
			float stageTime = 0.0f;
			bool nearBeaver = false;
			bool busy = false;
			float countdown = 0.0f;
			float raceTime = 0.0f;
			std::size_t nextCheckpoint = 0;
			Outcome outcome = Outcome::NONE;
			float outcomeAge = 0.0f;
			int completions = 0;
			float bestTime = 0.0f;
			bool newBest = false;
			std::vector<Event> events;
		};
	}
}
