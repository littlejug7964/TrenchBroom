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

#pragma once

#include "base/Result.h"

#include "kd/reflection_decl.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace tb
{
namespace fs
{
class FileSystem;
} // namespace fs

namespace mdl
{

/**
 * An Anachronox animated texture description (ATD).
 *
 * An ATD lists bitmaps and the frames of the animation, each of which shows one of the
 * bitmaps.
 */
struct Atd
{
  std::vector<std::filesystem::path> bitmaps;

  /** For each frame, the index of the bitmap it shows. */
  std::vector<size_t> frames;

  kdl_reflect_decl(Atd, bitmaps, frames);
};

bool isAtdTexture(const std::filesystem::path& path);

Result<Atd> parseAtd(std::string_view str);

/**
 * Returns the path of the bitmap shown by the first frame of the ATD at the given path.
 *
 * Bitmap paths are relative to the game file system. If a bitmap is not found there, it
 * is looked up relative to the ATD's directory.
 */
Result<std::filesystem::path> findAtdFirstFrameBitmap(
  const fs::FileSystem& fs, const std::filesystem::path& path);

} // namespace mdl
} // namespace tb
