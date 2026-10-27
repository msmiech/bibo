#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <cmath>
#include <vector>

#include "Resources/SkinnedModel.hpp"
#include "Scene/Beaver.hpp"
#include "Scene/BeaverModel.hpp"
#include "Scene/CreatureMeshes.hpp"
#include "Scene/Hedgehog.hpp"
#include "Scene/HedgehogModel.hpp"
#include "Scene/Raccoon.hpp"
#include "Scene/RaccoonModel.hpp"
#include "Scene/Salmon.hpp"
#include "TestSupport.hpp"

using bibo::scene::Beaver;
using bibo::scene::BeaverModel;
using bibo::scene::CreatureModel;
using bibo::scene::Hedgehog;
using bibo::scene::HedgehogModel;
using bibo::scene::kBeaverHeight;
using bibo::scene::kBeaverNeck;
using bibo::scene::kCreatureModelCount;
using bibo::scene::kRaccoonHeight;
using bibo::scene::kRaccoonShoulder;
using bibo::scene::LargestPool;
using bibo::scene::Raccoon;
using bibo::scene::RaccoonFans;
using bibo::scene::RaccoonModel;
using bibo::scene::SalmonPools;
using bibo::scene::WaterPool;

using glm::vec2;
using glm::vec3;

namespace {
const float kFrame = 1.0f / 60.0f;

/**
 * Flat ground at y = 2 on a strip along z (|x| < 5), like the floor of the
 * track
 */
bool Strip(vec2 position, float& height) {
  height = 2.0f;
  return std::abs(position.x) < 5.0f;
}

WaterPool Pool(vec3 center) {
  WaterPool water;
  water.center = center;
  water.radius = 3.0f;
  for (float x = -2.0f; x <= 2.0f; x += 1.0f) {
    water.spots.push_back(vec2(center.x + x, center.z));
  }
  return water;
}
}  // namespace

TEST_CASE("A hedgehog strolls around its home on the ground it can walk on",
          "[animals]") {
  const vec2 home(0.0f, 10.0f);
  Hedgehog hedgehog(home, vec2(0.0f, 1.0f), 7u, Strip);
  CHECK(hedgehog.GetPosition() == vec3(0.0f, 2.0f, 10.0f));
  CHECK_FALSE(hedgehog.IsWalking());  // sniffs first

  float walked = 0.0f, farthest = 0.0f;
  vec3 last = hedgehog.GetPosition();
  bool walked_once = false;
  for (int i = 0; i < 60 * 60; ++i) {
    hedgehog.Update(kFrame);
    vec3 position = hedgehog.GetPosition();
    walked += glm::distance(position, last);
    last = position;
    walked_once = walked_once || hedgehog.IsWalking();
    farthest =
        std::max(farthest, glm::distance(vec2(position.x, position.z), home));
    REQUIRE(std::abs(position.x) < 5.0f);  // stays on the strip
    REQUIRE(position.y == 2.0f);
    // The collision sphere is around the body, which stands on the ground
    REQUIRE(hedgehog.GetColliderCenter().y - hedgehog.GetColliderRadius() >
            1.9f);
  }
  CAPTURE(walked, farthest);
  CHECK(walked_once);
  CHECK(walked > 15.0f);  // it walks, but slowly (and pauses)
  CHECK(walked < Hedgehog::kWalkSpeed * 60.0f);
  CHECK(farthest <= Hedgehog::kHomeRadius + 0.5f);

  SECTION("Its feet are under its body and step while it walks") {
    Hedgehog::Pose pose = hedgehog.GetPose();
    for (const vec3& foot : pose.feet) {
      CHECK(glm::distance(vec2(foot.x, foot.z), vec2(last.x, last.z)) <
            0.4f * Hedgehog::kScale);
      CHECK(foot.y >= 2.0f - 1e-4f);
    }
  }
  SECTION("Reset: back home") {
    hedgehog.Reset();
    CHECK(hedgehog.GetPosition() == vec3(0.0f, 2.0f, 10.0f));
  }
  SECTION("Startled it hops up and lands again") {
    float before = hedgehog.GetColliderCenter().y;
    hedgehog.Startle();
    hedgehog.Update(0.2f);
    CHECK(hedgehog.GetColliderCenter().y > before + 0.2f);
    hedgehog.Update(0.5f);
    // it may bob with its steps
    CHECK(std::abs(hedgehog.GetColliderCenter().y - before) < 0.1f);
  }
}

