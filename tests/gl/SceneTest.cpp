#include <GL/glew.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

#include "Physics/Physics.hpp"
#include "Scene/Scene.hpp"
#include "TestSupport.hpp"
#include "World/Landmarks.hpp"

using bibo::test::AssetPath;
using bibo::test::AssetWorkingDirectory;
using bibo::test::GlContext;

using bibo::Shader;
using bibo::physics::PhysicsWorld;
using bibo::scene::Scene;

namespace {
/**
 * Offscreen framebuffer (the window is hidden, its pixels are not defined)
 */
class Framebuffer {
 public:
  Framebuffer(int width, int height) : width_(width), height_(height) {
    glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glGenRenderbuffers(2, renderbuffers_);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers_[0]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, renderbuffers_[0]);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers_[1]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, renderbuffers_[1]);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, width, height);
  }

  ~Framebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(2, renderbuffers_);
    glDeleteFramebuffers(1, &framebuffer_);
  }

  void Clear() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  std::vector<unsigned char> Pixels() {
    std::vector<unsigned char> rgba(width_ * height_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return rgba;
  }

 private:
  int width_, height_;
  GLuint framebuffer_ = 0;
  GLuint renderbuffers_[2] = {};
};
}  // namespace

TEST_CASE("The bear waits at the start of the race without losing",
          "[gl][scene]") {
  GlContext gl;
  // the scene loads its files relative to the working directory
  AssetWorkingDirectory assets;
  Shader main_shader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physics_world;
  Scene scene(&gl.Window(), &main_shader, &physics_world);

  // 5 seconds without input
  for (int i = 0; i < 5 * 60; ++i) {
    scene.Update(1.0f / 60.0f);
  }

  // No race without talking to the beaver, the bear is not in the water
  CHECK(scene.GetRaceQuest().GetStage() ==
        bibo::scene::RaceQuest::Stage::kIdle);
  CHECK(scene.GetRaceQuest().GetOutcome() ==
        bibo::scene::RaceQuest::Outcome::kNone);
  CHECK_FALSE(scene.IsBearInWater());

  // The checks share the scene, one after the other (building it for each of
  // them takes long, e.g. with the sanitizers of the CI) A frame is rendered
  // without OpenGL errors
  {
    // Like Main.cpp draw: shadow maps, the scene in HDR, bloom, exposure and
    // tone mapping into the framebuffer
    bibo::render::Renderer renderer(800, 600);
    Framebuffer framebuffer(800, 600);
    framebuffer.Clear();
    renderer.Render(scene, bibo::render::RenderSettings(), 1.0f / 60.0f);
    CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());

    // A bright day: the image is neither black nor white
    std::vector<unsigned char> pixels = framebuffer.Pixels();
    double sum = 0.0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
      sum += (pixels[i] + pixels[i + 1] + pixels[i + 2]) / 3.0;
    }
    double average = sum / static_cast<double>(pixels.size() / 4);
    CAPTURE(average);
    CHECK(average > 40.0);
    CHECK(average < 230.0);
  }

  // The world is only drawn where the camera looks
  {
    bibo::render::Renderer renderer(800, 600);
    Framebuffer framebuffer(800, 600);
    renderer.Render(scene, bibo::render::RenderSettings(), 1.0f / 60.0f);
    bibo::world::World::Statistics statistics =
        scene.GetWorld().GetStatistics();
    CAPTURE(statistics.drawn_chunks, statistics.meshes,
            statistics.drawn_instances, statistics.instances);
    CHECK(statistics.drawn_chunks > 0);
    CHECK(statistics.drawn_chunks < statistics.meshes);
    CHECK(statistics.drawn_instances > 0);
    CHECK(statistics.drawn_instances < static_cast<int>(statistics.instances));
  }

  // Graphics options can be switched off
  {
    bibo::render::RenderSettings settings;
    settings.hdr = false;
    settings.bloom = false;
    settings.shadows = false;
    settings.normal_mapping = false;
    settings.transparency = false;
    scene.SetViewFrustumCullingEnabled(false);
    CHECK_FALSE(scene.IsViewFrustumCullingEnabled());
    bibo::render::Renderer renderer(800, 600);
    Framebuffer framebuffer(800, 600);
    renderer.Render(scene, settings, 1.0f / 60.0f);
    settings.wireframe = true;
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    renderer.Render(scene, settings, 1.0f / 60.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
    // Without culling the whole world is drawn
    bibo::world::World::Statistics statistics =
        scene.GetWorld().GetStatistics();
    CHECK(statistics.drawn_chunks == statistics.meshes);
    scene.SetViewFrustumCullingEnabled(true);
  }

  // View frustum culling only skips geometries that would not cover any pixel
  {
    // Every geometry is drawn in white without depth test: the image shows all
    // covered pixels. (A shaded image is not exactly reproducible: where
    // surfaces fight for the same depth, the driver may decide differently in
    // two frames.)
    Shader silhouette_shader(AssetPath("tests/gl/silhouette.vert").string(),
                             AssetPath("tests/gl/silhouette.frag").string());
    // size of the window, the camera uses its aspect ratio
    Framebuffer framebuffer(800, 600);
    glDisable(GL_DEPTH_TEST);
    silhouette_shader.Enable();
    REQUIRE(scene.IsViewFrustumCullingEnabled());
    // Looking back over the start: the stone ring far ahead on the track is
    // behind the camera
    scene.GetCamera().Rotate(glm::pi<float>(), 0.0f);

    framebuffer.Clear();
    scene.RenderFromCamera(&silhouette_shader);
    const int drawn_with_culling = scene.GetDrawnGeometryCount();
    const std::vector<unsigned char> with_culling = framebuffer.Pixels();

    scene.SetViewFrustumCullingEnabled(false);
    framebuffer.Clear();
    scene.RenderFromCamera(&silhouette_shader);
    const int drawn_without_culling = scene.GetDrawnGeometryCount();
    const std::vector<unsigned char> without_culling = framebuffer.Pixels();

    CAPTURE(drawn_with_culling, drawn_without_culling,
            scene.GetGeometryCount());
    CHECK(drawn_without_culling == scene.GetGeometryCount());
    CHECK(drawn_with_culling > 0);
    CHECK(drawn_with_culling < drawn_without_culling);
    REQUIRE(with_culling.size() == without_culling.size());
    int different_pixels = 0;
    for (std::size_t i = 0; i < with_culling.size(); i += 4) {
      if (with_culling[i] != without_culling[i] && ++different_pixels <= 10) {
        WARN("Pixel " << (i / 4) % 800 << ", " << (i / 4) / 800
                      << " is only covered without culling");
      }
    }
    CHECK(different_pixels == 0);
    CHECK_THAT(gl.Errors(), Catch::Matchers::IsEmpty());
  }
}

