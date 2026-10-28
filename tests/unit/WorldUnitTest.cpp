#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "Render/ShadowCascades.hpp"
#include "Resources/Shader.hpp"
#include "TestSupport.hpp"
#include "World/Noise.hpp"
#include "World/WorkerPool.hpp"
#include "World/WorldStreamer.hpp"

namespace noise = bibo::world::noise;
using bibo::world::Box;
using bibo::world::WorkerPool;
using glm::vec2;
using glm::vec3;

TEST_CASE("The noise of the world is deterministic and smooth",
          "[world][noise]") {
  SECTION(
      "The same position and seed always give the same value, another seed "
      "another one") {
    for (int i = 0; i < 100; ++i) {
      vec3 position(i * 0.37f, i * -1.13f, i * 2.71f);
      CHECK(noise::Gradient(position, 7u) == noise::Gradient(position, 7u));
      CHECK(noise::Fbm(vec2(position), 4, 7u) ==
            noise::Fbm(vec2(position), 4, 7u));
    }
    CHECK(noise::Gradient(vec2(0.5f, 0.25f), 1u) !=
          noise::Gradient(vec2(0.5f, 0.25f), 2u));
  }

  SECTION(
      "Gradient noise is 0 at the lattice points and stays within -1.5 and "
      "1.5") {
    CHECK(noise::Gradient(vec2(3.0f, -7.0f), 5u) == 0.0f);
    CHECK(noise::Gradient(vec3(3.0f, -7.0f, 2.0f), 5u) == 0.0f);
    float lowest = 0.0f, highest = 0.0f;
    for (int i = 0; i < 20000; ++i) {
      vec3 position(i * 0.0731f, i * 0.0173f, i * -0.0457f);
      float value = noise::Fbm(position, 4, 3u);
      lowest = std::min(lowest, value);
      highest = std::max(highest, value);
      float ridge = noise::Ridged(vec2(position), 4, 3u);
      REQUIRE(ridge >= 0.0f);
      REQUIRE(ridge <= 1.0f);
    }
    CHECK(lowest > -1.5f);
    CHECK(highest < 1.5f);
    CHECK(highest - lowest > 0.8f);  // not flat
  }

  SECTION("Close positions have close values (continuous)") {
    for (int i = 0; i < 1000; ++i) {
      vec2 position(i * 0.913f, i * 0.377f);
      CHECK(std::abs(noise::Fbm(position, 5, 9u) -
                     noise::Fbm(position + vec2(1e-3f, 0.0f), 5, 9u)) < 0.02f);
    }
  }

  SECTION("The random numbers are in their range") {
    noise::Random random(42u);
    for (int i = 0; i < 1000; ++i) {
      float unit = random.Unit();
      REQUIRE(unit >= 0.0f);
      REQUIRE(unit < 1.0f);
      float value = random.Range(-3.0f, 5.0f);
      REQUIRE(value >= -3.0f);
      REQUIRE(value < 5.0f);
    }
    noise::Random same(42u);
    CHECK(same.Unit() == noise::Random(42u).Unit());
  }
}

TEST_CASE("The worker pool does all jobs, the most important first",
          "[world][threads]") {
  SECTION("All jobs are done") {
    WorkerPool pool(3);
    std::atomic<int> done{0};
    for (int i = 0; i < 200; ++i) {
      pool.Submit([&done] { ++done; }, static_cast<float>(i % 7));
    }
    pool.WaitIdle();
    CHECK(done == 200);
    CHECK(pool.GetQueuedCount() == 0);
    CHECK(pool.GetThreadCount() == 3);
  }

  SECTION("With one thread the lowest priority value comes first") {
    WorkerPool pool(1);
    std::mutex mutex;
    std::vector<int> order;
    // The first job blocks the thread until all are queued
    std::atomic<bool> release{false};
    pool.Submit(
        [&] {
          while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        },
        -1.0f);
    for (int i : {5, 1, 4, 2, 3}) {
      pool.Submit(
          [&mutex, &order, i] {
            std::lock_guard<std::mutex> lock(mutex);
            order.push_back(i);
          },
          static_cast<float>(i));
    }
    release = true;
    pool.WaitIdle();
    CHECK(order == std::vector<int>{1, 2, 3, 4, 5});
  }

  SECTION("Jobs that did not start are dropped when the pool is destroyed") {
    std::atomic<int> done{0};
    {
      WorkerPool pool(1);
      std::atomic<bool> release{false};
      pool.Submit(
          [&] {
            while (!release)
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
          },
          0.0f);
      for (int i = 0; i < 50; ++i) {
        pool.Submit([&done] { ++done; }, 1.0f);
      }
      pool.CancelQueued();
      release = true;
    }
    CHECK(done == 0);
  }
}

TEST_CASE("The distance to a box is 0 inside of it", "[world]") {
  Box box{vec3(0.0f), vec3(2.0f, 4.0f, 6.0f)};
  CHECK(bibo::world::DistanceToBox(vec3(1.0f, 1.0f, 1.0f), box) == 0.0f);
  CHECK_THAT(bibo::world::DistanceToBox(vec3(5.0f, 2.0f, 3.0f), box),
             Catch::Matchers::WithinAbs(3.0f, 1e-6f));
  CHECK_THAT(bibo::world::DistanceToBox(vec3(5.0f, 8.0f, 3.0f), box),
             Catch::Matchers::WithinAbs(5.0f, 1e-6f));
}

