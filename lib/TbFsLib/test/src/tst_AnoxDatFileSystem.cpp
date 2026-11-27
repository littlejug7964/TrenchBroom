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

#include "Matchers.h"
#include "TestEnvironment.h"
#include "fs/AnoxDatFileSystem.h"
#include "fs/DiskIO.h"
#include "fs/File.h" // IWYU pragma: keep
#include "fs/PathInfo.h"
#include "fs/TestUtils.h"
#include "fs/TraversalMode.h"

#include "kd/result.h"

#include <filesystem>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace tb::fs
{

TEST_CASE("AnoxDatFileSystem")
{
  const auto fsTestPath = getFixtureRoot() / "test/fs/Dat/";

  SECTION("doReadDirectory")
  {
    SECTION("reads stored and compressed entries")
    {
      // anoxdat_entries.dat contains
      // - "plain.txt": a stored entry containing "hello"
      // - "compressed.txt": a zlib compressed entry
      // - "corrupt.bin": an entry claiming to be compressed, but containing garbage
      // - "wrong_size.bin": a valid zlib stream that decompresses to more bytes than
      //   the entry's uncompressed size
      // - "Mixed\Case\Name.TXT": a stored entry whose name uses backslashes
      auto fs = openFS<AnoxDatFileSystem>(fsTestPath / "anoxdat_entries.dat");

      const auto plain = fs->openFile("plain.txt") | kdl::value();
      auto plainReader = plain->reader();
      CHECK(plainReader.readString(plainReader.size()) == "hello");

      auto expectedCompressedContent = std::string{};
      for (size_t i = 0; i < 8; ++i)
      {
        expectedCompressedContent += "hello compressed ";
      }

      const auto compressed = fs->openFile("compressed.txt") | kdl::value();
      auto compressedReader = compressed->reader();
      CHECK(
        compressedReader.readString(compressedReader.size())
        == expectedCompressedContent);

      CHECK(fs->openFile("corrupt.bin").is_error());
      CHECK(fs->openFile("wrong_size.bin").is_error());
    }

    SECTION("converts backslashes in entry names to path separators")
    {
      const std::shared_ptr<FileSystem> fs =
        openFS<AnoxDatFileSystem>(fsTestPath / "anoxdat_entries.dat");

      CHECK(fs->pathInfo("Mixed") == PathInfo::Directory);
      CHECK(fs->pathInfo("mixed/case") == PathInfo::Directory);
      CHECK(fs->pathInfo("mixed/case/name.txt") == PathInfo::File);

      CHECK_THAT(
        fs->find("Mixed", TraversalMode::Recursive),
        MatchesPathsResult({
          "Mixed/Case",
          "Mixed/Case/Name.TXT",
        }));
    }

    SECTION("returns an error if the magic is wrong")
    {
      const auto file =
        Disk::openFile(fsTestPath / "anoxdat_wrong_magic.dat") | kdl::value();
      CHECK(createImageFileSystem<AnoxDatFileSystem>(file).is_error());
    }

    SECTION("returns an error if the version is unknown")
    {
      const auto file =
        Disk::openFile(fsTestPath / "anoxdat_wrong_version.dat") | kdl::value();
      CHECK(createImageFileSystem<AnoxDatFileSystem>(file).is_error());
    }

    SECTION("returns an error if the directory cannot be read")
    {
      // anoxdat_truncated.dat claims a one entry directory starting right after the
      // header, but the file ends there
      const auto file =
        Disk::openFile(fsTestPath / "anoxdat_truncated.dat") | kdl::value();
      CHECK(createImageFileSystem<AnoxDatFileSystem>(file).is_error());
    }
  }
}

} // namespace tb::fs