TEST_CASE(
    "The camera turns around the bear on the track without going into the "
    "terrain",
    "[gl][scene][camera]") {
  GlContext gl;
  AssetWorkingDirectory assets;
  Shader main_shader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physics_world;
  Scene scene(&gl.Window(), &main_shader, &physics_world);
  const float frame = 1.0f / 60.0f;
  scene.Update(frame);

  bibo::input::PlayerInput input;
  SECTION("Standing at the start") {}
  SECTION("Running along the track") { input.move = glm::vec2(0.0f, 1.0f); }
  SECTION("Jumping") {
    input.move = glm::vec2(0.2f, 1.0f);
    input.jump = true;
  }

  int checked = 0;
  for (int i = 0; i < 4 * 60; ++i) {
    // Round and round, low (looking up from the ground) and high
    input.look = glm::vec2(0.04f, (i / 60) % 2 == 0 ? -0.05f : 0.05f);
    if (input.jump) {
      input.jump = (i / 20) % 2 == 0;  // presses again and again
    }
    scene.Update(frame, input);
    bibo::scene::Camera& camera = scene.GetCamera();
    glm::vec3 position = camera.GetPosition();
    bibo::physics::RayHit hit;
    // Nothing static (terrain, rocks) above the camera, so it is not below the
    // surface
    if (physics_world.CastRay(position + glm::vec3(0.0f, 100.0f, 0.0f),
                              glm::vec3(0.0f, -100.0f, 0.0f), hit, true)) {
      CAPTURE(i, position, hit.position, camera.GetPitch());
      CHECK(hit.position.y < position.y);
    }
    // And not in the middle of something
    CHECK_FALSE(physics_world.CastSphere(position, glm::vec3(0.0f, 0.01f, 0.0f),
                                         0.1f, hit, true));
    ++checked;
  }
  CHECK(checked > 60);
}

