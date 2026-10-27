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

using namespace bibo;
using namespace bibo::world;

namespace {
const char* const TERRAIN_NODE = "Boden";
const char* const WATER_NODE = "Water";
const float DEFAULT_SEA_LEVEL = -0.48f;

WorldStreamer::Settings streamerSettings(int level) {
  WorldStreamer::Settings settings;
  const float LOD_DISTANCES[3][3] = {
      {32.0f, 72.0f, 150.0f}, {48.0f, 100.0f, 210.0f}, {64.0f, 136.0f, 280.0f}};
  const float SMALL_RADIUS[3] = {40.0f, 56.0f, 72.0f};
  level = std::clamp(level, 0, 2);
  std::copy(LOD_DISTANCES[level], LOD_DISTANCES[level] + 3,
            settings.lodDistances);
  settings.smallRadius = SMALL_RADIUS[level];
  return settings;
}

float vegetationDetail(int level) {
  const float DETAIL[3] = {0.5f, 0.75f, 1.0f};
  return DETAIL[std::clamp(level, 0, 2)];
}
}  // namespace

Atmosphere Atmosphere::polarDay(float elevation, float azimuth) {
  Atmosphere atmosphere;
  float e = glm::radians(elevation), a = glm::radians(azimuth);
  atmosphere.toSun = glm::vec3(std::cos(e) * std::sin(a), std::sin(e),
                               std::cos(e) * std::cos(a));
  atmosphere.sunColor = glm::vec3(1.0f, 0.93f, 0.82f) * 4.2f;
  return atmosphere;
}

World::World(const aiScene& track, glm::vec2 start, glm::vec2 startDirection,
             const std::vector<glm::vec2>& checkpoints,
             const std::vector<KeepClear>& keepClear,
             physics::PhysicsWorld* physicsWorld)
    : atmosphere(Atmosphere::polarDay()),
      lowDetail(render::isSoftwareRenderer()) {
  std::vector<glm::vec3> vertices;
  std::vector<unsigned int> indices;
  if (!TrackTerrain::loadNodeMesh(track, TERRAIN_NODE, vertices, indices)) {
    util::fatalError(std::string("The track has no terrain (node ") +
                     TERRAIN_NODE + ")");
  }
  WorldGenerator::Description description;
  description.track = std::make_unique<TrackTerrain>(vertices, indices);
  description.start = start;
  description.startDirection = startDirection;
  description.checkpoints = checkpoints;
  description.keepClear = keepClear;
  // The water of the track is the sea level
  glm::vec3 waterCenter;
  float waterRadius;
  description.seaLevel =
      TrackTerrain::nodeSphere(track, WATER_NODE, waterCenter, waterRadius)
          ? waterCenter.y
          : DEFAULT_SEA_LEVEL;
  generator = std::make_unique<WorldGenerator>(std::move(description));

  if (lowDetail) {
    detailLevel = 0;
    std::cout << "Software rendering: the world is drawn with less detail"
              << std::endl;
  }
  streamer = std::make_unique<WorldStreamer>(*generator, physicsWorld,
                                             streamerSettings(detailLevel));
  materials = std::make_unique<MaterialTextures>(lowDetail ? 256 : 512);
  sky = std::make_unique<SkyRenderer>(lowDetail);
  terrain = std::make_unique<TerrainRenderer>();
  vegetation =
      std::make_unique<VegetationRenderer>(vegetationDetail(detailLevel));
  water = std::make_unique<WaterRenderer>();
  snow = std::make_unique<SnowRenderer>(lowDetail ? 2500 : 16000);
  createWorldMap();
}

World::~World() {
  // The streamer first (its threads use the generator), then the textures
  streamer.reset();
  glDeleteTextures(1, &worldMap);
}