TEST_CASE(
    "Salmon only jump while the bear is close to their pool, out of the water "
    "and back into it",
    "[animals]") {
  SalmonPools salmon({Pool(vec3(0.0f, -0.5f, 0.0f))}, 3u);
  const vec3 far(0.0f, 1.0f, 100.0f);
  for (int i = 0; i < 5 * 60; ++i) {
    salmon.Update(kFrame, far, far, true);
  }
  CHECK(salmon.GetJumps().empty());

  const vec3 near(0.0f, 1.0f, 15.0f);  // not close enough to catch one
  float highest = -1e9f;
  int jumps = 0;
  for (int i = 0; i < 10 * 60; ++i) {
    std::size_t before = salmon.GetJumps().size();
    REQUIRE(salmon.Update(kFrame, near, near, true) == -1);
    jumps += salmon.GetJumps().size() > before ? 1 : 0;
    for (const SalmonPools::Jump& jump : salmon.GetJumps()) {
      vec3 position = jump.Position();
      highest = std::max(highest, position.y);
      REQUIRE(position.y >= -0.5f - 0.31f);  // from just below the surface
      CHECK(glm::distance(vec2(position.x, position.z), vec2(0.0f)) <
            3.0f + 4.0f);  // not far from its pool
    }
    REQUIRE(salmon.GetJumps().size() <= 2);
  }
  CAPTURE(jumps, highest);
  CHECK(jumps >= 4);  // every now and then
  // high enough for the bear to catch it in a jump
  CHECK(highest > -0.5f + 2.0f);
  CHECK_FALSE(salmon.GetDroplets().empty());  // splashes
}

TEST_CASE("The bear catches salmon in the air, none jump out of closed pools",
          "[animals]") {
  SalmonPools salmon(
      {Pool(vec3(0.0f, -0.5f, 0.0f)), Pool(vec3(0.0f, -0.5f, 40.0f))}, 5u);
  const vec3 near(0.0f, 1.0f, 8.0f);
  // Waits for a salmon of the first pool, then puts its mouth where the salmon
  // is
  auto catch_one = [&](const std::vector<bool>& closed) {
    int caught = -1;
    for (int i = 0; i < 10 * 60 && caught < 0; ++i) {
      vec3 mouth = near;
      if (!salmon.GetJumps().empty() && salmon.GetJumps().front().time > 0.3f) {
        mouth = salmon.GetJumps().front().Position();
      }
      caught = salmon.Update(kFrame, near, mouth, true, closed);
    }
    return caught;
  };
  REQUIRE(catch_one({}) == 0);

  SECTION("Open pools stay open: the bear can catch another one there") {
    CHECK(catch_one({}) == 0);
    CHECK(catch_one({false, false}) == 0);
  }
  SECTION(
      "No more salmon jump out of a closed pool, and none can be caught "
      "there") {
    const std::vector<bool> closed = {true, false};
    for (int i = 0; i < 3 * 60; ++i) {
      salmon.Update(kFrame, near, near, true, closed);
    }
    for (int i = 0; i < 5 * 60; ++i) {
      for (const SalmonPools::Jump& jump : salmon.GetJumps()) {
        REQUIRE(jump.pool != 0);
      }
      vec3 mouth = salmon.GetJumps().empty()
                       ? near
                       : salmon.GetJumps().front().Position();
      REQUIRE(salmon.Update(kFrame, near, mouth, true, closed) == -1);
    }
  }
  SECTION(
      "The salmon of a pool that is closed while they are in the air cannot be "
      "caught") {
    for (int i = 0; i < 10 * 60 && (salmon.GetJumps().empty() ||
                                    salmon.GetJumps().front().time < 0.3f);
         ++i) {
      salmon.Update(kFrame, near, near, false);
    }
    REQUIRE_FALSE(salmon.GetJumps().empty());
    CHECK(salmon.Update(kFrame, near, salmon.GetJumps().front().Position(),
                        true, {true, true}) == -1);
  }
  SECTION("Not while the bear cannot catch one (it holds one already)") {
    const vec3 second(0.0f, 1.0f, 48.0f);
    for (int i = 0; i < 10 * 60; ++i) {
      vec3 mouth = salmon.GetJumps().empty()
                       ? second
                       : salmon.GetJumps().front().Position();
      REQUIRE(salmon.Update(kFrame, second, mouth, false) == -1);
    }
  }
  SECTION("Reset: no salmon in the air") {
    salmon.Reset();
    CHECK(salmon.GetJumps().empty());
    CHECK(salmon.GetDroplets().empty());
  }
}

