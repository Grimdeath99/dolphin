// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "VideoCommon/GraphicsModEditor/EditorState.h"

namespace GraphicsModEditor::Controls
{
class TextureControl
{
public:
  explicit TextureControl(EditorState& state);
  void DrawImGui();

private:
  EditorState& m_state;
};
}  // namespace GraphicsModEditor::Controls
