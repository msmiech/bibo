#include <GL/glew.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <cmath>
#include <vector>

#include "Render/FrameUniforms.hpp"
#include "TestSupport.hpp"
#include "World/MaterialTextures.hpp"
#include "World/SkyRenderer.hpp"
#include "World/TerrainRenderer.hpp"
#include "World/VegetationMeshes.hpp"
#include "World/World.hpp"

using namespace bibo::test;
using namespace bibo::world;

namespace {
struct Statistics {
  glm::vec3 average = glm::vec3(0.0f);
  float variation = 0.0f;  // standard deviation of the brightness
};

Statistics statistics(const std::vector<float>& rgba) {
  Statistics result;
  std::size_t count = rgba.size() / 4;
  double sum = 0.0, squares = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    glm::vec3 color(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
    result.average += color / static_cast<float>(count);
    float brightness = (color.r + color.g + color.b) / 3.0f;
    sum += brightness;
    squares += brightness * brightness;
  }
  double mean = sum / static_cast<double>(count);
  result.variation = static_cast<float>(std::sqrt(
      std::max(0.0, squares / static_cast<double>(count) - mean * mean)));
  return result;
}

/**
 * The texels of a layer of a texture array (an sRGB texture is converted to
 * linear colors)
 */
std::vector<float> layer(GLuint texture, int size, int index, bool srgb) {
  std::vector<float> all(static_cast<std::size_t>(size) * size * 4 *
                         MATERIAL_LAYER_COUNT);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
  glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, GL_FLOAT, all.data());
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  auto first =
      all.begin() + static_cast<std::ptrdiff_t>(index) * size * size * 4;
  std::vector<float> texels(
      first, first + static_cast<std::ptrdiff_t>(size) * size * 4);
  for (std::size_t i = 0; srgb && i < texels.size(); ++i) {
    if (i % 4 != 3) {
      texels[i] = std::pow(texels[i], 2.2f);
    }
  }
  return texels;
}
}  // namespace

TEST_CASE(
    "The materials of the world are generated with color, roughness and a "
    "normal map",
    "[gl][world]") {
  GlContext gl;
  AssetWorkingDirectory assets;
  const int SIZE = 64;
  MaterialTextures materials(SIZE);
  REQUIRE(gl.errors().empty());

  const int index = GENERATE(range(0, static_cast<int>(MATERIAL_LAYER_COUNT)));
  CAPTURE(index);
  Statistics albedo =
      statistics(layer(materials.getAlbedo(), SIZE, index, true));
  std::vector<float> normals = layer(materials.getNormal(), SIZE, index, false);
  Statistics normal = statistics(normals);
  CAPTURE(albedo.average, albedo.variation, normal.average);

  // A pattern, not a plain color
  CHECK(albedo.variation > 0.005f);
  // Physically plausible colors: no material is darker than charcoal or
  // brighter than snow
  CHECK(albedo.average.r + albedo.average.g + albedo.average.b > 0.02f);
  CHECK(glm::all(glm::lessThan(albedo.average, glm::vec3(0.95f))));
  // Tangent space normals point mostly away from the surface: x and y about 0
  // (0.5 stored)
  CHECK(std::abs(normal.average.r - 0.5f) < 0.1f);
  CHECK(std::abs(normal.average.g - 0.5f) < 0.1f);
  for (std::size_t i = 0; i < normals.size(); i += 4) {
    glm::vec2 xy(normals[i] * 2.0f - 1.0f, normals[i + 1] * 2.0f - 1.0f);
    REQUIRE(glm::length(xy) <= 1.01f);
    REQUIRE(normals[i + 3] > 0.3f);  // ambient occlusion
  }
  if (index == MATERIAL_SNOW) {
    CHECK(albedo.average.b > 0.6f);  // bright, a bit blue
  }
  if (index == MATERIAL_ROCK) {
    CHECK(albedo.average.g < 0.3f);  // dark granite
  }
  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE(
    "The sky is blue above and the sun light comes from above the horizon",
    "[gl][world]") {
  GlContext gl;
  AssetWorkingDirectory assets;
  Atmosphere atmosphere = Atmosphere::polarDay();
  CHECK(atmosphere.toSun.y > 0.3f);
  CHECK(atmosphere.toSun.y < 0.7f);  // low, polar sun

  bibo::render::FrameUniformBuffer frame;
  bibo::render::FrameUniforms uniforms;
  uniforms.sunDirectionShadows = glm::vec4(atmosphere.toSun, 1.0f);
  uniforms.sunColorNormalMapping = glm::vec4(atmosphere.sunColor, 1.0f);
  uniforms.groundAmbient.w = atmosphere.auroraIntensity;
  uniforms.wind = glm::vec4(atmosphere.windDirection, 1.0f, 0.0f);
  frame.upload(uniforms);
  frame.bind();
  SkyRenderer sky(true);
  sky.update();
  REQUIRE(gl.errors().empty());

  // Faces of the cube map: +x, -x, +y, -y, +z, -z
  auto face = [&](int index) {
    GLint size = 0;
    glBindTexture(GL_TEXTURE_CUBE_MAP, sky.getEnvironment());
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X + index, 0,
                             GL_TEXTURE_WIDTH, &size);
    std::vector<float> pixels(static_cast<std::size_t>(size) * size * 4);
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + index, 0, GL_RGBA, GL_FLOAT,
                  pixels.data());
    return statistics(pixels).average;
  };
  glm::vec3 up = face(2), down = face(3);
  CAPTURE(up, down);
  CHECK(up.b > up.r);
  CHECK(up.b > 0.1f);  // a bright day
  CHECK(down.r + down.g + down.b < up.r + up.g + up.b);
  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}

