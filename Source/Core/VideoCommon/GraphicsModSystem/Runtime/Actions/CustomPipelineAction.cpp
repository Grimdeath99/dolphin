// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModSystem/Runtime/Actions/CustomPipelineAction.h"

#include <algorithm>
#include <array>
#include <optional>

#include <fmt/format.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Core/System.h"

#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/Assets/CustomAssetLoader.h"
#include "VideoCommon/Assets/DirectFilesystemAssetLibrary.h"
#include "VideoCommon/GraphicsModEditor/Controls/AssetDisplay.h"
#include "VideoCommon/GraphicsModEditor/EditorEvents.h"
#include "VideoCommon/GraphicsModEditor/EditorMain.h"
#include "VideoCommon/ShaderGenCommon.h"
#include "VideoCommon/TextureCacheBase.h"

namespace
{
bool IsQualifier(std::string_view value)
{
  static std::array<std::string_view, 7> qualifiers = {"attribute", "const",   "highp",  "lowp",
                                                       "mediump",   "uniform", "varying"};
  return std::find(qualifiers.begin(), qualifiers.end(), value) != qualifiers.end();
}

bool IsBuiltInMacro(std::string_view value)
{
  static std::array<std::string_view, 5> built_in = {"__LINE__", "__FILE__", "__VERSION__",
                                                     "GL_core_profile", "GL_compatibility_profile"};
  return std::find(built_in.begin(), built_in.end(), value) != built_in.end();
}

std::vector<std::string> GlobalConflicts(std::string_view source)
{
  std::string_view last_identifier = "";
  std::vector<std::string> global_result;
  u32 scope = 0;
  for (u32 i = 0; i < source.size(); i++)
  {
    // If we're out of global scope, we don't care
    // about any of the details
    if (scope > 0)
    {
      if (source[i] == '{')
      {
        scope++;
      }
      else if (source[i] == '}')
      {
        scope--;
      }
      continue;
    }

    const auto parse_identifier = [&]() {
      const u32 start = i;
      for (; i < source.size(); i++)
      {
        if (!Common::IsAlpha(source[i]) && source[i] != '_' && !std::isdigit(source[i]))
          break;
      }
      u32 end = i;
      i--;  // unwind
      return source.substr(start, end - start);
    };

    if (Common::IsAlpha(source[i]) || source[i] == '_')
    {
      const std::string_view identifier = parse_identifier();
      if (IsQualifier(identifier))
        continue;
      if (IsBuiltInMacro(identifier))
        continue;
      last_identifier = identifier;
    }
    else if (source[i] == '#')
    {
      const auto parse_until_end_of_preprocessor = [&]() {
        bool continue_until_next_newline = false;
        for (; i < source.size(); i++)
        {
          if (source[i] == '\n')
          {
            if (continue_until_next_newline)
              continue_until_next_newline = false;
            else
              break;
          }
          else if (source[i] == '\\')
          {
            continue_until_next_newline = true;
          }
        }
      };
      i++;
      const std::string_view identifier = parse_identifier();
      if (identifier == "define")
      {
        i++;
        // skip whitespace
        while (source[i] == ' ')
        {
          i++;
        }
        global_result.push_back(std::string{parse_identifier()});
        parse_until_end_of_preprocessor();
      }
      else
      {
        parse_until_end_of_preprocessor();
      }
    }
    else if (source[i] == '{')
    {
      scope++;
    }
    else if (source[i] == '(')
    {
      // Unlikely the user will be using layouts but...
      if (last_identifier == "layout")
        continue;

      // Since we handle equality, we can assume the identifier
      // before '(' is a function definition
      global_result.push_back(std::string{last_identifier});
    }
    else if (source[i] == '=')
    {
      global_result.push_back(std::string{last_identifier});
      i++;
      for (; i < source.size(); i++)
      {
        if (source[i] == ';')
          break;
      }
    }
    else if (source[i] == '/')
    {
      if ((i + 1) >= source.size())
        continue;

      if (source[i + 1] == '/')
      {
        // Go to end of line...
        for (; i < source.size(); i++)
        {
          if (source[i] == '\n')
            break;
        }
      }
      else if (source[i + 1] == '*')
      {
        // Multiline, look for first '*/'
        for (; i < source.size(); i++)
        {
          if (source[i] == '/' && source[i - 1] == '*')
            break;
        }
      }
    }
  }

  // Sort the conflicts from largest to smallest string
  // this way we can ensure smaller strings that are a substring
  // of the larger string are able to be replaced appropriately
  std::sort(global_result.begin(), global_result.end(),
            [](const std::string& first, const std::string& second) {
              return first.size() > second.size();
            });
  return global_result;
}

void WriteDefines(ShaderCode* out, const std::vector<std::string>& texture_code_names,
                  u32 texture_unit)
{
  for (std::size_t i = 0; i < texture_code_names.size(); i++)
  {
    const auto& code_name = texture_code_names[i];
    out->Write("#define {}_UNIT_{{0}} {}\n", code_name, texture_unit);
    out->Write(
        "#define {0}_COORD_{{0}} float3(data.texcoord[data.texmap_to_texcoord_index[{1}]].xy, "
        "{2})\n",
        code_name, texture_unit, i);
  }
}

}  // namespace

