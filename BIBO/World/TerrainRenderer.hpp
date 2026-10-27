#pragma once
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
  static constexpr float FADE_TIME = 0.75f;

  TerrainRenderer();
  ~TerrainRenderer();
  TerrainRenderer(const TerrainRenderer&) = delete;
  TerrainRenderer& operator=(const TerrainRenderer&) = delete;

  /**
   * Uploads the new mesh of a chunk (or removes it). The mesh it replaces fades
   * out from now on.
   * @param time Of the world (seconds)
   */
  void apply(const WorldStreamer::MeshUpdate& update, float time);
  /**
   * @param frustum Chunks outside of it are not drawn (nullptr: all)
   * @param time Of the world (seconds), for the fades between the levels of
   * detail
   * @return Number of drawn chunks
   */
  int draw(const scene::ViewFrustum* frustum, float time);
  /**
   * Into a shadow map (depth only)
   */
  void drawShadow(const render::ShadowCascade& cascade);
  int getChunkCount() const { return static_cast<int>(chunks.size()); }
  /**
   * @return Chunks that fade from their old level of detail to the new one
   * right now
   */
  int getFadingCount() const { return static_cast<int>(fading.size()); }
  /**
   * @return Triangles drawn by the last draw
   */
  std::size_t getDrawnTriangles() const { return drawnTriangles; }

 private:
  struct GpuChunk {
    GLuint vertexArray = 0, vertexBuffer = 0, indexBuffer = 0;
    GLsizei indexCount = 0;
    Box bounds;
    int lod = 0;
    float uploadTime = 0.0f;
  };
  // The mesh that a chunk had before, while it fades out
  struct FadingChunk {
    GpuChunk old;
    float start = 0.0f;
  };
  void release(GpuChunk& chunk);
  bool drawChunk(const GpuChunk& chunk, const scene::ViewFrustum* frustum);

  std::unique_ptr<Shader> shader;
  std::unique_ptr<Shader> ditherShader;
  std::unique_ptr<Shader> depthShader;
  std::unordered_map<ChunkKey, GpuChunk, ChunkKeyHash> chunks;
  std::unordered_map<ChunkKey, FadingChunk, ChunkKeyHash> fading;
  std::size_t drawnTriangles = 0;
};
}  // namespace world
}  // namespace bibo
