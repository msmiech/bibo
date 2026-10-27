#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <random>
#include <vector>

namespace bibo {
namespace world {
class WorldGenerator;
}

namespace scene {
/**
 * Water on the track that the bear has to jump over (the ground there is below
 * the sea level)
 */
struct WaterPool {
  glm::vec3 center;            // on the water surface
  float radius = 0.0f;         // around the center, all of the water is inside
  float trackDistance = 0.0f;  // along the track
  float area = 0.0f;           // of the water surface (square meters, roughly)
  std::vector<glm::vec2> spots;  // deep enough for a salmon to jump out
};

/**
 * Finds the water on the floor of the track (along its center line, in driving
 * direction)
 */
std::vector<WaterPool> findWaterPools(const world::WorldGenerator& generator);
/**
 * @return The index of the pool with the most water, -1 if there is none
 */
int largestPool(const std::vector<WaterPool>& pools);
/**
 * Where a runner goes along the track between two distances (e.g. in the race):
 * on the trail, and where the trail is under water around it, through the
 * middle of the widest dry part of the floor
 * @return Points about a meter apart, in driving direction
 */
std::vector<glm::vec2> runningRoute(const world::WorldGenerator& generator,
                                    float from, float to);
/**
 * A dry, flat place next to a pool on the floor of the track or at the foot of
 * its walls, out of the way of runners (as far from runningRoute as possible)
 * (e.g. for an animal that watches the pool)
 * @param position On the ground
 * @param direction Where it looks from there: at the middle of the pool
 * (horizontal)
 * @return false if there is none
 */
bool findShoreSpot(const world::WorldGenerator& generator,
                   const WaterPool& pool, glm::vec3& position,
                   glm::vec3& direction);

/**
 * The salmon of the pools: when the bear comes close to a pool, salmon jump out
 * of it every now and then. The bear catches one when it touches it in the air
 * (e.g. when it jumps over the water). No salmon jump out of a closed pool
 * (e.g. where the bear already caught one for a quest, or all pools during the
 * race).
 */
class SalmonPools {
 public:
  struct Jump {
    int pool = 0;
    glm::vec3 start;        // where it left the water
    glm::vec3 velocity;     // at the start
    float time = 0.0f;      // since the start
    float duration = 0.0f;  // until it is back in the water
    float spin =
        0.0f;  // rolls around its length while it flies (radians per second)
    float wiggle = 0.0f;  // phase of the tail
    glm::vec3 position() const;
    /**
     * Head forward (+z of the model) along the flight, back up
     */
    glm::quat rotation() const;
  };
  struct Droplet {
    glm::vec3 position;
    glm::vec3 velocity;
    float size = 0.0f;
    float surface = 0.0f;  // it disappears below
  };

  // How close the bear has to come to a pool before salmon jump (horizontally
  // from its center)
  static constexpr float APPROACH_DISTANCE = 20.0f;
  // Length of a salmon (the model is 1 long)
  static constexpr float SCALE = 1.2f;
  static constexpr float GRAVITY = 11.0f;
  // How close the salmon has to come to the mouth of the bear or the middle of
  // its body to be caught
  static constexpr float MOUTH_REACH = 0.9f;
  static constexpr float BODY_REACH = 1.6f;

  SalmonPools(std::vector<WaterPool> pools, std::uint32_t seed);

  /**
   * Salmon jump out of the pools close to the bear, fly and splash back into
   * the water
   * @param canCatch The bear can catch a salmon now (not while it holds one or
   * tumbles)
   * @param closed Per pool: true if no salmon jump out of it (missing: open),
   * its salmon still in the air cannot be caught
   * @return The pool whose salmon the bear caught in this update, -1 if none
   */
  int update(float timeDelta, glm::vec3 bearCenter, glm::vec3 bearMouth,
             bool canCatch, const std::vector<bool>& closed = {});
  /**
   * No salmon in the air (e.g. when the game is restarted)
   */
  void reset();

  const std::vector<WaterPool>& getPools() const { return pools; }
  const std::vector<Jump>& getJumps() const { return jumps; }
  const std::vector<Droplet>& getDroplets() const { return droplets; }
  /**
   * Droplets of water fly up where something falls into the water (e.g. a
   * salmon or the bear)
   * @param surface Height of the water surface
   */
  void splash(glm::vec3 position, float surface, int count);

 private:
  float uniform(float from, float to);
  void startJump(int pool, glm::vec3 bearCenter);

  std::vector<WaterPool> pools;
  std::vector<float> nextJump;  // per pool: seconds until the next salmon jumps
  std::vector<Jump> jumps;
  std::vector<Droplet> droplets;
  std::uint32_t seed;
  std::mt19937 random;
};
}  // namespace scene
}  // namespace bibo