TEST_CASE("Every vegetation model has levels of detail with fewer triangles",
          "[world][vegetation]") {
  // No graphics needed: the meshes are generated on the CPU
  const int model = GENERATE(range(0, MODEL_COUNT));
  CAPTURE(model);
  std::vector<VegetationMesh> lods = buildModel(static_cast<Model>(model));
  REQUIRE_FALSE(lods.empty());
  REQUIRE(lods.size() <= 3);
  for (std::size_t lod = 0; lod < lods.size(); ++lod) {
    const VegetationMesh& mesh = lods[lod];
    CAPTURE(lod, mesh.vertices.size(), mesh.indices.size());
    REQUIRE(mesh.indices.size() % 3 == 0);
    REQUIRE_FALSE(mesh.indices.empty());
    for (std::uint32_t index : mesh.indices) {
      REQUIRE(index < mesh.vertices.size());
    }
    if (lod > 0) {
      CHECK(mesh.indices.size() < lods[lod - 1].indices.size());
    }
    // About as large as the scattering expects
    CHECK(mesh.radius > 0.3f * modelRadius(static_cast<Model>(model)));
    CHECK(mesh.radius < 1.5f * (modelHeight(static_cast<Model>(model)) +
                                modelRadius(static_cast<Model>(model))));
    for (const VegetationVertex& vertex : mesh.vertices) {
      REQUIRE(std::abs(glm::length(vertex.normal) - 1.0f) < 0.01f);
      REQUIRE(vertex.parameters[0] <= VEGETATION_GRASS);
    }
  }
}

TEST_CASE(
    "A chunk fades from its old level of detail to the new one instead of "
    "popping",
    "[gl][world]") {
  GlContext gl;
  // the renderer loads its shaders relative to the working directory
  AssetWorkingDirectory assets;
  TerrainRenderer terrain;
  const ChunkKey key = {1, 0, 2};
  auto mesh = [&](int lod) {
    auto chunk = std::make_shared<ChunkMesh>();
    chunk->key = key;
    chunk->lod = lod;
    for (glm::vec3 position :
         {glm::vec3(32.0f, 1.0f, 64.0f), glm::vec3(64.0f, 1.0f, 64.0f),
          glm::vec3(32.0f, 1.0f, 96.0f)}) {
      TerrainVertex vertex = {};
      vertex.position = position;
      vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
      chunk->vertices.push_back(vertex);
    }
    chunk->indices = {0, 2, 1};
    chunk->surfaceIndexCount = 3;
    chunk->bounds = chunkBox(key);
    return std::shared_ptr<const ChunkMesh>(chunk);
  };

  // Loading: a new chunk, and one that changes in the same moment, just appear
  terrain.apply({key, mesh(2)}, 1.0f);
  terrain.apply({key, mesh(1)}, 1.0f);
  CHECK(terrain.getChunkCount() == 1);
  CHECK(terrain.getFadingCount() == 0);

  terrain.apply({key, mesh(0)}, 2.0f);
  CHECK(terrain.getChunkCount() == 1);
  CHECK(terrain.getFadingCount() == 1);
  // Both meshes are drawn while it fades, the old one is released afterwards
  terrain.draw(nullptr, 2.0f + 0.5f * TerrainRenderer::FADE_TIME);
  CHECK(terrain.getDrawnTriangles() == 2);
  CHECK(terrain.getFadingCount() == 1);
  terrain.draw(nullptr, 2.0f + TerrainRenderer::FADE_TIME);
  CHECK(terrain.getDrawnTriangles() == 1);
  CHECK(terrain.getFadingCount() == 0);

  // A chunk without a surface is removed at once
  terrain.apply({key, mesh(1)}, 5.0f);
  terrain.apply({key, nullptr}, 5.5f);
  CHECK(terrain.getChunkCount() == 0);
  CHECK(terrain.getFadingCount() == 0);
  CHECK_THAT(gl.errors(), Catch::Matchers::IsEmpty());
}