TEST_CASE("Every shadow cascade covers its slice of the view frustum",
          "[render][shadow]") {
  const glm::mat4 view =
      glm::lookAt(vec3(10.0f, 5.0f, -3.0f), vec3(30.0f, 0.0f, -40.0f),
                  vec3(0.0f, 1.0f, 0.0f));
  const float field_of_view = 66.0f, aspect = 16.0f / 9.0f, near = 0.15f;
  const std::vector<float> splits = {14.0f, 42.0f, 120.0f, 320.0f};
  const vec3 to_sun = glm::normalize(vec3(0.57f, 0.45f, 0.68f));
  std::vector<bibo::render::ShadowCascade> cascades =
      bibo::render::ComputeCascades(view, field_of_view, aspect, near, splits,
                                    to_sun, 2048, 200.0f);
  REQUIRE(cascades.size() == splits.size());

  glm::mat4 camera_to_world = glm::inverse(view);
  float tangent = std::tan(glm::radians(field_of_view) * 0.5f);
  float start = near;
  for (std::size_t i = 0; i < cascades.size(); ++i) {
    CAPTURE(i);
    const bibo::render::ShadowCascade& cascade = cascades[i];
    CHECK(cascade.split_near == start);
    CHECK(cascade.split_far == splits[i]);
    CHECK(cascade.texel_size > 0.0f);
    if (i > 0) {
      // further away, coarser
      CHECK(cascade.texel_size > cascades[i - 1].texel_size);
    }
    // The corners of the slice are inside of the shadow map (in normalized
    // device coordinates)
    for (float depth : {start, splits[i]}) {
      for (int corner = 0; corner < 4; ++corner) {
        vec3 camera((corner & 1 ? 1.0f : -1.0f) * depth * tangent * aspect,
                    (corner & 2 ? 1.0f : -1.0f) * depth * tangent, -depth);
        vec3 world = vec3(camera_to_world * glm::vec4(camera, 1.0f));
        glm::vec4 projected = cascade.light_space * glm::vec4(world, 1.0f);
        CAPTURE(world, projected);
        CHECK(std::abs(projected.x) <= 1.001f);
        CHECK(std::abs(projected.y) <= 1.001f);
        CHECK(std::abs(projected.z) <= 1.0f);
        CHECK(cascade.Contains(world, 0.0f));
      }
    }
    // Something between the sun and the slice casts a shadow into it, something
    // far behind the camera does not
    vec3 middle =
        vec3(camera_to_world *
             glm::vec4(0.0f, 0.0f, -0.5f * (start + splits[i]), 1.0f));
    CHECK(cascade.Contains(middle + to_sun * 100.0f, 1.0f));
    CHECK_FALSE(
        cascade.Contains(middle - to_sun * (cascade.depth + 10.0f), 1.0f));
    start = splits[i];
  }

  SECTION("The maps move in whole texels (no shimmering)") {
    const glm::mat4 moved = glm::translate(view, vec3(0.37f, 0.0f, 0.11f));
    std::vector<bibo::render::ShadowCascade> other =
        bibo::render::ComputeCascades(moved, field_of_view, aspect, near,
                                      splits, to_sun, 2048, 200.0f);
    for (std::size_t i = 0; i < cascades.size(); ++i) {
      // The world origin lands on the corner of a texel in both
      for (const bibo::render::ShadowCascade* cascade :
           {&cascades[i], &other[i]}) {
        glm::vec4 origin =
            cascade->light_space * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        vec2 texels = vec2(origin) * 1024.0f;
        CHECK_THAT(texels.x,
                   Catch::Matchers::WithinAbs(std::round(texels.x), 0.01f));
        CHECK_THAT(texels.y,
                   Catch::Matchers::WithinAbs(std::round(texels.y), 0.01f));
      }
    }
  }
}

TEST_CASE("Shader files include other shader files", "[shader]") {
  namespace fs = std::filesystem;
  fs::path folder = fs::temp_directory_path() / "bibo_shader_include_test";
  fs::create_directories(folder / "include");
  std::ofstream(folder / "main.frag")
      << "#version 430 core\n#include \"include/a.glsl\"\n  #include "
         "\"include/a.glsl\"\nvoid main() {}\n";
  std::ofstream(folder / "include" / "a.glsl")
      << "#ifndef A\n#define A\n#include \"b.glsl\"\nfloat a() { return b(); "
         "}\n#endif\n";
  std::ofstream(folder / "include" / "b.glsl") << "float b() { return 1.0; }\n";
  std::ofstream(folder / "broken.frag")
      << "#version 430 core\n#include \"missing.glsl\"\n";

  std::string error;
  std::string code =
      bibo::Shader::Preprocess((folder / "main.frag").string(), error);
  CHECK(error.empty());
  CHECK(code.starts_with("#version 430 core\n"));  // still the first line
  CHECK_THAT(code,
             Catch::Matchers::ContainsSubstring("float b() { return 1.0; }"));
  CHECK_THAT(code,
             Catch::Matchers::ContainsSubstring("float a() { return b(); }"));
  CHECK_THAT(code, !Catch::Matchers::ContainsSubstring("#include"));

  code = bibo::Shader::Preprocess((folder / "broken.frag").string(), error);
  CHECK(code.empty());
  CHECK_THAT(error, Catch::Matchers::ContainsSubstring("missing.glsl"));
  fs::remove_all(folder);
}
