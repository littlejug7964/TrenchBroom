/*
 Copyright (C) 2026 Steve

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdl/LoadAnoxMd2Model.h"

#include "base/Logger.h"
#include "base/Macros.h"
#include "fs/FileSystem.h"
#include "fs/ReaderException.h"
#include "gl/IndexRangeMap.h"
#include "gl/IndexRangeMapBuilder.h"
#include "gl/Material.h"
#include "gl/PrimType.h"
#include "mdl/LoadSkin.h"
#include "mdl/MaterialUtils.h"

#include "kd/k.h"
#include "kd/path_utils.h"
#include "kd/ranges/to.h"
#include "kd/result.h"

#include <fmt/format.h>

#include <algorithm>
#include <numeric>
#include <optional>
#include <ranges>
#include <vector>

namespace tb::mdl
{
namespace
{
namespace AnoxMd2Layout
{
constexpr int Ident = (('2' << 24) + ('P' << 16) + ('D' << 8) + 'I');
constexpr int Version = 15;
constexpr size_t SkinNameLength = 64;
constexpr size_t FrameNameLength = 16;
constexpr size_t FrameHeaderLength = 6 * sizeof(float) + FrameNameLength;
} // namespace AnoxMd2Layout

/**
 * The extensions to try if a skin cannot be found by its own name. Skins are often
 * referred to with an extension that differs from the actual file's extension. Images
 * are preferred over animated and procedural textures (ATD).
 */
const auto SkinExtensions = std::vector<std::vector<std::filesystem::path>>{
  {".tga", ".png", ".bmp"},
  {".atd"},
};

/**
 * The resolution determines how the vertex positions of a frame are stored. Every vertex
 * position is followed by a 16 bit normal index.
 */
enum class Resolution
{
  // 3 unsigned bytes
  Low = 0,
  // a 32 bit integer, x: bits 0-10, y: bits 11-20, z: bits 21-31
  Medium = 1,
  // 3 unsigned 16 bit integers
  High = 2,
};

std::optional<Resolution> parseResolution(const int resolution)
{
  switch (resolution)
  {
  case 0:
    return Resolution::Low;
  case 1:
    return Resolution::Medium;
  case 2:
    return Resolution::High;
  default:
    return std::nullopt;
  }
}

struct AnoxMd2Frame
{
  std::string name;
  std::vector<vm::vec3f> positions;
};

struct AnoxMd2MeshVertex
{
  size_t vertexIndex;
  vm::vec2f uv;
};

struct AnoxMd2Mesh
{
  gl::PrimType type;
  std::vector<AnoxMd2MeshVertex> vertices;
};

vm::vec3f parsePosition(fs::Reader& reader, const Resolution resolution)
{
  switch (resolution)
  {
  case Resolution::Low: {
    const auto x = reader.readUnsignedChar<unsigned char>();
    const auto y = reader.readUnsignedChar<unsigned char>();
    const auto z = reader.readUnsignedChar<unsigned char>();
    return vm::vec3f{float(x), float(y), float(z)};
  }
  case Resolution::Medium: {
    const auto packed = reader.readUnsignedInt<uint32_t>();
    const auto x = packed & 0x7FFu;
    const auto y = (packed >> 11) & 0x3FFu;
    const auto z = (packed >> 21) & 0x7FFu;
    return vm::vec3f{float(x), float(y), float(z)};
  }
  case Resolution::High: {
    const auto x = reader.readUnsignedInt<uint16_t>();
    const auto y = reader.readUnsignedInt<uint16_t>();
    const auto z = reader.readUnsignedInt<uint16_t>();
    return vm::vec3f{float(x), float(y), float(z)};
  }
    switchDefault();
  }
}

AnoxMd2Frame parseFrame(
  fs::Reader reader, const size_t vertexCount, const Resolution resolution)
{
  const auto scale = reader.readVec<float, 3>();
  const auto offset = reader.readVec<float, 3>();
  auto name = reader.readString(AnoxMd2Layout::FrameNameLength);

  auto positions = std::vector<vm::vec3f>{};
  positions.reserve(vertexCount);

  for (size_t i = 0; i < vertexCount; ++i)
  {
    positions.push_back(parsePosition(reader, resolution) * scale + offset);
    /* const auto normalIndex = */ reader.readUnsignedInt<uint16_t>();
  }

  return AnoxMd2Frame{std::move(name), std::move(positions)};
}

std::vector<AnoxMd2Mesh> parseMeshes(fs::Reader reader, const size_t vertexCount)
{
  auto meshes = std::vector<AnoxMd2Mesh>{};

  // vertex count is signed, where < 0 indicates a triangle fan and > 0 indicates a
  // triangle strip, and 0 terminates the list
  while (!reader.eof())
  {
    const auto meshVertexCount = reader.readInt<int32_t>();
    if (meshVertexCount == 0)
    {
      break;
    }

    const auto type =
      meshVertexCount < 0 ? gl::PrimType::TriangleFan : gl::PrimType::TriangleStrip;

    auto vertices = std::vector<AnoxMd2MeshVertex>{};
    vertices.reserve(size_t(std::abs(meshVertexCount)));

    for (int i = 0; i < std::abs(meshVertexCount); ++i)
    {
      const auto u = reader.readFloat<float>();
      const auto v = reader.readFloat<float>();
      const auto vertexIndex = reader.readSize<int32_t>();
      if (vertexIndex >= vertexCount)
      {
        throw fs::ReaderException{
          fmt::format("Vertex index {} is out of bounds", vertexIndex)};
      }
      vertices.push_back({vertexIndex, {u, v}});
    }

    meshes.push_back({type, std::move(vertices)});
  }

  return meshes;
}

