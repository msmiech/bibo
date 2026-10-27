#ifndef BIBO_WORLD_TERRAINRENDERER_HPP_
#define BIBO_WORLD_TERRAINRENDERER_HPP_
#include <GL/glew.h>

#include <memory>
#include <unordered_map>

#include "../Render/ShadowCascades.hpp"
#include "../Resources/Shader.hpp"
#include "../Scene/ViewFrustum.hpp"
#include "Chunk.hpp"
#include "WorldStreamer.hpp"

namespace bibo {
namespace world {
/**
 * Draws the meshes of the chunks (in the level of detail the streamer gave
 * them), only those in the view frustum. When a chunk gets another level of
 * detail, the old mesh fades into the new one (dithered) instead of popping.
 */
class TerrainRenderer {
 public:
  // Seconds that a chunk takes to fade from its old level of detail to the new
  // one
  static constexpr float kFadeTime = 0.75f;

  TerrainRenderer();
  ~TerrainRenderer();
  TerrainRenderer(const TerrainRenderer&) = delete;
  TerrainRenderer& operator=(const TerrainRenderer&) = delete;

  /**
   * Uploads the new mesh of a chunk (or removes it). The mesh it replaces fades
   * out from now on.
   * @param time Of the world (seconds)
   */
  void Apply(const WorldStreamer::MeshUpdate& update, float time);
  /**
   * @param frustum Chunks outside of it are not drawn (nullptr: all)
   * @param time Of the world (seconds), for the fades between the levels of
   * detail
   * @return Number of drawn chunks
   */
  int Draw(const scene::ViewFrustum* frustum, float time);
  /**
   * Into a shadow map (depth only)
   */
  void DrawShadow(const render::ShadowCascade& cascade);
  int GetChunkCount() const { return static_cast<int>(chunks_.size()); }
  /**
   * @return Chunks that fade from their old level of detail to the new one
   * right now
   */
  int GetFadingCount() const { return static_cast<int>(fading_.size()); }
  /**
   * @return Triangles drawn by the last draw
   */
  std::size_t GetDrawnTriangles() const { return drawn_triangles_; }

 private:
  struct GpuChunk {
    GLuint vertex_array = 0, vertex_buffer = 0, index_buffer = 0;
    GLsizei index_count = 0;
    Box bounds;
    int lod = 0;
    float upload_time = 0.0f;
  };
  // The mesh that a chunk had before, while it fades out
  struct FadingChunk {
    GpuChunk old;
    float start = 0.0f;
  };
  void Release(GpuChunk& chunk);
  bool DrawChunk(const GpuChunk& chunk, const scene::ViewFrustum* frustum);

  std::unique_ptr<Shader> shader_;
  std::unique_ptr<Shader> dither_shader_;
  std::unique_ptr<Shader> depth_shader_;
  std::unordered_map<ChunkKey, GpuChunk, ChunkKeyHash> chunks_;
  std::unordered_map<ChunkKey, FadingChunk, ChunkKeyHash> fading_;
  std::size_t drawn_triangles_ = 0;
};
}  // namespace world
}  // namespace bibo

#endif  // BIBO_WORLD_TERRAINRENDERER_HPP_
