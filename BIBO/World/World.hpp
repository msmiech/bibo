#ifndef BIBO_WORLD_WORLD_HPP_
#define BIBO_WORLD_WORLD_HPP_
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "../Physics/Physics.hpp"
#include "../Render/FrameUniforms.hpp"
#include "../Render/ShadowCascades.hpp"
#include "../Scene/ViewFrustum.hpp"
#include "Chunk.hpp"
#include "FoliageInteraction.hpp"
#include "WorldGenerator.hpp"
#include "WorldStreamer.hpp"

// NOLINTNEXTLINE(readability-identifier-naming): Assimp type
struct aiScene;

namespace bibo {
namespace world {
class MaterialTextures;
class SkyRenderer;
class TerrainRenderer;
class VegetationRenderer;
class WaterRenderer;
class SnowRenderer;

/**
 * Sun, sky and weather of the island: a bright polar day with a low sun, polar
 * lights and a bit of snow
 */
struct Atmosphere {
  glm::vec3 to_sun;     // direction towards the sun
  glm::vec3 sun_color;  // radiance
  float aurora_intensity = 1.0f;
  // how much of the upper sides of the trees and stones is covered with snow
  float snow_on_objects = 0.3f;
  float fog_density = 0.0011f;  // at the sea level
  float fog_falloff = 0.012f;   // the fog gets thinner with the height
  float fog_maximum = 0.93f;
  float fog_sun_scattering = 0.25f;
  glm::vec2 wind_direction = glm::normalize(glm::vec2(0.8f, 0.6f));
  float wind_strength = 1.0f;

  /**
   * @param elevation Of the sun above the horizon (degrees)
   * @param azimuth Of the sun, from +z towards +x (degrees)
   */
  static Atmosphere PolarDay(float elevation = 27.0f, float azimuth = 40.0f);
};

/**
 * The world around the race track: the generated island (WorldGenerator),
 * streamed in chunks and columns with levels of detail (WorldStreamer), in the
 * physics world around the bear, and drawn with the terrain, vegetation, sky,
 * water and snow renderers. Needs an OpenGL context.
 */
class World {
 public:
  struct Statistics {
    WorldStreamer::Statistics streaming;
    int meshes = 0;  // chunks with a mesh on the graphics card
    int drawn_chunks = 0;
    std::size_t drawn_triangles = 0;
    int drawn_instances = 0;
    std::size_t instances = 0;
  };

  /**
   * @param track The track file: the terrain (node "Boden"), the water level
   * (node "Water")
   * @param start Start and finish of the lap, startDirection: driving direction
   * there
   * @param checkpoints In driving direction
   * @param keepClear Objects of the track that the world must not cover (signs,
   * the finish line, the rocks)
   * @param physicsWorld Gets the terrain and the trees around the bear
   */
  World(const aiScene& track, glm::vec2 start, glm::vec2 start_direction,
        const std::vector<glm::vec2>& checkpoints,
        const std::vector<KeepClear>& keep_clear,
        physics::PhysicsWorld* physics_world);
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  /**
   * Streams the world for the camera and the bear (physics), lets the time pass
   * (wind, polar lights, snow)
   * @param bodies The bear and the animals that push the plants aside (in the
   * same order every frame)
   */
  void Update(float time_delta, glm::vec3 camera, glm::vec3 bear,
              const std::vector<FoliageInteraction::Body>& bodies = {});
  /**
   * Waits until the world around the camera and the bear is complete (loading)
   */
  void FinishLoading(glm::vec3 camera, glm::vec3 bear, float timeout_seconds);
  /**
   * The ground under the bear is in the physics world right now (e.g. after it
   * was moved back to the start)
   */
  void EnsureCollision(glm::vec3 bear, glm::vec3 camera);

  /**
   * Sun, sky, fog, wind, time and the world map (the camera, the shadows and
   * the settings are set by the renderer)
   */
  void FillFrameUniforms(render::FrameUniforms& uniforms) const;
  /**
   * Renders what the frame needs before the scene (the sky cube map), after the
   * frame uniforms were uploaded
   */
  void PrepareFrame();
  /**
   * Binds the textures of the world that the shaders of the scene use
   * (materials, sky, world map)
   */
  void BindTextures() const;
  void DrawShadows(const render::ShadowCascade& cascade, glm::vec3 camera);
  /**
   * Terrain and vegetation
   * @param culling Leave out what is outside of the view frustum
   */
  void DrawOpaque(const scene::ViewFrustum& frustum, glm::vec3 camera,
                  bool culling);
  void DrawSky();
  /**
   * Water and snow (after everything else)
   */
  void DrawTransparent(bool transparency);

  /**
   * @param level 0 low, 1 medium, 2 high: how far the levels of detail and the
   * vegetation reach
   */
  void SetDetail(int level);
  const WorldGenerator& GetGenerator() const { return *generator_; }
  const Atmosphere& GetAtmosphere() const { return atmosphere_; }
  const FoliageInteraction& GetFoliageInteraction() const { return foliage_; }
  float GetTime() const { return time_; }
  Statistics GetStatistics() const;

 private:
  void CreateWorldMap();

  std::unique_ptr<WorldGenerator> generator_;
  Atmosphere atmosphere_;
  FoliageInteraction foliage_;
  bool low_detail_;
  int detail_level_ = 2;
  float time_ = 0.0f;
  std::unique_ptr<MaterialTextures> materials_;
  std::unique_ptr<SkyRenderer> sky_;
  std::unique_ptr<TerrainRenderer> terrain_;
  std::unique_ptr<VegetationRenderer> vegetation_;
  std::unique_ptr<WaterRenderer> water_;
  std::unique_ptr<SnowRenderer> snow_;
  GLuint world_map_ = 0;
  glm::vec2 world_map_origin_ = glm::vec2(0.0f);
  float world_map_size_ = 1.0f;
  int drawn_chunks_ = 0;
  int drawn_instances_ = 0;
  // Declared last: destroyed first (it removes its bodies from the physics
  // world and stops its threads)
  std::unique_ptr<WorldStreamer> streamer_;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_WORLD_HPP_
