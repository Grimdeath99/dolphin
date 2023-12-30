// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/Assets/MeshAsset.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <utility>

#include <tinygltf/tiny_gltf.h>

#include "Common/Logging/Log.h"
#include "Common/Matrix.h"
#include "Common/StringUtil.h"
#include "Common/VariantUtil.h"
#include "VideoCommon/Assets/CustomAssetLibrary.h"

namespace VideoCommon
{
namespace
{
Common::Matrix44 BuildMatrixFromNode(const tinygltf::Node& node)
{
  if (!node.matrix.empty())
  {
    Common::Matrix44 matrix;
    for (std::size_t i = 0; i < node.matrix.size(); i++)
    {
      matrix.data[i] = static_cast<float>(node.matrix[i]);
    }
    return matrix;
  }

  Common::Matrix44 matrix = Common::Matrix44::Identity();

  // Check individual components

  if (!node.scale.empty())
  {
    matrix *= Common::Matrix44::FromMatrix33(Common::Matrix33::Scale(
        Common::Vec3{static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
                     static_cast<float>(node.scale[2])}));
  }

  if (!node.rotation.empty())
  {
    matrix *= Common::Matrix44::FromQuaternion(Common::Quaternion(
        static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
        static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));
  }

  if (!node.translation.empty())
  {
    matrix *= Common::Matrix44::Translate(Common::Vec3{static_cast<float>(node.translation[0]),
                                                       static_cast<float>(node.translation[1]),
                                                       static_cast<float>(node.translation[2])});
  }

  return matrix;
}

bool GLTFComponentTypeToAttributeFormat(int component_type, AttributeFormat* format)
{
  switch (component_type)
  {
  case TINYGLTF_COMPONENT_TYPE_BYTE:
  {
    format->type = ComponentFormat::Byte;
    format->integer = false;
  }
  break;
  case TINYGLTF_COMPONENT_TYPE_DOUBLE:
  {
    return false;
  }
  break;
  case TINYGLTF_COMPONENT_TYPE_FLOAT:
  {
    format->type = ComponentFormat::Float;
    format->integer = false;
  }
  break;
  case TINYGLTF_COMPONENT_TYPE_INT:
  {
    format->type = ComponentFormat::Float;
    format->integer = true;
  }
  break;
  case TINYGLTF_COMPONENT_TYPE_SHORT:
  {
    format->type = ComponentFormat::Short;
    format->integer = false;
  }
  break;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
  {
    format->type = ComponentFormat::UByte;
    format->integer = false;
  }
  break;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
  {
    return false;
  }
  break;
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
  {
    format->type = ComponentFormat::UShort;
    format->integer = false;
  }
  break;
  };

  return true;
}

void UpdateVertexStrideFromPrimitive(const tinygltf::Model& model, u32 accessor_index,
                                     MeshDataChunk* chunk)
{
  const tinygltf::Accessor& accessor = model.accessors[accessor_index];

  const int component_count = tinygltf::GetNumComponentsInType(accessor.type);
  if (component_count == -1)
  {
    // TODO: error
    return;
  }

  const int component_size =
      tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
  if (component_size == -1)
  {
    // TODO: error
    return;
  }

  chunk->vertex_stride += component_size * component_count;
}

void CopyBufferDataFromPrimitive(const tinygltf::Model& model, u32 accessor_index,
                                 std::size_t* outbound_offset, MeshDataChunk* chunk)
{
  const tinygltf::Accessor& accessor = model.accessors[accessor_index];

  const int component_count = tinygltf::GetNumComponentsInType(accessor.type);
  if (component_count == -1)
  {
    // TODO: error
    return;
  }

  const int component_size =
      tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(accessor.componentType));
  if (component_size == -1)
  {
    // TODO: error
    return;
  }

  const tinygltf::BufferView& buffer_view = model.bufferViews[accessor.bufferView];
  const tinygltf::Buffer& buffer = model.buffers[buffer_view.buffer];

  if (buffer_view.byteStride == 0)
  {
    // Data is tightly packed
    const auto data = &buffer.data[accessor.byteOffset + buffer_view.byteOffset];
    for (std::size_t i = 0; i < accessor.count; i++)
    {
      const std::size_t vertex_data_offset = i * chunk->vertex_stride + *outbound_offset;
      memcpy(&chunk->vertex_data[vertex_data_offset], &data[i * component_size * component_count],
             component_size * component_count);
    }
  }
  else
  {
    // Data is interleaved
    const auto data = &buffer.data[accessor.byteOffset + buffer_view.byteOffset];
    for (std::size_t i = 0; i < accessor.count; i++)
    {
      const std::size_t vertex_data_offset = i * chunk->vertex_stride + *outbound_offset;
      const std::size_t gltf_data_offset = i * buffer_view.byteStride;

      memcpy(&chunk->vertex_data[vertex_data_offset], &data[gltf_data_offset],
             component_size * component_count);
    }
  }