TEST_CASE(
    "The bear walks to the beaver, accepts the race and is brought to the "
    "starting line for the countdown",
    "[gl][scene][quest]") {
  using bibo::scene::RaceQuest;
  GlContext gl;
  AssetWorkingDirectory assets;
  Shader main_shader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physics_world;
  Scene scene(&gl.Window(), &main_shader, &physics_world);
  const float frame = 1.0f / 60.0f;
  for (int i = 0; i < 60; ++i) {
    scene.Update(frame);
  }
  const RaceQuest& quest = scene.GetRaceQuest();
  CHECK(quest.IsOffered());
  CHECK_FALSE(quest.CanTalk());  // the bear starts a few steps away
  // The beaver is in front of the bear, a bit to the side
  glm::vec3 to_beaver =
      scene.GetBeaver().GetPosition() - scene.GetBear().GetPosition();
  CAPTURE(to_beaver);
  CHECK(to_beaver.z < -3.0f);
  CHECK(glm::length(glm::vec2(to_beaver.x, to_beaver.z)) < 15.0f);

  // Walks there (relative to the camera, like the player)
  bibo::input::PlayerInput input;
  for (int i = 0; i < 8 * 60 && !quest.CanTalk(); ++i) {
    glm::vec3 direction =
        scene.GetBeaver().GetPosition() - scene.GetBear().GetPosition();
    direction.y = 0.0f;
    direction = glm::normalize(direction);
    bibo::scene::Camera& camera = scene.GetCamera();
    input.move = 0.4f * glm::vec2(glm::dot(direction, camera.GetRight()),
                                  glm::dot(direction, camera.GetForward()));
    scene.Update(frame, input);
  }
  REQUIRE(quest.CanTalk());

  // Talks, goes through the pages and says yes
  input = bibo::input::PlayerInput();
  input.interact = input.confirm = true;
  scene.Update(frame, input);
  REQUIRE(quest.GetStage() == RaceQuest::Stage::kTalking);
  bibo::input::PlayerInput press;
  press.confirm = true;
  for (int i = 0; i < 40 * 60 && quest.GetStage() == RaceQuest::Stage::kTalking;
       ++i) {
    scene.Update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
  }
  REQUIRE(quest.GetStage() == RaceQuest::Stage::kWalkingToStart);
  std::vector<RaceQuest::Event> events = scene.TakeEvents();
  CHECK(
      std::count(events.begin(), events.end(), RaceQuest::Event::kBeaverSound) >
      3);  // it chatters while its words appear
  CHECK(std::count(events.begin(), events.end(), RaceQuest::Event::kAccepted) ==
        1);
  CHECK(scene.GetCamera().IsInShot());  // the cutscene

  // The cutscene: the bear walks up to the line by itself and turns along the
  // track (the player cannot move it)
  bibo::input::PlayerInput away;
  away.move = glm::vec2(1.0f, 0.0f);
  for (int i = 0;
       i < 15 * 60 && quest.GetStage() == RaceQuest::Stage::kWalkingToStart;
       ++i) {
    scene.Update(frame, away);
  }
  REQUIRE(quest.GetStage() == RaceQuest::Stage::kCountdown);
  CHECK(quest.GetStageTime() < 0.1f);
  glm::vec3 bear = scene.GetBear().GetPosition();
  glm::vec3 start = quest.GetStartPosition();
  CAPTURE(bear, start);
  CHECK(glm::length(glm::vec2(bear.x - start.x, bear.z - start.z)) < 1.0f);
  CHECK(glm::dot(scene.GetBear().GetDirection(), quest.GetStartDirection()) >
        0.99f);
  CHECK_FALSE(scene.GetCamera().IsInShot());

  // It stands still during the countdown, then the race starts
  for (int i = 0; i < (RaceQuest::kCountdownSeconds + 1) * 60 &&
                  quest.GetStage() == RaceQuest::Stage::kCountdown;
       ++i) {
    scene.Update(frame, away);
  }
  CHECK(glm::length(glm::vec2(scene.GetBear().GetPosition().x - bear.x,
                              scene.GetBear().GetPosition().z - bear.z)) <
        0.3f);
  REQUIRE(quest.GetStage() == RaceQuest::Stage::kRacing);
  CHECK(quest.IsPlayerInControl());

  SECTION("Restarting the game ends the race") {
    scene.Restart();
    CHECK(quest.GetStage() == RaceQuest::Stage::kIdle);
    CHECK(quest.IsOffered());
  }
}

