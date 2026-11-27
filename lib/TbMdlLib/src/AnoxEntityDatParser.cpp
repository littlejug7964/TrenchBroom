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

#include "mdl/AnoxEntityDatParser.h"

#include "base/FileLocation.h"
#include "base/ParserStatus.h"
#include "el/Expression.h"
#include "el/Types.h"
#include "el/Value.h"
#include "mdl/EntityDefinitionClassInfo.h"
#include "mdl/ModelDefinition.h"

#include "kd/ranges/to.h"
#include "kd/string_compare.h"
#include "kd/string_format.h"
#include "kd/string_utils.h"

#include "vm/bbox.h"
#include "vm/vec.h"

#include <fmt/format.h>

#include <optional>
#include <ranges>
#include <string>

namespace tb::mdl
{
namespace
{

namespace Fields
{
constexpr size_t ClassName = 0;
constexpr size_t Model = 1;
constexpr size_t Scale = 2;
constexpr size_t Mins = 6;
constexpr size_t Maxs = 9;
constexpr size_t Description = 23;

// the number of fields up to and including the bounding box
constexpr size_t RequiredCount = 12;
} // namespace Fields

bool isComment(const std::string_view line)
{
  return line.starts_with("//") || line.starts_with(';');
}

std::vector<std::string> splitFields(const std::string_view line)
{
  return line | std::views::split('|') | std::views::transform([](auto field) {
           return kdl::str_trim(std::string_view{field.begin(), field.end()});
         })
         | kdl::ranges::to<std::vector>();
}

std::optional<vm::vec3d> parseVec3(
  const std::vector<std::string>& fields, const size_t first)
{
  auto result = vm::vec3d{};
  for (size_t i = 0; i < 3; ++i)
  {
    const auto value = kdl::str_to_double(fields[first + i]);
    if (!value)
    {
      return std::nullopt;
    }
    result[i] = *value;
  }
  return result;
}

std::optional<ModelDefinition> parseModelDefinition(
  const std::string& modelPath, const vm::vec3d& scale, const FileLocation& location)
{
  // entities without a model use "none" or "nomodel", and entities that use one of the
  // engine's procedural models use its number, e.g. "#1"
  if (
    modelPath.empty() || kdl::ci::str_is_equal(modelPath, "none")
    || kdl::ci::str_is_equal(modelPath, "nomodel") || modelPath.starts_with('#'))
  {
    return std::nullopt;
  }

  // the game uses backslashes and forward slashes interchangeably
  auto modelMap = el::MapType{
    {ModelSpecificationKeys::Path,
     el::Value{kdl::str_replace_every(modelPath, "\\", "/")}},
  };

  if (scale != vm::vec3d{1, 1, 1})
  {
    modelMap.emplace(
      ModelSpecificationKeys::Scale,
      el::Value{fmt::format("{} {} {}", scale.x(), scale.y(), scale.z())});
  }

  return ModelDefinition{
    el::ExpressionNode{el::LiteralExpression{el::Value{std::move(modelMap)}}, location}};
}

std::optional<EntityDefinitionClassInfo> parseClassInfo(
  const std::string_view line, const size_t lineNum, ParserStatus& status)
{
  const auto location = FileLocation{lineNum};
  const auto fields = splitFields(line);

  if (fields.size() < Fields::RequiredCount)
  {
    status.warn(
      location,
      fmt::format(
        "Skipping entity definition with {} fields, expected at least {}",
        fields.size(),
        Fields::RequiredCount));
    return std::nullopt;
  }

  const auto& className = fields[Fields::ClassName];
  if (className.empty())
  {
    status.warn(location, "Skipping entity definition without class name");
    return std::nullopt;
  }

  const auto scale = parseVec3(fields, Fields::Scale);
  const auto mins = parseVec3(fields, Fields::Mins);
  const auto maxs = parseVec3(fields, Fields::Maxs);
  if (!scale || !mins || !maxs)
  {
    status.warn(
      location,
      fmt::format("Skipping entity definition '{}' with invalid numbers", className));
    return std::nullopt;
  }

  auto description =
    fields.size() > Fields::Description && !fields[Fields::Description].empty()
      ? std::optional{fields[Fields::Description]}
      : std::nullopt;

  return EntityDefinitionClassInfo{
    EntityDefinitionClassType::PointClass,
    location,
    className,
    std::move(description),
    std::nullopt,
    vm::bbox3d{vm::min(*mins, *maxs), vm::max(*mins, *maxs)},
    parseModelDefinition(fields[Fields::Model], *scale, location),
    std::nullopt,
    {},
    {},
  };
}

} // namespace

AnoxEntityDatParser::AnoxEntityDatParser(
  std::string_view str, const Color& defaultEntityColor)
  : EntityDefinitionParser{defaultEntityColor}
  , m_str{str}
{
}

std::vector<EntityDefinitionClassInfo> AnoxEntityDatParser::parseClassInfos(
  ParserStatus& status)
{
  auto result = std::vector<EntityDefinitionClassInfo>{};

  auto lineNum = size_t(0);
  for (const auto line_ : m_str | std::views::split('\n'))
  {
    ++lineNum;

    const auto line = kdl::str_trim(std::string_view{line_.begin(), line_.end()});
    if (line.empty() || isComment(line))
    {
      continue;
    }

    if (auto classInfo = parseClassInfo(line, lineNum, status))
    {
      result.push_back(std::move(*classInfo));
    }
  }

  return result;
}

} // namespace tb::mdl
