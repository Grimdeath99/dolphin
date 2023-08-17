// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModEditor/Controls/MaterialControl.h"

#include <optional>
#include <string>

#include <fmt/format.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "VideoCommon/Assets/MaterialAsset.h"
#include "VideoCommon/Assets/ShaderAsset.h"
#include "VideoCommon/GraphicsModEditor/Controls/AssetDisplay.h"
#include "VideoCommon/GraphicsModEditor/EditorAssetSource.h"
#include "VideoCommon/GraphicsModEditor/EditorEvents.h"

namespace GraphicsModEditor::Controls
{
namespace
{
template <typename ElementType, std::size_t ElementSize, bool IsColor>
void DrawNumericUniformControl(const std::string& code_name,
                               std::optional<VideoCommon::MaterialProperty::Value>* value,
                               VideoCommon::CustomAssetLibrary::TimeType* last_data_write)
{
  if (*value)
  {
    if constexpr (ElementSize == 1)
    {
      if (const auto val = std::get_if<ElementType>(&value->value()))
      {
        if constexpr (std::is_same<ElementType, int>::value)
        {
          if (ImGui::InputInt(fmt::format("##{}", code_name).c_str(), val))
          {
            *last_data_write = std::chrono::system_clock::now();
            GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
          }
        }
        else
        {
          if (ImGui::InputFloat(fmt::format("##{}", code_name).c_str(), val))
          {
            *last_data_write = std::chrono::system_clock::now();
            GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
          }
        }
      }
    }
    else
    {
      if (const auto val = std::get_if<std::array<ElementType, ElementSize>>(&value->value()))
      {
        if constexpr (ElementSize == 2)
        {
          if constexpr (std::is_same<ElementType, int>::value)
          {
            if (ImGui::InputInt2(fmt::format("##{}", code_name).c_str(), val->data()))
            {
              *last_data_write = std::chrono::system_clock::now();
              GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
            }
          }
          else
          {
            if (ImGui::InputFloat2(fmt::format("##{}", code_name).c_str(), val->data()))
            {
              *last_data_write = std::chrono::system_clock::now();
              GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
            }
          }
        }
        else if constexpr (ElementSize == 3)
        {
          if constexpr (std::is_same<ElementType, int>::value)
          {
            if (ImGui::InputInt3(fmt::format("##{}", code_name).c_str(), val->data()))
            {
              *last_data_write = std::chrono::system_clock::now();
              GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
            }
          }
          else
          {
            if constexpr (IsColor)
            {
              if (ImGui::ColorEdit3(fmt::format("##{}", code_name).c_str(), val->data()))
              {
                *last_data_write = std::chrono::system_clock::now();
                GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              }
            }
            else
            {
              if (ImGui::InputFloat3(fmt::format("##{}", code_name).c_str(), val->data()))
              {
                *last_data_write = std::chrono::system_clock::now();
                GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              }
            }
          }
        }
        else if constexpr (ElementSize == 4)
        {
          if constexpr (std::is_same<ElementType, int>::value)
          {
            if (ImGui::InputInt4(fmt::format("##{}", code_name).c_str(), val->data()))
            {
              *last_data_write = std::chrono::system_clock::now();
              GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
            }
          }
          else
          {
            if constexpr (IsColor)
            {
              if (ImGui::ColorEdit4(fmt::format("##{}", code_name).c_str(), val->data()))
              {
                *last_data_write = std::chrono::system_clock::now();
                GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              }
            }
            else
            {
              if (ImGui::InputFloat4(fmt::format("##{}", code_name).c_str(), val->data()))
              {
                *last_data_write = std::chrono::system_clock::now();
                GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              }
            }
          }
        }
      }
    }
  }
  else
  {
    if constexpr (ElementSize == 1)
    {
      *value = ElementType{};
    }
    else
    {
      *value = std::array<ElementType, ElementSize>{};
    }
    DrawNumericUniformControl<ElementType, ElementSize, IsColor>(code_name, value, last_data_write);
  }
}
}  // namespace
MaterialControl::MaterialControl(EditorState& state) : m_state(state)
{
}

void MaterialControl::DrawImGui(VideoCommon::MaterialData* material,
                                VideoCommon::CustomAssetLibrary::TimeType* last_data_write)
{
  if (ImGui::BeginTable("MaterialShaderForm", 2))
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("Shader");
    ImGui::TableNextColumn();

    if (AssetDisplay("MaterialShaderAsset", &m_state, &material->shader_asset,
                     AssetDataType::PixelShader))
    {
      *last_data_write = std::chrono::system_clock::now();
      GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
    }
    ImGui::EndTable();
  }

