// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/Assets/MaterialAsset.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/VariantUtil.h"
#include "VideoCommon/Assets/CustomAssetLibrary.h"
#include "VideoCommon/ShaderGenCommon.h"

namespace VideoCommon
{
namespace
{
// While not optimal, we pad our data to match std140 shader requirements
// this memory constant indicates the memory stride for a single uniform
// regardless of data type
constexpr std::size_t MemorySize = sizeof(float) * 4;

template <typename ElementType, std::size_t ElementCount>
bool ParseNumeric(const CustomAssetLibrary::AssetID& asset_id, const picojson::value& json_value,
                  std::string_view code_name, std::optional<MaterialProperty::Value>* value)
{
  static_assert(ElementCount <= 4, "Numeric data expected to be four elements or less");
  if constexpr (ElementCount == 1)
  {
    if (!json_value.is<double>())
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset id '{}' material has attribute '{}' where "
                    "a double was expected but not provided.",
                    asset_id, code_name);
      return false;
    }

    *value = static_cast<ElementType>(json_value.get<double>());
  }
  else
  {
    if (!json_value.is<picojson::array>())
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset id '{}' material has attribute '{}' where "
                    "an array was expected but not provided.",
                    asset_id, code_name);
      return false;
    }

    const auto json_data = json_value.get<picojson::array>();

    if (json_data.size() != ElementCount)
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset id '{}' material has attribute '{}' with incorrect number "
                    "of elements, expected {}",
                    asset_id, code_name, ElementCount);
      return false;
    }

    if (!std::all_of(json_data.begin(), json_data.end(),
                     [](const picojson::value& v) { return v.is<double>(); }))
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset id '{}' material has attribute '{}' where "
                    "all elements are not of type double.",
                    asset_id, code_name);
      return false;
    }

    std::array<ElementType, ElementCount> data;
    for (std::size_t i = 0; i < ElementCount; i++)
    {
      data[i] = static_cast<ElementType>(json_data[i].get<double>());
    }
    *value = std::move(data);
  }

  return true;
}
bool ParsePropertyValue(const CustomAssetLibrary::AssetID& asset_id, MaterialProperty::Type type,
                        const picojson::value& json_value, std::string_view code_name,
                        std::optional<MaterialProperty::Value>* value)
{
  switch (type)
  {
  case MaterialProperty::Type::Type_TextureAsset:
  {
    if (json_value.is<std::string>())
    {
      *value = json_value.to_str();
      return true;
    }
  }
  case MaterialProperty::Type::Type_Int:
    return ParseNumeric<s32, 1>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Int2:
    return ParseNumeric<s32, 2>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Int3:
    return ParseNumeric<s32, 3>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Int4:
    return ParseNumeric<s32, 4>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Float:
    return ParseNumeric<float, 1>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Float2:
    return ParseNumeric<float, 2>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Float3:
    return ParseNumeric<float, 3>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Float4:
    return ParseNumeric<float, 4>(asset_id, json_value, code_name, value);

  case MaterialProperty::Type::Type_Bool:
  {
    if (json_value.is<bool>())
    {
      *value = json_value.get<bool>();
      return true;
    }
  }
  break;
  };

  ERROR_LOG_FMT(VIDEO, "Asset '{}' failed to parse the json, value is not valid for type '{}'",
                asset_id, type);
  return false;
}

