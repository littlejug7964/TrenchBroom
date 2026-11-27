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

#include "TestParserStatus.h"
#include "el/VariableStore.h"
#include "mdl/AnoxEntityDatParser.h"
#include "mdl/CatchConfig.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionTestUtils.h"
#include "mdl/ModelDefinition.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{

TEST_CASE("AnoxEntityDatParser")
{
  const auto defaultColor = RgbaF{1.0f, 1.0f, 1.0f, 1.0f};
  auto status = TestParserStatus{};

  SECTION("ignores empty lines and comments")
  {
    const auto file = R"(
// a comment
; another comment

    )";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    CHECK(parser.parseDefinitions(status) == std::vector<EntityDefinition>{});
    CHECK(status.countStatus(LogLevel::Warn) == 0u);
  }

  SECTION("parses a point entity definition")
  {
    const auto file =
      "ob_eye|models/mymodels/eye.md2|1.0|1.0|1.0|general|-16|-16|0|16|16|64|noshadow|1|"
      "54|8.0|8|1|0|0:0|0|0|none|myeye\r\n";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    const auto definitions = parser.parseDefinitions(status).value();
    REQUIRE(definitions.size() == 1u);

    const auto& definition = definitions.front();
    CHECK(definition.name == "ob_eye");
    CHECK(definition.description == "myeye");
    CHECK(definition.color == Color{defaultColor});
    CHECK(definition.propertyDefinitions.empty());

    REQUIRE(getType(definition) == EntityDefinitionType::Point);
    CHECK(
      definition.pointEntityDefinition->bounds
      == vm::bbox3d{{-16.0, -16.0, 0.0}, {16.0, 16.0, 64.0}});
    CHECK(
      getModelSpecification(definition)
      == ModelSpecification{"models/mymodels/eye.md2", 0, 0});
    CHECK(
      definition.pointEntityDefinition->modelDefinition.scale(
        el::NullVariableStore{}, std::nullopt)
      == vm::vec3d{1, 1, 1});
  }

  SECTION("parses a definition with only the required fields")
  {
    const auto file = "ob_box|models/box.md2|1|1|1|general|-8|-8|-8|8|8|8";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    const auto definitions = parser.parseDefinitions(status).value();
    REQUIRE(definitions.size() == 1u);

    const auto& definition = definitions.front();
    CHECK(definition.name == "ob_box");
    CHECK(definition.description == "");
    CHECK(
      definition.pointEntityDefinition->bounds
      == vm::bbox3d{{-8.0, -8.0, -8.0}, {8.0, 8.0, 8.0}});
  }

  SECTION("parses the model scale")
  {
    const auto file = "ob_big|models/box.md2|2|2|3|general|-8|-8|-8|8|8|8";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    const auto definitions = parser.parseDefinitions(status).value();
    REQUIRE(definitions.size() == 1u);

    CHECK(
      definitions.front().pointEntityDefinition->modelDefinition.scale(
        el::NullVariableStore{}, std::nullopt)
      == vm::vec3d{2, 2, 3});
  }

  SECTION("converts backslashes in the model path")
  {
    const auto file = R"(ob_box|models\props\box.mda|1|1|1|general|-8|-8|-8|8|8|8)";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    const auto definitions = parser.parseDefinitions(status).value();
    REQUIRE(definitions.size() == 1u);

    CHECK(
      getModelSpecification(definitions.front())
      == ModelSpecification{"models/props/box.mda", 0, 0});
  }

  SECTION("omits the model if there is none")
  {
    const auto file = R"(ob_none|none|1|1|1|general|-8|-8|-8|8|8|8
ob_empty||1|1|1|general|-8|-8|-8|8|8|8)";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    const auto definitions = parser.parseDefinitions(status).value();
    REQUIRE(definitions.size() == 2u);

    CHECK(getModelSpecification(definitions[0]) == ModelSpecification{});
    CHECK(getModelSpecification(definitions[1]) == ModelSpecification{});
  }

  SECTION("repairs inverted bounds")
  {
    const auto file = "ob_box|models/box.md2|1|1|1|general|8|-8|8|-8|8|-8";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    const auto definitions = parser.parseDefinitions(status).value();
    REQUIRE(definitions.size() == 1u);

    CHECK(
      definitions.front().pointEntityDefinition->bounds
      == vm::bbox3d{{-8.0, -8.0, -8.0}, {8.0, 8.0, 8.0}});
  }

  SECTION("skips invalid lines")
  {
    const auto file = R"(ob_short|models/box.md2|1|1|1
ob_nan|models/box.md2|1|1|1|general|-8|-8|x|8|8|8
|models/box.md2|1|1|1|general|-8|-8|-8|8|8|8
ob_box|models/box.md2|1|1|1|general|-8|-8|-8|8|8|8)";

    auto parser = AnoxEntityDatParser{file, defaultColor};
    const auto definitions = parser.parseDefinitions(status).value();
    REQUIRE(definitions.size() == 1u);

    CHECK(definitions.front().name == "ob_box");
    CHECK(status.countStatus(LogLevel::Warn) == 3u);
  }
}

} // namespace tb::mdl