TEST_CASE("The largest pool is the one with the most water", "[animals]") {
  CHECK(LargestPool({}) == -1);
  std::vector<WaterPool> pools = {Pool(vec3(0.0f)),
                                  Pool(vec3(0.0f, 0.0f, 40.0f)),
                                  Pool(vec3(0.0f, 0.0f, 80.0f))};
  pools[0].area = 30.0f;
  pools[1].area = 75.0f;
  pools[2].area = 50.0f;
  CHECK(LargestPool(pools) == 1);
}

TEST_CASE("The models of the animals are closed meshes of the right size",
          "[animals]") {
  const int index = GENERATE(range(0, kCreatureModelCount));
  CAPTURE(index);
  bibo::world::VegetationMesh mesh =
      BuildCreatureModel(static_cast<CreatureModel>(index));
  REQUIRE_FALSE(mesh.vertices.empty());
  REQUIRE(mesh.indices.size() % 3 == 0);
  for (std::uint32_t i : mesh.indices) {
    REQUIRE(i < mesh.vertices.size());
  }
  for (const bibo::world::VegetationVertex& vertex : mesh.vertices) {
    REQUIRE(std::isfinite(vertex.position.x + vertex.position.y +
                          vertex.position.z));
    REQUIRE(std::abs(glm::length(vertex.normal) - 1.0f) < 1e-3f);
    CHECK(vertex.parameters[1] == 0);  // no wind
    CHECK(vertex.parameters[2] == 0);  // no snow on them
  }
  CHECK(mesh.radius > 0.0f);
  // the hedgehog and the salmon are about 1 long, a droplet has a radius of 1
  CHECK(mesh.radius <= 1.0001f);
}

TEST_CASE(
    "The hedgehog has spines on its back, fur on its face and belly, eyes, a "
    "nose and claws",
    "[animals]") {
  using bibo::resources::SkinnedModel;
  HedgehogModel model = HedgehogModel::Build();
  const std::size_t vertices = model.positions.size();
  REQUIRE(vertices > 0);
  REQUIRE(model.normals.size() == vertices);
  REQUIRE(model.colors.size() == vertices);
  REQUIRE(model.surfaces.size() == vertices);
  REQUIRE(model.parts.size() == vertices);
  REQUIRE(model.combs.size() == vertices);
  REQUIRE(model.spines.size() == vertices);
  REQUIRE(model.indices.size() % 3 == 0);
  for (unsigned int index : model.indices) {
    REQUIRE(index < vertices);
  }
  CAPTURE(model.spine_triangles.size(), model.fur_triangles.size());
  // mostly spines
  CHECK(model.spine_triangles.size() > model.fur_triangles.size());
  CHECK_FALSE(model.fur_triangles.empty());
  for (std::size_t i = 0; i < model.spine_triangles.size(); i += 3) {
    // Down to the belly (they hide the legs) and behind the face
    vec3 center = (model.positions[model.spine_triangles[i]] +
                   model.positions[model.spine_triangles[i + 1]] +
                   model.positions[model.spine_triangles[i + 2]]) /
                  3.0f;
    REQUIRE(center.y > 0.03f);
    REQUIRE(center.z < 0.45f);
  }
  int surfaces[7] = {};
  for (std::size_t v = 0; v < vertices; ++v) {
    surfaces[static_cast<int>(model.surfaces[v])]++;
    REQUIRE(std::abs(glm::length(model.normals[v]) - 1.0f) < 1e-3f);
    if (model.parts[v] == HedgehogModel::kBody &&
        static_cast<int>(model.surfaces[v]) == SkinnedModel::kFur) {
      REQUIRE(model.positions[v].y >= 0.0f);  // not in the ground
    }
  }
  CHECK(surfaces[SkinnedModel::kEye] > 0);
  CHECK(surfaces[SkinnedModel::kNose] > 0);
  CHECK(surfaces[SkinnedModel::kClaw] > 0);
  // The snout points forward, it is the front of the hedgehog
  float front = -1e9f;
  for (std::size_t v = 0; v < vertices; ++v) {
    if (model.parts[v] == HedgehogModel::kBody) {
      front = std::max(front, model.positions[v].z);
    }
  }
  CHECK(front > 0.5f);
}