  // Look up shader
  auto asset = m_state.m_user_data.m_asset_library->GetAssetFromID(material->shader_asset);
  if (!asset)
  {
    ImGui::Text("Please choose a shader for this material");
  }
  else
  {
    auto shader = std::get_if<std::unique_ptr<VideoCommon::PixelShaderData>>(&asset->m_data);
    if (!shader)
    {
      ImGui::Text(
          fmt::format("Asset id '{}' was not type shader!", material->shader_asset).c_str());
    }
    else
    {
      VideoCommon::PixelShaderData* shader_data = shader->get();
      if (std::any_of(shader_data->m_properties.begin(), shader_data->m_properties.end(),
                      [](const auto& pair) { return pair.first == ""; }))
      {
        ImGui::Text(fmt::format("The shader '{}' has invalid or incomplete properties!",
                                material->shader_asset)
                        .c_str());
      }
      else
      {
        DrawControl(shader_data, material, last_data_write);
      }
    }
  }
}

void MaterialControl::DrawControl(VideoCommon::PixelShaderData* shader,
                                  VideoCommon::MaterialData* material,
                                  VideoCommon::CustomAssetLibrary::TimeType* last_data_write)
{
  if (shader->m_properties.size() && ImGui::CollapsingHeader("Properties"))
  {
    if (ImGui::BeginTable("MaterialPropertiesForm", 2))
    {
      material->properties.resize(shader->m_properties.size());
      std::size_t property_index = 0;
      for (const auto& [name, shader_property] : shader->m_properties)
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text(name.c_str());
        ImGui::TableNextColumn();

        auto& material_property = material->properties[property_index];
        material_property.m_code_name = name;
        switch (shader_property.m_type)
        {
        case VideoCommon::ShaderProperty::Type::Type_SamplerArrayShared_Main:
        case VideoCommon::ShaderProperty::Type::Type_SamplerArrayShared_Additional:
        case VideoCommon::ShaderProperty::Type::Type_Sampler2D:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_TextureAsset;
          if (material_property.m_value)
          {
            auto& value = *material_property.m_value;
            if (auto asset_id = std::get_if<VideoCommon::CustomAssetLibrary::AssetID>(&value))
            {
              if (AssetDisplay(material_property.m_code_name, &m_state, asset_id,
                               AssetDataType::Texture))
              {
                *last_data_write = std::chrono::system_clock::now();
                GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              }
            }
          }
          else
          {
            VideoCommon::CustomAssetLibrary::AssetID asset_id = "";
            if (AssetDisplay(material_property.m_code_name, &m_state, &asset_id,
                             AssetDataType::Texture))
            {
              *last_data_write = std::chrono::system_clock::now();
              GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              material_property.m_value = std::move(asset_id);
            }
          }
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Int:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Int;
          DrawNumericUniformControl<s32, 1, false>(material_property.m_code_name,
                                                   &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Int2:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Int2;
          DrawNumericUniformControl<s32, 2, false>(material_property.m_code_name,
                                                   &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Int3:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Int3;
          DrawNumericUniformControl<s32, 3, false>(material_property.m_code_name,
                                                   &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Int4:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Int4;
          DrawNumericUniformControl<s32, 4, false>(material_property.m_code_name,
                                                   &material_property.m_value, last_data_write);
        }
        case VideoCommon::ShaderProperty::Type::Type_Float:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Float;
          DrawNumericUniformControl<float, 1, false>(material_property.m_code_name,
                                                     &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Float2:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Float2;
          DrawNumericUniformControl<float, 2, false>(material_property.m_code_name,
                                                     &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Float3:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Float3;
          DrawNumericUniformControl<float, 3, false>(material_property.m_code_name,
                                                     &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Float4:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Float4;
          DrawNumericUniformControl<float, 4, false>(material_property.m_code_name,
                                                     &material_property.m_value, last_data_write);
        }
        break;

        case VideoCommon::ShaderProperty::Type::Type_RGBA:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Float4;
          DrawNumericUniformControl<float, 4, true>(material_property.m_code_name,
                                                    &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_RGB:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Float3;
          DrawNumericUniformControl<float, 3, true>(material_property.m_code_name,
                                                    &material_property.m_value, last_data_write);
        }
        break;
        case VideoCommon::ShaderProperty::Type::Type_Bool:
        {
          material_property.m_type = VideoCommon::MaterialProperty::Type::Type_Bool;
          if (material_property.m_value)
          {
            if (const auto val = std::get_if<bool>(&material_property.m_value.value()))
            {
              if (ImGui::Checkbox(fmt::format("##{}", name).c_str(), val))
              {
                *last_data_write = std::chrono::system_clock::now();
                GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              }
            }
          }
          else
          {
            material_property.m_value = false;
            if (const auto val = std::get_if<bool>(&material_property.m_value.value()))
            {
              if (ImGui::Checkbox(fmt::format("##{}", name).c_str(), val))
              {
                *last_data_write = std::chrono::system_clock::now();
                GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
              }
            }
          }
        }
        break;
        };

        property_index++;
      }
      ImGui::EndTable();
    }
  }
}
}  // namespace GraphicsModEditor::Controls