std::vector<size_t> parsePrimitiveCounts(fs::Reader reader, const size_t count)
{
  auto primitiveCounts = std::vector<size_t>{};
  primitiveCounts.reserve(count);

  for (size_t i = 0; i < count; ++i)
  {
    primitiveCounts.push_back(reader.readSize<uint16_t>());
  }

  return primitiveCounts;
}

/**
 * Groups the meshes by skin. The n-th skin applies to the next primitiveCounts[n] meshes.
 */
Result<std::vector<std::vector<AnoxMd2Mesh>>> groupMeshesBySkin(
  std::vector<AnoxMd2Mesh> meshes, const std::vector<size_t>& primitiveCounts)
{
  if (primitiveCounts.empty())
  {
    return std::vector<std::vector<AnoxMd2Mesh>>{std::move(meshes)};
  }

  const auto totalCount =
    std::accumulate(primitiveCounts.begin(), primitiveCounts.end(), size_t(0));
  if (totalCount > meshes.size())
  {
    return Error{fmt::format(
      "Primitive counts add up to {}, but model has only {} primitives",
      totalCount,
      meshes.size())};
  }

  auto result = std::vector<std::vector<AnoxMd2Mesh>>{};
  result.reserve(primitiveCounts.size());

  auto it = std::move_iterator{meshes.begin()};
  for (const auto count : primitiveCounts)
  {
    const auto end = std::next(it, std::ptrdiff_t(count));
    result.emplace_back(it, end);
    it = end;
  }

  return result;
}

std::vector<std::filesystem::path> parseSkins(fs::Reader reader, const size_t count)
{
  auto skins = std::vector<std::filesystem::path>{};
  skins.reserve(count);

  for (size_t i = 0; i < count; ++i)
  {
    skins.push_back(kdl::parse_utf8_or_latin1_path(
      reader.readString(AnoxMd2Layout::SkinNameLength), K(convert_separators)));
  }

  return skins;
}

/**
 * Finds the file of the given skin.
 *
 * Most skins are referred to by their file name only and are found next to the model.
 * Many models refer to their skins as BMP files, but the skins are actually TGA, PNG or
 * ATD files, so the skin is also looked up with other extensions.
 */
std::filesystem::path findSkinFile(
  const std::filesystem::path& skinPath,
  const std::filesystem::path& modelPath,
  const fs::FileSystem& fs)
{
  auto candidates = std::vector<std::filesystem::path>{};
  if (skinPath.has_parent_path())
  {
    candidates.push_back(skinPath);
  }
  candidates.push_back(modelPath.parent_path() / skinPath.filename());

  for (const auto& extensions : SkinExtensions)
  {
    for (const auto& candidate : candidates)
    {
      if (auto skinFile = findMaterialFile(fs, candidate, extensions);
          skinFile.is_success())
      {
        return std::move(skinFile).value();
      }
    }
  }

  return skinPath;
}

gl::Material loadAnoxSkin(
  const std::filesystem::path& skinPath,
  const std::filesystem::path& modelPath,
  const fs::FileSystem& fs,
  Logger& logger)
{
  return loadSkin(findSkinFile(skinPath, modelPath, fs), fs, logger);
}

std::vector<EntityModelVertex> getVertices(
  const AnoxMd2Frame& frame, const std::vector<AnoxMd2MeshVertex>& meshVertices)
{
  return meshVertices | std::views::transform([&](const auto& meshVertex) {
           return EntityModelVertex{
             frame.positions[meshVertex.vertexIndex], meshVertex.uv};
         })
         | kdl::ranges::to<std::vector>();
}

void buildFrame(
  EntityModelData& model,
  const AnoxMd2Frame& frame,
  const std::vector<std::vector<AnoxMd2Mesh>>& meshesBySkin)
{
  auto bounds = std::optional<vm::bbox3f>{};

  auto builders = std::vector<gl::IndexRangeMapBuilder<EntityModelVertex::Type>>{};
  builders.reserve(meshesBySkin.size());

  for (const auto& meshes : meshesBySkin)
  {
    size_t vertexCount = 0;
    auto size = gl::IndexRangeMap::Size{};
    for (const auto& mesh : meshes)
    {
      vertexCount += mesh.vertices.size();
      size.inc(mesh.type);
    }

    auto& builder = builders.emplace_back(vertexCount, size);
    for (const auto& mesh : meshes)
    {
      const auto vertices = getVertices(frame, mesh.vertices);

      if (
        const auto meshBounds = vm::bbox3f::build(
          vertices | std::views::transform(gl::GetVertexComponent<0>())))
      {
        bounds = bounds ? vm::merge(*bounds, *meshBounds) : meshBounds;
      }

      if (mesh.type == gl::PrimType::TriangleFan)
      {
        builder.addTriangleFan(vertices);
      }
      else
      {
        builder.addTriangleStrip(vertices);
      }
    }
  }

  auto& modelFrame = model.addFrame(frame.name, bounds.value_or(vm::bbox3f{}));
  for (size_t i = 0; i < builders.size(); ++i)
  {
    auto& surface = model.surface(i);
    surface.addMesh(
      modelFrame, std::move(builders[i].vertices()), std::move(builders[i].indices()));
  }
}

} // namespace

