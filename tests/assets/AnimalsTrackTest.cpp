#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <cmath>
#include <string>

#include "Scene/Hedgehog.hpp"
#include "Scene/RaceQuest.hpp"
#include "Scene/Salmon.hpp"
#include "TestSupport.hpp"
#include "World/TrackTerrain.hpp"
#include "World/WorldGenerator.hpp"

using bibo::test::NodeModelMatrix;
using bibo::test::SharedWorldGenerator;
using bibo::test::TrackNode;
using bibo::test::TrackScene;

using bibo::scene::Hedgehog;
using bibo::scene::WaterPool;
using glm::vec2;
using glm::vec3;

TEST_CASE(
    "There are pools of water on the track that are deep enough for salmon",
    "[assets][world][animals]") {
  const bibo::world::WorldGenerator& generator = SharedWorldGenerator();
  const float sea = generator.GetSeaLevel();
  std::vector<WaterPool> pools = bibo::scene::FindWaterPools(generator);
  REQUIRE(pools.size() >= 4);
  float last_distance = -1.0f;
  for (const WaterPool& pool : pools) {
    CAPTURE(pool.center, pool.track_distance);
    CHECK(pool.center.y == sea);
    CHECK(pool.track_distance > last_distance);  // in driving direction
    last_distance = pool.track_distance;
    CHECK(generator.GetPath()
              .FindNearest(vec2(pool.center.x, pool.center.z))
              .found);
    REQUIRE_FALSE(pool.spots.empty());
    for (vec2 spot : pool.spots) {
      CHECK(generator.Height(spot) < sea - 0.6f);
      CHECK(glm::distance(spot, vec2(pool.center.x, pool.center.z)) <=
            pool.radius);
    }
  }
  // Apart from each other
  for (std::size_t i = 1; i < pools.size(); ++i) {
    CHECK(glm::distance(pools[i].center, pools[i - 1].center) >
          pools[i].radius + pools[i - 1].radius);
  }
}

TEST_CASE(
    "Mishka lies on dry ground next to the largest pool, beside the trail",
    "[assets][world][animals]") {
  const bibo::world::WorldGenerator& generator = SharedWorldGenerator();
  const float sea = generator.GetSeaLevel();
  std::vector<WaterPool> pools = bibo::scene::FindWaterPools(generator);
  CHECK(pools.size() == 6);
  int largest = bibo::scene::LargestPool(pools);
  REQUIRE(largest >= 0);
  const WaterPool& pool = pools[largest];
  for (const WaterPool& other : pools) {
    CHECK(other.area > 0.0f);
    CHECK(other.area <= pool.area);
  }

  vec3 spot, look;
  REQUIRE(bibo::scene::FindShoreSpot(generator, pool, spot, look));
  CAPTURE(spot, look, pool.center);
  CHECK(spot.y > sea + 0.1f);  // dry
  // on the ground
  CHECK(std::abs(generator.Height(vec2(spot.x, spot.z)) - spot.y) < 1e-4f);
  bibo::world::TrackPath::Nearest nearest =
      generator.GetPath().FindNearest(vec2(spot.x, spot.z));
  REQUIRE(nearest.found);
  CHECK(std::abs(nearest.lateral) <=
        // on the floor of the track or at the foot of its walls
        nearest.floor_half_width + 4.0f);
  CHECK(
      glm::distance(vec2(spot.x, spot.z), vec2(pool.center.x, pool.center.z)) <
      pool.radius + 8.0f);  // it can see the salmon
  CHECK(glm::dot(look, glm::normalize(vec3(pool.center.x - spot.x, 0.0f,
                                           pool.center.z - spot.z))) > 0.99f);

  // Not in the way of the race: far from the route past the pool (also from the
  // line between its points)
  std::vector<vec2> route = bibo::scene::RunningRoute(
      generator, pool.track_distance - 40.0f, pool.track_distance + 40.0f);
  REQUIRE(route.size() > 60);
  float clearance = 1e9f;
  for (std::size_t i = 0; i + 1 < route.size(); ++i) {
    vec2 segment = route[i + 1] - route[i];
    float along =
        glm::clamp(glm::dot(vec2(spot.x, spot.z) - route[i], segment) /
                       glm::dot(segment, segment),
                   0.0f, 1.0f);
    clearance = std::min(clearance, glm::distance(vec2(spot.x, spot.z),
                                                  route[i] + segment * along));
  }
  CAPTURE(clearance);
  CHECK(clearance > 4.0f);
}

