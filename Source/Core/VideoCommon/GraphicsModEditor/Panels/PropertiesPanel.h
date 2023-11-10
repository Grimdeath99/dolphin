// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <set>

#include "Common/HookableEvent.h"
#include "VideoCommon/GraphicsModEditor/Controls/MaterialControl.h"
#include "VideoCommon/GraphicsModEditor/Controls/ShaderControl.h"
#include "VideoCommon/GraphicsModEditor/Controls/TextureControl.h"
#include "VideoCommon/GraphicsModEditor/EditorState.h"
#include "VideoCommon/GraphicsModEditor/EditorTypes.h"

namespace GraphicsModEditor::Panels
{
class PropertiesPanel
{
public:
  PropertiesPanel(EditorState& state);

  // Renders ImGui windows to the currently-bound framebuffer.
  void DrawImGui();

private:
  void DrawCallIDSelected(const DrawCallID& selected_object);
  void FBCallIDSelected(const FBInfo& selected_object);
  void AssetDataSelected(EditorAsset* selected_object);
  void SelectionOccurred(const std::set<SelectableType>& selection);

  Common::EventHook m_selection_event;

  EditorState& m_state;

  std::set<SelectableType> m_selected_targets;

  Controls::MaterialControl m_material_control;
  Controls::ShaderControl m_shader_control;
  Controls::TextureControl m_texture_control;
};
}  // namespace GraphicsModEditor::Panels