void World::createWorldMap() {
  // Top view of the island: the height of the ground, the top of the cave roofs
  // (no snow below) and the coast (foam)
  const int SIZE = lowDetail ? 256 : 512;
  Box bounds = generator->getBounds();
  worldMapOrigin = glm::vec2(bounds.minimum.x, bounds.minimum.z);
  worldMapSize = std::max(bounds.maximum.x - bounds.minimum.x,
                          bounds.maximum.z - bounds.minimum.z);
  std::vector<glm::vec4> texels(static_cast<std::size_t>(SIZE) * SIZE);
  parallelFor(SIZE, [&](int row) {
    for (int column = 0; column < SIZE; ++column) {
      glm::vec2 position =
          worldMapOrigin +
          (glm::vec2(static_cast<float>(column), static_cast<float>(row)) +
           0.5f) *
              (worldMapSize / static_cast<float>(SIZE));
      float inland, cliff;
      generator->coast(position, inland, cliff);
      float coastal = 1.0f - glm::smoothstep(8.0f, 40.0f, std::abs(inland));
      texels[static_cast<std::size_t>(row) * SIZE + column] =
          glm::vec4(generator->height(position),
                    generator->caveRoofTop(position), coastal, 0.0f);
    }
  });
  glGenTextures(1, &worldMap);
  glBindTexture(GL_TEXTURE_2D, worldMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SIZE, SIZE, 0, GL_RGBA, GL_FLOAT,
               texels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void World::update(float timeDelta, glm::vec3 camera, glm::vec3 bear,
                   const std::vector<FoliageInteraction::Body>& bodies) {
  time += timeDelta;
  foliage.update(timeDelta, bodies, camera);
  streamer->update(camera, bear);
  for (const WorldStreamer::MeshUpdate& update : streamer->takeMeshUpdates()) {
    terrain->apply(update, time);
  }
  for (const WorldStreamer::VegetationUpdate& update :
       streamer->takeVegetationUpdates()) {
    vegetation->apply(update);
  }
}

void World::finishLoading(glm::vec3 camera, glm::vec3 bear,
                          float timeoutSeconds) {
  streamer->ensureCollision(bear, 12.0f);
  if (!streamer->finish(camera, bear, timeoutSeconds)) {
    std::cout << "Part of the world is still loading, it is streamed while the "
                 "game runs"
              << std::endl;
  }
  update(0.0f, camera, bear);
  Statistics statistics = getStatistics();
  std::cout << "World: " << statistics.streaming.chunks << " chunks ("
            << statistics.streaming.chunksAtLod[0] << " in full detail), "
            << statistics.instances << " plants and stones, "
            << statistics.streaming.collisionBodies << " collision bodies"
            << std::endl;
}

void World::ensureCollision(glm::vec3 bear, glm::vec3 camera) {
  streamer->ensureCollision(bear, 12.0f);
  update(0.0f, camera, bear);
}

void World::fillFrameUniforms(render::FrameUniforms& uniforms) const {
  uniforms.cameraPositionTime.w = time;
  uniforms.sunDirectionShadows = glm::vec4(glm::normalize(atmosphere.toSun),
                                           uniforms.sunDirectionShadows.w);
  uniforms.sunColorNormalMapping =
      glm::vec4(atmosphere.sunColor, uniforms.sunColorNormalMapping.w);
  uniforms.skyAmbientSnow =
      glm::vec4(0.45f, 0.55f, 0.7f, atmosphere.snowOnObjects);
  uniforms.groundAmbient =
      glm::vec4(0.05f, 0.06f, 0.05f, atmosphere.auroraIntensity);
  uniforms.fogDensity =
      glm::vec4(atmosphere.fogDensity, atmosphere.fogFalloff,
                generator->getSeaLevel(), atmosphere.fogMaximum);
  // The wind blows in gusts
  float gusts =
      0.75f + 0.25f * std::sin(time * 0.13f) + 0.1f * std::sin(time * 0.71f);
  uniforms.wind = glm::vec4(atmosphere.windDirection,
                            atmosphere.windStrength * gusts, 0.0f);
  foliage.fillFrameUniforms(uniforms);
  uniforms.worldMap =
      glm::vec4(worldMapOrigin, worldMapSize, generator->getSeaLevel());
  uniforms.shadowSettings.y = atmosphere.fogSunScattering;
}

void World::prepareFrame() { sky->update(); }

void World::bindTextures() const {
  materials->bind();
  sky->bind();
  glActiveTexture(GL_TEXTURE0 + render::TextureUnit::WORLD_MAP);
  glBindTexture(GL_TEXTURE_2D, worldMap);
  glActiveTexture(GL_TEXTURE0);
}

void World::drawShadows(const render::ShadowCascade& cascade,
                        glm::vec3 camera) {
  terrain->drawShadow(cascade);
  vegetation->drawShadow(cascade, camera);
}

void World::drawOpaque(const scene::ViewFrustum& frustum, glm::vec3 camera,
                       bool culling) {
  bindTextures();
  GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
  glEnable(GL_CULL_FACE);
  drawnChunks = terrain->draw(culling ? &frustum : nullptr, time);
  drawnInstances = vegetation->draw(frustum, camera);
  cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
}

void World::drawSky() { sky->draw(); }

void World::drawTransparent(bool transparency) {
  bindTextures();
  water->draw(transparency);
  snow->draw();
}

void World::setDetail(int level) {
  if (lowDetail) {
    level = 0;
  }
  detailLevel = std::clamp(level, 0, 2);
  streamer->setSettings(streamerSettings(detailLevel));
  vegetation->setDetail(vegetationDetail(detailLevel));
}

World::Statistics World::getStatistics() const {
  Statistics statistics;
  statistics.streaming = streamer->getStatistics();
  statistics.meshes = terrain->getChunkCount();
  statistics.drawnChunks = drawnChunks;
  statistics.drawnTriangles = terrain->getDrawnTriangles();
  statistics.drawnInstances = drawnInstances;
  statistics.instances = vegetation->getInstanceCount();
  return statistics;
}
