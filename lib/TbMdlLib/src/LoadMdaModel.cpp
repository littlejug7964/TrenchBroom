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

#include "mdl/LoadMdaModel.h"

#include "fs/File.h"
#include "fs/FileSystem.h"
#include "mdl/LoadAnoxMd2Model.h"

#include "kd/k.h"
#include "kd/path_utils.h"
#include "kd/reflection_impl.h"
#include "kd/result.h"
#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <algorithm>
#include <cctype>
#include <optional>

namespace tb::mdl
{

kdl_reflect_impl(MdaProfile);

kdl_reflect_impl(Mda);

kdl_reflect_impl(MdaProfilePath);

namespace
{

constexpr auto MdaMagic = std::string_view{"MDA1"};

struct MdaToken
{
  std::string_view value;
  size_t line;
};

/**
 * Splits an MDA file into tokens. Tokens are separated by whitespace, braces are
 * tokens of their own and double quotes enclose a single token. A # starts a comment
 * that extends to the end of the line.
 */
std::vector<MdaToken> tokenize(const std::string_view str)
{
  auto tokens = std::vector<MdaToken>{};

  auto line = size_t(1);
  auto i = size_t(0);
  while (i < str.size())
  {
    const auto c = str[i];
    if (c == '\n')
    {
      ++line;
      ++i;
    }
    else if (std::isspace(static_cast<unsigned char>(c)))
    {
      ++i;
    }
    else if (c == '#')
    {
      i = std::min(str.find('\n', i), str.size());
    }
    else if (c == '{' || c == '}')
    {
      tokens.push_back({str.substr(i, 1), line});
      ++i;
    }
    else if (c == '"')
    {
      const auto end = std::min(str.find('"', i + 1), str.size());
      tokens.push_back({str.substr(i + 1, end - i - 1), line});
      i = std::min(end + 1, str.size());
    }
    else
    {
      const auto end = std::min(str.find_first_of(" \t\r\n{}\"#", i), str.size());
      tokens.push_back({str.substr(i, end - i), line});
      i = end;
    }
  }

  return tokens;
}

/**
 * Paths in MDA files may use backslashes as separators.
 */
std::filesystem::path parsePath(const std::string_view str)
{
  return kdl::parse_utf8_or_latin1_path(std::string{str}, K(convert_separators));
}

class MdaParser
{
private:
  std::vector<MdaToken> m_tokens;
  size_t m_index = 0;

public:
  explicit MdaParser(const std::string_view str)
    : m_tokens{tokenize(str)}
  {
  }

  Result<Mda> parse()
  {
    if (eof() || peek().value != MdaMagic)
    {
      return Error{"Missing MDA1 header"};
    }
    next();

    auto mda = Mda{};
    while (!eof())
    {
      const auto token = next();
      if (kdl::ci::str_is_equal(token.value, "basemodel") && !eof())
      {
        mda.baseModel = parsePath(next().value);
      }
      else if (kdl::ci::str_is_equal(token.value, "profile"))
      {
        if (auto profile = parseProfile())
        {
          mda.profiles.push_back(std::move(*profile));
        }
        else
        {
          return Error{fmt::format("Invalid profile at line {}", token.line)};
        }
      }
      else
      {
        // other directives, such as headtri, and the encoded binary data ($ and &) are
        // not needed
        skipStatement(token);
      }
    }

    if (mda.baseModel.empty())
    {
      return Error{"Missing base model"};
    }

    return mda;
  }

private:
  bool eof() const { return m_index >= m_tokens.size(); }

  const MdaToken& peek() const { return m_tokens[m_index]; }

  MdaToken next() { return m_tokens[m_index++]; }

  bool expect(const std::string_view value)
  {
    if (!eof() && peek().value == value)
    {
      next();
      return true;
    }
    return false;
  }

  /**
   * Skips the remainder of the statement started by the given token, i.e., the rest of
   * the token's line and any block that starts on it. Stops at a closing brace that
   * ends the enclosing block.
   */
  void skipStatement(const MdaToken& token)
  {
    auto depth = token.value == "{" ? size_t(1) : size_t(0);
    while (!eof() && (depth > 0 || (peek().line == token.line && peek().value != "}")))
    {
      const auto current = next();
      if (current.value == "{")
      {
        ++depth;
      }
      else if (current.value == "}" && depth > 0)
      {
        --depth;
      }
    }
  }

  std::optional<MdaProfile> parseProfile()
  {
    auto profile = MdaProfile{};
    if (!eof() && peek().value != "{")
    {
      profile.tag = next().value;
    }

    if (!expect("{"))
    {
      return std::nullopt;
    }

    while (!eof() && peek().value != "}")
    {
      const auto token = next();
      if (kdl::ci::str_is_equal(token.value, "skin"))
      {
        if (auto skin = parseSkin())
        {
          profile.skins.push_back(std::move(*skin));
        }
        else
        {
          return std::nullopt;
        }
      }
      else
      {
        skipStatement(token);
      }
    }

    return expect("}") ? std::optional{std::move(profile)} : std::nullopt;
  }

