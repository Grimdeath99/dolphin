// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModSystem/Runtime/Actions/CustomMeshAction.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Core/System.h"

#include "VideoCommon/Assets/CustomAssetLoader.h"
#include "VideoCommon/GraphicsModEditor/Controls/AssetDisplay.h"
#include "VideoCommon/GraphicsModEditor/EditorEvents.h"
#include "VideoCommon/GraphicsModEditor/EditorMain.h"

std::unique_ptr<CustomMeshAction>
CustomMeshAction::Create(const picojson::value& json_data,
                         std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
{
  VideoCommon::CustomAssetLibrary::AssetID mesh_asset;

  // TODO...

  return std::make_unique<CustomMeshAction>(std::move(library), std::move(mesh_asset));
}

CustomMeshAction::CustomMeshAction(std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
    : m_library(std::move(library))
{
}

CustomMeshAction::CustomMeshAction(std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
                                   VideoCommon::CustomAssetLibrary::AssetID mesh_asset_id)
    : m_library(std::move(library)), m_mesh_asset_id(std::move(mesh_asset_id))
{
}

CustomMeshAction::~CustomMeshAction() = default;

void CustomMeshAction::OnDrawStarted(GraphicsModActionData::DrawStarted* draw_started)
{
  if (!draw_started) [[unlikely]]
    return;

  if (!draw_started->mesh_chunk) [[unlikely]]
    return;

  if (!draw_started->current_mesh_index) [[unlikely]]
    return;

  if (!draw_started->more_data) [[unlikely]]
    return;

  if (!draw_started->custom_pixel_shader) [[unlikely]]
    return;

  if (!draw_started->material_uniform_buffer) [[unlikely]]
    return;

  if (m_mesh_asset_id == "") [[unlikely]]
    return;

  auto& loader = Core::System::GetInstance().GetCustomAssetLoader();

  if (!m_cached_mesh_asset.m_asset || m_mesh_asset_id != m_cached_mesh_asset.m_asset->GetAssetId())
  {
    m_cached_mesh_asset.m_asset = loader.LoadMesh(m_mesh_asset_id, m_library);
  }

  const auto mesh_data = m_cached_mesh_asset.m_asset->GetData();
  if (!mesh_data)
  {
    return;
  }

  if (m_cached_mesh_asset.m_asset->GetLastLoadedTime() > m_cached_mesh_asset.m_cached_write_time ||
      m_transform_changed || m_mesh_asset_changed)
  {
    // Hold onto a reference since our stored render chunks
    // point to this data
    m_mesh_data = mesh_data;
    m_cached_mesh_asset.m_cached_write_time = m_cached_mesh_asset.m_asset->GetLastLoadedTime();
    m_render_chunks.clear();
    for (const auto& mesh_chunk : mesh_data->m_mesh_chunks)
    {
      RenderChunk render_chunk;
      PortableVertexDeclaration vertex_declaration = mesh_chunk.vertex_declaration;
      vertex_declaration.posmtx = draw_started->current_vertex_format.GetVertexDeclaration().posmtx;
      render_chunk.m_native_vertex_format = g_gfx->CreateNativeVertexFormat(vertex_declaration);
      render_chunk.m_mesh_chunk.indices = mesh_chunk.indices.get();
      render_chunk.m_mesh_chunk.num_indices = mesh_chunk.num_indices;
      render_chunk.m_mesh_chunk.vertex_format = render_chunk.m_native_vertex_format.get();
      render_chunk.m_mesh_chunk.num_vertices = mesh_chunk.num_vertices;
      render_chunk.m_mesh_chunk.vertex_stride =
          render_chunk.m_native_vertex_format->GetVertexStride();
      render_chunk.m_mesh_chunk.vertices = mesh_chunk.vertex_data.get();
      render_chunk.m_mesh_chunk.primitive_type = mesh_chunk.primitive_type;
      render_chunk.m_mesh_chunk.components_available = mesh_chunk.components_available;

      // TODO: this should be passed from mesh and _technically_
      // is a state of the render pipeline
      render_chunk.m_mesh_chunk.cull_mode = CullMode::None;

      const auto scale = Common::Matrix33::Scale(Common::Vec3{m_scale, m_scale, m_scale});
      const auto rotation = Common::Quaternion::RotateXYZ(m_rotation);
      render_chunk.m_mesh_chunk.transform =
          Common::Matrix44::Translate(m_translation) * Common::Matrix44::FromQuaternion(rotation) *
          Common::Matrix44::FromMatrix33(scale) * mesh_chunk.transform;

      for (std::size_t i = 0; i < vertex_declaration.texcoords.size(); i++)
      {
        auto& texcoord = vertex_declaration.texcoords[i];
        if (texcoord.enable)
        {
          render_chunk.m_tex_units.push_back(static_cast<u32>(i));
        }
      }

      m_render_chunks.push_back(std::move(render_chunk));
    }
    m_transform_changed = false;
    m_mesh_asset_changed = false;
  }

  auto& curr_render_chunk = m_render_chunks[*draw_started->current_mesh_index];
  const auto& curr_mesh_chunk = mesh_data->m_mesh_chunks[*draw_started->current_mesh_index];

  curr_render_chunk.m_custom_pipeline.UpdatePixelData(
      loader, m_library, curr_render_chunk.m_tex_units,
      mesh_data->m_mesh_material_to_material_asset_id[curr_mesh_chunk.material_name]);

  *draw_started->mesh_chunk = curr_render_chunk.m_mesh_chunk;
  CustomPixelShader custom_pixel_shader;
  custom_pixel_shader.custom_shader =
      curr_render_chunk.m_custom_pipeline.m_last_generated_shader_code.GetBuffer();
  custom_pixel_shader.material_uniform_block =
      curr_render_chunk.m_custom_pipeline.m_last_generated_material_code.GetBuffer();
  *draw_started->custom_pixel_shader = custom_pixel_shader;
  *draw_started->material_uniform_buffer = curr_render_chunk.m_custom_pipeline.m_material_data;

  (*draw_started->current_mesh_index)++;
  if (*draw_started->current_mesh_index < mesh_data->m_mesh_chunks.size())
  {
    *draw_started->more_data = true;
  }
}

void CustomMeshAction::DrawImGui()
{
  auto& editor = Core::System::GetInstance().GetGraphicsModEditor();
  if (ImGui::CollapsingHeader("Custom mesh", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if (ImGui::BeginTable("CustomMeshForm", 2))
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Mesh");
      ImGui::TableNextColumn();
      if (GraphicsModEditor::Controls::AssetDisplay("MeshValue", editor.GetEditorState(),
                                                    &m_mesh_asset_id, GraphicsModEditor::Mesh))
      {
        GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
        m_mesh_asset_changed = true;
      }
      ImGui::EndTable();
    }
  }
  if (ImGui::CollapsingHeader("Custom mesh transform", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if (ImGui::BeginTable("CustomMeshTransform", 2))
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Scale");
      ImGui::TableNextColumn();
      if (ImGui::InputFloat("##Scale", &m_scale))
      {
        GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
        m_transform_changed = true;
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Rotation");
      ImGui::TableNextColumn();
      if (ImGui::InputFloat3("##Rotation", m_rotation.data.data()))
      {
        GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
        m_transform_changed = true;
      }
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Translate");
      ImGui::TableNextColumn();
      if (ImGui::InputFloat3("##Translate", m_translation.data.data()))
      {
        GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
        m_transform_changed = true;
      }
      ImGui::EndTable();
    }
  }
}

void CustomMeshAction::SerializeToConfig(picojson::object*)
{
}

std::string CustomMeshAction::GetFactoryName() const
{
  return "custom_mesh";
}
