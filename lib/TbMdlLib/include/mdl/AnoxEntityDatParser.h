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

#include "mdl/EntityDefinitionParser.h"

#include <string_view>
#include <vector>

namespace tb
{
class ParserStatus;

namespace mdl
{
struct EntityDefinitionClassInfo;

/**
 * Parses Anachronox's model entity list (models/entity.dat, also known as entities.dat).
 *
 * Every line defines a point entity class with pipe separated fields:
 *
 * classname|model|scale x|scale y|scale z|type|mins x|mins y|mins z|maxs x|maxs y|maxs z|
 * shadow|solid|walk speed|run speed|speed|lighting|blending|target sequence|misc|no mip|
 * spawn sequence|description
 *
 * Lines starting with // or ; are comments. Only the fields up to the bounding box are
 * required, and only the class name, model, scale, bounding box and description are used.
 */
class AnoxEntityDatParser : public EntityDefinitionParser
{
private:
  std::string_view m_str;

public:
  AnoxEntityDatParser(std::string_view str, const Color& defaultEntityColor);

private:
  std::vector<EntityDefinitionClassInfo> parseClassInfos(ParserStatus& status) override;
};

} // namespace mdl
} // namespace tb