bool ParseMaterialProperties(const CustomAssetLibrary::AssetID& asset_id,
                             const picojson::array& values_data,
                             std::vector<MaterialProperty>* material_property)
{
  for (const auto& value_data : values_data)
  {
    VideoCommon::MaterialProperty property;
    if (!value_data.is<picojson::object>())
    {
      ERROR_LOG_FMT(VIDEO, "Asset '{}' failed to parse the json, value is not the right json type",
                    asset_id);
      return false;
    }
    const auto& value_data_obj = value_data.get<picojson::object>();

    const auto type_iter = value_data_obj.find("type");
    if (type_iter == value_data_obj.end())
    {
      ERROR_LOG_FMT(VIDEO, "Asset '{}' failed to parse the json, value entry 'type' not found",
                    asset_id);
      return false;
    }
    if (!type_iter->second.is<std::string>())
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset '{}' failed to parse the json, value entry 'type' is not "
                    "the right json type",
                    asset_id);
      return false;
    }
    std::string type = type_iter->second.to_str();
    Common::ToLower(&type);

    static constexpr std::array<std::pair<std::string_view, MaterialProperty::Type>,
                                static_cast<int>(MaterialProperty::Type::Type_Max)>
        pairs = {{
            {"texture_asset", MaterialProperty::Type::Type_TextureAsset},
            {"int", MaterialProperty::Type::Type_Int},
            {"int2", MaterialProperty::Type::Type_Int2},
            {"int3", MaterialProperty::Type::Type_Int3},
            {"int4", MaterialProperty::Type::Type_Int4},
            {"float", MaterialProperty::Type::Type_Float},
            {"float2", MaterialProperty::Type::Type_Float2},
            {"float3", MaterialProperty::Type::Type_Float3},
            {"float4", MaterialProperty::Type::Type_Float4},
            {"bool", MaterialProperty::Type::Type_Bool},
        }};
    if (const auto it = std::find_if(pairs.begin(), pairs.end(),
                                     [&](const auto& pair) { return pair.first == type; });
        it != pairs.end())
    {
      property.m_type = it->second;
    }
    else
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset '{}' failed to parse json, property entry type '{}' is "
                    "an invalid option",
                    asset_id, type_iter->second.to_str());
      return false;
    }

    const auto code_name_iter = value_data_obj.find("code_name");
    if (code_name_iter == value_data_obj.end())
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset '{}' failed to parse the json, value entry "
                    "'code_name' not found",
                    asset_id);
      return false;
    }
    if (!code_name_iter->second.is<std::string>())
    {
      ERROR_LOG_FMT(VIDEO,
                    "Asset '{}' failed to parse the json, value entry 'code_name' is not "
                    "the right json type",
                    asset_id);
      return false;
    }
    property.m_code_name = code_name_iter->second.to_str();

    const auto value_iter = value_data_obj.find("value");
    if (value_iter != value_data_obj.end())
    {
      if (!ParsePropertyValue(asset_id, property.m_type, value_iter->second, property.m_code_name,
                              &property.m_value))
        return false;
    }

    material_property->push_back(std::move(property));
  }

  return true;
}

template <typename T, std::size_t N>
picojson::array ArrayToPicoArray(const std::array<T, N>& value)
{
  picojson::array result;
  for (std::size_t i = 0; i < N; i++)
  {
    result.push_back(picojson::value{static_cast<double>(value[i])});
  }
  return result;
}
}  // namespace

