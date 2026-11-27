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
#include "fs/Reader.h"
#include "mdl/EntityModel.h"

#include <filesystem>
#include <vector>

namespace tb
{
class Logger;

namespace fs
{
class FileSystem;
} // namespace fs

namespace mdl
{

/**
 * Returns whether the given file is an Anachronox MD2 model. Anachronox MD2 models use
 * the Quake 2 MD2 ident, but a different version (15) and a different layout.
 */
bool canLoadAnoxMd2Model(const std::filesystem::path& path, fs::Reader reader);

/**
 * Loads an Anachronox MD2 model.
 *
 * Every skin of an Anachronox MD2 model is applied to its own set of primitives, so the
 * model is loaded with one surface per skin.
 *
 * Skins are looked up relative to the game file system and in the model's directory.
 *
 * If skin paths are given, the n-th of these replaces the n-th skin stored in the model.
 * This is used to apply the skins of an MDA model description.
 */
Result<EntityModelData> loadAnoxMd2Model(
  const std::filesystem::path& path,
  fs::Reader reader,
  const fs::FileSystem& fs,
  Logger& logger,
  const std::vector<std::filesystem::path>& skinPaths = {});

} // namespace mdl
} // namespace tb
