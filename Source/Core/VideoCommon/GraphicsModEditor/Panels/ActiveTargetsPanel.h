// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <set>
#include <vector>

#include "Common/HookableEvent.h"
#include "VideoCommon/GraphicsModEditor/EditorState.h"
#include "VideoCommon/GraphicsModEditor/EditorTypes.h"

namespace GraphicsModEditor::Panels
{
class ActiveTargetsPanel
{
public:
  explicit ActiveTargetsPanel(EditorState& state);

  // Renders ImGui windows to the currently-bound framebuffer.
  void DrawImGui();

  void AddDrawCall(DrawCallData draw_call);
  void AddFBCall(FBCallData fb_call);

private:
  void DrawCallPanel();
  void EFBPanel();
  void EndOfFrame();
  void SelectionChanged();
  Common::EventHook m_end_of_frame_event;
  Common::EventHook m_selection_event;

  EditorState& m_state;

  // Target tracking
  std::vector<DrawCallData*> m_current_draw_calls;
  std::vector<DrawCallData*> m_upcoming_draw_calls;
  std::map<DrawCallID, DrawCallData> m_upcoming_draw_call_id_to_data;

  std::vector<FBCallData*> m_current_fb_calls;
  std::vector<FBCallData*> m_upcoming_fb_calls;
  std::map<FBInfo, FBCallData> m_upcoming_fb_call_id_to_data;

  // Track open nodes
  std::set<DrawCallID> m_open_draw_call_nodes;
  std::set<FBInfo> m_open_fb_call_nodes;

  // Selected nodes
  std::set<SelectableType> m_selected_nodes;
  bool m_selection_list_changed;
};
}  // namespace GraphicsModEditor::Panels
