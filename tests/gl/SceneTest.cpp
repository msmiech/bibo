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

using namespace bibo::test;
using bibo::Shader;
using bibo::physics::PhysicsWorld;
using bibo::scene::Scene;

namespace {
/**
 * Offscreen framebuffer (the window is hidden, its pixels are not defined)
 */
class Framebuffer {
 public:
  Framebuffer(int width, int height) : width(width), height(height) {
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenRenderbuffers(2, renderbuffers);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[0]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, renderbuffers[0]);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[1]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, renderbuffers[1]);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, width, height);
  }

  ~Framebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(2, renderbuffers);
    glDeleteFramebuffers(1, &framebuffer);
  }

  void clear() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }

  std::vector<unsigned char> pixels() {
    std::vector<unsigned char> rgba(width * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return rgba;
  }

 private:
  int width, height;
  GLuint framebuffer = 0;
  GLuint renderbuffers[2] = {};
};
}  // namespace

TEST_CASE("The bear waits at the start of the race without losing",
          "[gl][scene]") {
  GlContext gl;
  AssetWorkingDirectory
      assets;  // the scene loads its files relative to the working directory
  Shader mainShader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physicsWorld;
  Scene scene(&gl.window(), &mainShader, &physicsWorld);

  // 5 seconds without input
  for (int i = 0; i < 5 * 60; ++i) {
    scene.update(1.0f / 60.0f);
  }

  // No race without talking to the beaver, the bear is not in the water
  CHECK(scene.getRaceQuest().getStage() == bibo::scene::RaceQuest::Stage::IDLE);
  CHECK(scene.getRaceQuest().getOutcome() ==
        bibo::scene::RaceQuest::Outcome::NONE);
  CHECK_FALSE(scene.isBearInWater());

  // The checks share the scene, one after the other (building it for each of
  // them takes long, e.g. with the sanitizers of the CI) A frame is rendered
  // without OpenGL errors
  {
    // Like Main.cpp draw: shadow maps, the scene in HDR, bloom, exposure and
    // tone mapping into the framebuffer
    bibo::render::Renderer renderer(800, 600);
    Framebuffer framebuffer(800, 600);
    framebuffer.clear();
    renderer.render(scene, bibo::render::RenderSettings(), 1.0f / 60.0f);
    CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());

    // A bright day: the image is neither black nor white
    std::vector<unsigned char> pixels = framebuffer.pixels();
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
    renderer.render(scene, bibo::render::RenderSettings(), 1.0f / 60.0f);
    bibo::world::World::Statistics statistics =
        scene.getWorld().getStatistics();
    CAPTURE(statistics.drawnChunks, statistics.meshes,
            statistics.drawnInstances, statistics.instances);
    CHECK(statistics.drawnChunks > 0);
    CHECK(statistics.drawnChunks < statistics.meshes);
    CHECK(statistics.drawnInstances > 0);
    CHECK(statistics.drawnInstances < static_cast<int>(statistics.instances));
  }

  // Graphics options can be switched off
  {
    bibo::render::RenderSettings settings;
    settings.hdr = false;
    settings.bloom = false;
    settings.shadows = false;
    settings.normalMapping = false;
    settings.transparency = false;
    scene.setViewFrustumCullingEnabled(false);
    CHECK_FALSE(scene.isViewFrustumCullingEnabled());
    bibo::render::Renderer renderer(800, 600);
    Framebuffer framebuffer(800, 600);
    renderer.render(scene, settings, 1.0f / 60.0f);
    settings.wireframe = true;
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    renderer.render(scene, settings, 1.0f / 60.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
    // Without culling the whole world is drawn
    bibo::world::World::Statistics statistics =
        scene.getWorld().getStatistics();
    CHECK(statistics.drawnChunks == statistics.meshes);
    scene.setViewFrustumCullingEnabled(true);
  }

  // View frustum culling only skips geometries that would not cover any pixel
  {
    // Every geometry is drawn in white without depth test: the image shows all
    // covered pixels. (A shaded image is not exactly reproducible: where
    // surfaces fight for the same depth, the driver may decide differently in
    // two frames.)
    Shader silhouetteShader(assetPath("tests/gl/silhouette.vert").string(),
                            assetPath("tests/gl/silhouette.frag").string());
    Framebuffer framebuffer(
        800, 600);  // size of the window, the camera uses its aspect ratio
    glDisable(GL_DEPTH_TEST);
    silhouetteShader.enable();
    REQUIRE(scene.isViewFrustumCullingEnabled());
    // Looking back over the start: the stone ring far ahead on the track is
    // behind the camera
    scene.getCamera().rotate(glm::pi<float>(), 0.0f);

    framebuffer.clear();
    scene.renderFromCamera(&silhouetteShader);
    const int drawnWithCulling = scene.getDrawnGeometryCount();
    const std::vector<unsigned char> withCulling = framebuffer.pixels();

    scene.setViewFrustumCullingEnabled(false);
    framebuffer.clear();
    scene.renderFromCamera(&silhouetteShader);
    const int drawnWithoutCulling = scene.getDrawnGeometryCount();
    const std::vector<unsigned char> withoutCulling = framebuffer.pixels();

    CAPTURE(drawnWithCulling, drawnWithoutCulling, scene.getGeometryCount());
    CHECK(drawnWithoutCulling == scene.getGeometryCount());
    CHECK(drawnWithCulling > 0);
    CHECK(drawnWithCulling < drawnWithoutCulling);
    REQUIRE(withCulling.size() == withoutCulling.size());
    int differentPixels = 0;
    for (std::size_t i = 0; i < withCulling.size(); i += 4) {
      if (withCulling[i] != withoutCulling[i] && ++differentPixels <= 10) {
        WARN("Pixel " << (i / 4) % 800 << ", " << (i / 4) / 800
                      << " is only covered without culling");
      }
    }
    CHECK(differentPixels == 0);
    CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
  }
}

