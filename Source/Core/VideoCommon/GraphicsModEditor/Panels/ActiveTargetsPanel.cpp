// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/GraphicsModEditor/Panels/ActiveTargetsPanel.h"

#include <algorithm>

#include <fmt/format.h>
#include <imgui.h>

#include "VideoCommon/GraphicsModEditor/EditorEvents.h"
#include "VideoCommon/GraphicsModEditor/EditorTypes.h"
#include "VideoCommon/GraphicsModSystem/Runtime/Actions/CustomPipelineAction.h"
#include "VideoCommon/GraphicsModSystem/Runtime/Actions/MoveAction.h"
#include "VideoCommon/GraphicsModSystem/Runtime/Actions/ScaleAction.h"
#include "VideoCommon/GraphicsModSystem/Runtime/Actions/SkipAction.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoEvents.h"

namespace GraphicsModEditor::Panels
{
ActiveTargetsPanel::ActiveTargetsPanel(EditorState& state) : m_state(state)
{
  m_end_of_frame_event =
      AfterFrameEvent::Register([this] { EndOfFrame(); }, "EditorActiveTargetsPanelEnd");

  m_selection_event = EditorEvents::ItemsSelectedEvent::Register(
      [this](const std::set<SelectableType>& selected_targets) {
        if (selected_targets.size() == 1 &&
            std::holds_alternative<EditorAsset*>(*selected_targets.begin()))
        {
          m_selected_nodes.clear();

          // Clear highlighted nodes
          m_state.m_editor_data.m_operation_and_draw_call_id_to_actions.clear();
          m_state.m_editor_data.m_fb_call_id_to_actions.clear();
        }
      },
      "EditorActiveTargetsPanelSelection");
}

void ActiveTargetsPanel::DrawImGui()
{
  // Set the active target panel first use size and position
  const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  u32 default_window_height = g_presenter->GetTargetRectangle().GetHeight() -
                              ((float)g_presenter->GetTargetRectangle().GetHeight() * 0.1);
  u32 default_window_width = ((float)g_presenter->GetTargetRectangle().GetWidth() * 0.15);
  ImGui::SetNextWindowPos(ImVec2(main_viewport->WorkPos.x + default_window_width / 4,
                                 main_viewport->WorkPos.y +
                                     ((float)g_presenter->GetTargetRectangle().GetHeight() * 0.05)),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(default_window_width, default_window_height),
                           ImGuiCond_FirstUseEver);

  m_selection_list_changed = false;
  ImGui::Begin("Scene Panel");

  ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
  if (ImGui::BeginTabBar("SceneTabs", tab_bar_flags))
  {
    if (ImGui::BeginTabItem("Draw Calls"))
    {
      DrawCallPanel();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("EFBs"))
    {
      EFBPanel();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();

  // Now look in the selected nodes list
  // and make sure all elements existed this frame (if not, remove them!)
  for (auto selected_iter = m_selected_nodes.begin(); selected_iter != m_selected_nodes.end();)
  {
    if (auto target = std::get_if<DrawCallID>(&*selected_iter))
    {
      if (m_state.m_runtime_data.m_draw_call_id_to_data.contains(*target))
      {
        selected_iter++;
      }
      else
      {
        selected_iter = m_selected_nodes.erase(selected_iter);
        m_selection_list_changed = true;
      }
    }
    else if (auto fb_target = std::get_if<FBInfo>(&*selected_iter))
    {
      if (m_state.m_runtime_data.m_fb_call_id_to_data.contains(*fb_target))
      {
        selected_iter++;
      }
      else
      {
        selected_iter = m_selected_nodes.erase(selected_iter);
        m_selection_list_changed = true;
      }
    }
    else
    {
      selected_iter++;
    }
  }

  if (m_selection_list_changed)
  {
    SelectionChanged();
  }
}

void ActiveTargetsPanel::AddDrawCall(DrawCallData draw_call)
{
  // Skip efb textures, they are handled by the
  // EFB panel
  if (draw_call.m_id.m_texture_hash.starts_with("efb1"))
    return;

  if (const auto iter = m_state.m_runtime_data.m_draw_call_id_to_data.find(draw_call.m_id);
      iter != m_state.m_runtime_data.m_draw_call_id_to_data.end())
  {
    draw_call.m_time = iter->second.m_time;
    auto id = draw_call.m_id;
    const auto [it, added] =
        m_upcoming_draw_call_id_to_data.insert_or_assign(std::move(id), std::move(draw_call));
    if (added)
      m_upcoming_draw_calls.push_back(&it->second);
  }
  else
  {
    auto id = draw_call.m_id;
    const auto [it, added] =
        m_upcoming_draw_call_id_to_data.try_emplace(std::move(id), std::move(draw_call));
    if (added)
      m_upcoming_draw_calls.push_back(&it->second);
  }
}

void ActiveTargetsPanel::AddFBCall(FBCallData fb_call)
{
  if (const auto iter = m_state.m_runtime_data.m_fb_call_id_to_data.find(fb_call.m_id);
      iter != m_state.m_runtime_data.m_fb_call_id_to_data.end())
  {
    fb_call.m_time = iter->second.m_time;
    auto id = fb_call.m_id;
    const auto [it, added] =
        m_upcoming_fb_call_id_to_data.insert_or_assign(std::move(id), std::move(fb_call));
    if (added)
      m_upcoming_fb_calls.push_back(&it->second);
  }
  else
  {
    auto id = fb_call.m_id;
    const auto [it, added] =
        m_upcoming_fb_call_id_to_data.try_emplace(std::move(id), std::move(fb_call));
    if (added)
      m_upcoming_fb_calls.push_back(&it->second);
  }
}

void ActiveTargetsPanel::DrawCallPanel()
{
  auto& imgui_io = ImGui::GetIO();
  static constexpr ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth;
  for (DrawCallData* draw_call : m_current_draw_calls)
  {
    const auto& user_data = m_state.m_user_data.m_draw_call_id_to_user_data[draw_call->m_id];
    const auto target_actions_iter =
        m_state.m_user_data.m_draw_call_id_to_actions.find(draw_call->m_id);

    ImGuiTreeNodeFlags node_flags;

    if (target_actions_iter == m_state.m_user_data.m_draw_call_id_to_actions.end())
      node_flags = ImGuiTreeNodeFlags_Leaf;
    else
      node_flags = base_flags;

    if (m_selected_nodes.contains(draw_call->m_id))
      node_flags |= ImGuiTreeNodeFlags_Selected;

    // ImGui::SetNextItemWidth(25.0f);
    ImGui::Image(m_state.m_editor_data.m_name_to_texture["filled_cube"].get(), ImVec2{25, 25});
    ImGui::SameLine();

    ImGui::SetNextItemOpen(m_open_draw_call_nodes.contains(draw_call->m_id));
    const std::string id = draw_call->m_id.GetID();
    std::string_view name;
    if (user_data.m_friendly_name.empty())
      name = id;
    else
      name = user_data.m_friendly_name;
    const bool node_open = ImGui::TreeNodeEx(id.c_str(), node_flags, "%s", name.data());

    bool ignore_target_context_menu = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_::ImGuiMouseButton_Left) || ImGui::IsItemFocused())
    {
      if (!imgui_io.KeyCtrl)
        m_selected_nodes.clear();
      m_selected_nodes.insert(draw_call->m_id);
      m_selection_list_changed = true;
    }
    if (!node_open)
    {
      m_open_draw_call_nodes.erase(draw_call->m_id);
    }
    else
    {
      m_open_draw_call_nodes.insert(draw_call->m_id);
      if (target_actions_iter != m_state.m_user_data.m_draw_call_id_to_actions.end())
      {
        std::vector<GraphicsModAction*> actions_to_delete;
        for (const auto& action : target_actions_iter->second)
        {
          node_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                       ImGuiTreeNodeFlags_SpanAvailWidth;

          if (m_selected_nodes.contains(action.get()))
            node_flags |= ImGuiTreeNodeFlags_Selected;
          ImGui::TreeNodeEx(action->GetID().c_str(), node_flags, "%s", action->GetName().c_str());
          if (ImGui::IsItemClicked(ImGuiMouseButton_::ImGuiMouseButton_Left) ||
              ImGui::IsItemFocused())
          {
            if (!imgui_io.KeyCtrl)
              m_selected_nodes.clear();
            m_selected_nodes.insert(action.get());
            m_selection_list_changed = true;
          }

          if (ImGui::BeginPopupContextItem())
          {
            ignore_target_context_menu = true;
            if (ImGui::Selectable("Delete"))
            {
              actions_to_delete.push_back(action.get());
            }
            ImGui::EndPopup();
          }
          ImGui::OpenPopupOnItemClick(action->GetID().c_str(), ImGuiPopupFlags_MouseButtonRight);
        }

        for (const auto& action : actions_to_delete)
        {
          // Removal from owning memory container
          target_actions_iter->second.erase(
              std::remove_if(target_actions_iter->second.begin(), target_actions_iter->second.end(),
                             [action](auto& action_own) { return action_own.get() == action; }),
              target_actions_iter->second.end());
          if (target_actions_iter->second.empty())
          {
            m_state.m_user_data.m_draw_call_id_to_actions.erase(target_actions_iter);
          }

          // Removal from reference memory container
          std::vector<const OperationAndDrawCallID*> operation_keys;
          for (auto& [operation_and_target, actions] :
               m_state.m_user_data.m_operation_and_draw_call_id_to_actions)
          {
            // Skip targets that don't match
            if (operation_and_target.m_draw_call_id != draw_call->m_id)
            {
              continue;
            }

            std::erase(actions, action);
            if (actions.empty())
            {
              operation_keys.push_back(&operation_and_target);
            }
          }
          for (const OperationAndDrawCallID* operation_key : operation_keys)
            m_state.m_user_data.m_operation_and_draw_call_id_to_actions.erase(*operation_key);

          m_selected_nodes.erase(action);
          m_selection_list_changed = true;
        }
      }
      ImGui::TreePop();
    }

    if (!ignore_target_context_menu)
    {
      if (ImGui::BeginPopupContextItem(id.c_str()))
      {
        if (ImGui::Selectable("Add move action"))
        {
          OperationAndDrawCallID::Operation operation;
          if (draw_call->m_projection_type == ProjectionType::Orthographic)
            operation = OperationAndDrawCallID::Operation::Projection2D;
          else
            operation = OperationAndDrawCallID::Operation::Projection3D;

          OperationAndDrawCallID operation_and_target{operation, draw_call->m_id};
          auto& operation_actions =
              m_state.m_user_data.m_operation_and_draw_call_id_to_actions[operation_and_target];
          auto& actions = m_state.m_user_data.m_draw_call_id_to_actions[draw_call->m_id];
          auto action = std::make_unique<EditorAction>(MoveAction::Create());
          action->SetName(" Move action");
          action->SetID(fmt::format("{}.Move action.{}", id, actions.size()));
          actions.push_back(std::move(action));
          operation_actions.push_back(actions.back().get());
          m_open_draw_call_nodes.insert(draw_call->m_id);
        }
        else if (ImGui::Selectable("Add scale action"))
        {
          OperationAndDrawCallID::Operation operation;
          if (draw_call->m_projection_type == ProjectionType::Orthographic)
            operation = OperationAndDrawCallID::Operation::Projection2D;
          else
            operation = OperationAndDrawCallID::Operation::Projection3D;
          OperationAndDrawCallID operation_and_target{operation, draw_call->m_id};
          auto& operation_actions =
              m_state.m_user_data.m_operation_and_draw_call_id_to_actions[operation_and_target];
          auto& actions = m_state.m_user_data.m_draw_call_id_to_actions[draw_call->m_id];
          auto action = std::make_unique<EditorAction>(ScaleAction::Create());
          action->SetName("Scale action");
          action->SetID(fmt::format("{}.Skip action.{}", id, actions.size()));
          actions.push_back(std::move(action));
          operation_actions.push_back(actions.back().get());
          m_open_draw_call_nodes.insert(draw_call->m_id);
        }
        else if (ImGui::Selectable("Add skip action"))
        {
          OperationAndDrawCallID::Operation operation = OperationAndDrawCallID::Operation::Draw;
          OperationAndDrawCallID operation_and_target{operation, draw_call->m_id};
          auto& operation_actions =
              m_state.m_user_data.m_operation_and_draw_call_id_to_actions[operation_and_target];
          auto& actions = m_state.m_user_data.m_draw_call_id_to_actions[draw_call->m_id];
          auto action = std::make_unique<EditorAction>(std::make_unique<SkipAction>());
          action->SetName("Skip action");
          action->SetID(fmt::format("{}.Skip action.{}", id, actions.size()));
          actions.push_back(std::move(action));
          operation_actions.push_back(actions.back().get());
          m_open_draw_call_nodes.insert(draw_call->m_id);
        }
        else if (ImGui::Selectable("Add pipeline action"))
        {
          const OperationAndDrawCallID draw_operation_and_target{
              OperationAndDrawCallID::Operation::Draw, draw_call->m_id};
          auto& draw_operation_actions =
              m_state.m_user_data
                  .m_operation_and_draw_call_id_to_actions[draw_operation_and_target];

          const OperationAndDrawCallID tex_create_operation_and_target{
              OperationAndDrawCallID::Operation::TextureCreate, draw_call->m_id};
          auto& tex_create_operation_actions =
              m_state.m_user_data
                  .m_operation_and_draw_call_id_to_actions[tex_create_operation_and_target];

          const OperationAndDrawCallID tex_load_operation_and_target{
              OperationAndDrawCallID::Operation::TextureLoad, draw_call->m_id};
          auto& tex_load_operation_actions =
              m_state.m_user_data
                  .m_operation_and_draw_call_id_to_actions[tex_load_operation_and_target];

          auto& actions = m_state.m_user_data.m_draw_call_id_to_actions[draw_call->m_id];
          auto action = std::make_unique<EditorAction>(
              CustomPipelineAction::Create(m_state.m_user_data.m_asset_library));
          action->SetName("Custom pipeline action");
          action->SetID(fmt::format("{}.Custom pipeline action.{}", id, actions.size()));
          actions.push_back(std::move(action));
          draw_operation_actions.push_back(actions.back().get());
          tex_create_operation_actions.push_back(actions.back().get());
          tex_load_operation_actions.push_back(actions.back().get());
          m_open_draw_call_nodes.insert(draw_call->m_id);
        }
        ImGui::EndPopup();
      }
      ImGui::OpenPopupOnItemClick(id.c_str(), ImGuiPopupFlags_MouseButtonRight);
    }
  }
}

void ActiveTargetsPanel::EFBPanel()
{
  auto& imgui_io = ImGui::GetIO();
  static constexpr ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth;
  for (FBCallData* fb_call : m_current_fb_calls)
  {
    const auto& user_data = m_state.m_user_data.m_fb_call_id_to_user_data[fb_call->m_id];
    const auto target_actions_iter =
        m_state.m_user_data.m_fb_call_id_to_actions.find(fb_call->m_id);

    ImGuiTreeNodeFlags node_flags;

    if (target_actions_iter == m_state.m_user_data.m_fb_call_id_to_actions.end())
      node_flags = ImGuiTreeNodeFlags_Leaf;
    else
      node_flags = base_flags;

    if (m_selected_nodes.contains(fb_call->m_id))
      node_flags |= ImGuiTreeNodeFlags_Selected;

    // ImGui::SetNextItemWidth(25.0f);
    ImGui::Image(m_state.m_editor_data.m_name_to_texture["filled_cube"].get(), ImVec2{25, 25});
    ImGui::SameLine();

    ImGui::SetNextItemOpen(m_open_fb_call_nodes.contains(fb_call->m_id));
    const std::string id = fmt::format("{}x{}_{}", fb_call->m_id.m_width, fb_call->m_id.m_height,
                                       static_cast<int>(fb_call->m_id.m_texture_format));
    std::string_view name;
    if (user_data.m_friendly_name.empty())
      name = id;
    else
      name = user_data.m_friendly_name;
    const bool node_open = ImGui::TreeNodeEx(id.c_str(), node_flags, "%s", name.data());

    bool ignore_target_context_menu = false;
    if (ImGui::IsItemClicked(ImGuiMouseButton_::ImGuiMouseButton_Left) || ImGui::IsItemFocused())
    {
      if (!imgui_io.KeyCtrl)
        m_selected_nodes.clear();
      m_selected_nodes.insert(fb_call->m_id);
      m_selection_list_changed = true;
    }
    if (!node_open)
    {
      m_open_fb_call_nodes.erase(fb_call->m_id);
    }
    else
    {
      m_open_fb_call_nodes.insert(fb_call->m_id);
      if (target_actions_iter != m_state.m_user_data.m_fb_call_id_to_actions.end())
      {
        std::vector<GraphicsModAction*> actions_to_delete;
        for (const auto& action : target_actions_iter->second)
        {
          node_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                       ImGuiTreeNodeFlags_SpanAvailWidth;

          if (m_selected_nodes.contains(action.get()))
            node_flags |= ImGuiTreeNodeFlags_Selected;
          ImGui::TreeNodeEx(action->GetID().c_str(), node_flags, "%s", action->GetName().c_str());
          if (ImGui::IsItemClicked(ImGuiMouseButton_::ImGuiMouseButton_Left) ||
              ImGui::IsItemFocused())
          {
            if (!imgui_io.KeyCtrl)
              m_selected_nodes.clear();
            m_selected_nodes.insert(action.get());
            m_selection_list_changed = true;
          }

          if (ImGui::BeginPopupContextItem())
          {
            ignore_target_context_menu = true;
            if (ImGui::Selectable("Delete"))
            {
              actions_to_delete.push_back(action.get());
            }
            ImGui::EndPopup();
          }
          ImGui::OpenPopupOnItemClick(action->GetID().c_str(), ImGuiPopupFlags_MouseButtonRight);
        }

        for (const auto& action : actions_to_delete)
        {
          // Removal from owning memory container
          target_actions_iter->second.erase(
              std::remove_if(target_actions_iter->second.begin(), target_actions_iter->second.end(),
                             [action](auto& action_own) { return action_own.get() == action; }),
              target_actions_iter->second.end());
          if (target_actions_iter->second.empty())
          {
            m_state.m_user_data.m_fb_call_id_to_actions.erase(target_actions_iter);
          }

          // Removal from reference memory container
          std::vector<const FBInfo*> fb_keys;
          for (auto& [fb, actions] : m_state.m_user_data.m_fb_call_id_to_reference_actions)
          {
            // Skip targets that don't match
            if (fb != fb_call->m_id)
            {
              continue;
            }

            std::erase(actions, action);
            if (actions.empty())
            {
              fb_keys.push_back(&fb);
            }
          }
          for (const FBInfo* fb : fb_keys)
            m_state.m_user_data.m_fb_call_id_to_reference_actions.erase(*fb);

          m_selected_nodes.erase(action);
          m_selection_list_changed = true;
        }
      }
      ImGui::TreePop();
    }

    if (!ignore_target_context_menu)
    {
      if (ImGui::BeginPopupContextItem(id.c_str()))
      {
        if (ImGui::Selectable("Add skip action"))
        {
          auto& actions = m_state.m_user_data.m_fb_call_id_to_actions[fb_call->m_id];
          auto& reference_actions =
              m_state.m_user_data.m_fb_call_id_to_reference_actions[fb_call->m_id];
          auto action = std::make_unique<EditorAction>(std::make_unique<SkipAction>());
          action->SetName("Skip action");
          action->SetID(fmt::format("{}.Skip action.{}", id, actions.size()));
          actions.push_back(std::move(action));
          reference_actions.push_back(actions.back().get());
          m_open_fb_call_nodes.insert(fb_call->m_id);
        }
        ImGui::EndPopup();
      }
      ImGui::OpenPopupOnItemClick(id.c_str(), ImGuiPopupFlags_MouseButtonRight);
    }
  }
}

void ActiveTargetsPanel::EndOfFrame()
{
  if (!m_upcoming_draw_call_id_to_data.empty())
  {
    m_state.m_runtime_data.m_draw_call_id_to_data = std::move(m_upcoming_draw_call_id_to_data);
    m_current_draw_calls = std::move(m_upcoming_draw_calls);
    std::sort(m_current_draw_calls.begin(), m_current_draw_calls.end(),
              [](DrawCallData* f1, DrawCallData* f2) { return f1->m_time < f2->m_time; });
  }

  if (!m_upcoming_fb_call_id_to_data.empty())
  {
    m_state.m_runtime_data.m_fb_call_id_to_data = std::move(m_upcoming_fb_call_id_to_data);
    m_current_fb_calls = std::move(m_upcoming_fb_calls);
    std::sort(m_current_fb_calls.begin(), m_current_fb_calls.end(),
              [](FBCallData* f1, FBCallData* f2) { return f1->m_time < f2->m_time; });
  }
}

void ActiveTargetsPanel::SelectionChanged()
{
  m_state.m_editor_data.m_operation_and_draw_call_id_to_actions.clear();
  m_state.m_editor_data.m_fb_call_id_to_actions.clear();
  for (const auto& selected_item : m_selected_nodes)
  {
    if (auto draw_target = std::get_if<DrawCallID>(&selected_item))
    {
      m_state.m_editor_data.m_operation_and_draw_call_id_to_actions.try_emplace(
          OperationAndDrawCallID{OperationAndDrawCallID::Operation::Draw, *draw_target},
          std::vector<GraphicsModAction*>{m_state.m_editor_data.m_highlight_action.get()});

      m_state.m_editor_data.m_operation_and_draw_call_id_to_actions.try_emplace(
          OperationAndDrawCallID{OperationAndDrawCallID::Operation::TextureCreate, *draw_target},
          std::vector<GraphicsModAction*>{m_state.m_editor_data.m_highlight_action.get()});

      m_state.m_editor_data.m_operation_and_draw_call_id_to_actions.try_emplace(
          OperationAndDrawCallID{OperationAndDrawCallID::Operation::TextureLoad, *draw_target},
          std::vector<GraphicsModAction*>{m_state.m_editor_data.m_highlight_action.get()});
    }
    else if (auto fb_target = std::get_if<FBInfo>(&selected_item))
    {
      m_state.m_editor_data.m_fb_call_id_to_actions.try_emplace(
          *fb_target,
          std::vector<GraphicsModAction*>{m_state.m_editor_data.m_highlight_action.get()});
    }
  }

  EditorEvents::ItemsSelectedEvent::Trigger(m_selected_nodes);
}
}  // namespace GraphicsModEditor::Panels
