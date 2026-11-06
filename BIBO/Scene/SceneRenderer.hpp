#ifndef BIBO_SCENE_SCENERENDERER_HPP_
#define BIBO_SCENE_SCENERENDERER_HPP_
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

#include "../Render/Renderer.hpp"
#include "../Resources/Shader.hpp"
#include "Actors.hpp"
#include "BearRenderer.hpp"
#include "Camera.hpp"
#include "CreatureRenderer.hpp"
#include "FurryRenderer.hpp"
#include "Geometry.hpp"
#include "HedgehogRenderer.hpp"
#include "QuestDirector.hpp"
#include "ViewFrustum.hpp"

namespace bibo {
namespace world {
class World;
}
namespace scene {
/**
 * Draws the island for render::Renderer: the world, the objects of the track,
 * the bear and the animals (their renderers belong to it) and the exclamation
 * marks above the partners of the quests.
 */
class SceneRenderer : public render::RenderableScene {
 public:
  /**
   * @param geometries The objects of the track, drawn with the main shader
   * @param arrows The signs of the track (they glow)
   * All must stay alive as long as the renderer.
   */
  SceneRenderer(Shader* main_shader, Camera& camera, world::World& world,
                const Actors& actors,
                const std::vector<std::unique_ptr<Geometry>>& geometries,
                const std::vector<std::unique_ptr<Geometry>>& arrows);
  ~SceneRenderer() override;

  /**
   * Puts the animals where they are drawn in this frame, and the exclamation
   * marks above the partners of the quests
   */
  void Place(const QuestDirector& quests);

  // render::RenderableScene
  render::CameraView GetCameraView() override;
  void FillFrameUniforms(render::FrameUniforms& uniforms) override;
  void PrepareFrame() override;
  void DrawShadowCasters(const render::ShadowCascade& cascade) override;
  void DrawMain(const render::RenderSettings& settings) override;

  /**
   * Only the objects of the track and the bear with the given shader (e.g.
   * their silhouettes), the camera's view and projection
   */
  void RenderFromCamera(Shader* shader);
  /**
   * Only the geometries that are (partially) inside of the view frustum of the
   * camera are drawn
   */
  void SetViewFrustumCullingEnabled(bool view_frustum_culling_enabled) {
    view_frustum_culling_enabled_ = view_frustum_culling_enabled;
  }
  bool IsViewFrustumCullingEnabled() const {
    return view_frustum_culling_enabled_;
  }
  /**
   * @return Number of geometries drawn by the last RenderFromCamera or DrawMain
   */
  int GetDrawnGeometryCount() const { return drawn_geometry_count_; }
  /**
   * @return Number of geometries: the objects of the track and the bear
   */
  int GetGeometryCount() const {
    return static_cast<int>(geometries_.size()) + 1;
  }

 private:
  void PlaceSalmon(glm::vec3 center, const glm::quat& rotation, int bites,
                   float wiggle);
  /**
   * The skinned vertices of the bears to their renderers (when they changed)
   */
  void UploadBear();
  void UploadMishka();
  bool IsBearVisible(const ViewFrustum& frustum) const;
  /**
   * The objects of the track (not the world)
   * @param frustum Geometries outside of it are not drawn (nullptr: draw all)
   * @param with_bear The bear is drawn with the given shader too (its surface),
   * otherwise left out
   * @return Number of drawn geometries
   */
  int DrawGeometries(Shader* shader, const glm::mat4& view,
                     const glm::mat4& projection, const ViewFrustum* frustum,
                     bool with_bear);
  /**
   * @return false if the geometry is outside of the frustum and was not drawn
   */
  bool DrawGeometry(Shader* shader, Geometry* geometry,
                    GLint normal_mapping_enabled_location,
                    const ViewFrustum* frustum);

  Shader* main_shader_;
  Camera& camera_;
  world::World& world_;
  const Actors& actors_;
  const std::vector<std::unique_ptr<Geometry>>& geometries_;
  const std::vector<std::unique_ptr<Geometry>>& arrows_;

  std::unique_ptr<Shader> depth_shader_;
  std::unique_ptr<Shader> light_shader_;
  std::unique_ptr<BearRenderer> bear_renderer_;
  unsigned int bear_uploaded_ = 0;
  // The salmon, the droplets and the exclamation marks
  std::unique_ptr<CreatureRenderer> creatures_;
  std::unique_ptr<HedgehogRenderer> hedgehog_renderer_;
  std::unique_ptr<FurryRenderer> beaver_renderer_;
  std::unique_ptr<BearRenderer> mishka_renderer_;
  unsigned int mishka_uploaded_ = 0;
  std::unique_ptr<FurryRenderer> chonk_renderer_;
  bool chonk_posed_ = false;
  std::unique_ptr<FurryRenderer> fans_renderer_;
  // the fan is drawn under the island (its pose need not change)
  std::vector<bool> fans_hidden_;

  bool view_frustum_culling_enabled_ = true;
  bool normal_mapping_enabled_ = true;
  int drawn_geometry_count_ = 0;
};
}  // namespace scene
}  // namespace bibo

#endif  // BIBO_SCENE_SCENERENDERER_HPP_