TEST_CASE(
    "The camera turns around the bear on the track without going into the "
    "terrain",
    "[gl][scene][camera]") {
  GlContext gl;
  AssetWorkingDirectory assets;
  Shader mainShader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physicsWorld;
  Scene scene(&gl.window(), &mainShader, &physicsWorld);
  const float frame = 1.0f / 60.0f;
  scene.update(frame);

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
    scene.update(frame, input);
    bibo::scene::Camera& camera = scene.getCamera();
    glm::vec3 position = camera.getPosition();
    bibo::physics::RayHit hit;
    // Nothing static (terrain, rocks) above the camera, so it is not below the
    // surface
    if (physicsWorld.castRay(position + glm::vec3(0.0f, 100.0f, 0.0f),
                             glm::vec3(0.0f, -100.0f, 0.0f), hit, true)) {
      CAPTURE(i, position, hit.position, camera.getPitch());
      CHECK(hit.position.y < position.y);
    }
    // And not in the middle of something
    CHECK_FALSE(physicsWorld.castSphere(position, glm::vec3(0.0f, 0.01f, 0.0f),
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
  Shader mainShader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physicsWorld;
  Scene scene(&gl.window(), &mainShader, &physicsWorld);
  const float frame = 1.0f / 60.0f;
  for (int i = 0; i < 60; ++i) {
    scene.update(frame);
  }
  const RaceQuest& quest = scene.getRaceQuest();
  CHECK(quest.isOffered());
  CHECK_FALSE(quest.canTalk());  // the bear starts a few steps away
  // The beaver is in front of the bear, a bit to the side
  glm::vec3 toBeaver =
      scene.getBeaver().getPosition() - scene.getBear().getPosition();
  CAPTURE(toBeaver);
  CHECK(toBeaver.z < -3.0f);
  CHECK(glm::length(glm::vec2(toBeaver.x, toBeaver.z)) < 15.0f);

  // Walks there (relative to the camera, like the player)
  bibo::input::PlayerInput input;
  for (int i = 0; i < 8 * 60 && !quest.canTalk(); ++i) {
    glm::vec3 direction =
        scene.getBeaver().getPosition() - scene.getBear().getPosition();
    direction.y = 0.0f;
    direction = glm::normalize(direction);
    bibo::scene::Camera& camera = scene.getCamera();
    input.move = 0.4f * glm::vec2(glm::dot(direction, camera.getRight()),
                                  glm::dot(direction, camera.getForward()));
    scene.update(frame, input);
  }
  REQUIRE(quest.canTalk());

  // Talks, goes through the pages and says yes
  input = bibo::input::PlayerInput();
  input.interact = input.confirm = true;
  scene.update(frame, input);
  REQUIRE(quest.getStage() == RaceQuest::Stage::TALKING);
  bibo::input::PlayerInput press;
  press.confirm = true;
  for (int i = 0; i < 40 * 60 && quest.getStage() == RaceQuest::Stage::TALKING;
       ++i) {
    scene.update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
  }
  REQUIRE(quest.getStage() == RaceQuest::Stage::WALKING_TO_START);
  std::vector<RaceQuest::Event> events = scene.takeEvents();
  CHECK(
      std::count(events.begin(), events.end(), RaceQuest::Event::BEAVER_SOUND) >
      3);  // it chatters while its words appear
  CHECK(std::count(events.begin(), events.end(), RaceQuest::Event::ACCEPTED) ==
        1);
  CHECK(scene.getCamera().isInShot());  // the cutscene

  // The cutscene: the bear walks up to the line by itself and turns along the
  // track (the player cannot move it)
  bibo::input::PlayerInput away;
  away.move = glm::vec2(1.0f, 0.0f);
  for (int i = 0;
       i < 15 * 60 && quest.getStage() == RaceQuest::Stage::WALKING_TO_START;
       ++i) {
    scene.update(frame, away);
  }
  REQUIRE(quest.getStage() == RaceQuest::Stage::COUNTDOWN);
  CHECK(quest.getStageTime() < 0.1f);
  glm::vec3 bear = scene.getBear().getPosition();
  glm::vec3 start = quest.getStartPosition();
  CAPTURE(bear, start);
  CHECK(glm::length(glm::vec2(bear.x - start.x, bear.z - start.z)) < 1.0f);
  CHECK(glm::dot(scene.getBear().getDirection(), quest.getStartDirection()) >
        0.99f);
  CHECK_FALSE(scene.getCamera().isInShot());

  // It stands still during the countdown, then the race starts
  for (int i = 0; i < (RaceQuest::COUNTDOWN_SECONDS + 1) * 60 &&
                  quest.getStage() == RaceQuest::Stage::COUNTDOWN;
       ++i) {
    scene.update(frame, away);
  }
  CHECK(glm::length(glm::vec2(scene.getBear().getPosition().x - bear.x,
                              scene.getBear().getPosition().z - bear.z)) <
        0.3f);
  REQUIRE(quest.getStage() == RaceQuest::Stage::RACING);
  CHECK(quest.isPlayerInControl());

  SECTION("Restarting the game ends the race") {
    scene.restart();
    CHECK(quest.getStage() == RaceQuest::Stage::IDLE);
    CHECK(quest.isOffered());
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
  Shader mainShader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physicsWorld;
  Scene scene(&gl.window(), &mainShader, &physicsWorld);
  const float frame = 1.0f / 60.0f;
  const SalmonQuest& fishing = scene.getSalmonQuest();
  const RaceQuest& race = scene.getRaceQuest();
  const bibo::scene::WaterPool& pool =
      scene.getSalmon().getPools()[scene.getMishkaPool()];
  glm::vec3 mishka = scene.getMishka().getPosition();
  CHECK(glm::distance(glm::vec2(mishka.x, mishka.z),
                      glm::vec2(pool.center.x, pool.center.z)) <
        pool.radius + 8.0f);
  CHECK(fishing.isOffered());
  CHECK(fishing.getPoolCount() ==
        static_cast<int>(scene.getSalmon().getPools().size()));

  // The bear is put on dry ground on the floor of the track next to Mishka
  const bibo::world::WorldGenerator& generator =
      scene.getWorld().getGenerator();
  glm::vec3 place(0.0f);
  bool found = false;
  for (int i = 0; i < 32 && !found; ++i) {
    float angle = static_cast<float>(i) * glm::two_pi<float>() / 32.0f;
    glm::vec2 point = glm::vec2(mishka.x, mishka.z) +
                      3.5f * glm::vec2(std::cos(angle), std::sin(angle));
    bibo::world::TrackPath::Nearest nearest =
        generator.getPath().nearest(point);
    float height = generator.height(point);
    if (height > generator.getSeaLevel() + 0.3f && nearest.found &&
        std::abs(nearest.lateral) < nearest.floorHalfWidth - 1.0f) {
      place = glm::vec3(
          point.x, height + bibo::scene::BearCharacter::groundOffset() + 0.05f,
          point.y);
      found = true;
    }
  }
  REQUIRE(found);
  auto putBear = [&]() {
    scene.getWorld().ensureCollision(place, scene.getCamera().getPosition());
    scene.getBear().getCharacter().placeAt(place, mishka - place);
    for (int i = 0; i < 60; ++i) {
      scene.update(frame);
    }
  };

  SECTION(
      "Fishing: talk, accept; the beaver does not offer the race meanwhile") {
    putBear();
    REQUIRE(fishing.canTalk());
    bibo::input::PlayerInput input;
    input.interact = input.confirm = true;
    scene.update(frame, input);
    REQUIRE(fishing.getStage() == SalmonQuest::Stage::TALKING);
    CHECK(scene.getCamera().isInShot());
    bibo::input::PlayerInput press;
    press.confirm = true;
    for (int i = 0;
         i < 40 * 60 && fishing.getStage() == SalmonQuest::Stage::TALKING;
         ++i) {
      scene.update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
    }
    REQUIRE(fishing.getStage() == SalmonQuest::Stage::FISHING);
    CHECK(fishing.isActive());
    std::vector<SalmonQuest::Event> events = scene.takeSalmonEvents();
    CHECK(std::count(events.begin(), events.end(),
                     SalmonQuest::Event::BEAR_SOUND) > 3);
    CHECK(std::count(events.begin(), events.end(),
                     SalmonQuest::Event::ACCEPTED) == 1);
    scene.update(frame);
    CHECK_FALSE(race.isOffered());
    CHECK(scene.getScore() == 0);

    scene.restart();
    scene.update(frame);
    CHECK_FALSE(fishing.isActive());
    CHECK(fishing.isOffered());
    CHECK(race.isOffered());
  }
  SECTION("Salmon jump at the pool, but not during the race") {
    putBear();
    // Outside of the race it is
    bibo::physics::RayHit hit;
    glm::vec3 collider = scene.getMishka().getColliderCenter();
    glm::vec3 across =
        glm::vec3(scene.getMishka().getColliderRadius() + 0.5f, 0.0f, 0.0f);
    CHECK(physicsWorld.castRay(collider + across, -2.0f * across, hit));
    int jumps = 0;
    for (int i = 0; i < 4 * 60; ++i) {
      scene.update(frame);
      jumps += static_cast<int>(scene.getSalmon().getJumps().size());
    }
    CHECK(jumps > 0);

    // The race: accepted at the beaver (the scene moves the bear to the start),
    // then the bear is put back to the pool
    scene.restart();
    bibo::input::PlayerInput input;
    for (int i = 0; i < 8 * 60 && !race.canTalk(); ++i) {
      glm::vec3 direction = glm::normalize(scene.getBeaver().getPosition() -
                                           scene.getBear().getPosition());
      bibo::scene::Camera& camera = scene.getCamera();
      input.move = 0.4f * glm::vec2(glm::dot(direction, camera.getRight()),
                                    glm::dot(direction, camera.getForward()));
      scene.update(frame, input);
    }
    REQUIRE(race.canTalk());
    input = bibo::input::PlayerInput();
    input.interact = input.confirm = true;
    scene.update(frame, input);
    bibo::input::PlayerInput press;
    press.confirm = true;
    for (int i = 0; i < 60 * 60 && race.getStage() != RaceQuest::Stage::RACING;
         ++i) {
      scene.update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
    }
    REQUIRE(race.getStage() == RaceQuest::Stage::RACING);
    CHECK_FALSE(fishing.isOffered());
    // Mishka is no obstacle during the race
    CHECK_FALSE(physicsWorld.castRay(collider + across, -2.0f * across, hit));
    putBear();
    for (int i = 0; i < 4 * 60 && race.getStage() == RaceQuest::Stage::RACING;
         ++i) {
      scene.update(frame);
      REQUIRE(scene.getSalmon().getJumps().empty());
    }
    CHECK(race.getStage() == RaceQuest::Stage::RACING);
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
  Shader mainShader("Shader/main_shader.vert", "Shader/main_shader.frag");
  PhysicsWorld physicsWorld;
  Scene scene(&gl.window(), &mainShader, &physicsWorld);
  const float frame = 1.0f / 60.0f;
  const StonesQuest& quest = scene.getStonesQuest();
  const bibo::world::WorldGenerator& generator =
      scene.getWorld().getGenerator();
  CHECK(quest.isOffered());
  CHECK(scene.getChonk().getActivity() == Raccoon::Activity::SITTING);
  // Chonk sits beside the first part of a foot path, on dry land
  glm::vec3 chonk = scene.getChonk().getPosition();
  CHECK(generator.getLandmarks().pathDistance(glm::vec2(chonk.x, chonk.z)) <
        bibo::world::Landmarks::PATH_HALF_WIDTH + 4.0f);
  CHECK(chonk.y > generator.getSeaLevel());

  // Puts the bear on the ground at a distance from a point, looking at it
  auto putBear = [&](glm::vec3 target, float distance) {
    for (int i = 0; i < 32; ++i) {
      float angle = static_cast<float>(i) * glm::two_pi<float>() / 32.0f;
      glm::vec2 point = glm::vec2(target.x, target.z) +
                        distance * glm::vec2(std::cos(angle), std::sin(angle));
      float height = generator.height(point);
      if (height > generator.getSeaLevel() + 0.3f &&
          generator.getLandmarks().pathDistance(point) < 6.0f) {
        glm::vec3 place(
            point.x,
            height + bibo::scene::BearCharacter::groundOffset() + 0.05f,
            point.y);
        scene.getWorld().ensureCollision(place,
                                         scene.getCamera().getPosition());
        scene.getBear().getCharacter().placeAt(place, target - place);
        for (int k = 0; k < 60; ++k) {
          scene.update(frame);
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
    scene.update(frame, input);
    for (int i = 0; i < 60 * 60 && !quest.isPlayerInControl(); ++i) {
      scene.update(frame, i % 30 == 0 ? press : bibo::input::PlayerInput());
    }
    scene.update(frame);  // the other quests learn that this one is on (or
                          // over)
  };

  REQUIRE(putBear(chonk, 3.5f));
  REQUIRE(scene.getTalkPartner() == Scene::TalkPartner::CHONK);
  CHECK(quest.canTalk());
  talk();
  REQUIRE(quest.getStage() == StonesQuest::Stage::EXPLORING);
  std::vector<StonesQuest::Event> events = scene.takeStonesEvents();
  CHECK(std::count(events.begin(), events.end(),
                   StonesQuest::Event::ACCEPTED) == 1);
  CHECK(scene.takeRaccoonChatters() >
        3);  // Chonk chitters while its words appear
  CHECK_FALSE(scene.getRaceQuest().isOffered());  // one quest at a time
  // The bear faced Chonk from close by while they talked
  glm::vec3 offset = chonk - scene.getBear().getPosition();
  CHECK(glm::length(glm::vec2(offset.x, offset.z)) < 4.2f);

  // At the stones: the flat stone in the middle
  glm::vec3 stones = scene.getStonesCenter();
  REQUIRE(putBear(stones, 2.5f));
  CHECK(quest.hasArrived());
  REQUIRE(scene.getTalkPartner() == Scene::TalkPartner::STONES);
  talk();
  REQUIRE(quest.getStage() == StonesQuest::Stage::RETURNING);
  CHECK(quest.hasReport());

  // Back to Chonk with the report
  REQUIRE(putBear(chonk, 3.5f));
  talk();
  CHECK(quest.hasCompleted());
  CHECK_FALSE(quest.isActive());
  CHECK(quest.getReport() == StonesQuest::Report::TRUTH);
  events = scene.takeStonesEvents();
  CHECK(std::count(events.begin(), events.end(),
                   StonesQuest::Event::COMPLETED) == 1);
  CHECK(scene.getRaceQuest().isOffered());

  SECTION("Restarting keeps the completed quest, Chonk is back at its place") {
    scene.restart();
    scene.update(frame);
    CHECK(quest.hasCompleted());
    CHECK_FALSE(quest.isOffered());
    CHECK(scene.getChonk().getPosition() == chonk);
  }
}