bool canLoadAnoxMd2Model(const std::filesystem::path& path, fs::Reader reader)
{
  if (!kdl::path_has_extension(kdl::path_to_lower(path), ".md2"))
  {
    return false;
  }

  const auto ident = reader.readInt<int32_t>();
  const auto version = reader.readInt<int16_t>();

  return ident == AnoxMd2Layout::Ident && version == AnoxMd2Layout::Version;
}

Result<EntityModelData> loadAnoxMd2Model(
  const std::filesystem::path& path,
  fs::Reader reader,
  const fs::FileSystem& fs,
  Logger& logger,
  const std::vector<std::filesystem::path>& skinPaths)
{
  try
  {
    const auto ident = reader.readInt<int32_t>();
    const auto version = reader.readInt<int16_t>();
    const auto resolution = parseResolution(reader.readInt<int16_t>());

    if (ident != AnoxMd2Layout::Ident)
    {
      return Error{fmt::format("Unknown Anachronox MD2 model ident: {}", ident)};
    }

    if (version != AnoxMd2Layout::Version)
    {
      return Error{fmt::format("Unknown Anachronox MD2 model version: {}", version)};
    }

    if (!resolution)
    {
      return Error{"Unknown Anachronox MD2 model resolution"};
    }

    /* const auto skinWidth = */ reader.readSize<int32_t>();
    /* const auto skinHeight = */ reader.readSize<int32_t>();
    const auto frameSize = reader.readSize<int32_t>();

    const auto skinCount = reader.readSize<int32_t>();
    const auto vertexCount = reader.readSize<int32_t>();
    /* const auto uvCoordCount = */ reader.readSize<int32_t>();
    /* const auto triangleCount = */ reader.readSize<int32_t>();
    const auto commandCount = reader.readSize<int32_t>();
    const auto frameCount = reader.readSize<int32_t>();

    // Character models have up to about 1,100 frames, and loading all of them takes a
    // lot of time and memory, so only the first frame is loaded
    const auto loadedFrameCount = std::min(frameCount, size_t(1));

    const auto skinOffset = reader.readSize<int32_t>();
    /* const auto uvCoordOffset = */ reader.readSize<int32_t>();
    /* const auto triangleOffset = */ reader.readSize<int32_t>();
    const auto frameOffset = reader.readSize<int32_t>();
    const auto commandOffset = reader.readSize<int32_t>();
    /* const auto endOffset = */ reader.readSize<int32_t>();

    const auto primitiveCountCount = reader.readSize<int32_t>();
    const auto primitiveCountOffset = reader.readSize<int32_t>();

    const auto skins = parseSkins(reader.subReaderFromBegin(skinOffset), skinCount);
    const auto primitiveCounts = parsePrimitiveCounts(
      reader.subReaderFromBegin(primitiveCountOffset), primitiveCountCount);
    auto meshes = parseMeshes(
      reader.subReaderFromBegin(commandOffset, commandCount * 4), vertexCount);

    return groupMeshesBySkin(std::move(meshes), primitiveCounts)
           | kdl::transform([&](auto meshesBySkin) {
               auto data = EntityModelData{PitchType::Normal, Orientation::Oriented};

               for (size_t i = 0; i < meshesBySkin.size(); ++i)
               {
                 const auto skinPath = i < skinPaths.size() ? skinPaths[i]
                                       : i < skins.size()   ? skins[i]
                                                            : std::filesystem::path{};

                 auto& surface = data.addSurface(
                   !skinPath.empty() ? skinPath.stem().string()
                                     : path.filename().string(),
                   loadedFrameCount);
                 if (!skinPath.empty())
                 {
                   auto materials = std::vector<gl::Material>{};
                   materials.push_back(loadAnoxSkin(skinPath, path, fs, logger));
                   surface.setSkins(std::move(materials));
                 }
               }

               const auto frameDataSize =
                 AnoxMd2Layout::FrameHeaderLength
                 + vertexCount
                     * (resolution == Resolution::Low      ? 5u
                        : resolution == Resolution::Medium ? 6u
                                                           : 8u);

               for (size_t i = 0; i < loadedFrameCount; ++i)
               {
                 const auto frame = parseFrame(
                   reader.subReaderFromBegin(frameOffset + i * frameSize, frameDataSize),
                   vertexCount,
                   *resolution);
                 buildFrame(data, frame, meshesBySkin);
               }

               return data;
             });
  }
  catch (const fs::ReaderException& e)
  {
    return Error{e.what()};
  }
}

} // namespace tb::mdl