TEST_CASE(
    "Mishka lies at the largest pool and offers fishing, no salmon jump during "
    "the race",
    "[gl][scene][quest][salmon]") {
  using bibo::scene::RaceQuest;
  using bibo::scene::SalmonQuest;
  GlContext gl;
  AssetWorkingDirectory assets;
  Shader main_shader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physics_world;
  Scene scene(&gl.Window(), &main_shader, &physics_world);
  const float frame = 1.0f / 60.0f;
  const SalmonQuest& fishing = scene.GetSalmonQuest();
  const RaceQuest& race = scene.GetRaceQuest();
  const bibo::scene::WaterPool& pool =
      scene.GetSalmon().GetPools()[scene.GetMishkaPool()];
  glm::vec3 mishka = scene.GetMishka().GetPosition();
  CHECK(glm::distance(glm::vec2(mishka.x, mishka.z),
                      glm::vec2(pool.center.x, pool.center.z)) <
        pool.radius + 8.0f);
  CHECK(fishing.IsOffered());
  CHECK(fishing.GetPoolCount() ==
        static_cast<int>(scene.GetSalmon().GetPools().size()));

  // The bear is put on dry ground on the floor of the track next to Mishka
  const bibo::world::WorldGenerator& generator =
      scene.GetWorld().GetGenerator();
  glm::vec3 place(0.0f);
  bool found = false;
  for (int i = 0; i < 32 && !found; ++i) {
    float angle = static_cast<float>(i) * glm::two_pi<float>() / 32.0f;
    glm::vec2 point = glm::vec2(mishka.x, mishka.z) +
                      3.5f * glm::vec2(std::cos(angle), std::sin(angle));
    bibo::world::TrackPath::Nearest nearest =
        generator.GetPath().FindNearest(point);
    float height = generator.Height(point);
    if (height > generator.GetSeaLevel() + 0.3f && nearest.found &&
        std::abs(nearest.lateral) < nearest.floor_half_width - 1.0f) {
      place = glm::vec3(
          point.x, height + bibo::scene::BearCharacter::GroundOffset() + 0.05f,
          point.y);
      found = true;
    }
  }
  REQUIRE(found);
  auto put_bear = [&]() {
    scene.GetWorld().EnsureCollision(place, scene.GetCamera().GetPosition());
    scene.GetBear().GetCharacter().PlaceAt(place, mishka - place);
    for (int i = 0; i < 60; ++i) {
      scene.Update(frame);
    }
  };

  SECTION(
      "Fishing: talk, accept; the beaver does not offer the race meanwhile") {
    put_bear();
    REQUIRE(fishing.CanTalk());
    bibo::input::PlayerInput input;
    input.interact = input.confirm = true;
    scene.Update(frame, input);
    REQUIRE(fishing.GetStage() == SalmonQuest::Stage::kTalking);
    CHECK(scene.GetCamera().IsInShot());
    bibo::input::PlayerInput press;
    press.confirm = true;
    for (int i = 0;
         i < 40 * 60 && fishing.GetStage() == SalmonQuest::Stage::kTalking;
         ++i) {
      scene.Update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
    }
    REQUIRE(fishing.GetStage() == SalmonQuest::Stage::kFishing);
    CHECK(fishing.IsActive());
    std::vector<SalmonQuest::Event> events = scene.TakeSalmonEvents();
    CHECK(std::count(events.begin(), events.end(),
                     SalmonQuest::Event::kBearSound) > 3);
    CHECK(std::count(events.begin(), events.end(),
                     SalmonQuest::Event::kAccepted) == 1);
    scene.Update(frame);
    CHECK_FALSE(race.IsOffered());
    CHECK(scene.GetScore() == 0);

    scene.Restart();
    scene.Update(frame);
    CHECK_FALSE(fishing.IsActive());
    CHECK(fishing.IsOffered());
    CHECK(race.IsOffered());
  }
  SECTION("Salmon jump at the pool, but not during the race") {
    put_bear();
    // Outside of the race it is
    bibo::physics::RayHit hit;
    glm::vec3 collider = scene.GetMishka().GetColliderCenter();
    glm::vec3 across =
        glm::vec3(scene.GetMishka().GetColliderRadius() + 0.5f, 0.0f, 0.0f);
    CHECK(physics_world.CastRay(collider + across, -2.0f * across, hit));
    int jumps = 0;
    for (int i = 0; i < 4 * 60; ++i) {
      scene.Update(frame);
      jumps += static_cast<int>(scene.GetSalmon().GetJumps().size());
    }
    CHECK(jumps > 0);

    // The race: accepted at the beaver (the scene moves the bear to the start),
    // then the bear is put back to the pool
    scene.Restart();
    bibo::input::PlayerInput input;
    for (int i = 0; i < 8 * 60 && !race.CanTalk(); ++i) {
      glm::vec3 direction = glm::normalize(scene.GetBeaver().GetPosition() -
                                           scene.GetBear().GetPosition());
      bibo::scene::Camera& camera = scene.GetCamera();
      input.move = 0.4f * glm::vec2(glm::dot(direction, camera.GetRight()),
                                    glm::dot(direction, camera.GetForward()));
      scene.Update(frame, input);
    }
    REQUIRE(race.CanTalk());
    input = bibo::input::PlayerInput();
    input.interact = input.confirm = true;
    scene.Update(frame, input);
    bibo::input::PlayerInput press;
    press.confirm = true;
    for (int i = 0; i < 60 * 60 && race.GetStage() != RaceQuest::Stage::kRacing;
         ++i) {
      scene.Update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
    }
    REQUIRE(race.GetStage() == RaceQuest::Stage::kRacing);
    CHECK_FALSE(fishing.IsOffered());
    // Mishka is no obstacle during the race
    CHECK_FALSE(physics_world.CastRay(collider + across, -2.0f * across, hit));
    put_bear();
    for (int i = 0; i < 4 * 60 && race.GetStage() == RaceQuest::Stage::kRacing;
         ++i) {
      scene.Update(frame);
      REQUIRE(scene.GetSalmon().GetJumps().empty());
    }
    CHECK(race.GetStage() == RaceQuest::Stage::kRacing);
  }
}