std::unique_ptr<CustomPipelineAction>
CustomPipelineAction::Create(std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
{
  return std::make_unique<CustomPipelineAction>(std::move(library));
}

std::unique_ptr<CustomPipelineAction>
CustomPipelineAction::Create(const picojson::value& json_data,
                             std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
{
  std::vector<CustomPipelineAction::PipelinePassPassDescription> pipeline_passes;

  const auto& passes_json = json_data.get("passes");
  if (passes_json.is<picojson::array>())
  {
    for (const auto& passes_json_val : passes_json.get<picojson::array>())
    {
      CustomPipelineAction::PipelinePassPassDescription pipeline_pass;
      if (!passes_json_val.is<picojson::object>())
      {
        ERROR_LOG_FMT(VIDEO,
                      "Failed to load custom pipeline action, 'passes' has an array value that "
                      "is not an object!");
        return nullptr;
      }

      auto pass = passes_json_val.get<picojson::object>();
      if (!pass.contains("pixel_material_asset"))
      {
        ERROR_LOG_FMT(VIDEO,
                      "Failed to load custom pipeline action, 'passes' value missing required "
                      "field 'pixel_material_asset'");
        return nullptr;
      }

      auto pixel_material_asset_json = pass["pixel_material_asset"];
      if (!pixel_material_asset_json.is<std::string>())
      {
        ERROR_LOG_FMT(VIDEO, "Failed to load custom pipeline action, 'passes' field "
                             "'pixel_material_asset' is not a string!");
        return nullptr;
      }
      pipeline_pass.m_pixel_material_asset = pixel_material_asset_json.to_str();
      pipeline_passes.push_back(std::move(pipeline_pass));
    }
  }

  if (pipeline_passes.empty())
  {
    ERROR_LOG_FMT(VIDEO, "Failed to load custom pipeline action, must specify at least one pass");
    return nullptr;
  }

  if (pipeline_passes.size() > 1)
  {
    ERROR_LOG_FMT(
        VIDEO,
        "Failed to load custom pipeline action, multiple passes are not currently supported");
    return nullptr;
  }

  return std::make_unique<CustomPipelineAction>(std::move(library), std::move(pipeline_passes));
}

CustomPipelineAction::CustomPipelineAction(std::shared_ptr<VideoCommon::CustomAssetLibrary> library)
    : m_library(std::move(library))
{
}

CustomPipelineAction::CustomPipelineAction(
    std::shared_ptr<VideoCommon::CustomAssetLibrary> library,
    std::vector<PipelinePassPassDescription> pass_descriptions)
    : m_library(std::move(library)), m_passes_config(std::move(pass_descriptions))
{
  m_passes.resize(m_passes_config.size());
}

CustomPipelineAction::~CustomPipelineAction() = default;

void CustomPipelineAction::OnTextureLoad(GraphicsModActionData::TextureLoad* load)
{
  if (!load->force_texture_reload) [[unlikely]]
    return;

  if (m_trigger_texture_reload)
  {
    m_trigger_texture_reload = false;
    *load->force_texture_reload = true;
  }
}

void CustomPipelineAction::OnDrawStarted(GraphicsModActionData::DrawStarted* draw_started)
{
  if (!draw_started) [[unlikely]]
    return;

  if (!draw_started->custom_pixel_shader) [[unlikely]]
    return;

  if (!draw_started->material_uniform_buffer) [[unlikely]]
    return;

  if (!m_valid)
    return;

  if (m_passes.empty()) [[unlikely]]
    return;

  // For now assume a single pass
  auto& pass = m_passes[0];

  if (!pass.m_pixel_shader.m_asset) [[unlikely]]
    return;

  const auto shader_data = pass.m_pixel_shader.m_asset->GetData();
  if (shader_data)
  {
    if (m_last_generated_shader_code.GetBuffer().empty())
    {
      // Calculate shader details
      std::string color_shader_data =
          ReplaceAll(shader_data->m_shader_source, "custom_main", CUSTOM_PIXELSHADER_COLOR_FUNC);
      const auto global_conflicts = GlobalConflicts(color_shader_data);
      color_shader_data = ReplaceAll(color_shader_data, "\r\n", "\n");
      color_shader_data = ReplaceAll(color_shader_data, "{", "{{");
      color_shader_data = ReplaceAll(color_shader_data, "}", "}}");
      // First replace global conflicts with dummy strings
      // This avoids the problem where a shorter word
      // is in a longer word, ex two functions:  'execute' and 'execute_fast'
      for (std::size_t i = 0; i < global_conflicts.size(); i++)
      {
        const std::string& identifier = global_conflicts[i];
        color_shader_data =
            ReplaceAll(color_shader_data, identifier, fmt::format("_{0}_DOLPHIN_TEMP_{0}_", i));
      }
      // Now replace the temporaries with the actual value
      for (std::size_t i = 0; i < global_conflicts.size(); i++)
      {
        const std::string& identifier = global_conflicts[i];
        color_shader_data = ReplaceAll(color_shader_data, fmt::format("_{0}_DOLPHIN_TEMP_{0}_", i),
                                       fmt::format("{}_{{0}}", identifier));
      }

      for (const auto& texture_code_name : m_texture_code_names)
      {
        color_shader_data =
            ReplaceAll(color_shader_data, fmt::format("{}_COORD", texture_code_name),
                       fmt::format("{}_COORD_{{0}}", texture_code_name));
        color_shader_data = ReplaceAll(color_shader_data, fmt::format("{}_UNIT", texture_code_name),
                                       fmt::format("{}_UNIT_{{0}}", texture_code_name));
      }

      WriteDefines(&m_last_generated_shader_code, m_texture_code_names, draw_started->texture_unit);
      m_last_generated_shader_code.Write("{}", color_shader_data);
    }
    CustomPixelShader custom_pixel_shader;
    custom_pixel_shader.custom_shader = m_last_generated_shader_code.GetBuffer();
    custom_pixel_shader.material_uniform_block = m_last_generated_material_code.GetBuffer();
    *draw_started->custom_pixel_shader = custom_pixel_shader;
    *draw_started->material_uniform_buffer = m_material_data;
  }
}

void CustomPipelineAction::OnTextureCreate(GraphicsModActionData::TextureCreate* create)
{
  if (!create->custom_textures) [[unlikely]]
    return;

  if (!create->additional_dependencies) [[unlikely]]
    return;

  if (m_passes_config.empty()) [[unlikely]]
    return;

  if (m_passes.empty()) [[unlikely]]
    return;

  m_valid = true;
  auto& loader = Core::System::GetInstance().GetCustomAssetLoader();

  // For now assume a single pass
  const auto& pass_config = m_passes_config[0];
  auto& pass = m_passes[0];

  if (!pass.m_pixel_material.m_asset ||
      pass_config.m_pixel_material_asset != pass.m_pixel_material.m_asset->GetAssetId())
  {
    pass.m_pixel_material.m_asset =
        loader.LoadMaterial(pass_config.m_pixel_material_asset, m_library);
  }
  create->additional_dependencies->push_back(VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
      pass.m_pixel_material.m_asset, pass.m_pixel_material.m_asset->GetLastLoadedTime()});

  const auto material_data = pass.m_pixel_material.m_asset->GetData();
  if (!material_data)
  {
    m_valid = false;
    return;
  }

  std::size_t max_material_data_size = 0;
  if (!pass.m_pixel_shader.m_asset ||
      pass.m_pixel_material.m_asset->GetLastLoadedTime() >
          pass.m_pixel_material.m_cached_write_time ||
      material_data->shader_asset != pass.m_pixel_shader.m_asset->GetAssetId())
  {
    m_last_generated_shader_code = ShaderCode{};
    m_last_generated_material_code = ShaderCode{};
    pass.m_pixel_shader.m_asset = loader.LoadPixelShader(material_data->shader_asset, m_library);
    pass.m_pixel_shader.m_cached_write_time = pass.m_pixel_shader.m_asset->GetLastLoadedTime();
    pass.m_pixel_material.m_cached_write_time = pass.m_pixel_material.m_asset->GetLastLoadedTime();
    for (const auto& property : material_data->properties)
    {
      max_material_data_size += VideoCommon::MaterialProperty::GetMemorySize(property);
      VideoCommon::MaterialProperty::WriteAsShaderCode(m_last_generated_material_code, property);
    }
    m_material_data.resize(max_material_data_size);
  }
  create->additional_dependencies->push_back(VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
      pass.m_pixel_shader.m_asset, pass.m_pixel_shader.m_asset->GetLastLoadedTime()});

  const auto shader_data = pass.m_pixel_shader.m_asset->GetData();
  if (!shader_data)
  {
    m_valid = false;
    return;
  }

  if (shader_data->m_properties.size() != material_data->properties.size())
  {
    m_valid = false;
    return;
  }

  m_texture_code_names.clear();
  std::optional<std::size_t> main_texture_offset;
  bool has_shared_texture = false;
  std::vector<VideoCommon::CachedAsset<VideoCommon::GameTextureAsset>> game_assets;
  u8* material_buffer = m_material_data.data();
  for (std::size_t index = 0; index < material_data->properties.size(); index++)
  {
    auto& property = material_data->properties[index];
    const auto shader_it = shader_data->m_properties.find(property.m_code_name);
    if (shader_it == shader_data->m_properties.end())
    {
      ERROR_LOG_FMT(VIDEO,
                    "Custom pipeline for texture '{}' has material asset '{}' that uses a "
                    "code name of '{}' but that can't be found on shader asset '{}'!",
                    create->texture_name, pass.m_pixel_material.m_asset->GetAssetId(),
                    property.m_code_name, pass.m_pixel_shader.m_asset->GetAssetId());
      m_valid = false;
      return;
    }

    // TODO: Compare shader and material type

    if (property.m_type == VideoCommon::MaterialProperty::Type::Type_TextureAsset)
    {
      if (shader_it->second.m_type ==
          VideoCommon::ShaderProperty::Type::Type_SamplerArrayShared_Main)
      {
        main_texture_offset = index;
      }
      else if (shader_it->second.m_type ==
               VideoCommon::ShaderProperty::Type::Type_SamplerArrayShared_Additional)
      {
        has_shared_texture = true;
      }
      else if (shader_it->second.m_type == VideoCommon::ShaderProperty::Type::Type_Sampler2D)
      {
        // nop for now
        continue;
      }
      else
      {
        ERROR_LOG_FMT(VIDEO,
                      "Custom pipeline for texture '{}', material asset '{}' has property texture "
                      "for shader property '{}' that does not support textures!",
                      create->texture_name, pass.m_pixel_material.m_asset->GetAssetId(),
                      property.m_code_name);
        m_valid = false;
        return;
      }

      if (property.m_value)
      {
        if (auto* value = std::get_if<VideoCommon::CustomAssetLibrary::AssetID>(&*property.m_value))
        {
          if (*value == "")
          {
            game_assets.push_back(VideoCommon::CachedAsset<VideoCommon::GameTextureAsset>{});
            continue;
          }
          auto asset = loader.LoadGameTexture(*value, m_library);
          if (asset)
          {
            const auto loaded_time = asset->GetLastLoadedTime();
            game_assets.push_back(VideoCommon::CachedAsset<VideoCommon::GameTextureAsset>{
                std::move(asset), loaded_time});
          }
        }
      }
      else
      {
        game_assets.push_back(VideoCommon::CachedAsset<VideoCommon::GameTextureAsset>{});
      }
    }
    else
    {
      if (property.m_value)
      {
        VideoCommon::MaterialProperty::WriteToMemory(material_buffer, property);
      }
    }
  }

  if (has_shared_texture && !main_texture_offset)
  {
    ERROR_LOG_FMT(
        VIDEO,
        "Custom pipeline for texture '{}' has shared texture sampler asset but no main texture!",
        create->texture_name);
    m_valid = false;
    return;
  }
  // Note: we swap here instead of doing a clear + append of the member
  // variable so that any loaded assets from previous iterations
  // won't be let go
  std::swap(pass.m_game_textures, game_assets);

  if (main_texture_offset)
  {
    auto& main_texture_asset = pass.m_game_textures[*main_texture_offset];
    if (!main_texture_asset.m_asset)
      return;

    const auto main_texture_data = main_texture_asset.m_asset->GetData();
    if (!main_texture_data)
    {
      create->additional_dependencies->push_back(VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
          main_texture_asset.m_asset, main_texture_asset.m_cached_write_time});
      m_valid = false;
      return;
    }

    if (main_texture_data->m_texture.m_slices.empty() ||
        main_texture_data->m_texture.m_slices[0].m_levels.empty())
    {
      ERROR_LOG_FMT(VIDEO,
                    "Custom pipeline for texture '{}' has main texture '{}' that does not have any "
                    "texture data",
                    create->texture_name, main_texture_asset.m_asset->GetAssetId());
      create->additional_dependencies->push_back(VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
          main_texture_asset.m_asset, main_texture_asset.m_cached_write_time});
      m_valid = false;
      return;
    }

    // First loop, make sure all textures match the existing asset size
    for (std::size_t index = 0; index < pass.m_game_textures.size(); index++)
    {
      if (index == *main_texture_offset)
        continue;

      auto& game_texture = pass.m_game_textures[index];
      if (game_texture.m_asset)
      {
        auto data = game_texture.m_asset->GetData();
        if (data)
        {
          if (data->m_texture.m_slices.empty() || data->m_texture.m_slices[0].m_levels.empty())
          {
            ERROR_LOG_FMT(VIDEO,
                          "Custom pipeline for texture '{}' has asset '{}' that does not have any "
                          "texture data",
                          create->texture_name, game_texture.m_asset->GetAssetId());
            create->additional_dependencies->push_back(
                VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
                    main_texture_asset.m_asset, main_texture_asset.m_cached_write_time});
            create->additional_dependencies->push_back(
                VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
                    game_texture.m_asset, game_texture.m_cached_write_time});
            m_valid = false;
            return;
          }
          else if (main_texture_data->m_texture.m_slices[0].m_levels[0].width !=
                       data->m_texture.m_slices[0].m_levels[0].width ||
                   main_texture_data->m_texture.m_slices[0].m_levels[0].height !=
                       data->m_texture.m_slices[0].m_levels[0].height)
          {
            ERROR_LOG_FMT(VIDEO,
                          "Custom pipeline for texture '{}' has asset '{}' that does not match "
                          "the width/height of the main texture.  Texture {}x{} vs asset {}x{}",
                          create->texture_name, game_texture.m_asset->GetAssetId(),
                          main_texture_data->m_texture.m_slices[0].m_levels[0].width,
                          main_texture_data->m_texture.m_slices[0].m_levels[0].height,
                          data->m_texture.m_slices[0].m_levels[0].width,
                          data->m_texture.m_slices[0].m_levels[0].height);
            create->additional_dependencies->push_back(
                VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
                    main_texture_asset.m_asset, main_texture_asset.m_cached_write_time});
            create->additional_dependencies->push_back(
                VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
                    game_texture.m_asset, game_texture.m_cached_write_time});
            m_valid = false;
            return;
          }
        }
        else
        {
          create->additional_dependencies->push_back(
              VideoCommon::CachedAsset<VideoCommon::CustomAsset>{
                  main_texture_asset.m_asset, main_texture_asset.m_cached_write_time});
          create->additional_dependencies->push_back(
              VideoCommon::CachedAsset<VideoCommon::CustomAsset>{game_texture.m_asset,
                                                                 game_texture.m_cached_write_time});
          m_valid = false;
          return;
        }
      }
    }

    // Since all the shared textures are owned by this action, we can clear out any previous
    // textures
    create->custom_textures->clear();
    create->custom_textures->push_back(main_texture_asset);
    m_texture_code_names.push_back(material_data->properties[*main_texture_offset].m_code_name);

    // Second loop, add all the other textures after the main texture
    for (std::size_t index = 0; index < pass.m_game_textures.size(); index++)
    {
      if (index == *main_texture_offset)
        continue;

      auto& game_texture = pass.m_game_textures[index];
      if (game_texture.m_asset)
      {
        create->custom_textures->push_back(game_texture);
        m_texture_code_names.push_back(material_data->properties[index].m_code_name);
      }
    }
  }
}

