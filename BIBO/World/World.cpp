#include "World.hpp"

#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "../Render/RenderUtil.hpp"
#include "../Util/Console.hpp"
#include "MaterialTextures.hpp"
#include "Parallel.hpp"
#include "SkyRenderer.hpp"
#include "TerrainRenderer.hpp"
#include "VegetationRenderer.hpp"
#include "WaterRenderer.hpp"

namespace bibo::world {

namespace {
const char* const kTerrainNode = "Boden";
const char* const kWaterNode = "Water";
const float kDefaultSeaLevel = -0.48f;

WorldStreamer::Settings StreamerSettings(int level) {
  WorldStreamer::Settings settings;
  const float lod_distances[3][3] = {
      {32.0f, 72.0f, 150.0f}, {48.0f, 100.0f, 210.0f}, {64.0f, 136.0f, 280.0f}};
  const float small_radius[3] = {40.0f, 56.0f, 72.0f};
  level = std::clamp(level, 0, 2);
  std::copy(lod_distances[level], lod_distances[level] + 3,
            settings.lod_distances);
  settings.small_radius = small_radius[level];
  return settings;
}

float VegetationDetail(int level) {
  const float detail[3] = {0.5f, 0.75f, 1.0f};
  return detail[std::clamp(level, 0, 2)];
}
}  // namespace

Atmosphere Atmosphere::PolarDay(float elevation, float azimuth) {
  Atmosphere atmosphere;
  float e = glm::radians(elevation), a = glm::radians(azimuth);
  atmosphere.to_sun = glm::vec3(std::cos(e) * std::sin(a), std::sin(e),
                                std::cos(e) * std::cos(a));
  atmosphere.sun_color = glm::vec3(1.0f, 0.93f, 0.82f) * 4.2f;
  return atmosphere;
}

World::World(const aiScene& track, glm::vec2 start, glm::vec2 start_direction,
             const std::vector<glm::vec2>& checkpoints,
             const std::vector<KeepClear>& keep_clear,
             physics::PhysicsWorld* physics_world)
    : atmosphere_(Atmosphere::PolarDay()),
      low_detail_(render::IsSoftwareRenderer()) {
  std::vector<glm::vec3> vertices;
  std::vector<unsigned int> indices;
  if (!TrackTerrain::LoadNodeMesh(track, kTerrainNode, vertices, indices)) {
    util::FatalError(std::string("The track has no terrain (node ") +
                     kTerrainNode + ")");
  }
  WorldGenerator::Description description;
  description.track = std::make_unique<TrackTerrain>(vertices, indices);
  description.start = start;
  description.start_direction = start_direction;
  description.checkpoints = checkpoints;
  description.keep_clear = keep_clear;
  // The water of the track is the sea level
  glm::vec3 water_center;
  float water_radius;
  description.sea_level =
      TrackTerrain::NodeSphere(track, kWaterNode, water_center, water_radius)
          ? water_center.y
          : kDefaultSeaLevel;
  generator_ = std::make_unique<WorldGenerator>(std::move(description));

  if (low_detail_) {
    detail_level_ = 0;
    std::cout << "Software rendering: the world is drawn with less detail"
              << std::endl;
  }
  streamer_ = std::make_unique<WorldStreamer>(*generator_, physics_world,
                                              StreamerSettings(detail_level_));
  materials_ = std::make_unique<MaterialTextures>(low_detail_ ? 256 : 512);
  sky_ = std::make_unique<SkyRenderer>(low_detail_);
  terrain_ = std::make_unique<TerrainRenderer>();
  vegetation_ =
      std::make_unique<VegetationRenderer>(VegetationDetail(detail_level_));
  water_ = std::make_unique<WaterRenderer>();
  snow_ = std::make_unique<SnowRenderer>(low_detail_ ? 2500 : 16000);
  CreateWorldMap();
}

World::~World() {
  // The streamer first (its threads use the generator), then the textures
  streamer_.reset();
  glDeleteTextures(1, &world_map_);
}

void World::CreateWorldMap() {
  // Top view of the island: the height of the ground, the top of the cave roofs
  // (no snow below) and the coast (foam)
  const int size = low_detail_ ? 256 : 512;
  Box bounds = generator_->GetBounds();
  world_map_origin_ = glm::vec2(bounds.minimum.x, bounds.minimum.z);
  world_map_size_ = std::max(bounds.maximum.x - bounds.minimum.x,
                             bounds.maximum.z - bounds.minimum.z);
  std::vector<glm::vec4> texels(static_cast<std::size_t>(size) * size);
  ParallelFor(size, [&](int row) {
    for (int column = 0; column < size; ++column) {
      glm::vec2 position =
          world_map_origin_ +
          (glm::vec2(static_cast<float>(column), static_cast<float>(row)) +
           0.5f) *
              (world_map_size_ / static_cast<float>(size));
      float inland, cliff;
      generator_->Coast(position, inland, cliff);
      float coastal = 1.0f - glm::smoothstep(8.0f, 40.0f, std::abs(inland));
      texels[static_cast<std::size_t>(row) * size + column] =
          glm::vec4(generator_->Height(position),
                    generator_->CaveRoofTop(position), coastal, 0.0f);
    }
  });
  glGenTextures(1, &world_map_);
  glBindTexture(GL_TEXTURE_2D, world_map_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size, size, 0, GL_RGBA, GL_FLOAT,
               texels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void World::Update(float time_delta, glm::vec3 camera, glm::vec3 bear,
                   const std::vector<FoliageInteraction::Body>& bodies) {
  time_ += time_delta;
  foliage_.Update(time_delta, bodies, camera);
  streamer_->Update(camera, bear);
  for (const WorldStreamer::MeshUpdate& update : streamer_->TakeMeshUpdates()) {
    terrain_->Apply(update, time_);
  }
  for (const WorldStreamer::VegetationUpdate& update :
       streamer_->TakeVegetationUpdates()) {
    vegetation_->Apply(update);
  }
}

void World::FinishLoading(glm::vec3 camera, glm::vec3 bear,
                          float timeout_seconds) {
  streamer_->EnsureCollision(bear, 12.0f);
  if (!streamer_->Finish(camera, bear, timeout_seconds)) {
    std::cout << "Part of the world is still loading, it is streamed while the "
                 "game runs"
              << std::endl;
  }
  Update(0.0f, camera, bear);
  Statistics statistics = GetStatistics();
  std::cout << "World: " << statistics.streaming.chunks << " chunks ("
            << statistics.streaming.chunks_at_lod[0] << " in full detail), "
            << statistics.instances << " plants and stones, "
            << statistics.streaming.collision_bodies << " collision bodies"
            << std::endl;
}

void World::EnsureCollision(glm::vec3 bear, glm::vec3 camera) {
  streamer_->EnsureCollision(bear, 12.0f);
  Update(0.0f, camera, bear);
}

void World::FillFrameUniforms(render::FrameUniforms& uniforms) const {
  uniforms.camera_position_time.w = time_;
  uniforms.sun_direction_shadows = glm::vec4(glm::normalize(atmosphere_.to_sun),
                                             uniforms.sun_direction_shadows.w);
  uniforms.sun_color_normal_mapping =
      glm::vec4(atmosphere_.sun_color, uniforms.sun_color_normal_mapping.w);
  uniforms.sky_ambient_snow =
      glm::vec4(0.45f, 0.55f, 0.7f, atmosphere_.snow_on_objects);
  uniforms.ground_ambient =
      glm::vec4(0.05f, 0.06f, 0.05f, atmosphere_.aurora_intensity);
  uniforms.fog_density =
      glm::vec4(atmosphere_.fog_density, atmosphere_.fog_falloff,
                generator_->GetSeaLevel(), atmosphere_.fog_maximum);
  // The wind blows in gusts
  float gusts =
      0.75f + 0.25f * std::sin(time_ * 0.13f) + 0.1f * std::sin(time_ * 0.71f);
  uniforms.wind = glm::vec4(atmosphere_.wind_direction,
                            atmosphere_.wind_strength * gusts, 0.0f);
  foliage_.FillFrameUniforms(uniforms);
  uniforms.world_map =
      glm::vec4(world_map_origin_, world_map_size_, generator_->GetSeaLevel());
  uniforms.shadow_settings.y = atmosphere_.fog_sun_scattering;
}

void World::PrepareFrame() { sky_->Update(); }

void World::BindTextures() const {
  materials_->Bind();
  sky_->Bind();
  glActiveTexture(GL_TEXTURE0 + render::texture_unit::kWorldMap);
  glBindTexture(GL_TEXTURE_2D, world_map_);
  glActiveTexture(GL_TEXTURE0);
}

void World::DrawShadows(const render::ShadowCascade& cascade,
                        glm::vec3 camera) {
  terrain_->DrawShadow(cascade);
  vegetation_->DrawShadow(cascade, camera);
}

void World::DrawOpaque(const scene::ViewFrustum& frustum, glm::vec3 camera,
                       bool culling) {
  BindTextures();
  GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
  glEnable(GL_CULL_FACE);
  drawn_chunks_ = terrain_->Draw(culling ? &frustum : nullptr, time_);
  drawn_instances_ = vegetation_->Draw(frustum, camera);
  cull_face ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

void World::DrawSky() { sky_->Draw(); }

void World::DrawTransparent(bool transparency) {
  BindTextures();
  water_->Draw(transparency);
  snow_->Draw();
}

void World::SetDetail(int level) {
  if (low_detail_) {
    level = 0;
  }
  detail_level_ = std::clamp(level, 0, 2);
  streamer_->SetSettings(StreamerSettings(detail_level_));
  vegetation_->SetDetail(VegetationDetail(detail_level_));
}

World::Statistics World::GetStatistics() const {
  Statistics statistics;
  statistics.streaming = streamer_->GetStatistics();
  statistics.meshes = terrain_->GetChunkCount();
  statistics.drawn_chunks = drawn_chunks_;
  statistics.drawn_triangles = terrain_->GetDrawnTriangles();
  statistics.drawn_instances = drawn_instances_;
  statistics.instances = vegetation_->GetInstanceCount();
  return statistics;
}

}  // namespace bibo::world
