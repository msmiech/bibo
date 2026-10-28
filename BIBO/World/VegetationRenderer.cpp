#include "VegetationRenderer.hpp"

#include <algorithm>
#include <cstddef>
#include <glm/gtc/type_ptr.hpp>

namespace bibo::world {

namespace {
const int kMaxLods = 3;
// Width of the cross-fade between two levels of detail (relative to the
// distance where they change)
const float kLodBand = 0.2f;
// The stones dissolve over this last part of their largest distance
const float kDissolveBand = 0.15f;

// Distances of the levels of detail, the largest distance, the distance up to
// which it casts shadows (0: never), how much it sways in the wind, how far the
// bear and the animals push it aside (relative to its height), whether it is
// seen from both sides and whether the distances grow with its scale
const struct {
  float lod1, lod2, maximum, shadow, wind, interaction;
  bool two_sided, scaled;
} kModelSettings[kModelCount] = {
    {50.0f, 150.0f, 1100.0f, 450.0f, 0.35f, 0.0f, true, false},  // kSpruceTall
    {50.0f, 150.0f, 1100.0f, 450.0f, 0.35f, 0.0f, true, false},  // kSpruceWide
    {40.0f, 120.0f, 900.0f, 300.0f, 0.3f, 0.0f, true, false},    // kSpruceYoung
    {50.0f, 150.0f, 1100.0f, 450.0f, 0.4f, 0.0f, true, false},   // PINE
    {30.0f, 1e9f, 220.0f, 60.0f, 0.08f, 0.35f, false, false},    // BUSH
    {30.0f, 1e9f, 220.0f, 60.0f, 0.06f, 0.4f, true, false},      // JUNIPER
    {1e9f, 1e9f, 42.0f, 0.0f, 0.12f, 0.8f, true, false},         // FERN
    {1e9f, 1e9f, 40.0f, 0.0f, 0.12f, 0.9f, true, false},         // GRASS
    {1e9f, 1e9f, 40.0f, 0.0f, 0.12f, 0.9f, true, false},         // kGrassDry
    {1e9f, 1e9f, 28.0f, 0.0f, 0.02f, 0.5f, false, false},        // kMushroomRed
    {1e9f, 1e9f, 28.0f, 0.0f, 0.02f, 0.5f, false, false},     // kMushroomBrown
    {1e9f, 1e9f, 60.0f, 0.0f, 0.02f, 0.5f, false, false},     // kMushroomGlow
    {12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true},   // kStoneRound
    {12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true},   // kStoneFlat
    {12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true},   // kStoneSharp
    {12.0f, 40.0f, 80.0f, 100.0f, 0.0f, 0.0f, false, true},   // kStoneStanding
    {35.0f, 1e9f, 200.0f, 120.0f, 0.0f, 0.0f, false, false},  // LOG
    {35.0f, 1e9f, 160.0f, 80.0f, 0.0f, 0.0f, false, false}};  // STUMP

Instance WithFade(const Instance& instance, float fade) {
  Instance faded = instance;
  faded.parameters.w = fade;
  return faded;
}
}  // namespace

LodBlend BlendLod(float distance, const float* lod_distances, int lod_count,
                  float band) {
  for (int i = 0; i < lod_count - 1; ++i) {
    float start = lod_distances[i] * (1.0f - 0.5f * band),
          end = lod_distances[i] * (1.0f + 0.5f * band);
    if (distance < start) {
      return {i, 0.0f};
    }
    if (distance < end) {
      return {i, (distance - start) / (end - start)};
    }
  }
  return {std::max(lod_count - 1, 0), 0.0f};
}

void VegetationRenderer::ColumnData::UpdateBounds() {
  glm::vec3 low(1e9f), high(-1e9f);
  float reach = 0.0f;
  for (const std::vector<Instance>& layer : layers) {
    for (const Instance& instance : layer) {
      low = glm::min(low, glm::vec3(instance.position_scale));
      high = glm::max(high, glm::vec3(instance.position_scale));
      reach = std::max(reach, (ModelHeight(instance.GetModel()) +
                               ModelRadius(instance.GetModel())) *
                                  instance.position_scale.w);
    }
  }
  if (low.x > high.x) {
    center = glm::vec3(0.0f);
    radius = 0.0f;
    return;
  }
  center = 0.5f * (low + high);
  radius = 0.5f * glm::length(high - low) + reach;
}

VegetationRenderer::VegetationRenderer(float detail)
    : shader_(std::make_unique<Shader>("Shader/vegetation.vert",
                                       "Shader/vegetation.frag")),
      dither_shader_(std::make_unique<Shader>(
          "Shader/vegetation.vert", "Shader/vegetation.frag", "LOD_DITHER")),
      depth_shader_(std::make_unique<Shader>("Shader/vegetation_depth.vert",
                                             "Shader/depth_shader.frag")),
      detail_(detail) {
  glGenBuffers(1, &instance_buffer_);
  models_.resize(kModelCount);
  sorted_.resize(kModelCount * kMaxLods);
  sorted_dithered_.resize(kModelCount * kMaxLods);
  for (int m = 0; m < kModelCount; ++m) {
    ModelData& model = models_[m];
    model.lod_distances[0] = kModelSettings[m].lod1;
    model.lod_distances[1] = kModelSettings[m].lod2;
    model.max_distance = kModelSettings[m].maximum;
    model.shadow_distance = kModelSettings[m].shadow;
    model.wind_amplitude = kModelSettings[m].wind;
    model.interaction = kModelSettings[m].interaction;
    model.height = ModelHeight(static_cast<Model>(m));
    model.two_sided = kModelSettings[m].two_sided;
    model.scale_distances = kModelSettings[m].scaled;
    for (const VegetationMesh& mesh : BuildModel(static_cast<Model>(m))) {
      GpuMesh gpu;
      gpu.index_count = static_cast<GLsizei>(mesh.indices.size());
      gpu.radius = mesh.radius;
      glGenVertexArrays(1, &gpu.vertex_array);
      glBindVertexArray(gpu.vertex_array);
      glGenBuffers(1, &gpu.vertex_buffer);
      glBindBuffer(GL_ARRAY_BUFFER, gpu.vertex_buffer);
      glBufferData(GL_ARRAY_BUFFER,
                   mesh.vertices.size() * sizeof(VegetationVertex),
                   mesh.vertices.data(), GL_STATIC_DRAW);
      glGenBuffers(1, &gpu.index_buffer);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.index_buffer);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   mesh.indices.size() * sizeof(std::uint32_t),
                   mesh.indices.data(), GL_STATIC_DRAW);
      // Binding 0: the vertices of the model, binding 1: the instances (set for
      // every batch)
      glBindVertexBuffer(0, gpu.vertex_buffer, 0, sizeof(VegetationVertex));
      glVertexAttribFormat(0, 3, GL_FLOAT, GL_FALSE,
                           offsetof(VegetationVertex, position));
      glVertexAttribFormat(1, 3, GL_FLOAT, GL_FALSE,
                           offsetof(VegetationVertex, normal));
      glVertexAttribFormat(2, 2, GL_FLOAT, GL_FALSE,
                           offsetof(VegetationVertex, uv));
      glVertexAttribFormat(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                           offsetof(VegetationVertex, color));
      glVertexAttribFormat(4, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                           offsetof(VegetationVertex, parameters));
      for (GLuint attribute = 0; attribute < 5; ++attribute) {
        glVertexAttribBinding(attribute, 0);
        glEnableVertexAttribArray(attribute);
      }
      glBindVertexBuffer(1, instance_buffer_, 0, sizeof(Instance));
      glVertexBindingDivisor(1, 1);
      glVertexAttribFormat(5, 4, GL_FLOAT, GL_FALSE,
                           offsetof(Instance, position_scale));
      glVertexAttribFormat(6, 4, GL_FLOAT, GL_FALSE,
                           offsetof(Instance, rotation));
      glVertexAttribFormat(7, 4, GL_FLOAT, GL_FALSE,
                           offsetof(Instance, parameters));
      for (GLuint attribute = 5; attribute < 8; ++attribute) {
        glVertexAttribBinding(attribute, 1);
        glEnableVertexAttribArray(attribute);
      }
      glBindVertexArray(0);
      model.lods.push_back(gpu);
    }
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

VegetationRenderer::~VegetationRenderer() {
  for (ModelData& model : models_) {
    for (GpuMesh& mesh : model.lods) {
      glDeleteVertexArrays(1, &mesh.vertex_array);
      GLuint buffers[] = {mesh.vertex_buffer, mesh.index_buffer};
      glDeleteBuffers(2, buffers);
    }
  }
  glDeleteBuffers(1, &instance_buffer_);
}

void VegetationRenderer::Apply(const WorldStreamer::VegetationUpdate& update) {
  ColumnData& column = columns_[update.key];
  std::vector<Instance>& layer =
      column.layers[update.layer == Layer::kLarge ? 0 : 1];
  if (update.instances) {
    layer = *update.instances;
  } else {
    layer.clear();
  }
  column.UpdateBounds();
}

std::size_t VegetationRenderer::GetInstanceCount() const {
  std::size_t count = 0;
  for (const auto& entry : columns_) {
    count += entry.second.layers[0].size() + entry.second.layers[1].size();
  }
  return count;
}

void VegetationRenderer::Collect(const scene::ViewFrustum* frustum,
                                 const render::ShadowCascade* shadow_cascade,
                                 glm::vec3 camera) {
  for (std::vector<Instance>& list : sorted_) {
    list.clear();
  }
  for (std::vector<Instance>& list : sorted_dithered_) {
    list.clear();
  }
  const float reach = 1100.0f * detail_;
  for (const auto& entry : columns_) {
    const ColumnData& column = entry.second;
    if (column.radius <= 0.0f ||
        glm::distance(column.center, camera) - column.radius > reach) {
      continue;
    }
    if (shadow_cascade
            ? !shadow_cascade->Contains(column.center, column.radius)
            : frustum->SphereInFrustum(column.center, column.radius) ==
                  scene::ViewFrustum::kOutside) {
      continue;
    }
    // Nothing small casts a shadow
    for (int layer = 0; layer < (shadow_cascade ? 1 : 2); ++layer) {
      for (const Instance& instance : column.layers[layer]) {
        int m = static_cast<int>(instance.GetModel());
        const ModelData& model = models_[m];
        glm::vec3 position(instance.position_scale);
        float scale = instance.position_scale.w;
        float distance = glm::distance(position, camera);
        float factor =
            detail_ * (model.scale_distances ? std::max(scale, 0.3f) : 1.0f);
        float maximum = model.max_distance * factor;
        if (distance > maximum) {
          continue;
        }
        float radius = model.lods[0].radius * scale;
        glm::vec3 center =
            position +
            glm::vec3(0.0f, 0.5f * ModelHeight(instance.GetModel()) * scale,
                      0.0f);
        if (shadow_cascade) {
          if (model.shadow_distance <= 0.0f ||
              distance > model.shadow_distance * factor ||
              (model.scale_distances && scale < 0.7f) ||
              !shadow_cascade->Contains(center, radius)) {
            continue;
          }
        } else if (radius > 2.0f && frustum->SphereInFrustum(center, radius) ==
                                        scene::ViewFrustum::kOutside) {
          continue;
        }
        int lod_count = std::min(static_cast<int>(model.lods.size()), kMaxLods);
        const float lod_distances[2] = {model.lod_distances[0] * factor,
                                        model.lod_distances[1] * factor};
        LodBlend blend = BlendLod(distance, lod_distances, lod_count, kLodBand);
        int nearest = blend.next < 0.5f ? blend.lod : blend.lod + 1;
        if (shadow_cascade) {
          // One level is enough for the shadow, the shadow of a coarser model
          // looks the same
          if (nearest == 0 && distance > 25.0f) {
            nearest = std::min(1, lod_count - 1);
          }
          sorted_[m * kMaxLods + nearest].push_back(instance);
          continue;
        }
        // The stones dissolve before they are left out (they do not shrink,
        // their distance depends on their size)
        float dissolve = model.scale_distances
                             ? (distance - maximum * (1.0f - kDissolveBand)) /
                                   (maximum * kDissolveBand)
                             : 0.0f;
        if (dissolve > 0.0f) {
          sorted_dithered_[m * kMaxLods + nearest].push_back(
              WithFade(instance, 1.0f - dissolve));
        } else if (blend.next > 0.0f) {
          // The finer level gives way to the coarser one on the same pixels
          sorted_dithered_[m * kMaxLods + blend.lod].push_back(
              WithFade(instance, blend.next - 1.0f));
          sorted_dithered_[m * kMaxLods + blend.lod + 1].push_back(
              WithFade(instance, blend.next));
        } else {
          sorted_[m * kMaxLods + blend.lod].push_back(instance);
        }
      }
    }
  }
  staging_.clear();
  batches_.clear();
  for (int dithered = 0; dithered < 2; ++dithered) {
    const std::vector<std::vector<Instance>>& lists =
        dithered ? sorted_dithered_ : sorted_;
    for (int m = 0; m < kModelCount; ++m) {
      for (int lod = 0; lod < kMaxLods; ++lod) {
        const std::vector<Instance>& list = lists[m * kMaxLods + lod];
        if (!list.empty()) {
          batches_.push_back(
              {m, lod, staging_.size(), list.size(), dithered == 1});
          staging_.insert(staging_.end(), list.begin(), list.end());
        }
      }
    }
  }
  if (!staging_.empty()) {
    glBindBuffer(GL_ARRAY_BUFFER, instance_buffer_);
    // New memory, the graphics card may still read the previous instances
    glBufferData(GL_ARRAY_BUFFER, staging_.size() * sizeof(Instance), nullptr,
                 GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, staging_.size() * sizeof(Instance),
                    staging_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
}

void VegetationRenderer::DrawBatches(Shader& program, bool shadow_pass,
                                     bool dithered) {
  GLint wind_location = program.GetUniformLocation("windAmplitude");
  GLint fade_location = program.GetUniformLocation("fadeDistances");
  GLint interaction_location = program.GetUniformLocation("interaction");
  for (const Batch& batch : batches_) {
    if (batch.dithered != dithered) {
      continue;
    }
    const ModelData& model = models_[batch.model];
    const GpuMesh& mesh = model.lods[batch.lod];
    if (!shadow_pass) {
      model.two_sided ? glDisable(GL_CULL_FACE) : glEnable(GL_CULL_FACE);
    }
    glUniform1f(wind_location, model.wind_amplitude);
    glUniform2f(interaction_location, model.interaction, model.height);
    float maximum = model.max_distance * detail_;
    if (model.scale_distances) {
      glUniform2f(fade_location, 1e9f,
                  2e9f);  // the distance depends on the size of the stone
    } else {
      glUniform2f(fade_location, maximum * 0.75f, maximum);
    }
    glBindVertexArray(mesh.vertex_array);
    glBindVertexBuffer(1, instance_buffer_,
                       static_cast<GLintptr>(batch.first * sizeof(Instance)),
                       sizeof(Instance));
    glDrawElementsInstanced(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT,
                            nullptr, static_cast<GLsizei>(batch.count));
  }
  glBindVertexArray(0);
}

int VegetationRenderer::Draw(const scene::ViewFrustum& frustum,
                             glm::vec3 camera) {
  Collect(&frustum, nullptr, camera);
  GLboolean cull_face = glIsEnabled(GL_CULL_FACE);
  shader_->Enable();
  DrawBatches(*shader_, false, false);
  // The fading instances with the shader that leaves out pixels (the others
  // keep the early depth test)
  dither_shader_->Enable();
  DrawBatches(*dither_shader_, false, true);
  cull_face ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
  return static_cast<int>(staging_.size());
}

void VegetationRenderer::DrawShadow(const render::ShadowCascade& cascade,
                                    glm::vec3 camera) {
  Collect(nullptr, &cascade, camera);
  depth_shader_->Enable();
  glUniformMatrix4fv(depth_shader_->GetUniformLocation("lightSpaceMatrix"), 1,
                     GL_FALSE, glm::value_ptr(cascade.light_space));
  DrawBatches(*depth_shader_, true, false);
}

}  // namespace bibo::world