TEST_CASE(
    "The beaver sits upright: fur, orange front teeth, eyes, a nose, a mouth, "
    "claws and a flat tail",
    "[animals][beaver]") {
  using bibo::resources::SkinnedModel;
  BeaverModel model = BeaverModel::Build();
  const std::size_t vertices = model.positions.size();
  REQUIRE(vertices > 0);
  REQUIRE(model.normals.size() == vertices);
  REQUIRE(model.colors.size() == vertices);
  REQUIRE(model.surfaces.size() == vertices);
  REQUIRE(model.parts.size() == vertices);
  REQUIRE(model.combs.size() == vertices);
  REQUIRE(model.indices.size() % 3 == 0);
  for (unsigned int index : model.indices) {
    REQUIRE(index < vertices);
  }
  CHECK(model.spine_triangles.empty());  // no spines
  // fur on most of it
  CHECK(model.fur_triangles.size() > model.indices.size() / 3);

  int surfaces[7] = {};
  int parts[BeaverModel::kPartCount] = {};
  for (std::size_t v = 0; v < vertices; ++v) {
    surfaces[static_cast<int>(model.surfaces[v])]++;
    REQUIRE(model.parts[v] >= 0);
    REQUIRE(model.parts[v] < BeaverModel::kPartCount);
    parts[model.parts[v]]++;
    REQUIRE(std::abs(glm::length(model.normals[v]) - 1.0f) < 1e-3f);
    REQUIRE(std::abs(glm::length(model.combs[v]) - 1.0f) < 1e-3f);
  }
  CHECK(surfaces[SkinnedModel::kEye] > 0);
  CHECK(surfaces[SkinnedModel::kNose] > 0);
  CHECK(surfaces[SkinnedModel::kTeeth] > 0);
  CHECK(surfaces[SkinnedModel::kMouth] > 0);
  CHECK(surfaces[SkinnedModel::kTongue] > 0);
  CHECK(surfaces[SkinnedModel::kClaw] > 0);
  for (int part = 0; part < BeaverModel::kPartCount; ++part) {
    CAPTURE(part);
    CHECK(parts[part] > 0);
  }
  // The teeth are orange
  for (std::size_t v = 0; v < vertices; ++v) {
    if (static_cast<int>(model.surfaces[v]) == SkinnedModel::kTeeth) {
      REQUIRE(model.colors[v].r > 2.0f * model.colors[v].g);
      REQUIRE(model.colors[v].g > model.colors[v].b);
    }
  }
  // The body sits on the ground and is about 1 high with its head (the head is
  // in the space of the neck)
  float bottom = 1e9f, top = -1e9f;
  for (std::size_t v = 0; v < vertices; ++v) {
    if (model.parts[v] == BeaverModel::kBody) {
      bottom = std::min(bottom, model.positions[v].y);
    }
    if (model.parts[v] == BeaverModel::kHead) {
      top = std::max(top, model.positions[v].y + kBeaverNeck.y);
    }
  }
  CHECK(bottom >= -0.01f);
  CHECK(bottom < 0.05f);
  CHECK(top > 0.9f * kBeaverHeight);
  CHECK(top < 1.1f * kBeaverHeight);
}

