#include "SceneRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "../World/World.hpp"
#include "BeaverModel.hpp"
#include "RaccoonModel.hpp"

namespace bibo::scene {

using glm::mat4;
using glm::quat;
using glm::vec3;
using glm::vec4;
using resources::Model;
using std::make_unique;
using std::unique_ptr;
using std::vector;

namespace {
const std::size_t kBeaverFurCount = 16000;
const std::size_t kChonkFurCount = 16000, kFanFurCount = 9000;
// Hidden raccoons are drawn far under the island (out of every view)
const vec3 kHiddenPlace(0.0f, -1000.0f, 0.0f);

// The skinned vertices of a bear to its renderer (when they changed)
template <typename Skinned>
void UploadSkin(const Skinned& skinned, BearRenderer& renderer,
                unsigned int& uploaded) {
  if (uploaded != skinned.GetVersion()) {
    renderer.Upload(skinned.GetPositions(), skinned.GetNormals(),
                    skinned.GetFurOffsets(), skinned.GetCombDirections());
    uploaded = skinned.GetVersion();
  }
}
}  // namespace

SceneRenderer::SceneRenderer(Shader* main_shader, Camera& camera,
                             world::World& world, const Actors& actors,
                             const vector<unique_ptr<Geometry>>& geometries,
                             const vector<unique_ptr<Geometry>>& arrows)
    : main_shader_(main_shader),
      camera_(camera),
      world_(world),
      actors_(actors),
      geometries_(geometries),
      arrows_(arrows) {
  depth_shader_ = make_unique<Shader>("Shader/depth_shader.vert",
                                      "Shader/depth_shader.frag");
  light_shader_ = make_unique<Shader>("Shader/light_source_shader.vert",
                                      "Shader/light_source_shader.frag");
  bear_renderer_ = make_unique<BearRenderer>(actors.GetBear().GetModel(),
                                             Bear::kStrandCount);
  creatures_ = make_unique<CreatureRenderer>();
  hedgehog_renderer_ =
      make_unique<HedgehogRenderer>(actors.GetHedgehogs().size());

  FurryRenderer::Strands strands{.fur_count = kBeaverFurCount, .fur_seed = 91};
  FurryRenderer::Bounds bounds{.center = vec3(0.0f, 0.5f, 0.0f),
                               .radius = 0.8f};
  beaver_renderer_ =
      make_unique<FurryRenderer>(BeaverModel::Build(), 1, strands, bounds);

  mishka_renderer_ = make_unique<BearRenderer>(actors.GetMishkaModel(),
                                               RestingBear::kStrandCount);
  mishka_renderer_->SetMaxFurLength(fur::kMaxLength * RestingBear::kFurLength);

  strands = {.fur_count = kChonkFurCount, .fur_seed = 133};
  bounds = {.center = vec3(0.0f, 0.35f, 0.0f), .radius = 0.8f};
  chonk_renderer_ = make_unique<FurryRenderer>(
      RaccoonModel::Build(Actors::kChonkGirth), 1, strands, bounds);
  strands.fur_count = kFanFurCount;
  strands.fur_seed = 177;
  fans_renderer_ = make_unique<FurryRenderer>(
      RaccoonModel::Build(), RaccoonFans::kCount, strands, bounds);
  main_shader_->Enable();  // the renderers compiled their own shaders
}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::PlaceSalmon(vec3 center, const quat& rotation, int bites,
                                float wiggle) {
  const float scale = SalmonPools::kScale;
  if (bites < 1) {
    creatures_->Add(CreatureModel::kSalmonHead, center, rotation, scale);
  }
  if (bites < 2) {
    creatures_->Add(CreatureModel::kSalmonBody, center, rotation, scale);
  }
  if (bites < 3) {
    vec3 joint = center + rotation * vec3(0.0f, 0.0f, kSalmonTailJoint * scale);
    creatures_->Add(CreatureModel::kSalmonTail, joint,
                    rotation * angleAxis(wiggle, vec3(0.0f, 1.0f, 0.0f)),
                    scale);
  }
}

void SceneRenderer::Place(const QuestDirector& quests) {
  creatures_->Clear();
  const vector<Hedgehog>& hedgehogs = actors_.GetHedgehogs();
  for (std::size_t i = 0; i < hedgehogs.size(); ++i) {
    Hedgehog::Pose pose = hedgehogs[i].GetPose();
    mat4 scale = glm::scale(mat4(1.0f), vec3(Hedgehog::kScale));
    mat4 body = glm::translate(mat4(1.0f), pose.body_position) *
                glm::mat4_cast(pose.body_rotation) * scale;
    std::array<mat4, 4> feet;
    for (std::size_t foot = 0; foot < feet.size(); ++foot) {
      feet[foot] = glm::translate(mat4(1.0f), pose.feet[foot]) *
                   glm::mat4_cast(pose.foot_rotation) * scale;
    }
    hedgehog_renderer_->SetPose(i, body, feet, pose.bristle);
  }
  const SalmonPools& salmon = actors_.GetSalmon();
  for (const SalmonPools::Jump& jump : salmon.GetJumps()) {
    PlaceSalmon(jump.Position(), jump.Rotation(), 0,
                0.5f * std::sin(18.0f * jump.time + jump.wiggle));
  }
  BearCharacter::HeldFish held;
  if (actors_.GetBear().GetCharacter().GetHeldFish(held)) {
    PlaceSalmon(held.center, held.rotation, held.bites, held.wiggle);
  }
  for (const SalmonPools::Droplet& droplet : salmon.GetDroplets()) {
    creatures_->Add(CreatureModel::kDroplet, droplet.position,
                    quat(1.0f, 0.0f, 0.0f, 0.0f), droplet.size);
  }
  beaver_renderer_->SetPose(0, actors_.GetBeaver().GetPose());

  // The partners of the quests that have something for the bear: a golden
  // exclamation mark, a blue one if the quest was completed and can be done
  // again
  for (const QuestDirector::MarkerSpot& spot : quests.GetMarkers()) {
    CreatureModel marker = spot.marker == Quest::Marker::kRepeatable
                               ? CreatureModel::kQuestMarkerRepeatable
                               : CreatureModel::kQuestMarker;
    creatures_->Add(marker, spot.partner->GetMarkerPosition(),
                    spot.partner->GetMarkerRotation(),
                    spot.partner->GetMarkerScale());
  }

  // The vertices of the raccoons are moved on the CPU: Chonk only while the
  // camera is close enough to see it move, a hidden fan once when it goes back
  // into the woods
  const Raccoon& chonk = actors_.GetChonk();
  if (!chonk_posed_ || distance(camera_.GetPosition(), chonk.GetPosition()) <
                           Actors::kAnimateDistance) {
    chonk_renderer_->SetPose(0, chonk.GetPose());
    chonk_posed_ = true;
  }
  const vector<Raccoon>& raccoons = actors_.GetFans().GetRaccoons();
  fans_hidden_.resize(raccoons.size(), false);
  for (std::size_t i = 0; i < raccoons.size(); ++i) {
    if (raccoons[i].IsVisible()) {
      fans_renderer_->SetPose(i, raccoons[i].GetPose());
    } else if (!fans_hidden_[i]) {
      fans_renderer_->SetPose(
          i, vector<mat4>(RaccoonModel::kPartCount,
                          glm::translate(mat4(1.0f), kHiddenPlace) *
                              glm::scale(mat4(1.0f), vec3(Raccoon::kScale))));
    }
    fans_hidden_[i] = !raccoons[i].IsVisible();
  }
}

render::CameraView SceneRenderer::GetCameraView() {
  render::CameraView view;
  view.view = camera_.GetViewMatrix();
  view.projection = camera_.GetProjectionMatrix();
  view.position = camera_.GetPosition();
  view.field_of_view = camera_.GetFieldOfView();
  view.aspect_ratio = camera_.GetAspectRatio();
  view.near_plane = camera_.GetNearPlane();
  return view;
}

void SceneRenderer::FillFrameUniforms(render::FrameUniforms& uniforms) {
  world_.FillFrameUniforms(uniforms);
}

void SceneRenderer::PrepareFrame() { world_.PrepareFrame(); }

void SceneRenderer::UploadBear() {
  UploadSkin(actors_.GetBear().GetCharacter(), *bear_renderer_, bear_uploaded_);
}

void SceneRenderer::UploadMishka() {
  UploadSkin(actors_.GetMishka(), *mishka_renderer_, mishka_uploaded_);
}

bool SceneRenderer::IsBearVisible(const ViewFrustum& frustum) const {
  const BearCharacter& character = actors_.GetBear().GetCharacter();
  return frustum.SphereInFrustum(character.GetBoundsCenter(),
                                 character.GetBoundsRadius()) !=
         ViewFrustum::kOutside;
}

void SceneRenderer::DrawShadowCasters(const render::ShadowCascade& cascade) {
  world_.DrawShadows(cascade, camera_.GetPosition());
  creatures_->DrawShadow(cascade);
  depth_shader_->Enable();
  glUniformMatrix4fv(depth_shader_->GetUniformLocation("lightSpaceMatrix"), 1,
                     GL_FALSE, value_ptr(cascade.light_space));
  hedgehog_renderer_->DrawSurfaces(depth_shader_.get());
  beaver_renderer_->DrawSurfaces(depth_shader_.get());
  chonk_renderer_->DrawSurfaces(depth_shader_.get());
  if (std::ranges::any_of(actors_.GetFans().GetRaccoons(),
                          &Raccoon::IsVisible)) {
    fans_renderer_->DrawSurfaces(depth_shader_.get());
  }
  const RestingBear& mishka = actors_.GetMishka();
  if (cascade.Contains(mishka.GetBoundsCenter(), mishka.GetBoundsRadius())) {
    UploadMishka();
    mishka_renderer_->DrawSurface(depth_shader_.get());
  }
  UploadBear();
  bear_renderer_->DrawSurface(depth_shader_.get());
  for (const unique_ptr<Geometry>& geometry : geometries_) {
    Model* model = geometry->GetModel();
    float scale = std::max({length(vec3(geometry->model_matrix[0])),
                            length(vec3(geometry->model_matrix[1])),
                            length(vec3(geometry->model_matrix[2]))});
    if (cascade.Contains(vec3(geometry->model_matrix *
                              vec4(model->GetBoundingSphereCenter(), 1.0f)),
                         model->GetBoundingSphereRadius() * scale)) {
      geometry->Draw(depth_shader_.get());
    }
  }
}

void SceneRenderer::DrawMain(const render::RenderSettings& settings) {
  normal_mapping_enabled_ = settings.normal_mapping;
  mat4 view = camera_.GetViewMatrix();
  mat4 projection = camera_.GetProjectionMatrix();
  ViewFrustum frustum = camera_.GetViewFrustum();
  vec3 eye = camera_.GetPosition();

  // 1. Opaque: the terrain and the vegetation, the salmon (shaded like the
  // vegetation), the objects of the track and the signs (they glow)
  world_.DrawOpaque(frustum, eye, view_frustum_culling_enabled_);
  creatures_->Draw();
  main_shader_->Enable();
  drawn_geometry_count_ =
      DrawGeometries(main_shader_, view, projection,
                     view_frustum_culling_enabled_ ? &frustum : nullptr, false);
  light_shader_->Enable();
  glUniformMatrix4fv(light_shader_->GetUniformLocation("view"), 1, GL_FALSE,
                     value_ptr(view));
  glUniformMatrix4fv(light_shader_->GetUniformLocation("projection"), 1,
                     GL_FALSE, value_ptr(projection));
  for (const unique_ptr<Geometry>& arrow : arrows_) {
    arrow->Draw(light_shader_.get());
  }

  // 2. The sky behind everything (not in wireframe mode)
  if (!settings.wireframe) {
    world_.DrawSky();
  }

  // 3. The bears, the hedgehogs and the beaver: their fur and spines are
  // blended with what is behind them
  BearRenderContext context;
  context.view = view;
  context.projection = projection;
  context.eye = eye;
  if (!view_frustum_culling_enabled_ || IsBearVisible(frustum)) {
    UploadBear();
    bear_renderer_->DrawShaded(
        context, actors_.GetBear().GetCharacter().GetBoundsCenter());
    ++drawn_geometry_count_;
  }
  hedgehog_renderer_->DrawShaded(context, frustum);
  beaver_renderer_->DrawShaded(context, frustum);
  chonk_renderer_->DrawShaded(context, frustum);
  fans_renderer_->DrawShaded(context, frustum);
  const RestingBear& mishka = actors_.GetMishka();
  if (!view_frustum_culling_enabled_ ||
      frustum.SphereInFrustum(mishka.GetBoundsCenter(),
                              mishka.GetBoundsRadius()) !=
          ViewFrustum::kOutside) {
    UploadMishka();
    mishka_renderer_->DrawShaded(context, mishka.GetBoundsCenter());
  }

  // 4. Transparent: the water, the falling snow
  world_.DrawTransparent(settings.transparency);
  glActiveTexture(GL_TEXTURE0);
}

void SceneRenderer::RenderFromCamera(Shader* shader) {
  ViewFrustum frustum = camera_.GetViewFrustum();
  shader->Enable();
  drawn_geometry_count_ = DrawGeometries(
      shader, camera_.GetViewMatrix(), camera_.GetProjectionMatrix(),
      view_frustum_culling_enabled_ ? &frustum : nullptr, true);
}

int SceneRenderer::DrawGeometries(Shader* shader, const mat4& view,
                                  const mat4& projection,
                                  const ViewFrustum* frustum, bool with_bear) {
  GLint view_location = shader->GetUniformLocation("view");
  GLint projection_location = shader->GetUniformLocation("projection");
  GLint normal_mapping_enabled_location =
      shader->GetUniformLocation("normalMappingEnabled");
  GLint texture_transparent_location =
      shader->GetUniformLocation("textureTransparent");
  if (view_location >= 0 && projection_location >= 0) {
    glUniformMatrix4fv(view_location, 1, GL_FALSE, value_ptr(view));
    glUniformMatrix4fv(projection_location, 1, GL_FALSE, value_ptr(projection));
  }
  if (texture_transparent_location >= 0) {
    glUniform1i(texture_transparent_location, false);
  }

  int drawn_geometries = 0;
  if (with_bear && (!frustum || IsBearVisible(*frustum))) {
    UploadBear();
    bear_renderer_->DrawSurface(shader);
    ++drawn_geometries;
  }
  for (const unique_ptr<Geometry>& geometry : geometries_) {
    if (DrawGeometry(shader, geometry.get(), normal_mapping_enabled_location,
                     frustum)) {
      ++drawn_geometries;
    }
  }

  if (normal_mapping_enabled_location >= 0) {
    glUniform1i(normal_mapping_enabled_location, normal_mapping_enabled_);
  }
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return drawn_geometries;
}

bool SceneRenderer::DrawGeometry(Shader* shader, Geometry* geometry,
                                 GLint normal_mapping_enabled_location,
                                 const ViewFrustum* frustum) {
  if (frustum && !geometry->IsVisible(*frustum)) {
    return false;
  }
  if (normal_mapping_enabled_location >= 0) {
    glUniform1i(normal_mapping_enabled_location,
                normal_mapping_enabled_ && geometry->GetHasNormalMap());
  }
  geometry->Draw(shader);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

}  // namespace bibo::scene
