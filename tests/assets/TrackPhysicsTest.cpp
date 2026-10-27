#include <assimp/scene.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "Physics/Physics.hpp"
#include "Scene/Bear.hpp"
#include "Scene/GameRules.hpp"
#include "Scene/Hedgehog.hpp"
#include "TestSupport.hpp"
#include "World/WorldGenerator.hpp"
#include "World/WorldStreamer.hpp"

using bibo::physics::BodyHandle;
using bibo::physics::CharacterController;
using bibo::physics::PhysicsWorld;
using bibo::physics::RayHit;
using bibo::test::NodeModelMatrix;
using bibo::test::SharedWorldGenerator;
using bibo::test::TrackFinishLine;
using bibo::test::TrackNode;
using bibo::test::TrackScene;

using bibo::scene::Bear;
using bibo::scene::Hedgehog;
using bibo::world::TrackPath;
using bibo::world::WorldStreamer;
namespace rules = bibo::scene::rules;
using glm::vec3;

namespace {
const float kFrame = 1.0f / 60.0f;
const vec3 kDown(0.0f, -5.0f, 0.0f);
const float kBackwardSpeed = 7.5f;  // Bear.cpp

vec3 StartPosition(const std::string& node_name) {
  return vec3(NodeModelMatrix(TrackNode(node_name))[3]);
}

/**
 * The bodies of the track as the game creates them: the ground under the sea
 * (Scene::InitPhysicsBodies), the hedgehogs at their homes where the rocks of
 * the track are (Scene::InitAnimals), the terrain and the trees around the bear
 * streamed by the world (WorldStreamer)
 */
struct TrackPhysics {
  PhysicsWorld world;
  std::vector<BodyHandle> hedgehogs;
  const vec3 bear_start = StartPosition("BearBody");
  std::unique_ptr<WorldStreamer> streamer;

  explicit TrackPhysics(bool with_hedgehogs = true) {
    world.CreateStaticBox(vec3(0.0f, -6.0f, 0.0f),
                          vec3(1000.0f, 1.0f, 1000.0f));  // ground
    for (int i = 0; with_hedgehogs && i < 12; ++i) {
      vec3 home;
      float radius;
      REQUIRE(bibo::world::TrackTerrain::NodeSphere(
          TrackScene(), ("Rock" + std::to_string(i + 1)).c_str(), home,
          radius));
      Hedgehog hedgehog(glm::vec2(home.x, home.z), Hedgehog::StartDirection(i),
                        Hedgehog::SeedOf(i),
                        Hedgehog::TrackFloor(SharedWorldGenerator()));
      hedgehogs.push_back(world.CreateKinematicSphere(
          hedgehog.GetColliderCenter(), hedgehog.GetColliderRadius()));
    }
    streamer = std::make_unique<WorldStreamer>(SharedWorldGenerator(), &world,
                                               WorldStreamer::Settings(), 2);
    streamer->EnsureCollision(bear_start, 12.0f);
  }

  ~TrackPhysics() {
    streamer.reset();  // removes its bodies from the world
  }

  struct Run {
    std::vector<vec3> path;  // position after every frame
    std::vector<BodyHandle> touched_bodies;
    bool supported = false;

    vec3 End() const { return path.back(); }
    bool Touched(BodyHandle body) const {
      return std::ranges::find(touched_bodies, body) != touched_bodies.end();
    }
  };

  /**
   * Places the bear and moves it every frame (like Bear::Update, DOWN is the
   * gravity), the world is streamed around it
   * @param velocity Velocity in a frame, from the position of the bear
   */
  template <typename Velocity>
  Run MoveBear(vec3 start, float seconds, Velocity velocity) {
    Run run;
    CharacterController bear(world, Bear::ControllerDesc(start),
                             [&](const CharacterController::Hit& hit) {
                               run.touched_bodies.push_back(hit.body);
                             });
    for (int i = 0; i < static_cast<int>(seconds / kFrame); ++i) {
      streamer->EnsureCollision(bear.GetPosition(), 8.0f);
      world.Update(kFrame);
      run.supported = bear.Move(velocity(bear.GetPosition()) * kFrame, kFrame);
      run.path.push_back(bear.GetPosition());
    }
    return run;
  }

  Run MoveBear(vec3 start, float seconds, vec3 velocity) {
    return MoveBear(start, seconds, [velocity](vec3) { return velocity; });
  }
};
}  // namespace

TEST_CASE("The bear starts standing on the terrain without touching a hedgehog",
          "[assets][physics][bear][world]") {
  TrackPhysics track;

  TrackPhysics::Run run = track.MoveBear(track.bear_start, 3.0f, kDown);

  vec3 position = run.End();
  CAPTURE(track.bear_start, position);
  CHECK(run.supported);
  CHECK(position.y >
        // did not fall through the terrain into the sea
        SharedWorldGenerator().GetSeaLevel() + 1.0f);
  // starts close to the surface
  CHECK(std::abs(position.y - track.bear_start.y) < 2.0f);
  CHECK(glm::distance(vec3(position.x, 0.0f, position.z),
                      // does not slide away
                      vec3(track.bear_start.x, 0.0f, track.bear_start.z)) <
        0.5f);
  for (BodyHandle hedgehog : track.hedgehogs) {
    CHECK_FALSE(run.Touched(hedgehog));  // would knock it over
  }
}