TEST_CASE("The route of runners goes along the trail and around the water",
          "[assets][world][animals]") {
  const bibo::world::WorldGenerator& generator = SharedWorldGenerator();
  const float sea = generator.GetSeaLevel();
  std::vector<WaterPool> pools = bibo::scene::FindWaterPools(generator);
  for (const WaterPool& pool : pools) {
    std::vector<vec2> route = bibo::scene::RunningRoute(
        generator, pool.track_distance - 30.0f, pool.track_distance + 30.0f);
    REQUIRE(route.size() > 40);
    int wet = 0;
    for (vec2 point : route) {
      wet += generator.Height(point) < sea ? 1 : 0;
      CHECK(generator.GetPath().FindNearest(point).found);
    }
    CAPTURE(pool.center, wet);
    // only where it cuts a corner of the water
    CHECK(wet <= static_cast<int>(route.size()) / 10);
    for (std::size_t i = 1; i < route.size(); ++i) {
      CHECK(glm::distance(route[i], route[i - 1]) < 2.0f);  // no jumps
    }
  }
}

TEST_CASE(
    "The hedgehogs live where the rocks of the track were and stay on the "
    "floor of the track",
    "[assets][world][animals]") {
  const int index = GENERATE(range(0, 12));
  vec3 rock;
  float radius;
  REQUIRE(bibo::world::TrackTerrain::NodeSphere(
      TrackScene(), ("Rock" + std::to_string(index + 1)).c_str(), rock,
      radius));
  const vec2 home(rock.x, rock.z);
  Hedgehog::Ground ground = Hedgehog::TrackFloor(SharedWorldGenerator());
  float height;
  CAPTURE(index, home);
  REQUIRE(ground(home, height));

  Hedgehog hedgehog(home, Hedgehog::StartDirection(index),
                    Hedgehog::SeedOf(index), ground);
  // on the ground, not floating like the rocks
  CHECK(std::abs(hedgehog.GetPosition().y - height) < 1e-4f);
  for (int i = 0; i < 60 * 60; ++i) {
    hedgehog.Update(1.0f / 60.0f);
    vec3 position = hedgehog.GetPosition();
    REQUIRE(ground(vec2(position.x, position.z), height));
    REQUIRE(std::abs(position.y - height) < 0.3f);
    REQUIRE(glm::distance(vec2(position.x, position.z), home) <=
            Hedgehog::kHomeRadius + 0.5f);
  }
}

TEST_CASE(
    "The race track counts as the track for the race quest, the forest and the "
    "sea do not",
    "[assets][world][quest]") {
  const bibo::world::WorldGenerator& generator = SharedWorldGenerator();
  const bibo::world::TrackPath& path = generator.GetPath();
  int checked = 0;
  for (std::size_t i = 0; i < path.GetSamples().size(); i += 3) {
    const bibo::world::TrackPath::Sample& sample = path.GetSamples()[i];
    vec2 right(-sample.tangent.y, sample.tangent.x);
    vec2 center(sample.position.x, sample.position.z);
    CAPTURE(sample.distance);
    // The whole floor, also a bit up the walls
    for (float lateral :
         {-sample.floor_left - 1.0f, 0.0f, sample.floor_right + 1.0f}) {
      vec2 point = center + right * lateral;
      REQUIRE(bibo::scene::RaceQuest::IsOnTrack(
          path, vec3(point.x, sample.position.y, point.y)));
    }
    ++checked;
  }
  CHECK(checked > 100);
  // The start, the forest next to it and the sea
  const vec3 start(NodeModelMatrix(TrackNode("BearBody"))[3]);
  CHECK(bibo::scene::RaceQuest::IsOnTrack(path, start));
  CHECK_FALSE(
      bibo::scene::RaceQuest::IsOnTrack(path, start + vec3(40.0f, 0.0f, 0.0f)));
  CHECK_FALSE(
      bibo::scene::RaceQuest::IsOnTrack(path, vec3(600.0f, 0.0f, 600.0f)));
}
