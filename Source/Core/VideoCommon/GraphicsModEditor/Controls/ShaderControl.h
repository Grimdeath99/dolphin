// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/GraphicsModEditor/EditorState.h"

namespace VideoCommon
{
struct PixelShaderData;
}

namespace GraphicsModEditor::Controls
{
class ShaderControl
{
public:
  explicit ShaderControl(EditorState& state);
  void DrawImGui(VideoCommon::PixelShaderData* shader);

private:
  EditorState& m_state;
};
}  // namespace GraphicsModEditor::Controls