TEST_CASE(
    "Chonk at the trailhead sends the bear to the stone circle and gets a "
    "report",
    "[gl][scene][quest][stones]") {
  using bibo::scene::Raccoon;
  using bibo::scene::StonesQuest;
  GlContext gl;
  AssetWorkingDirectory assets;
  Shader main_shader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physics_world;
  Scene scene(&gl.Window(), &main_shader, &physics_world);
  const float frame = 1.0f / 60.0f;
  const StonesQuest& quest = scene.GetStonesQuest();
  const bibo::world::WorldGenerator& generator =
      scene.GetWorld().GetGenerator();
  CHECK(quest.IsOffered());
  CHECK(scene.GetChonk().GetActivity() == Raccoon::Activity::kSitting);
  // Chonk sits beside the first part of a foot path, on dry land
  glm::vec3 chonk = scene.GetChonk().GetPosition();
  CHECK(generator.GetLandmarks().PathDistance(glm::vec2(chonk.x, chonk.z)) <
        bibo::world::Landmarks::kPathHalfWidth + 4.0f);
  CHECK(chonk.y > generator.GetSeaLevel());

  // Puts the bear on the ground at a distance from a point, looking at it
  auto put_bear = [&](glm::vec3 target, float distance) {
    for (int i = 0; i < 32; ++i) {
      float angle = static_cast<float>(i) * glm::two_pi<float>() / 32.0f;
      glm::vec2 point = glm::vec2(target.x, target.z) +
                        distance * glm::vec2(std::cos(angle), std::sin(angle));
      float height = generator.Height(point);
      if (height > generator.GetSeaLevel() + 0.3f &&
          generator.GetLandmarks().PathDistance(point) < 6.0f) {
        glm::vec3 place(
            point.x,
            height + bibo::scene::BearCharacter::GroundOffset() + 0.05f,
            point.y);
        scene.GetWorld().EnsureCollision(place,
                                         scene.GetCamera().GetPosition());
        scene.GetBear().GetCharacter().PlaceAt(place, target - place);
        for (int k = 0; k < 60; ++k) {
          scene.Update(frame);
        }
        return true;
      }
    }
    return false;
  };
  bibo::input::PlayerInput press;
  press.confirm = true;
  auto talk = [&]() {
    bibo::input::PlayerInput input;
    input.interact = input.confirm = true;
    scene.Update(frame, input);
    for (int i = 0; i < 60 * 60 && !quest.IsPlayerInControl(); ++i) {
      scene.Update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
    }
    // the other quests learn that this one is on (or over)
    scene.Update(frame);
  };

  REQUIRE(put_bear(chonk, 3.5f));
  REQUIRE(scene.GetTalkPartner() == Scene::TalkPartner::kChonk);
  CHECK(quest.CanTalk());
  talk();
  REQUIRE(quest.GetStage() == StonesQuest::Stage::kExploring);
  std::vector<StonesQuest::Event> events = scene.TakeStonesEvents();
  CHECK(std::count(events.begin(), events.end(),
                   StonesQuest::Event::kAccepted) == 1);
  // Chonk chitters while its words appear
  CHECK(scene.TakeRaccoonChatters() > 3);
  CHECK_FALSE(scene.GetRaceQuest().IsOffered());  // one quest at a time
  // The bear faced Chonk from close by while they talked
  glm::vec3 offset = chonk - scene.GetBear().GetPosition();
  CHECK(glm::length(glm::vec2(offset.x, offset.z)) < 4.2f);

  // At the stones: the flat stone in the middle
  glm::vec3 stones = scene.GetStonesCenter();
  REQUIRE(put_bear(stones, 2.5f));
  CHECK(quest.HasArrived());
  REQUIRE(scene.GetTalkPartner() == Scene::TalkPartner::kStones);
  talk();
  REQUIRE(quest.GetStage() == StonesQuest::Stage::kReturning);
  CHECK(quest.HasReport());

  // Back to Chonk with the report
  REQUIRE(put_bear(chonk, 3.5f));
  talk();
  CHECK(quest.HasCompleted());
  CHECK_FALSE(quest.IsActive());
  CHECK(quest.GetReport() == StonesQuest::Report::kTruth);
  events = scene.TakeStonesEvents();
  CHECK(std::count(events.begin(), events.end(),
                   StonesQuest::Event::kCompleted) == 1);
  CHECK(scene.GetRaceQuest().IsOffered());

  SECTION("Restarting keeps the completed quest, Chonk is back at its place") {
    scene.Restart();
    scene.Update(frame);
    CHECK(quest.HasCompleted());
    CHECK_FALSE(quest.IsOffered());
    CHECK(scene.GetChonk().GetPosition() == chonk);
  }
}