TEST_CASE(
    "Going backwards from the start neither passes a checkpoint nor completes "
    "the lap",
    "[assets][physics][bear][rules][world]") {
  // There is no wall behind the start: the finish line is a trigger and the
  // checkpoints have to be passed in their order
  TrackPhysics track;
  const rules::FinishLine finish_line = TrackFinishLine();
  const vec3 backwards(0.0f, 0.0f,
                       1.0f);  // the bear starts looking along -z (Bear::Bear)

  TrackPhysics::Run run = track.MoveBear(track.bear_start, 1.5f,
                                         kBackwardSpeed * backwards + kDown);

  CAPTURE(track.bear_start, run.End());
  CHECK(run.End().z >
        // walks past where the wall was (3 units behind the start)
        track.bear_start.z + 5.0f);
  CHECK_FALSE(rules::IsOnFinishLine(run.End(), finish_line));
  std::size_t next_checkpoint = 0;
  for (const vec3& position : run.path) {
    next_checkpoint = rules::PassCheckpoint(position, next_checkpoint);
    REQUIRE_FALSE(
        rules::HasCompletedLap(next_checkpoint, position, finish_line));
  }
  CHECK(next_checkpoint == 0);
}

TEST_CASE(
    "Far out in the sea there is ground under the water (the bear swims above "
    "it)",
    "[assets][physics][bear][world]") {
  TrackPhysics track;
  const vec3 sea(600.0f, 10.0f, 600.0f);
  REQUIRE(SharedWorldGenerator().Height(glm::vec2(sea.x, sea.z)) <
          SharedWorldGenerator().GetSeaLevel() - 5.0f);

  TrackPhysics::Run run = track.MoveBear(sea, 5.0f, kDown);

  CAPTURE(run.End());
  CHECK(run.supported);  // it does not fall forever
  CHECK(run.End().y < SharedWorldGenerator().GetSeaLevel() - 2.0f);
}

TEST_CASE("The ground of the physics world is the terrain of the track",
          "[assets][physics][world]") {
  // The collision meshes of the chunks are their surfaces, which are the track
  // terrain on the floor of the track
  TrackPhysics track(false);
  const TrackPath& path = SharedWorldGenerator().GetPath();
  const bibo::world::TrackTerrain& terrain = SharedWorldGenerator().GetTrack();
  float largest_difference = 0.0f;
  glm::vec3 worst(0.0f);
  for (std::size_t i = 0; i < path.GetSamples().size(); i += 12) {
    const TrackPath::Sample& sample = path.GetSamples()[i];
    glm::vec2 right(-sample.tangent.y, sample.tangent.x);
    for (float lateral : {-0.8f, 0.0f, 0.8f}) {
      glm::vec2 point =
          glm::vec2(sample.position.x, sample.position.z) +
          right * (lateral * std::min(sample.floor_left, sample.floor_right));
      if (terrain.Height(point) > sample.position.y + 0.5f) {
        // the floor is narrower here (on the walls the voxels round the steep
        // surface a bit)
        continue;
      }
      vec3 above(point.x, sample.position.y + 4.0f, point.y);
      track.streamer->EnsureCollision(above, 6.0f);
      RayHit hit;
      CAPTURE(i, point, above, SharedWorldGenerator().IsInCave(above));
      REQUIRE(track.world.CastRay(above, vec3(0.0f, -10.0f, 0.0f), hit, true));
      float difference = std::abs(hit.position.y - terrain.Height(point));
      if (difference > largest_difference) {
        largest_difference = difference;
        worst = hit.position;
      }
    }
  }
  CAPTURE(worst, terrain.Height(glm::vec2(worst.x, worst.z)));
  // a voxel is 1 unit, steep edges (of the puddles) are a bit rounded
  CHECK(largest_difference < 0.25f);
}

TEST_CASE("The caves leave room for the bear to run and jump",
          "[assets][physics][bear][world]") {
  // The roof of a cave is not part of the track terrain: along the trail
  // through the cave nothing is in the way of the bear, also not when it jumps
  // (over the water on the track)
  TrackPhysics track(false);
  const TrackPath& path = SharedWorldGenerator().GetPath();
  const std::vector<bibo::world::Cave>& caves =
      SharedWorldGenerator().GetCaves();
  REQUIRE(caves.size() >= 2);
  const std::size_t index = GENERATE_COPY(range(std::size_t(0), caves.size()));
  const bibo::world::Cave& cave = caves[index];
  CAPTURE(index, cave.start, cave.end);
  const float radius = 0.5f;  // of the capsule of the bear
  const float jump = 2.5f;    // above the floor, with the capsule

  for (float distance = cave.start - 10.0f; distance < cave.end + 10.0f;
       distance += 0.5f) {
    TrackPath::Sample sample = path.At(distance);
    TrackPath::Sample next = path.At(distance + 0.5f);
    // Over the water the bear jumps from the floor before it
    float floor = std::max({sample.position.y, next.position.y,
                            SharedWorldGenerator().GetSeaLevel()});
    vec3 from(sample.position.x, floor + radius + 0.4f, sample.position.z);
    vec3 to(next.position.x,
            std::max(next.position.y, SharedWorldGenerator().GetSeaLevel()) +
                radius + 0.4f,
            next.position.z);
    track.streamer->EnsureCollision(from, 4.0f);
    RayHit hit;
    CAPTURE(distance, from);
    // Forwards (above the steps of the floor, the bear climbs or jumps them)
    CHECK_FALSE(track.world.CastSphere(from + vec3(0.0f, 1.2f, 0.0f), to - from,
                                       radius, hit, true));
    // Up (a jump)
    CHECK_FALSE(track.world.CastSphere(from, vec3(0.0f, jump, 0.0f), radius,
                                       hit, true));
  }
}
