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

#include "TestEnvironment.h"
#include "TestLogger.h"
#include "base/Logger.h"
#include "fs/DiskFileSystem.h"
#include "fs/DiskIO.h"
#include "fs/VirtualFileSystem.h"
#include "gl/Material.h"
#include "gl/Texture.h"
#include "mdl/CatchConfig.h"
#include "mdl/EntityModel.h"
#include "mdl/LoadAnoxMd2Model.h"
#include "mdl/LoadMd2Model.h"

#include "kd/result.h"

#include "vm/bbox.h"
#include "vm/bbox_io.h" // IWYU pragma: keep

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::mdl
{

TEST_CASE("loadAnoxMd2Model")
{
  /*
  The fixture models have six vertices and two skins. The first skin draws a triangle
  strip over the vertices 0, 1, 2, and the second skin draws a triangle fan over the
  vertices 3, 4, 5. The vertex positions are (0 0 0), (8 0 0), (8 8 0), (0 0 4), (8 0 4)
  and (8 8 4).

  The first frame, stand_001, is scaled by 1 and offset by (-4 -4 0), the second frame,
  stand_002, is scaled by 2 and offset by (0 0 16).
  */

  const auto fixturePath = getFixtureRoot() / "test/mdl/LoadAnoxMd2Model";

  auto logger = TestLogger{};
  auto fs = fs::VirtualFileSystem{};
  fs.mount("", std::make_unique<fs::DiskFileSystem>(fixturePath));

  const auto openModel = [&](const std::string& name) {
    return fs::Disk::openFile(fixturePath / "models/test" / name) | kdl::value();
  };

  SECTION("canLoadAnoxMd2Model")
  {
    const auto file = openModel("res2.md2");
    CHECK(canLoadAnoxMd2Model("res2.md2", file->reader()));
    CHECK(canLoadAnoxMd2Model("RES2.MD2", file->reader()));
    CHECK_FALSE(canLoadAnoxMd2Model("res2.mdl", file->reader()));

    // Quake 2 models aren't Anachronox models and vice versa
    const auto q2File =
      fs::Disk::openFile(getFixtureRoot() / "test/mdl/LoadMd2Model/cube.md2")
      | kdl::value();
    CHECK_FALSE(canLoadAnoxMd2Model("cube.md2", q2File->reader()));
    CHECK_FALSE(canLoadMd2Model("res2.md2", file->reader()));
  }

  SECTION("loads all resolutions")
  {
    const auto modelName =
      GENERATE(as<std::string>{}, "res0.md2", "res1.md2", "res2.md2");
    CAPTURE(modelName);

    const auto file = openModel(modelName);
    const auto modelData =
      loadAnoxMd2Model("models/test/" + modelName, file->reader().buffer(), fs, logger)
      | kdl::value();

    CHECK(modelData.pitchType() == PitchType::Normal);
    CHECK(modelData.orientation() == Orientation::Oriented);

    REQUIRE(modelData.frameCount() == 2u);
    CHECK(modelData.frame(0)->name() == "stand_001");
    CHECK(modelData.frame(0)->bounds() == vm::bbox3f{{-4, -4, 0}, {4, 4, 4}});
    CHECK(modelData.frame(1)->name() == "stand_002");
    CHECK(modelData.frame(1)->bounds() == vm::bbox3f{{0, 0, 16}, {16, 16, 24}});

    REQUIRE(modelData.surfaceCount() == 2u);
    for (const auto& surface : modelData.surfaces())
    {
      CHECK(surface.frameCount() == 2u);
      CHECK(surface.skinCount() == 1u);
    }
  }

  SECTION("loads one skin per surface")
  {
    const auto file = openModel("res2.md2");
    const auto modelData =
      loadAnoxMd2Model("models/test/res2.md2", file->reader().buffer(), fs, logger)
      | kdl::value();

    REQUIRE(modelData.surfaceCount() == 2u);

    const auto& surface1 = modelData.surface(0);
    CHECK(surface1.name() == "skin_a");
    REQUIRE(surface1.skinCount() == 1u);
    CHECK(surface1.skin(0u)->texture()->width() == 16u);

    // the model refers to skin_b.tga, but the skin is a PNG file
    const auto& surface2 = modelData.surface(1);
    CHECK(surface2.name() == "skin_b");
    REQUIRE(surface2.skinCount() == 1u);
    CHECK(surface2.skin(0u)->texture()->width() == 8u);

    CHECK(logger.countMessages(LogLevel::Error) == 0);
  }

  SECTION("finds skins next to the model")
  {
    // bare_skins.md2 refers to its skins as SKIN_A.BMP and models\test\skin_b.bmp
    const auto file = openModel("bare_skins.md2");
    const auto modelData =
      loadAnoxMd2Model("models/test/bare_skins.md2", file->reader().buffer(), fs, logger)
      | kdl::value();

    REQUIRE(modelData.surfaceCount() == 2u);
    CHECK(modelData.surface(0).skin(0u)->texture()->width() == 16u);
    CHECK(modelData.surface(1).skin(0u)->texture()->width() == 8u);
    CHECK(logger.countMessages(LogLevel::Error) == 0);
  }

  SECTION("replaces skins")
  {
    const auto file = openModel("res2.md2");
    const auto modelData = loadAnoxMd2Model(
                             "models/test/res2.md2",
                             file->reader().buffer(),
                             fs,
                             logger,
                             {"models/test/profile_a.tga"})
                           | kdl::value();

    REQUIRE(modelData.surfaceCount() == 2u);
    CHECK(modelData.surface(0).name() == "profile_a");
    CHECK(modelData.surface(0).skin(0u)->texture()->width() == 4u);
    CHECK(modelData.surface(1).name() == "skin_b");
  }

  SECTION("returns an error for an unknown resolution")
  {
    const auto file = openModel("unknown_resolution.md2");
    CHECK(
      loadAnoxMd2Model(
        "models/test/unknown_resolution.md2", file->reader().buffer(), fs, logger)
      == Result<EntityModelData>{Error{"Unknown Anachronox MD2 model resolution"}});
  }

  SECTION("returns an error if the primitive counts exceed the primitives")
  {
    const auto file = openModel("too_many_primitives.md2");
    CHECK(
      loadAnoxMd2Model(
        "models/test/too_many_primitives.md2", file->reader().buffer(), fs, logger)
      == Result<EntityModelData>{
        Error{"Primitive counts add up to 3, but model has only 2 primitives"}});
  }
}

} // namespace tb::mdl
