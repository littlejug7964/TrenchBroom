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

#include "mdl/LoadEntityDefinitions.h"

#include "fs/DiskIO.h"
#include "fs/File.h"
#include "fs/FileSystem.h"
#include "mdl/AnoxEntityDatParser.h"
#include "mdl/DefParser.h"
#include "mdl/EntParser.h"
#include "mdl/FgdParser.h"

#include "kd/path_utils.h"
#include "kd/result.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <optional>

namespace tb::mdl
{
namespace
{

template <typename Parser, typename... Args>
Result<std::vector<EntityDefinition>> parseEntityDefinitions(
  const fs::File& file, ParserStatus& status, Args&&... parserArgs)
{
  auto reader = file.reader().buffer();
  auto parser = Parser{reader.stringView(), std::forward<Args>(parserArgs)...};
  return parser.parseDefinitions(status);
}

/**
 * Parses the given file according to the extension of the given path. If an include
 * path is given, FGD files can include other files relative to that path.
 */
Result<std::vector<EntityDefinition>> parseEntityDefinitions(
  const fs::File& file,
  const std::filesystem::path& path,
  const std::optional<std::filesystem::path>& includePath,
  const Color& defaultColor,
  ParserStatus& status)
{
  const auto extension = kdl::path_to_lower(path.extension());
  if (extension == ".fgd")
  {
    return includePath
             ? parseEntityDefinitions<FgdParser>(file, status, defaultColor, *includePath)
             : parseEntityDefinitions<FgdParser>(file, status, defaultColor);
  }
  if (extension == ".def")
  {
    return parseEntityDefinitions<DefParser>(file, status, defaultColor);
  }
  if (extension == ".ent")
  {
    return parseEntityDefinitions<EntParser>(file, status, defaultColor);
  }
  if (extension == ".dat")
  {
    return parseEntityDefinitions<AnoxEntityDatParser>(file, status, defaultColor);
  }

  return Error{fmt::format("Unknown entity definition format: {}", path)};
}

} // namespace

Result<std::vector<EntityDefinition>> loadEntityDefinitions(
  const std::filesystem::path& path, const Color& defaultColor, ParserStatus& status)
{
  return fs::Disk::openFile(path) | kdl::and_then([&](auto file) {
           return parseEntityDefinitions(*file, path, path, defaultColor, status);
         });
}

Result<std::vector<EntityDefinition>> loadEntityDefinitions(
  const fs::FileSystem& fs,
  const std::filesystem::path& path,
  const Color& defaultColor,
  ParserStatus& status)
{
  return fs.openFile(path) | kdl::and_then([&](auto file) {
           return parseEntityDefinitions(*file, path, std::nullopt, defaultColor, status);
         });
}

void addEntityDefinitions(
  std::vector<EntityDefinition>& entityDefinitions,
  std::vector<EntityDefinition> additionalEntityDefinitions)
{
  for (auto& entityDefinition : additionalEntityDefinitions)
  {
    if (std::ranges::none_of(entityDefinitions, [&](const auto& existingDefinition) {
          return existingDefinition.name == entityDefinition.name;
        }))
    {
      entityDefinitions.push_back(std::move(entityDefinition));
    }
  }
}

} // namespace tb::mdl