void MaterialProperty::WriteToMemory(u8*& buffer, const MaterialProperty& property)
{
  if (!property.m_value)
    return;

  switch (property.m_type)
  {
  case MaterialProperty::Type::Type_Int:
    if (auto* raw_value = std::get_if<s32>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(s32);
      std::memcpy(buffer, raw_value, DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Int2:
    if (auto* raw_value = std::get_if<std::array<s32, 2>>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(s32) * 2;
      std::memcpy(buffer, raw_value->data(), DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Int3:
    if (auto* raw_value = std::get_if<std::array<s32, 3>>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(s32) * 3;
      std::memcpy(buffer, raw_value->data(), DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Int4:
    if (auto* raw_value = std::get_if<std::array<s32, 4>>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(s32) * 4;
      std::memcpy(buffer, raw_value->data(), DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Float:
    if (auto* raw_value = std::get_if<float>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(float);
      std::memcpy(buffer, raw_value, DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Float2:
    if (auto* raw_value = std::get_if<std::array<float, 2>>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(float) * 2;
      std::memcpy(buffer, raw_value->data(), DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Float3:
    if (auto* raw_value = std::get_if<std::array<float, 3>>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(float) * 3;
      std::memcpy(buffer, raw_value->data(), DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Float4:
    if (auto* raw_value = std::get_if<std::array<float, 4>>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(float) * 4;
      std::memcpy(buffer, raw_value->data(), DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  case MaterialProperty::Type::Type_Bool:
    if (auto* raw_value = std::get_if<bool>(&*property.m_value))
    {
      static constexpr std::size_t DataSize = sizeof(bool);
      std::memcpy(buffer, raw_value, DataSize);
      std::memset(buffer + DataSize, 0, MemorySize - DataSize);
      buffer += MemorySize;
    }
    break;
  };
}

std::size_t MaterialProperty::GetMemorySize(const MaterialProperty& property)
{
  if (!property.m_value)
    return 0;

  switch (property.m_type)
  {
  case MaterialProperty::Type::Type_Int:
  case MaterialProperty::Type::Type_Int2:
  case MaterialProperty::Type::Type_Int3:
  case MaterialProperty::Type::Type_Int4:
  case MaterialProperty::Type::Type_Float:
  case MaterialProperty::Type::Type_Float2:
  case MaterialProperty::Type::Type_Float3:
  case MaterialProperty::Type::Type_Float4:
  case MaterialProperty::Type::Type_Bool:
    return MemorySize;
  };

  return 0;
}

void MaterialProperty::WriteAsShaderCode(ShaderCode& shader_source,
                                         const MaterialProperty& property)
{
  if (property.m_type == MaterialProperty::Type::Type_TextureAsset)
    return;

  static constexpr std::array<
      std::pair<MaterialProperty::Type, std::pair<std::string_view, std::size_t>>,
      static_cast<int>(MaterialProperty::Type::Type_Max)>
      pairs = {{
          {MaterialProperty::Type::Type_TextureAsset, {}},
          {MaterialProperty::Type::Type_Int, {"int", 1ul}},
          {MaterialProperty::Type::Type_Int2, {"int", 2ul}},
          {MaterialProperty::Type::Type_Int3, {"int", 3ul}},
          {MaterialProperty::Type::Type_Int4, {"int", 4ul}},
          {MaterialProperty::Type::Type_Float, {"float", 1ul}},
          {MaterialProperty::Type::Type_Float2, {"float", 2ul}},
          {MaterialProperty::Type::Type_Float3, {"float", 3ul}},
          {MaterialProperty::Type::Type_Float4, {"float", 4ul}},
          {MaterialProperty::Type::Type_Bool, {"bool", 1ul}},
      }};
  if (const auto it = std::find_if(pairs.begin(), pairs.end(),
                                   [&](const auto& pair) { return pair.first == property.m_type; });
      it != pairs.end())
  {
    const auto& element_count = it->second.second;
    const auto& type_name = it->second.first;
    if (element_count == 1)
    {
      shader_source.Write("{} {};\n", type_name, property.m_code_name);
    }
    else
    {
      shader_source.Write("{}{} {};\n", type_name, element_count, property.m_code_name);
    }
    for (std::size_t i = element_count; i < 4; i++)
    {
      shader_source.Write("{} {}_padding_{};\n", type_name, property.m_code_name, i + 1);
    }
  }
}

bool MaterialData::FromJson(const CustomAssetLibrary::AssetID& asset_id,
                            const picojson::object& json, MaterialData* data)
{
  const auto values_iter = json.find("values");
  if (values_iter == json.end())
  {
    ERROR_LOG_FMT(VIDEO, "Asset '{}' failed to parse json, 'values' not found", asset_id);
    return false;
  }
  if (!values_iter->second.is<picojson::array>())
  {
    ERROR_LOG_FMT(VIDEO, "Asset '{}' failed to parse json, 'values' is not the right json type",
                  asset_id);
    return false;
  }
  const auto& values_array = values_iter->second.get<picojson::array>();

  if (!ParseMaterialProperties(asset_id, values_array, &data->properties))
    return false;

  const auto shader_asset_iter = json.find("shader_asset");
  if (shader_asset_iter == json.end())
  {
    ERROR_LOG_FMT(VIDEO, "Asset '{}' failed to parse json, 'shader_asset' not found", asset_id);
    return false;
  }
  if (!shader_asset_iter->second.is<std::string>())
  {
    ERROR_LOG_FMT(VIDEO,
                  "Asset '{}' failed to parse json, 'shader_asset' is not the right json type",
                  asset_id);
    return false;
  }
  data->shader_asset = shader_asset_iter->second.to_str();

  return true;
}

void MaterialData::ToJson(picojson::object* obj, const MaterialData& data)
{
  if (!obj) [[unlikely]]
    return;

  auto& json_obj = *obj;

  picojson::array json_properties;
  for (const auto& property : data.properties)
  {
    picojson::object json_property;
    json_property["code_name"] = picojson::value{property.m_code_name};

    switch (property.m_type)
    {
    case MaterialProperty::Type::Type_TextureAsset:
      json_property["type"] = picojson::value{"texture_asset"};
      break;
    case MaterialProperty::Type::Type_Int:
      json_property["type"] = picojson::value{"int"};
      break;
    case MaterialProperty::Type::Type_Int2:
      json_property["type"] = picojson::value{"int2"};
      break;
    case MaterialProperty::Type::Type_Int3:
      json_property["type"] = picojson::value{"int3"};
      break;
    case MaterialProperty::Type::Type_Int4:
      json_property["type"] = picojson::value{"int4"};
      break;
    case MaterialProperty::Type::Type_Float:
      json_property["type"] = picojson::value{"float"};
      break;
    case MaterialProperty::Type::Type_Float2:
      json_property["type"] = picojson::value{"float2"};
      break;
    case MaterialProperty::Type::Type_Float3:
      json_property["type"] = picojson::value{"float3"};
      break;
    case MaterialProperty::Type::Type_Float4:
      json_property["type"] = picojson::value{"float4"};
      break;
    case MaterialProperty::Type::Type_Bool:
      json_property["type"] = picojson::value{"bool"};
      break;
    case MaterialProperty::Type::Type_Undefined:
      break;
    };

    if (property.m_value)
    {
      std::visit(overloaded{[&](const CustomAssetLibrary::AssetID& value) {
                              json_property["value"] = picojson::value{value};
                            },
                            [&](s32 value) {
                              json_property["value"] = picojson::value{static_cast<double>(value)};
                            },
                            [&](const std::array<s32, 2>& value) {
                              json_property["value"] = picojson::value{ArrayToPicoArray(value)};
                            },
                            [&](const std::array<s32, 3>& value) {
                              json_property["value"] = picojson::value{ArrayToPicoArray(value)};
                            },
                            [&](const std::array<s32, 4>& value) {
                              json_property["value"] = picojson::value{ArrayToPicoArray(value)};
                            },
                            [&](float value) {
                              json_property["value"] = picojson::value{static_cast<double>(value)};
                            },
                            [&](const std::array<float, 2>& value) {
                              json_property["value"] = picojson::value{ArrayToPicoArray(value)};
                            },
                            [&](const std::array<float, 3>& value) {
                              json_property["value"] = picojson::value{ArrayToPicoArray(value)};
                            },
                            [&](const std::array<float, 4>& value) {
                              json_property["value"] = picojson::value{ArrayToPicoArray(value)};
                            },
                            [&](bool value) { json_property["value"] = picojson::value{value}; }},
                 *property.m_value);
    }

    json_properties.push_back(picojson::value{json_property});
  }

  json_obj["values"] = picojson::value{json_properties};
  json_obj["shader_asset"] = picojson::value{data.shader_asset};
}

CustomAssetLibrary::LoadInfo MaterialAsset::LoadImpl(const CustomAssetLibrary::AssetID& asset_id)
{
  auto potential_data = std::make_shared<MaterialData>();
  const auto loaded_info = m_owning_library->LoadMaterial(asset_id, potential_data.get());
  if (loaded_info.m_bytes_loaded == 0)
    return {};
  {
    std::lock_guard lk(m_data_lock);
    m_loaded = true;
    m_data = std::move(potential_data);
  }
  return loaded_info;
}
}  // namespace VideoCommon
