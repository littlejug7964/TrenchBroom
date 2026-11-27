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
#include "fs/DiskFileSystem.h"
#include "fs/DiskIO.h"
#include "fs/VirtualFileSystem.h"
#include "gl/Material.h"
#include "gl/Texture.h"
#include "mdl/CatchConfig.h"
#include "mdl/EntityModel.h"
#include "mdl/LoadMdaModel.h"

#include "kd/result.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{

TEST_CASE("LoadMdaModel")
{
  SECTION("parseMda")
  {
    SECTION("parses the base model and the profiles")
    {
      CHECK(
        parseMda(R"(MDA1
# a comment
basemodel models/test/model.md2
headtri 1 2 3

profile {
  evaluate "true" # comment
  skin {
    pass {
      map models/test/skin_a
      alphafunc ge128
    }
  }
  skin {
    pass {
      uvgen sphere
    }
    pass {
      map "models/test/skin_b.png"
    }
    pass {
      map models/test/skin_c.tga
    }
  }
  skin {
    pass { blendmode add }
  }
}

profile RED{
  skin{pass{map models/test/red.tga}}
}

# ABSOLUTELY do not modify the following! It is binary data used by Anachronox.
$ANIP:14:14:12345678
&0123456789ABCDEF./
)")
        == Mda{
          "models/test/model.md2",
          {
            {"", {"models/test/skin_a.tga", "models/test/skin_b.png", ""}},
            {"RED", {"models/test/red.tga"}},
          },
        });
    }

    SECTION("returns an error if the header is missing")
    {
      CHECK(parseMda("basemodel models/test/model.md2").is_error());
    }

    SECTION("returns an error if the base model is missing")
    {
      CHECK(parseMda("MDA1\nprofile { skin { pass { map a.tga } } }").is_error());
    }

    SECTION("returns an error for an unterminated profile")
    {
      CHECK(parseMda("MDA1\nbasemodel a.md2\nprofile { skin { pass {").is_error());
    }
  }

  SECTION("parses backslashes in paths")
  {
    // MSVC mangles raw strings that contain backslashes in macro arguments
    const auto file = R"(MDA1
basemodel models\test\model.md2
profile { skin { pass { map "models\test\skin_a" } } }
)";

    CHECK(
      parseMda(file) == Mda{"models/test/model.md2", {{"", {"models/test/skin_a.tga"}}}});
  }

  SECTION("parseMdaProfilePath")
  {
    CHECK(
      parseMdaProfilePath("models/test/model.mda!HURT")
      == MdaProfilePath{"models/test/model.mda", "HURT"});
    CHECK(
      parseMdaProfilePath("models/test/MODEL.MDA!dflt")
      == MdaProfilePath{"models/test/MODEL.MDA", "dflt"});
    CHECK(parseMdaProfilePath("models/test/model.mda") == std::nullopt);
    CHECK(parseMdaProfilePath("models/test/model.md2!HURT") == std::nullopt);
    CHECK(parseMdaProfilePath("models/te!st/model.mda") == std::nullopt);
  }

  SECTION("canLoadMdaModel")
  {
    const auto canLoad = [](
                           const std::filesystem::path& path,
                           const std::string_view str) {
      return canLoadMdaModel(path, fs::Reader::from(str.data(), str.data() + str.size()));
    };

    CHECK(canLoad("model.mda", "MDA1\n"));
    CHECK(canLoad("MODEL.MDA", "MDA1\n"));
    CHECK_FALSE(canLoad("model.md2", "MDA1"));
    CHECK_FALSE(canLoad("model.mda", "MDA"));
    CHECK_FALSE(canLoad("model.mda", "IDP2"));
  }

  SECTION("loadMdaModel")
  {
    const auto fixturePath = getFixtureRoot() / "test/mdl/LoadAnoxMd2Model";

    auto logger = TestLogger{};
    auto fs = fs::VirtualFileSystem{};
    fs.mount("", std::make_unique<fs::DiskFileSystem>(fixturePath));

    SECTION("applies the skins of the default profile")
    {
      const auto file =
        fs::Disk::openFile(fixturePath / "models/test/model.mda") | kdl::value();
      const auto modelData =
        loadMdaModel(file->reader().buffer(), fs, logger) | kdl::value();

      CHECK(modelData.frameCount() == 1u);
      REQUIRE(modelData.surfaceCount() == 2u);

      CHECK(modelData.surface(0).name() == "profile_a");
      CHECK(modelData.surface(0).skin(0u)->texture()->width() == 4u);
      CHECK(modelData.surface(1).name() == "profile_b");
      CHECK(modelData.surface(1).skin(0u)->texture()->width() == 2u);
    }

    SECTION("applies the skins of the profile with the given tag")
    {
      const auto file =
        fs::Disk::openFile(fixturePath / "models/test/model.mda") | kdl::value();
      const auto modelData =
        loadMdaModel(file->reader().buffer(), fs, logger, "red") | kdl::value();

      REQUIRE(modelData.surfaceCount() == 2u);
      CHECK(modelData.surface(0).name() == "skin_a");
      CHECK(modelData.surface(0).skin(0u)->texture()->width() == 16u);

      // the profile has no skin for the second surface, so the model's skin is used
      CHECK(modelData.surface(1).name() == "skin_b");
    }

    SECTION("applies the skins of the default profile if the tag is unknown")
    {
      const auto file =
        fs::Disk::openFile(fixturePath / "models/test/model.mda") | kdl::value();
      const auto modelData =
        loadMdaModel(file->reader().buffer(), fs, logger, "XXXX") | kdl::value();

      REQUIRE(modelData.surfaceCount() == 2u);
      CHECK(modelData.surface(0).name() == "profile_a");
    }

    SECTION("returns an error if the base model is missing")
    {
      const auto file =
        fs::Disk::openFile(fixturePath / "models/test/no_basemodel.mda") | kdl::value();
      CHECK(loadMdaModel(file->reader().buffer(), fs, logger).is_error());
    }
  }
}

} // namespace tb::mdl
