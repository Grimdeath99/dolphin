// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/GraphicsModEditor/EditorState.h"

namespace VideoCommon
{
struct MaterialData;
struct PixelShaderData;
}  // namespace VideoCommon

namespace GraphicsModEditor::Controls
{
class MaterialControl
{
public:
  explicit MaterialControl(EditorState& state);
  void DrawImGui(VideoCommon::MaterialData* material,
                 VideoCommon::CustomAssetLibrary::TimeType* last_data_write);

private:
  void DrawControl(VideoCommon::PixelShaderData* shader, VideoCommon::MaterialData* material,
                   VideoCommon::CustomAssetLibrary::TimeType* last_data_write);
  EditorState& m_state;
};
}  // namespace GraphicsModEditor::Controls
