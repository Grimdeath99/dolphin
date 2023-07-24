// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

#include <imgui.h>

#include "Common/HookableEvent.h"
#include "VideoCommon/Assets/CustomAssetLibrary.h"
#include "VideoCommon/GraphicsModEditor/EditorState.h"

class AbstractTexture;

namespace GraphicsModEditor::Panels
{
class AssetBrowserPanel
{
public:
  explicit AssetBrowserPanel(EditorState& state);

  void ResetCurrentPath();

  // Renders ImGui windows to the currently-bound framebuffer.
  void DrawImGui();

private:
  static constexpr u32 thumbnail_size = 150;
  static constexpr u32 padding = 32;
  static constexpr ImVec2 thumbnail_imgui_size{thumbnail_size, thumbnail_size};

  void HandleAsset(const std::filesystem::path& asset_path, AbstractTexture* icon_texture,
                   std::string_view drag_drop_name);
  void BeginRename(const VideoCommon::CustomAssetLibrary::AssetID& asset_id,
                   std::string rename_text);
  void EndRename();
  EditorState& m_state;
  Common::EventHook m_selection_event;

  std::filesystem::path m_current_path;

  // Rename
  std::optional<VideoCommon::CustomAssetLibrary::AssetID> m_renamed_asset_id = std::nullopt;
  std::string m_rename_text = "";
  bool m_is_rename_focused = false;
  bool m_does_rename_have_error = false;
};
}  // namespace GraphicsModEditor::Panels