  std::optional<std::filesystem::path> parseSkin()
  {
    if (!expect("{"))
    {
      return std::nullopt;
    }

    auto skin = std::filesystem::path{};
    while (!eof() && peek().value != "}")
    {
      const auto token = next();
      if (kdl::ci::str_is_equal(token.value, "pass"))
      {
        if (auto map = parsePass())
        {
          // the first pass with a texture determines the skin
          if (skin.empty())
          {
            skin = std::move(*map);
          }
        }
        else
        {
          return std::nullopt;
        }
      }
      else
      {
        skipStatement(token);
      }
    }

    return expect("}") ? std::optional{std::move(skin)} : std::nullopt;
  }

  /**
   * Returns the texture of the pass, or an empty path if the pass has no texture.
   */
  std::optional<std::filesystem::path> parsePass()
  {
    if (!expect("{"))
    {
      return std::nullopt;
    }

    auto map = std::filesystem::path{};
    while (!eof() && peek().value != "}")
    {
      const auto token = next();
      if (kdl::ci::str_is_equal(token.value, "map") && !eof())
      {
        map = parsePath(next().value);

        // the game assumes a TGA image if the texture has no extension
        if (!map.has_extension())
        {
          map = kdl::path_add_extension(map, ".tga");
        }
      }
      else
      {
        skipStatement(token);
      }
    }

    return expect("}") ? std::optional{std::move(map)} : std::nullopt;
  }
};

/**
 * Returns the profile with the given tag. If no tag is given or there is no profile with
 * that tag, returns the default profile, which is either untagged or tagged DFLT, or the
 * first profile if there is no default profile.
 */
const MdaProfile* selectProfile(const Mda& mda, const std::string_view tag)
{
  const auto findProfile = [&](const auto& predicate) -> const MdaProfile* {
    const auto it = std::ranges::find_if(mda.profiles, predicate);
    return it != mda.profiles.end() ? &*it : nullptr;
  };

  if (!tag.empty())
  {
    if (const auto* profile = findProfile([&](const auto& p) {
          return kdl::ci::str_is_equal(p.tag, tag);
        }))
    {
      return profile;
    }
  }

  if (const auto* profile = findProfile([](const auto& p) {
        return p.tag.empty() || kdl::ci::str_is_equal(p.tag, "DFLT");
      }))
  {
    return profile;
  }

  return !mda.profiles.empty() ? &mda.profiles.front() : nullptr;
}

} // namespace

Result<Mda> parseMda(const std::string_view str)
{
  return MdaParser{str}.parse();
}

bool canLoadMdaModel(const std::filesystem::path& path, fs::Reader reader)
{
  if (!kdl::path_has_extension(kdl::path_to_lower(path), ".mda"))
  {
    return false;
  }

  return reader.canRead(MdaMagic.size())
         && reader.readString(MdaMagic.size()) == MdaMagic;
}

std::optional<MdaProfilePath> parseMdaProfilePath(const std::filesystem::path& path)
{
  const auto filename = path.filename().string();
  const auto separator = filename.rfind('!');
  if (separator == std::string::npos)
  {
    return std::nullopt;
  }

  const auto mdaFilename = std::filesystem::path{filename.substr(0, separator)};
  if (!kdl::path_has_extension(kdl::path_to_lower(mdaFilename), ".mda"))
  {
    return std::nullopt;
  }

  return MdaProfilePath{path.parent_path() / mdaFilename, filename.substr(separator + 1)};
}

Result<EntityModelData> loadMdaModel(
  fs::Reader reader,
  const fs::FileSystem& fs,
  Logger& logger,
  const std::string_view profileTag)
{
  return parseMda(reader.buffer().stringView()) | kdl::and_then([&](const auto& mda) {
           const auto* profile = selectProfile(mda, profileTag);
           const auto skins =
             profile ? profile->skins : std::vector<std::filesystem::path>{};

           return fs.openFile(mda.baseModel)
                  | kdl::and_then([&](const auto& file) -> Result<EntityModelData> {
                      auto baseModelReader = file->reader().buffer();
                      if (!canLoadAnoxMd2Model(mda.baseModel, baseModelReader))
                      {
                        return Error{
                          fmt::format("Unsupported base model '{}'", mda.baseModel)};
                      }
                      return loadAnoxMd2Model(
                        mda.baseModel, baseModelReader, fs, logger, skins);
                    });
         });
}

} // namespace tb::mdl