  *outbound_offset += component_size * component_count;

  // See:
  // https://www.reddit.com/r/vulkan/comments/oeg87z/loading_some_indexed_gltf_meshes_cause_weird/
  // See: https://toji.dev/webgpu-gltf-case-study/
  // See: https://github.com/zeux/meshoptimizer
}

void ReadGLTFMesh(std::string_view mesh_file, const tinygltf::Model& model,
                  const tinygltf::Mesh& mesh, const Common::Matrix44& mat, MeshData* data)
{
  for (std::size_t primitive_index = 0; primitive_index < mesh.primitives.size(); ++primitive_index)
  {
    MeshDataChunk chunk;
    chunk.transform = mat;
    const tinygltf::Primitive& primitive = mesh.primitives[primitive_index];
    if (primitive.indices == -1)
    {
      ERROR_LOG_FMT(VIDEO, "Mesh '{}' is expected to have indices but doesn't have any", mesh_file);
      return;
    }
    chunk.material_name = model.materials[primitive.material].name;
    const tinygltf::Accessor& index_accessor = model.accessors[primitive.indices];
    const tinygltf::BufferView& index_buffer_view = model.bufferViews[index_accessor.bufferView];
    const tinygltf::Buffer& index_buffer = model.buffers[index_buffer_view.buffer];
    const int index_stride = index_accessor.ByteStride(index_buffer_view);
    if (index_stride == -1)
    {
      ERROR_LOG_FMT(VIDEO, "Mesh '{}' has invalid stride", mesh_file);
      return;
    }
    chunk.indices = std::make_unique<u16[]>(index_accessor.count);
    auto index_src = &index_buffer.data[index_accessor.byteOffset + index_buffer_view.byteOffset];
    for (std::size_t i = 0; i < index_accessor.count; i++)
    {
      if (index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
      {
        const auto index_cast = reinterpret_cast<const u16*>(&index_src[i * index_stride]);
        chunk.indices[i] = *index_cast;
      }
      else if (index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
      {
        const auto index_cast = reinterpret_cast<const u8*>(&index_src[i * index_stride]);
        chunk.indices[i] = *index_cast;
      }
      else if (index_accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
      {
        // TODO: update Dolphin to support u32 indices
        const auto index_cast = reinterpret_cast<const u32*>(&index_src[i * index_stride]);
        chunk.indices[i] = static_cast<u16>(*index_cast);
      }
    }

    chunk.num_indices = static_cast<u32>(index_accessor.count);

    if (primitive.mode == TINYGLTF_MODE_TRIANGLES)
    {
      chunk.primitive_type = PrimitiveType::Triangles;
    }
    else if (primitive.mode == TINYGLTF_MODE_TRIANGLE_STRIP)
    {
      chunk.primitive_type = PrimitiveType::TriangleStrip;
    }
    else if (primitive.mode == TINYGLTF_MODE_TRIANGLE_FAN)
    {
      ERROR_LOG_FMT(VIDEO, "Mesh '{}' requires triangle fan but that is not supported", mesh_file);
      return;
    }
    else if (primitive.mode == TINYGLTF_MODE_LINE)
    {
      chunk.primitive_type = PrimitiveType::Lines;
    }
    else if (primitive.mode == TINYGLTF_MODE_POINTS)
    {
      chunk.primitive_type = PrimitiveType::Points;
    }

    chunk.vertex_stride = 0;
    static std::array<std::string_view, 12> all_names = {
        "POSITION",   "NORMAL",     "COLOR_0",    "COLOR_1",    "TEXCOORD_0", "TEXCOORD_1",
        "TEXCOORD_2", "TEXCOORD_3", "TEXCOORD_4", "TEXCOORD_5", "TEXCOORD_6", "TEXCOORD_7",
    };
    for (int i = 0; i < all_names.size(); i++)
    {
      const auto it = primitive.attributes.find(std::string{all_names[i]});
      if (it != primitive.attributes.end())
      {
        UpdateVertexStrideFromPrimitive(model, it->second, &chunk);
      }
    }
    chunk.vertex_declaration.stride = chunk.vertex_stride;

    const auto position_it = primitive.attributes.find("POSITION");
    if (position_it == primitive.attributes.end())
    {
      ERROR_LOG_FMT(VIDEO, "Mesh '{}' does not provide a POSITION attribute, that is required",
                    mesh_file);
      return;
    }
    std::size_t outbound_offset = 0;
    const tinygltf::Accessor& pos_accessor = model.accessors[position_it->second];
    chunk.num_vertices = static_cast<u32>(pos_accessor.count);
    chunk.vertex_data = std::make_unique<u8[]>(chunk.num_vertices * chunk.vertex_stride);
    CopyBufferDataFromPrimitive(model, position_it->second, &outbound_offset, &chunk);
    chunk.components_available = 0;
    chunk.vertex_declaration.position.enable = true;
    chunk.vertex_declaration.position.components = 3;
    chunk.vertex_declaration.position.offset = 0;
    if (!GLTFComponentTypeToAttributeFormat(pos_accessor.componentType,
                                            &chunk.vertex_declaration.position))
    {
      ERROR_LOG_FMT(VIDEO, "Mesh '{}' has invalid attribute format for position", mesh_file);
      return;
    }

    static std::array<std::string_view, 2> color_names = {
        "COLOR_0",
        "COLOR_1",
    };
    for (std::size_t i = 0; i < color_names.size(); i++)
    {
      const auto color_it = primitive.attributes.find(std::string{color_names[i]});
      if (color_it != primitive.attributes.end())
      {
        chunk.vertex_declaration.colors[i].offset = static_cast<int>(outbound_offset);
        CopyBufferDataFromPrimitive(model, color_it->second, &outbound_offset, &chunk);
        chunk.components_available |= VB_HAS_COL0 << i;

        chunk.vertex_declaration.colors[i].enable = true;
        chunk.vertex_declaration.colors[i].components = 3;
        const tinygltf::Accessor& accessor = model.accessors[color_it->second];
        if (!GLTFComponentTypeToAttributeFormat(accessor.componentType,
                                                &chunk.vertex_declaration.colors[i]))
        {
          ERROR_LOG_FMT(VIDEO, "Mesh '{}' has invalid attribute format for {}", mesh_file,
                        color_names[i]);
          return;
        }
      }
      else
      {
        chunk.vertex_declaration.colors[i].enable = false;
      }
    }

    const auto normal_it = primitive.attributes.find("NORMAL");
    if (normal_it != primitive.attributes.end())
    {
      chunk.vertex_declaration.normals[0].offset = static_cast<int>(outbound_offset);
      CopyBufferDataFromPrimitive(model, normal_it->second, &outbound_offset, &chunk);
      chunk.components_available |= VB_HAS_NORMAL;
      chunk.vertex_declaration.normals[0].enable = true;
      chunk.vertex_declaration.normals[0].components = 3;
      const tinygltf::Accessor& accessor = model.accessors[normal_it->second];
      if (!GLTFComponentTypeToAttributeFormat(accessor.componentType,
                                              &chunk.vertex_declaration.normals[0]))
      {
        ERROR_LOG_FMT(VIDEO, "Mesh '{}' has invalid attribute format for NORMAL", mesh_file);
        return;
      }
    }
    else
    {
      chunk.vertex_declaration.normals[0].enable = false;
    }

    static std::array<std::string_view, 8> texcoord_names = {
        "TEXCOORD_0", "TEXCOORD_1", "TEXCOORD_2", "TEXCOORD_3",
        "TEXCOORD_4", "TEXCOORD_5", "TEXCOORD_6", "TEXCOORD_7",
    };
    for (std::size_t i = 0; i < texcoord_names.size(); i++)
    {
      const auto texture_it = primitive.attributes.find(std::string{texcoord_names[i]});
      if (texture_it != primitive.attributes.end())
      {
        chunk.vertex_declaration.texcoords[i].offset = static_cast<int>(outbound_offset);
        CopyBufferDataFromPrimitive(model, texture_it->second, &outbound_offset, &chunk);
        chunk.components_available |= VB_HAS_UV0 << i;
        chunk.vertex_declaration.texcoords[i].enable = true;
        chunk.vertex_declaration.texcoords[i].components = 2;
        const tinygltf::Accessor& accessor = model.accessors[texture_it->second];
        if (!GLTFComponentTypeToAttributeFormat(accessor.componentType,
                                                &chunk.vertex_declaration.texcoords[i]))
        {
          ERROR_LOG_FMT(VIDEO, "Mesh '{}' has invalid attribute format for {}", mesh_file,
                        texcoord_names[i]);
          return;
        }
      }
      else
      {
        chunk.vertex_declaration.texcoords[i].enable = false;
      }
    }

    // Position matrix can be enabled if the draw that is using
    // this mesh needs it
    chunk.vertex_declaration.posmtx.enable = false;

    data->m_mesh_chunks.push_back(std::move(chunk));
  }
}

void ReadGLTFNodes(std::string_view mesh_file, const tinygltf::Model& model,
                   const tinygltf::Node& node, const Common::Matrix44& mat, MeshData* data)
{
  if (node.mesh != -1)
  {
    ReadGLTFMesh(mesh_file, model, model.meshes[node.mesh], mat, data);
  }

  for (std::size_t i = 0; i < node.children.size(); i++)
  {
    const tinygltf::Node& child = model.nodes[node.children[i]];
    const auto child_mat = mat * BuildMatrixFromNode(child);
    ReadGLTFNodes(mesh_file, model, child, child_mat, data);
  }
}

void ReadGLTFMaterials(std::string_view mesh_file, const tinygltf::Model& model, MeshData* data)
{
  for (std::size_t i = 0; i < model.materials.size(); i++)
  {
    const tinygltf::Material& material = model.materials[i];

    // TODO: export to Dolphin materials
    data->m_mesh_material_to_material_asset_id[material.name] = "";
  }
}

// See https://github.com/KhronosGroup/glTF-Sample-Models/tree/main/2.0/NegativeScaleTest
void ReadGLTF(std::string_view mesh_file, const tinygltf::Model& model, MeshData* data)
{
  int scene_index = model.defaultScene;
  if (scene_index == -1)
    scene_index = 0;

  const auto& scene = model.scenes[scene_index];
  const auto scene_node_indices = scene.nodes;
  for (std::size_t i = 0; i < scene_node_indices.size(); i++)
  {
    const tinygltf::Node& node = model.nodes[scene_node_indices[i]];
    const auto mat = BuildMatrixFromNode(node);
    ReadGLTFNodes(mesh_file, model, node, mat, data);
  }

  ReadGLTFMaterials(mesh_file, model, data);
}
}  // namespace
bool MeshData::FromJson(const VideoCommon::CustomAssetLibrary::AssetID& asset_id,
                        const picojson::object& json, MeshData* data)
{
  if (const auto iter = json.find("material_mapping"); iter != json.end())
  {
    if (!iter->second.is<picojson::object>())
    {
      ERROR_LOG_FMT(
          VIDEO,
          "Asset '{}' failed to parse json, expected 'material_mapping' to be of type object",
          asset_id);
      return false;
    }

    for (const auto& [material_name, asset_id_json] : iter->second.get<picojson::object>())
    {
      if (!asset_id_json.is<std::string>())
      {
        ERROR_LOG_FMT(
            VIDEO,
            "Asset '{}' failed to parse json, material name '{}' linked to a non-string value",
            asset_id, material_name);
        return false;
      }

      data->m_mesh_material_to_material_asset_id[material_name] = asset_id_json.to_str();
    }
  }
  return true;
}

void MeshData::ToJson(picojson::object* obj, const MeshData& data)
{
  if (!obj) [[unlikely]]
    return;

  auto& json_obj = *obj;

  picojson::object material_mapping;
  for (const auto& [material_name, asset_id] : data.m_mesh_material_to_material_asset_id)
  {
    material_mapping[material_name] = picojson::value{asset_id};
  }
  json_obj["material_mapping"] = picojson::value{material_mapping};
}

bool MeshData::FromDolphinMesh(std::span<const u8> raw_data, MeshData* data)
{
  std::size_t offset = 0;

  std::size_t chunk_size = 0;
  std::memcpy(&chunk_size, raw_data.data(), sizeof(std::size_t));
  offset += sizeof(std::size_t);

  data->m_mesh_chunks.reserve(chunk_size);
  for (std::size_t i = 0; i < chunk_size; i++)
  {
    MeshDataChunk chunk;

    std::memcpy(&chunk.num_vertices, raw_data.data() + offset, sizeof(u32));
    offset += sizeof(u32);

    std::memcpy(&chunk.vertex_stride, raw_data.data() + offset, sizeof(u32));
    offset += sizeof(u32);

    chunk.vertex_data = std::make_unique<u8[]>(chunk.num_vertices * chunk.vertex_stride);
    std::memcpy(chunk.vertex_data.get(), raw_data.data() + offset,
                chunk.num_vertices * chunk.vertex_stride);
    offset += chunk.num_vertices * chunk.vertex_stride;

    std::memcpy(&chunk.num_indices, raw_data.data() + offset, sizeof(u32));
    offset += sizeof(u32);

    chunk.indices = std::make_unique<u16[]>(chunk.num_indices);
    std::memcpy(chunk.indices.get(), raw_data.data() + offset, chunk.num_indices * sizeof(u16));
    offset += chunk.num_indices * sizeof(u16);

    std::memcpy(&chunk.vertex_declaration, raw_data.data() + offset,
                sizeof(PortableVertexDeclaration));
    offset += sizeof(PortableVertexDeclaration);

    std::memcpy(&chunk.primitive_type, raw_data.data() + offset, sizeof(PrimitiveType));
    offset += sizeof(PrimitiveType);

    std::memcpy(&chunk.components_available, raw_data.data() + offset, sizeof(u32));
    offset += sizeof(u32);

    std::memcpy(&chunk.transform.data[0], raw_data.data() + offset,
                chunk.transform.data.size() * sizeof(float));
    offset += chunk.transform.data.size() * sizeof(float);

    std::size_t material_name_size = 0;
    std::memcpy(&material_name_size, raw_data.data() + offset, sizeof(std::size_t));
    offset += sizeof(std::size_t);

    chunk.material_name.assign(raw_data.data() + offset,
                               raw_data.data() + offset + material_name_size);
    offset += material_name_size * sizeof(char);

    data->m_mesh_chunks.push_back(std::move(chunk));
  }

  return true;
}

void MeshData::ToDolphinMesh(File::IOFile* file_data, const MeshData& data)
{
  const std::size_t chunk_size = data.m_mesh_chunks.size();
  file_data->WriteBytes(&chunk_size, sizeof(std::size_t));
  for (const auto& chunk : data.m_mesh_chunks)
  {
    file_data->WriteBytes(&chunk.num_vertices, sizeof(u32));
    file_data->WriteBytes(&chunk.vertex_stride, sizeof(u32));
    file_data->WriteBytes(chunk.vertex_data.get(), chunk.num_vertices * chunk.vertex_stride);
    file_data->WriteBytes(&chunk.num_indices, sizeof(u32));
    file_data->WriteBytes(chunk.indices.get(), chunk.num_indices * sizeof(u16));
    file_data->WriteBytes(&chunk.vertex_declaration, sizeof(PortableVertexDeclaration));
    file_data->WriteBytes(&chunk.primitive_type, sizeof(PrimitiveType));
    file_data->WriteBytes(&chunk.components_available, sizeof(u32));
    file_data->WriteBytes(&chunk.transform.data[0], chunk.transform.data.size() * sizeof(float));

    const std::size_t material_name_size = chunk.material_name.size();
    file_data->WriteBytes(&material_name_size, sizeof(std::size_t));
    file_data->WriteBytes(&chunk.material_name[0], chunk.material_name.size() * sizeof(char));
  }
}

bool MeshData::FromGLTF(std::string_view gltf_file, MeshData* data)
{
  // See: https://github.com/KhronosGroup/glTF-Sample-Models/tree/main for examples
  if (gltf_file.ends_with(".glb"))
  {
    ERROR_LOG_FMT(VIDEO, "File '{}' with glb extension is not supported at this time", gltf_file);
    return false;
  }
  else if (gltf_file.ends_with(".gltf"))
  {
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string model_errors;
    std::string model_warnings;
    if (!loader.LoadASCIIFromFile(&model, &model_errors, &model_warnings, std::string{gltf_file}))
    {
      ERROR_LOG_FMT(VIDEO, "File '{}' was invalid GLTF, error: {}, warning: {}", gltf_file,
                    model_errors, model_warnings);
      return false;
    }
    ReadGLTF(gltf_file, model, data);
    return true;
  }

  ERROR_LOG_FMT(VIDEO, "GLTF '{}' has invalid extension", gltf_file);
  return false;
}

CustomAssetLibrary::LoadInfo MeshAsset::LoadImpl(const CustomAssetLibrary::AssetID& asset_id)
{
  auto potential_data = std::make_shared<MeshData>();
  const auto loaded_info = m_owning_library->LoadMesh(asset_id, potential_data.get());
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