void CustomPipelineAction::DrawImGui()
{
  auto& editor = Core::System::GetInstance().GetGraphicsModEditor();
  if (ImGui::CollapsingHeader("Custom pipeline", ImGuiTreeNodeFlags_DefaultOpen))
  {
    if (m_passes_config.size() == 1)
    {
      if (ImGui::BeginTable("CustomPipelineForm", 2))
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Material");
        ImGui::TableNextColumn();
        if (GraphicsModEditor::Controls::AssetDisplay(
                "CustomPipelineActionMaterial", editor.GetEditorState(),
                &m_passes_config[0].m_pixel_material_asset, GraphicsModEditor::Material))
        {
          m_trigger_texture_reload = true;
          GraphicsModEditor::EditorEvents::ChangeOccurredEvent::Trigger();
        }
        ImGui::EndTable();
      }
    }

    if (m_passes_config.empty())
    {
      if (ImGui::Button("Add pass"))
      {
        m_passes_config.emplace_back();
        m_passes.emplace_back();
      }
    }
    else
    {
      // Disable pass adding for now
      ImGui::BeginDisabled();
      ImGui::Button("Add pass");
      ImGui::EndDisabled();
    }
  }
}

void CustomPipelineAction::SerializeToConfig(picojson::object* obj)
{
  if (!obj) [[unlikely]]
    return;

  auto& json_obj = *obj;

  picojson::array serialized_passes;
  for (const auto& pass : m_passes_config)
  {
    picojson::object serialized_pass;
    serialized_pass["pixel_material_asset"] = picojson::value{pass.m_pixel_material_asset};
    serialized_passes.push_back(picojson::value{serialized_pass});
  }
  json_obj["passes"] = picojson::value{serialized_passes};
}

std::string CustomPipelineAction::GetFactoryName() const
{
  return "custom_pipeline";
}
