// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include "Common/CommonTypes.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Assets/CustomAssetLibrary.h"
#include "VideoCommon/Assets/CustomTextureData.h"
#include "VideoCommon/Assets/DirectFilesystemAssetLibrary.h"
#include "VideoCommon/Assets/MaterialAsset.h"
#include "VideoCommon/Assets/ShaderAsset.h"
#include "VideoCommon/GraphicsModSystem/Runtime/FBInfo.h"
#include "VideoCommon/GraphicsModSystem/Runtime/GraphicsModAction.h"
#include "VideoCommon/XFMemory.h"

namespace GraphicsModEditor
{
struct DrawCallID
{
  // Right now ID is just the texture, in the future
  // the ID will be composed of other data as well (position, mesh details, etc?)
  std::string GetID() const { return m_texture_hash; }
  std::string m_texture_hash;

  // Explicitly provide comparison category because some compilers
  // haven't written string operator<=>
  std::strong_ordering operator<=>(const DrawCallID&) const = default;
};

struct DrawCallData
{
  std::chrono::steady_clock::time_point m_time;
  ProjectionType m_projection_type;
  AbstractTexture* m_texture;
  DrawCallID m_id;
};

struct FBCallData
{
  std::chrono::steady_clock::time_point m_time;
  AbstractTexture* m_texture;
  FBInfo m_id;
};

struct DrawCallUserData
{
  std::string m_friendly_name;
};

struct FBCallUserData
{
  std::string m_friendly_name;
};

using EditorAssetData = std::variant<std::unique_ptr<VideoCommon::MaterialData>,
                                     std::unique_ptr<VideoCommon::PixelShaderData>,
                                     std::unique_ptr<VideoCommon::TextureData>>;
enum AssetDataType
{
  Material,
  PixelShader,
  Texture
};
struct EditorAsset
{
  VideoCommon::CustomAssetLibrary::AssetID m_asset_id;
  std::filesystem::path m_asset_path;
  EditorAssetData m_data;
  AssetDataType m_data_type;
  VideoCommon::CustomAssetLibrary::TimeType m_last_data_write;
  VideoCommon::DirectFilesystemAssetLibrary::AssetMap m_asset_map;
};

using SelectableType = std::variant<DrawCallID, FBInfo, GraphicsModAction*, EditorAsset*>;

class EditorAction final : public GraphicsModAction
{
public:
  explicit EditorAction(std::unique_ptr<GraphicsModAction> action) : m_action(std::move(action)) {}
  void OnDrawStarted(GraphicsModActionData::DrawStarted* draw) override
  {
    if (m_active)
      m_action->OnDrawStarted(draw);
  }
  void OnEFB(GraphicsModActionData::EFB* efb) override
  {
    if (m_active)
      m_action->OnEFB(efb);
  }
  void OnXFB() override
  {
    if (m_active)
      m_action->OnXFB();
  }
  void OnProjection(GraphicsModActionData::Projection* projection) override
  {
    if (m_active)
      m_action->OnProjection(projection);
  }
  void OnProjectionAndTexture(GraphicsModActionData::Projection* projection) override
  {
    if (m_active)
      m_action->OnProjectionAndTexture(projection);
  }
  void OnTextureLoad(GraphicsModActionData::TextureLoad* texture_load) override
  {
    if (m_active)
      m_action->OnTextureLoad(texture_load);
  }
  void OnTextureCreate(GraphicsModActionData::TextureCreate* texture_create) override
  {
    if (m_active)
      m_action->OnTextureCreate(texture_create);
  }
  void OnFrameEnd() override
  {
    if (m_active)
      m_action->OnFrameEnd();
  }

  void DrawImGui() override
  {
    ImGui::Checkbox("##EmptyCheckbox", &m_active);
    ImGui::SameLine();
    ImGui::InputText("##EmptyText", &m_name);
    m_action->DrawImGui();
  }

  void SerializeToConfig(picojson::object* obj) override
  {
    if (!obj) [[unlikely]]
      return;

    auto& json_obj = *obj;

    json_obj["name"] = picojson::value{m_name};
    json_obj["id"] = picojson::value{m_id};
    json_obj["active"] = picojson::value{m_active};
    m_action->SerializeToConfig(&json_obj);
  }

  std::string GetFactoryName() const override { return m_action->GetFactoryName(); }

  void SetName(std::string_view name) { m_name = std::string(name); }
  const std::string& GetName() const { return m_name; }

  void SetID(std::string_view id) { m_id = std::string(id); }
  const std::string& GetID() const { return m_id; }

  void SetActive(bool active) { m_active = active; }

protected:
  std::string m_name;

private:
  bool m_active = true;
  std::string m_id;
  std::unique_ptr<GraphicsModAction> m_action;
};
}  // namespace GraphicsModEditor
