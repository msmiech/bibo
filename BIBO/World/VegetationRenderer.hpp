#ifndef BIBO_WORLD_VEGETATIONRENDERER_HPP_
#define BIBO_WORLD_VEGETATIONRENDERER_HPP_
#include <GL/glew.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "../Render/ShadowCascades.hpp"
#include "../Resources/Shader.hpp"
#include "../Scene/ViewFrustum.hpp"
#include "Vegetation.hpp"
#include "VegetationMeshes.hpp"
#include "WorldStreamer.hpp"

namespace bibo {
namespace world {
/**
 * Which level of detail an instance has at a distance, and how far it has faded
 * into the next one
 */
struct LodBlend {
  int lod = 0;
  // 0: only lod, up to 1: lod + 1 replaces it (both are drawn, dithered)
  float next = 0.0f;
};

/**
 * @param lod_distances Where the levels 1, 2, ... begin (lod_count - 1 of them)
 * @param band Width of the cross-fade around every distance, relative to it
 */
LodBlend BlendLod(float distance, const float* lod_distances, int lod_count,
                  float band);

/**
 * Draws the instances of the vegetation. Every frame (and every shadow map) it
 * collects the visible instances of the columns in the view, picks the level of
 * detail of every instance by its distance from the camera and draws each model
 * and level with one instanced draw call. Around the distance where the level
 * changes both levels are drawn, each on a part of the pixels (dithered), so
 * that the models change gradually instead of popping. Far away the models
 * shrink to nothing (the stones dissolve) before they are left out.
 */
class VegetationRenderer {
 public:
  /**
   * @param detail 1: normal, smaller: models are left out and get coarser
   * closer to the camera
   */
  explicit VegetationRenderer(float detail);
  ~VegetationRenderer();
  VegetationRenderer(const VegetationRenderer&) = delete;
  VegetationRenderer& operator=(const VegetationRenderer&) = delete;

  void Apply(const WorldStreamer::VegetationUpdate& update);
  /**
   * @return Number of drawn instances
   */
  int Draw(const scene::ViewFrustum& frustum, glm::vec3 camera);
  void DrawShadow(const render::ShadowCascade& cascade, glm::vec3 camera);
  void SetDetail(float new_detail) { detail_ = new_detail; }
  std::size_t GetInstanceCount() const;

 private:
  struct GpuMesh {
    GLuint vertex_array = 0, vertex_buffer = 0, index_buffer = 0;
    GLsizei index_count = 0;
    float radius = 0.0f;
  };
  struct ModelData {
    std::vector<GpuMesh> lods;
    float lod_distances[2] = {1e9f, 1e9f};
    float max_distance = 100.0f;
    float shadow_distance = 0.0f;  // 0: no shadow
    float wind_amplitude = 0.0f;
    // how far the bear and the animals push it aside (relative to its height)
    float interaction = 0.0f;
    float height = 0.0f;
    bool two_sided = false;
    bool scale_distances = false;  // the distances grow with the scale (stones)
  };
  struct ColumnData {
    std::vector<Instance> layers[2];  // large, small
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;
    void UpdateBounds();
  };
  struct Batch {
    int model, lod;
    std::size_t first, count;
    // the instances fade (instance.parameters.w, Shader/include/dither.glsl)
    bool dithered;
  };

  /**
   * Sorts the instances into batches (model and level of detail) and uploads
   * them
   * @param shadow_cascade nullptr: for the camera
   */
  void Collect(const scene::ViewFrustum* frustum,
               const render::ShadowCascade* shadow_cascade, glm::vec3 camera);
  void DrawBatches(Shader& shader, bool shadow_pass, bool dithered);

  std::unique_ptr<Shader> shader_;
  std::unique_ptr<Shader> dither_shader_;
  std::unique_ptr<Shader> depth_shader_;
  std::vector<ModelData> models_;
  std::unordered_map<ColumnKey, ColumnData, ColumnKeyHash> columns_;
  GLuint instance_buffer_ = 0;
  std::vector<std::vector<Instance>> sorted_;  // per model and level of detail
  // the instances that fade, per model and level of detail
  std::vector<std::vector<Instance>> sorted_dithered_;
  std::vector<Instance> staging_;
  std::vector<Batch> batches_;
  float detail_;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_VEGETATIONRENDERER_HPP_
