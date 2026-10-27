#pragma once
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
 * @param lodDistances Where the levels 1, 2, ... begin (lodCount - 1 of them)
 * @param band Width of the cross-fade around every distance, relative to it
 */
LodBlend blendLod(float distance, const float* lodDistances, int lodCount,
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

  void apply(const WorldStreamer::VegetationUpdate& update);
  /**
   * @return Number of drawn instances
   */
  int draw(const scene::ViewFrustum& frustum, glm::vec3 camera);
  void drawShadow(const render::ShadowCascade& cascade, glm::vec3 camera);
  void setDetail(float newDetail) { detail = newDetail; }
  std::size_t getInstanceCount() const;

 private:
  struct GpuMesh {
    GLuint vertexArray = 0, vertexBuffer = 0, indexBuffer = 0;
    GLsizei indexCount = 0;
    float radius = 0.0f;
  };
  struct ModelData {
    std::vector<GpuMesh> lods;
    float lodDistances[2] = {1e9f, 1e9f};
    float maxDistance = 100.0f;
    float shadowDistance = 0.0f;  // 0: no shadow
    float windAmplitude = 0.0f;
    // how far the bear and the animals push it aside (relative to its height)
    float interaction = 0.0f;
    float height = 0.0f;
    bool twoSided = false;
    bool scaleDistances = false;  // the distances grow with the scale (stones)
  };
  struct ColumnData {
    std::vector<Instance> layers[2];  // large, small
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;
    void updateBounds();
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
   * @param shadowCascade nullptr: for the camera
   */
  void collect(const scene::ViewFrustum* frustum,
               const render::ShadowCascade* shadowCascade, glm::vec3 camera);
  void drawBatches(Shader& shader, bool shadowPass, bool dithered);

  std::unique_ptr<Shader> shader;
  std::unique_ptr<Shader> ditherShader;
  std::unique_ptr<Shader> depthShader;
  std::vector<ModelData> models;
  std::unordered_map<ColumnKey, ColumnData, ColumnKeyHash> columns;
  GLuint instanceBuffer = 0;
  std::vector<std::vector<Instance>> sorted;  // per model and level of detail
  // the instances that fade, per model and level of detail
  std::vector<std::vector<Instance>> sortedDithered;
  std::vector<Instance> staging;
  std::vector<Batch> batches;
  float detail;
};
}  // namespace world
}  // namespace bibo
