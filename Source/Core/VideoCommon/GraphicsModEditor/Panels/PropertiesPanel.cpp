// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModEditor/Panels/PropertiesPanel.h"

#include <filesystem>

#include <fmt/format.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Common/VariantUtil.h"
#include "VideoCommon/GraphicsModEditor/EditorEvents.h"
#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModAction.h"
#include "VideoCommon/Present.h"

namespace GraphicsModEditor::Panels
{
PropertiesPanel::PropertiesPanel(EditorState& state)
    : m_state(state), m_material_control(m_state), m_shader_control(m_state),
      m_texture_control(m_state)
{
  m_selection_event = EditorEvents::ItemsSelectedEvent::Register(
      [this](const auto& selected_targets) { SelectionOccurred(selected_targets); },
      "EditorPropertiesPanel");
}

void PropertiesPanel::DrawImGui()
{
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  u32 default_window_height = g_presenter->GetTargetRectangle().GetHeight() -
                              ((float)g_presenter->GetTargetRectangle().GetHeight() * 0.1);
  u32 default_window_width = ((float)g_presenter->GetTargetRectangle().GetWidth() * 0.15);
  ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x +
                                     g_presenter->GetTargetRectangle().GetWidth() -
                                     default_window_width * 1.25,
                                 main_viewport->WorkPos.y +
                                     ((float)g_presenter->GetTargetRectangle().GetHeight() * 0.05)),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(default_window_width, default_window_height),
                           ImGuiCond_FirstUseEver);

  ImGui::Begin("Properties Panel");

  if (m_selected_targets.size() > 1)
  {
    ImGui::Text("Multiple objects not yet supported");
  }
  else if (m_selected_targets.size() == 1)
  {
    std::visit(overloaded{[&](const DrawCallID& drawcallid) { DrawCallIDSelected(drawcallid); },
                          [&](const FBInfo& fbid) { FBCallIDSelected(fbid); },
                          [&](GraphicsModAction* action) { action->DrawImGui(); },
                          [&](EditorAsset* asset_data) { AssetDataSelected(asset_data); }},
               *m_selected_targets.begin());
  }
  ImGui::End();
}

void PropertiesPanel::DrawCallIDSelected(const DrawCallID& selected_object)
{
  const auto& data = m_state.m_runtime_data.m_draw_call_id_to_data[selected_object];
  auto& user_data = m_state.m_user_data.m_draw_call_id_to_user_data[selected_object];

  if (ImGui::BeginTable("FrameTargetForm", 2))
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("DisplayName");
    ImGui::TableNextColumn();
    ImGui::InputText("##FrameTargetDisplayName", &user_data.m_friendly_name);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("ID");
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", selected_object.GetID().c_str());

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Time Created");
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", fmt::format("{}", data.m_time.time_since_epoch().count()).c_str());

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Projection Type");
    ImGui::TableNextColumn();
    ImGui::Text("%s", fmt::format("{}", data.m_projection_type).c_str());

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Texture Name");
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", selected_object.m_texture_hash.c_str());

    if (data.m_texture)
    {
      const float column_width = ImGui::GetContentRegionAvail().x;
      float image_width = data.m_texture->GetWidth();
      float image_height = data.m_texture->GetHeight();
      const float image_aspect_ratio = image_width / image_height;

      image_width = column_width;
      image_height = column_width * image_aspect_ratio;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Texture");
      ImGui::TableNextColumn();
      ImGui::Image(data.m_texture, ImVec2{image_width, image_height});
      // TODO: right click menu to copy hash or to dump texture?
    }

    ImGui::EndTable();
  }
}

void PropertiesPanel::FBCallIDSelected(const FBInfo& selected_object)
{
  const auto& data = m_state.m_runtime_data.m_fb_call_id_to_data[selected_object];
  auto& user_data = m_state.m_user_data.m_fb_call_id_to_user_data[selected_object];

  if (ImGui::BeginTable("FBTargetForm", 2))
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("DisplayName");
    ImGui::TableNextColumn();
    ImGui::InputText("##FBTargetDisplayName", &user_data.m_friendly_name);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("ID");
    ImGui::TableNextColumn();
    ImGui::Text("%s", fmt::format("{}", selected_object.CalculateHash()).c_str());

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Time Created");
    ImGui::TableNextColumn();
    ImGui::Text("%s", fmt::format("{}", data.m_time.time_since_epoch().count()).c_str());

    if (data.m_texture)
    {
      const float column_width = ImGui::GetContentRegionAvail().x;
      float image_width = data.m_texture->GetWidth();
      float image_height = data.m_texture->GetHeight();
      const float image_aspect_ratio = image_width / image_height;

      image_width = column_width;
      image_height = column_width * image_aspect_ratio;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Texture");
      ImGui::TableNextColumn();
      ImGui::Image(data.m_texture, ImVec2{image_width, image_height});
    }

    ImGui::EndTable();
  }
}

void PropertiesPanel::AssetDataSelected(EditorAsset* selected_object)
{
  std::visit(
      overloaded{[&](const std::unique_ptr<VideoCommon::MaterialData>& material_data) {
                   m_material_control.DrawImGui(material_data.get(),
                                                &selected_object->m_last_data_write);
                 },
                 [&](const std::unique_ptr<VideoCommon::PixelShaderData>& pixel_shader_data) {
                   m_shader_control.DrawImGui(pixel_shader_data.get(),
                                              &selected_object->m_last_data_write);
                 },
                 [&](const std::unique_ptr<VideoCommon::TextureData>& texture_data) {
                   auto asset_preview = m_state.m_user_data.m_asset_library->GetAssetPreview(
                       selected_object->m_asset_id);
                   m_texture_control.DrawImGui(texture_data.get(), selected_object->m_asset_path,
                                               &selected_object->m_last_data_write, asset_preview);
                 }},
      selected_object->m_data);
}

void PropertiesPanel::SelectionOccurred(const std::set<SelectableType>& selection)
{
  m_selected_targets = selection;
}
}  // namespace GraphicsModEditor::Panels
