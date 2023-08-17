// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModEditor/Controls/ShaderControl.h"

#include <map>
#include <string>

#include <fmt/format.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "VideoCommon/Assets/ShaderAsset.h"

namespace GraphicsModEditor::Controls
{
ShaderControl::ShaderControl(EditorState& state) : m_state(state)
{
}

void ShaderControl::DrawImGui(VideoCommon::PixelShaderData* shader)
{
  if (ImGui::BeginTable("ShaderForm", 2))
  {
    std::map<std::string, std::string> name_rename;
    for (auto& pair : shader->m_properties)
    {
      std::string name = pair.first;
      auto& property = pair.second;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Name");
      ImGui::TableNextColumn();
      ImGui::InputText(fmt::format("##{}Name", pair.first).c_str(), &name);

      if (name != pair.first)
      {
        name_rename.try_emplace(pair.first, std::move(name));
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Description");
      ImGui::TableNextColumn();
      ImGui::InputText(fmt::format("##{}Desc", pair.first).c_str(), &property.m_description);

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("Type");
      ImGui::TableNextColumn();
      if (ImGui::BeginCombo(fmt::format("##{}Type", pair.first).c_str(),
                            fmt::to_string(property.m_type).c_str()))
      {
        for (auto e = VideoCommon::ShaderProperty::Type::Type_Undefined;
             e < VideoCommon::ShaderProperty::Type::Type_Max;
             static_cast<VideoCommon::ShaderProperty::Type>(static_cast<u32>(e) + 1))
        {
          if (e == VideoCommon::ShaderProperty::Type::Type_Undefined)
          {
            continue;
          }

          const bool is_selected = property.m_type == e;
          if (ImGui::Selectable(fmt::to_string(e).c_str(), is_selected))
          {
            property.m_type = e;
          }
        }
        ImGui::EndCombo();
      }

      for (const auto& [old_name, new_name] : name_rename)
      {
        auto shader_prop = shader->m_properties.extract(old_name);
        if (shader_prop)
        {
          shader_prop.key() = new_name;
          shader->m_properties.insert(std::move(shader_prop));
        }
      }
    }
    ImGui::EndTable();

    if (ImGui::Button("Add"))
    {
      shader->m_properties.try_emplace(fmt::format("Prop{}", shader->m_properties.size()),
                                       VideoCommon::ShaderProperty{});
    }
  }
}
}  // namespace GraphicsModEditor::Controls
