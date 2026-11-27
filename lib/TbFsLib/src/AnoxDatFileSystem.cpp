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

#include "fs/AnoxDatFileSystem.h"

#include "fs/File.h"
#include "fs/ReaderException.h"

#include "kd/k.h"
#include "kd/path_utils.h"
#include "kd/result.h"

#include <fmt/format.h>
#include <miniz.h>

#include <memory>
#include <string_view>

namespace tb::fs
{
namespace AnoxDatLayout
{
static const auto HeaderMagic = std::string_view{"ADAT"};
static const int Version = 9;
static const size_t EntryLength = 0x90;
static const size_t EntryNameLength = 0x80;
} // namespace AnoxDatLayout

namespace
{
Result<std::shared_ptr<File>> decompress(
  const std::shared_ptr<File>& file, const size_t uncompressedSize)
{
  const auto reader = file->reader().buffer();
  const auto compressed = reader.stringView();

  auto data = std::make_unique<char[]>(uncompressedSize);
  auto actualSize = mz_ulong(uncompressedSize);
  const auto status = mz_uncompress(
    reinterpret_cast<unsigned char*>(data.get()),
    &actualSize,
    reinterpret_cast<const unsigned char*>(compressed.data()),
    mz_ulong(compressed.size()));

  if (status != MZ_OK)
  {
    return Error{fmt::format("Could not decompress entry: {}", mz_error(status))};
  }

  if (actualSize != uncompressedSize)
  {
    return Error{fmt::format(
      "Decompressed entry has unexpected size: expected {} but got {}",
      uncompressedSize,
      actualSize)};
  }

  return std::static_pointer_cast<File>(
    std::make_shared<OwningBufferFile>(std::move(data), uncompressedSize));
}
} // namespace

Result<void> AnoxDatFileSystem::doReadDirectory()
{
  try
  {
    auto reader = m_file->reader();

    const auto magic = reader.readString(AnoxDatLayout::HeaderMagic.size());
    if (magic != AnoxDatLayout::HeaderMagic)
    {
      return Error{fmt::format("Unknown Anachronox data package magic: '{}'", magic)};
    }

    const auto directoryAddress = reader.readSize<int32_t>();
    const auto directorySize = reader.readSize<int32_t>();
    const auto version = reader.readInt<int32_t>();
    if (version != AnoxDatLayout::Version)
    {
      return Error{fmt::format("Unknown Anachronox data package version: {}", version)};
    }

    const auto entryCount = directorySize / AnoxDatLayout::EntryLength;
    reader.seekFromBegin(directoryAddress);

    for (size_t i = 0; i < entryCount; ++i)
    {
      const auto entryName = reader.readString(AnoxDatLayout::EntryNameLength);
      const auto entryAddress = reader.readSize<int32_t>();
      const auto uncompressedSize = reader.readSize<int32_t>();
      const auto compressedSize = reader.readSize<int32_t>();
      /* const auto checksum = */ reader.readUnsignedInt<uint32_t>();

      // entry names use backslashes as separators
      const auto entryPath =
        kdl::parse_utf8_or_latin1_path(entryName, K(convert_separators));

      // a compressed size of 0 indicates that the entry is stored uncompressed
      if (compressedSize > 0)
      {
        auto entryFile_ =
          std::make_shared<FileView>(m_file, entryAddress, compressedSize);
        addFile(
          entryPath,
          [entryFile = std::move(entryFile_),
           uncompressedSize]() -> Result<std::shared_ptr<File>> {
            return decompress(entryFile, uncompressedSize);
          });
      }
      else
      {
        auto entryFile_ = std::static_pointer_cast<File>(
          std::make_shared<FileView>(m_file, entryAddress, uncompressedSize));
        addFile(
          entryPath,
          [entryFile = std::move(entryFile_)]() -> Result<std::shared_ptr<File>> {
            return entryFile;
          });
      }
    }

    return kdl::void_success;
  }
  catch (const ReaderException& e)
  {
    return Error{e.what()};
  }
}

} // namespace tb::fs
