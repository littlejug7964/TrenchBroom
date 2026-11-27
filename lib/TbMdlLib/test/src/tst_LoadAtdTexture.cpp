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
#include "fs/DiskFileSystem.h"
#include "fs/VirtualFileSystem.h"
#include "gl/Texture.h"
#include "mdl/CatchConfig.h"
#include "mdl/LoadAtdTexture.h"
#include "mdl/LoadTexture.h"

#include "kd/result.h"

#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace tb::mdl
{

TEST_CASE("LoadAtdTexture")
{
  SECTION("isAtdTexture")
  {
    CHECK(isAtdTexture("textures/anim/glow.atd"));
    CHECK(isAtdTexture("textures/anim/GLOW.ATD"));
    CHECK_FALSE(isAtdTexture("textures/anim/glow.tga"));
  }

  SECTION("parseAtd")
  {
    SECTION("parses bitmaps and frames")
    {
      CHECK(parseAtd(R"(ATD1
# a comment
type = animation
width=64
height = 64

!bitmap file = textures/anim/a.tga
!bitmap file=textures/anim/b.tga # another comment

!frame bitmap=1 next=1 wait=0.5
!frame
  bitmap = 0
  next = 0
  wait = 0.5
)") == Atd{"animation", {"textures/anim/a.tga", "textures/anim/b.tga"}, {1, 0}, {}});
    }

    SECTION("parses an interform texture")
    {
      CHECK(parseAtd(R"(ATD1
type        = Interform
width       = 64
height      = 64
mother      = textures/anim/mother.png
mother_move = scroll
father      = textures/anim/father.png
palette     = textures/anim/palette.png
)") == Atd{"interform", {}, {}, "textures/anim/mother.png"});
    }

    SECTION("returns an error if the header is missing")
    {
      CHECK(parseAtd("!bitmap file=a.tga\n!frame bitmap=0").is_error());
    }

    SECTION("returns an error for an incomplete bitmap")
    {
      CHECK(parseAtd("ATD1\n!bitmap\n!frame bitmap=0").is_error());
    }

    SECTION("returns an error for a frame with an invalid bitmap")
    {
      CHECK(parseAtd("ATD1\n!bitmap file=a.tga\n!frame bitmap=1").is_error());
      CHECK(parseAtd("ATD1\n!bitmap file=a.tga\n!frame wait=1").is_error());
    }
  }

  SECTION("loadTexture")
  {
    auto fs = fs::VirtualFileSystem{};
    fs.mount(
      "",
      std::make_unique<fs::DiskFileSystem>(getFixtureRoot() / "test/mdl/LoadAtdTexture"));

    SECTION("loads the first frame's bitmap")
    {
      // the first frame shows glow2.tga, which is referred to relative to the ATD
      const auto texture =
        loadTexture("textures/anim/glow.atd", "anim/glow", fs) | kdl::value();
      CHECK(texture.width() == 8u);
      CHECK(texture.height() == 4u);

      CHECK(
        findAtdImage(fs, "textures/anim/glow.atd")
        == Result<std::filesystem::path>{"textures/anim/glow2.tga"});
    }

    SECTION("finds bitmaps without an extension")
    {
      // noext.atd refers to its bitmap as textures/anim/glow1
      const auto texture =
        loadTexture("textures/anim/noext.atd", "anim/noext", fs) | kdl::value();
      CHECK(texture.width() == 4u);
    }

    SECTION("loads the mother image of an interform texture")
    {
      const auto texture =
        loadTexture("textures/anim/form.atd", "anim/form", fs) | kdl::value();
      CHECK(texture.width() == 4u);
      CHECK(texture.height() == 2u);
    }

    SECTION("returns an error if there is no image")
    {
      CHECK(loadTexture("textures/anim/noise.atd", "anim/noise", fs).is_error());
    }

    SECTION("returns an error if the bitmap is missing")
    {
      CHECK(loadTexture("textures/anim/missing.atd", "anim/missing", fs).is_error());
    }
  }
}

} // namespace tb::mdl