TEST_CASE(
    "The beaver turns to the bear when it comes close, talks with its mouth "
    "and cheers",
    "[animals][beaver]") {
  const vec3 home(10.0f, 2.0f, -5.0f);
  Beaver beaver(home, vec3(1.0f, 0.0f, 0.0f));
  std::vector<glm::mat4> pose = beaver.GetPose();
  REQUIRE(pose.size() == static_cast<std::size_t>(BeaverModel::kPartCount));
  auto forward = [&]() {
    glm::mat4 body = beaver.GetPose()[BeaverModel::kBody];
    return glm::normalize(vec3(body[2].x, 0.0f, body[2].z));
  };
  CHECK(glm::dot(forward(), vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
  // Sitting on the ground, the marker above its head
  CHECK(std::abs(vec3(pose[BeaverModel::kBody][3]).y - home.y) < 0.05f);
  CHECK(beaver.GetMarkerPosition().y > home.y + kBeaverHeight * Beaver::kScale);
  CHECK(beaver.GetColliderRadius() > 0.5f);

  SECTION("A bear far away is not noticed") {
    for (int i = 0; i < 120; ++i) {
      beaver.Update(kFrame, home + vec3(0.0f, 1.0f, -40.0f));
    }
    CHECK(glm::dot(forward(), vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
  }
  SECTION("A bear close by: it turns to it") {
    for (int i = 0; i < 120; ++i) {
      beaver.Update(kFrame, home + vec3(0.0f, 1.0f, -6.0f));
    }
    CHECK(glm::dot(forward(), vec3(0.0f, 0.0f, -1.0f)) > 0.95f);
  }
  SECTION("Its eyes look at the bear: up when the bear is above its face") {
    auto gaze_y = [&](vec3 bear) {
      for (int i = 0; i < 120; ++i) {
        beaver.Update(kFrame, bear);
      }
      glm::mat3 eye(beaver.GetPose()[BeaverModel::kEyeLeft]);
      return glm::normalize(eye * BeaverModel::EyeGaze(true)).y;
    };
    vec3 face = beaver.GetFacePosition();
    float level = gaze_y(face + vec3(4.0f, 0.0f, 0.0f));
    float above = gaze_y(face + vec3(3.0f, 3.0f, 0.0f));
    CHECK(above > level + 0.2f);
    CHECK(beaver.GetFacePosition().y > face.y - 0.5f);
  }
  SECTION("Every sound opens and closes the mouth") {
    beaver.SetTalking(true);
    CHECK(beaver.IsTalking());
    beaver.Update(kFrame, home);
    CHECK(beaver.GetMouthOpen() < 0.05f);
    beaver.Speak();
    float widest = 0.0f;
    for (int i = 0; i < 12; ++i) {
      beaver.Update(kFrame, home);
      widest = std::max(widest, beaver.GetMouthOpen());
    }
    CHECK(widest > 0.8f);
    CHECK(beaver.GetMouthOpen() < 0.1f);
  }
  SECTION("Cheering: it hops") {
    beaver.Cheer();
    float highest = home.y;
    for (int i = 0; i < 60; ++i) {
      beaver.Update(kFrame, home + vec3(0.0f, 1.0f, -6.0f));
      highest =
          std::max(highest, vec3(beaver.GetPose()[BeaverModel::kBody][3]).y);
    }
    CHECK(highest > home.y + 0.1f);
    CHECK(std::abs(vec3(beaver.GetPose()[BeaverModel::kBody][3]).y - home.y) <
          0.05f);  // back on the ground
  }
  for (const glm::mat4& part : beaver.GetPose()) {
    REQUIRE(std::isfinite(part[3].x + part[3].y + part[3].z));
  }
}

TEST_CASE("A hedgehog scurries away from the bear when it comes close",
          "[animals]") {
  const vec2 home(0.0f, 10.0f);
  Hedgehog hedgehog(home, vec2(0.0f, 1.0f), 7u, Strip);
  hedgehog.Update(kFrame);
  vec3 start = hedgehog.GetPosition();
  // The bear comes from -z
  vec3 bear = start + vec3(0.0f, 0.0f, -3.0f);
  hedgehog.Watch(bear);
  hedgehog.Update(kFrame);
  CHECK(hedgehog.IsScurrying());
  for (int i = 0; i < 60 * 2; ++i) {
    hedgehog.Watch(bear);
    hedgehog.Update(kFrame);
    REQUIRE(std::abs(hedgehog.GetPosition().x) < 5.0f);
  }
  CHECK(glm::distance(hedgehog.GetPosition(), bear) >
        glm::distance(start, bear) + 1.5f);
  CHECK(glm::distance(vec2(hedgehog.GetPosition().x, hedgehog.GetPosition().z),
                      home) <= 1.5f * Hedgehog::kHomeRadius + 0.1f);

  SECTION("A bear far away does not bother it") {
    hedgehog.Reset();
    hedgehog.Watch(start + vec3(0.0f, 0.0f, -20.0f));
    hedgehog.Update(kFrame);
    CHECK_FALSE(hedgehog.IsScurrying());
  }
}

TEST_CASE("The beaver waves at the bear when it comes close",
          "[animals][beaver]") {
  const vec3 home(10.0f, 2.0f, -5.0f);
  Beaver beaver(home, vec3(1.0f, 0.0f, 0.0f));
  beaver.Update(kFrame, home + vec3(0.0f, 1.0f, -40.0f));
  CHECK_FALSE(beaver.IsWaving());
  beaver.Update(kFrame, home + vec3(0.0f, 1.0f, -8.0f));
  CHECK(beaver.IsWaving());
  // The right paw goes up
  glm::mat4 arm = beaver.GetPose()[BeaverModel::kArmRight];
  for (int i = 0; i < 30; ++i) {
    beaver.Update(kFrame, home + vec3(0.0f, 1.0f, -8.0f));
  }
  vec3 hand = vec3(beaver.GetPose()[BeaverModel::kArmRight] *
                   glm::vec4(0.0f, -0.2f, 0.09f, 1.0f));
  CHECK(hand.y > vec3(arm * glm::vec4(0.0f, -0.2f, 0.09f, 1.0f)).y + 0.2f);
  for (int i = 0; i < 120; ++i) {
    beaver.Update(kFrame, home + vec3(0.0f, 1.0f, -8.0f));
  }
  CHECK_FALSE(beaver.IsWaving());  // once, while the bear stays
}

TEST_CASE(
    "The raccoon stands on all fours: gray fur, a mask, eyes, a nose, a mouth, "
    "claws and a ringed tail",
    "[animals][raccoon]") {
  using bibo::resources::SkinnedModel;
  for (float girth : {1.0f, 1.45f}) {
    CAPTURE(girth);
    RaccoonModel model = RaccoonModel::Build(girth);
    const std::size_t vertices = model.positions.size();
    REQUIRE(vertices > 0);
    REQUIRE(model.normals.size() == vertices);
    REQUIRE(model.colors.size() == vertices);
    REQUIRE(model.surfaces.size() == vertices);
    REQUIRE(model.parts.size() == vertices);
    REQUIRE(model.combs.size() == vertices);
    for (unsigned int index : model.indices) {
      REQUIRE(index < vertices);
    }
    CHECK(model.spine_triangles.empty());
    CHECK(model.fur_triangles.size() > model.indices.size() / 3);
    int surfaces[7] = {};
    int parts[RaccoonModel::kPartCount] = {};
    int dark = 0, light = 0, tail_dark = 0, tail_light = 0;
    for (std::size_t v = 0; v < vertices; ++v) {
      surfaces[static_cast<int>(model.surfaces[v])]++;
      REQUIRE(model.parts[v] >= 0);
      REQUIRE(model.parts[v] < RaccoonModel::kPartCount);
      parts[model.parts[v]]++;
      REQUIRE(std::abs(glm::length(model.normals[v]) - 1.0f) < 1e-3f);
      REQUIRE(std::abs(glm::length(model.combs[v]) - 1.0f) < 1e-3f);
      if (static_cast<int>(model.surfaces[v]) == SkinnedModel::kFur &&
          model.parts[v] == RaccoonModel::kHead) {
        dark += model.colors[v].g < 0.03f ? 1 : 0;  // the mask
        light += model.colors[v].g > 0.4f ? 1 : 0;  // the white around it
      }
      if (model.parts[v] == RaccoonModel::kTail) {
        tail_dark += model.colors[v].g < 0.05f ? 1 : 0;
        tail_light += model.colors[v].g > 0.15f ? 1 : 0;
      }
    }
    CHECK(surfaces[SkinnedModel::kEye] > 0);
    CHECK(surfaces[SkinnedModel::kNose] > 0);
    CHECK(surfaces[SkinnedModel::kMouth] > 0);
    CHECK(surfaces[SkinnedModel::kTongue] > 0);
    CHECK(surfaces[SkinnedModel::kTeeth] > 0);
    CHECK(surfaces[SkinnedModel::kClaw] > 0);
    for (int part = 0; part < RaccoonModel::kPartCount; ++part) {
      CAPTURE(part);
      CHECK(parts[part] > 0);
    }
    CHECK(dark > 50);
    CHECK(light > 50);
    CHECK(tail_dark > 50);
    CHECK(tail_light > 50);
    // The legs reach the ground, the back is about kRaccoonHeight high
    float lowest = 1e9f, top = -1e9f;
    for (std::size_t v = 0; v < vertices; ++v) {
      int part = model.parts[v];
      if (part == RaccoonModel::kFrontLeft) {
        lowest = std::min(lowest, model.positions[v].y + kRaccoonShoulder.y);
      }
      if (part == RaccoonModel::kBody) {
        top = std::max(top, model.positions[v].y);
      }
    }
    CHECK(std::abs(lowest) < 0.02f);
    CHECK(top > 0.85f * kRaccoonHeight);
    CHECK(top < 1.15f * kRaccoonHeight);
  }
}

TEST_CASE(
    "A raccoon comes out of the woods, cheers for the bear on its hind legs "
    "and goes back",
    "[animals][raccoon]") {
  auto flat = [](vec2, float& height) {
    height = 2.0f;
    return true;
  };
  Raccoon raccoon(flat, 9u);
  CHECK_FALSE(raccoon.IsVisible());
  const vec3 bear(0.0f, 3.0f, 0.0f);
  raccoon.ComeAndCheer(vec2(20.0f, 0.0f), vec2(6.0f, 0.0f));
  CHECK(raccoon.GetActivity() == Raccoon::Activity::kComing);
  CHECK(raccoon.GetPosition() == vec3(20.0f, 2.0f, 0.0f));
  for (int i = 0;
       i < 60 * 8 && raccoon.GetActivity() == Raccoon::Activity::kComing; ++i) {
    raccoon.Update(kFrame, bear);
    REQUIRE(raccoon.GetUpright() < 0.5f);  // on all fours
  }
  REQUIRE(raccoon.GetActivity() == Raccoon::Activity::kCheering);
  CHECK(glm::distance(raccoon.GetPosition(), vec3(6.0f, 2.0f, 0.0f)) < 0.5f);
  // Stands up, turns to the bear, hops and chatters
  float highest = 0.0f;
  for (int i = 0; i < 60 * 3; ++i) {
    raccoon.Update(kFrame, bear);
    highest = std::max(highest,
                       vec3(raccoon.GetPose()[RaccoonModel::kHindLeft][3]).y);
  }
  CHECK(raccoon.GetUpright() > 0.9f);
  CHECK(glm::dot(raccoon.GetDirection(), vec3(-1.0f, 0.0f, 0.0f)) > 0.95f);
  CHECK(raccoon.TakeChatters() > 0);
  CHECK(highest > 2.0f + 0.1f);
  CHECK(raccoon.GetFacePosition().y > 2.0f + 1.0f);
  // The big jump
  raccoon.Cheer();
  float jump = 0.0f;
  for (int i = 0; i < 30; ++i) {
    raccoon.Update(kFrame, bear);
    jump =
        std::max(jump, vec3(raccoon.GetPose()[RaccoonModel::kHindLeft][3]).y);
  }
  CHECK(jump > highest + 0.2f);
  for (const glm::mat4& part : raccoon.GetPose()) {
    REQUIRE(std::isfinite(part[3].x + part[3].y + part[3].z));
  }

  raccoon.Leave();
  CHECK(raccoon.GetActivity() == Raccoon::Activity::kLeaving);
  for (int i = 0; i < 60 * 8 && raccoon.IsVisible(); ++i) {
    raccoon.Update(kFrame, bear);
  }
  CHECK_FALSE(raccoon.IsVisible());
}

TEST_CASE(
    "Chonk sits at its place, looks at the bear when it is close and talks "
    "with its mouth",
    "[animals][raccoon]") {
  auto flat = [](vec2, float& height) {
    height = 0.0f;
    return true;
  };
  Raccoon chonk(flat, 3u, 1.45f);
  chonk.SitAt(vec3(0.0f), vec3(0.0f, 0.0f, 1.0f));
  CHECK(chonk.GetActivity() == Raccoon::Activity::kSitting);
  CHECK(chonk.IsVisible());
  chonk.Leave();  // it stays
  CHECK(chonk.GetActivity() == Raccoon::Activity::kSitting);
  for (int i = 0; i < 120; ++i) {
    chonk.Update(kFrame, vec3(0.0f, 1.0f, -40.0f));
  }
  CHECK(glm::dot(chonk.GetDirection(), vec3(0.0f, 0.0f, 1.0f)) > 0.99f);
  CHECK(chonk.GetUpright() > 0.6f);
  CHECK(chonk.GetMarkerPosition().y > chonk.GetFacePosition().y + 0.5f);
  for (int i = 0; i < 120; ++i) {
    chonk.Update(kFrame, vec3(6.0f, 1.0f, 0.0f));
  }
  CHECK(glm::dot(chonk.GetDirection(), vec3(1.0f, 0.0f, 0.0f)) > 0.95f);

  chonk.SetTalking(true);
  chonk.Speak();
  float widest = 0.0f;
  for (int i = 0; i < 12; ++i) {
    chonk.Update(kFrame, vec3(6.0f, 1.0f, 0.0f));
    widest = std::max(widest, chonk.GetMouthOpen());
  }
  CHECK(widest > 0.8f);
  // A larger collision sphere than an ordinary raccoon
  Raccoon other(flat, 4u);
  CHECK(chonk.GetColliderRadius() > other.GetColliderRadius());
}

TEST_CASE(
    "Raccoons come to cheer ahead of the bear during a quest, a few at a time, "
    "and go when it is over",
    "[animals][raccoon]") {
  auto flat = [](vec2, float& height) {
    height = 0.0f;
    return true;
  };
  int asked = 0;
  RaccoonFans::Finder finder = [&](vec3 bear, vec3 heading,
                                   std::mt19937& random, vec2& from,
                                   vec2& spot) {
    ++asked;
    float side = random() % 2 == 0 ? 1.0f : -1.0f;
    spot = vec2(bear.x, bear.z) + vec2(heading.x, heading.z) * 15.0f +
           vec2(-heading.z, heading.x) * (6.0f * side) +
           vec2(0.0f, static_cast<float>(asked % 5));
    from = spot + vec2(-heading.z, heading.x) * (12.0f * side);
    return true;
  };
  RaccoonFans fans(flat, finder);
  const vec3 heading(1.0f, 0.0f, 0.0f);
  vec3 bear(0.0f);
  for (int i = 0; i < 60 * 5; ++i) {
    fans.Update(kFrame, false, bear, bear + vec3(0.0f, 1.0f, 0.0f), heading);
  }
  CHECK(fans.GetWatchingCount() == 0);
  CHECK(asked == 0);

  int most = 0;
  for (int i = 0; i < 60 * 20; ++i) {
    fans.Update(kFrame, true, bear, bear + vec3(0.0f, 1.0f, 0.0f), heading);
    most = std::max(most, fans.GetWatchingCount());
    REQUIRE(fans.GetWatchingCount() <= RaccoonFans::kMaxWatching);
  }
  CHECK(most >= 2);
  int cheering = 0;
  for (const Raccoon& raccoon : fans.GetRaccoons()) {
    cheering += raccoon.GetActivity() == Raccoon::Activity::kCheering ? 1 : 0;
  }
  CHECK(cheering >= 1);
  fans.Celebrate();
  CHECK(fans.TakeChatters() > 0);

  // The quest is over: back into the woods
  for (int i = 0; i < 60 * 10; ++i) {
    fans.Update(kFrame, false, bear, bear + vec3(0.0f, 1.0f, 0.0f), heading);
  }
  for (const Raccoon& raccoon : fans.GetRaccoons()) {
    CHECK_FALSE(raccoon.IsVisible());
  }
}
