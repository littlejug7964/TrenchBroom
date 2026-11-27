/*
 Copyright (C) 2025 Kristian Duske

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

#include "base/Color.h"
#include "base/Result.h"
#include "mdl/EntityDefinition.h"

#include <filesystem>
#include <vector>

namespace tb
{
class ParserStatus;

namespace fs
{
class FileSystem;
}

namespace mdl
{

Result<std::vector<EntityDefinition>> loadEntityDefinitions(
  const std::filesystem::path& path, const Color& defaultColor, ParserStatus& status);

/**
 * Loads entity definitions from the given file system. FGD files loaded this way cannot
 * include other files.
 */
Result<std::vector<EntityDefinition>> loadEntityDefinitions(
  const fs::FileSystem& fs,
  const std::filesystem::path& path,
  const Color& defaultColor,
  ParserStatus& status);

/**
 * Adds the given additional entity definitions to the given entity definitions, skipping
 * every additional definition whose name is already defined.
 */
void addEntityDefinitions(
  std::vector<EntityDefinition>& entityDefinitions,
  std::vector<EntityDefinition> additionalEntityDefinitions);

} // namespace mdl
} // namespace tb
