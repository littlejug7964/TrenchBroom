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

#include "mdl/LoadAtdTexture.h"

#include "fs/File.h"
#include "fs/FileSystem.h"
#include "fs/PathInfo.h"

#include "kd/path_utils.h"
#include "kd/reflection_impl.h"
#include "kd/result.h"
#include "kd/string_compare.h"
#include "kd/string_format.h"
#include "kd/string_utils.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <cctype>
#include <optional>
#include <string>

namespace tb::mdl
{

kdl_reflect_impl(Atd);

namespace
{

constexpr auto AtdMagic = std::string_view{"ATD1"};

/**
 * Splits an ATD file into tokens. Tokens are separated by whitespace and by '='. A token
 * that starts with # starts a comment that extends to the end of the line.
 */
std::vector<std::string_view> tokenize(const std::string_view str)
{
  auto tokens = std::vector<std::string_view>{};

  auto i = size_t(0);
  while (i < str.size())
  {
    const auto c = str[i];
    if (std::isspace(static_cast<unsigned char>(c)) || c == '=')
    {
      ++i;
    }
    else if (c == '#')
    {
      i = std::min(str.find('\n', i), str.size());
    }
    else
    {
      const auto end = std::min(str.find_first_of(" \t\r\n=", i), str.size());
      tokens.push_back(str.substr(i, end - i));
      i = end;
    }
  }

  return tokens;
}

/**
 * Returns the image that represents the given ATD: the mother image of an interform
 * texture, and the bitmap of the first frame of any other texture. Returns an empty path
 * if there is no such image.
 */
std::filesystem::path getAtdImage(const Atd& atd)
{
  if (atd.type == "interform")
  {
    return atd.mother;
  }

  if (atd.bitmaps.empty())
  {
    return {};
  }

  return atd.bitmaps[!atd.frames.empty() ? atd.frames.front() : 0];
}

} // namespace

bool isAtdTexture(const std::filesystem::path& path)
{
  return kdl::path_has_extension(kdl::path_to_lower(path), ".atd");
}

Result<Atd> parseAtd(const std::string_view str)
{
  if (!str.starts_with(AtdMagic))
  {
    return Error{"Missing ATD1 header"};
  }

  const auto tokens = tokenize(str.substr(AtdMagic.size()));

  auto atd = Atd{};
  for (size_t i = 0; i < tokens.size(); ++i)
  {
    if (kdl::ci::str_is_equal(tokens[i], "type") && i + 1 < tokens.size())
    {
      atd.type = kdl::str_to_lower(tokens[++i]);
    }
    else if (kdl::ci::str_is_equal(tokens[i], "mother") && i + 1 < tokens.size())
    {
      atd.mother = kdl::parse_path(std::string{tokens[++i]});
    }
    else if (kdl::ci::str_is_equal(tokens[i], "!bitmap"))
    {
      // !bitmap file=<path>
      if (
        i + 2 >= tokens.size() || !kdl::ci::str_is_equal(tokens[i + 1], "file")
        || tokens[i + 2].starts_with('!'))
      {
        return Error{fmt::format("Invalid bitmap {}", atd.bitmaps.size())};
      }
      atd.bitmaps.push_back(kdl::parse_path(std::string{tokens[i + 2]}));
      i += 2;
    }
    else if (kdl::ci::str_is_equal(tokens[i], "!frame"))
    {
      // !frame followed by key value pairs such as bitmap=<index> or wait=<seconds>
      auto bitmap = std::optional<size_t>{};
      while (i + 1 < tokens.size() && !tokens[i + 1].starts_with('!'))
      {
        if (kdl::ci::str_is_equal(tokens[i + 1], "bitmap") && i + 2 < tokens.size())
        {
          bitmap = kdl::str_to_size(tokens[i + 2]);
          ++i;
        }
        ++i;
      }

      if (!bitmap || *bitmap >= atd.bitmaps.size())
      {
        return Error{fmt::format("Invalid bitmap index in frame {}", atd.frames.size())};
      }
      atd.frames.push_back(*bitmap);
    }
  }

  return atd;
}

Result<std::filesystem::path> findAtdImage(
  const fs::FileSystem& fs, const std::filesystem::path& path)
{
  return fs.openFile(path) | kdl::and_then([](const auto& file) {
           return parseAtd(file->reader().buffer().stringView());
         })
         | kdl::and_then([&](const auto& atd) -> Result<std::filesystem::path> {
             const auto bitmap = getAtdImage(atd);
             if (bitmap.empty())
             {
               return Error{fmt::format("ATD '{}' has no image", path)};
             }

             if (isAtdTexture(bitmap))
             {
               return Error{fmt::format("ATD '{}' refers to another ATD", path)};
             }

             if (fs.pathInfo(bitmap) == fs::PathInfo::File)
             {
               return bitmap;
             }

             const auto relativeBitmap = path.parent_path() / bitmap;
             if (fs.pathInfo(relativeBitmap) == fs::PathInfo::File)
             {
               return relativeBitmap;
             }

             return Error{fmt::format("Could not find bitmap '{}'", bitmap)};
           });
}

} // namespace tb::mdl
