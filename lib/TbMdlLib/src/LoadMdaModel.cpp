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
        mda.baseModel = kdl::parse_path(std::string{next().value});
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
        map = kdl::parse_path(std::string{next().value});

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

const MdaProfile* defaultProfile(const Mda& mda)
{
  const auto it = std::ranges::find_if(mda.profiles, [](const auto& profile) {
    return profile.tag.empty() || kdl::ci::str_is_equal(profile.tag, "DFLT");
  });
  return it != mda.profiles.end() ? &*it
         : !mda.profiles.empty()  ? &mda.profiles.front()
                                  : nullptr;
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

Result<EntityModelData> loadMdaModel(
  std::string name, fs::Reader reader, const fs::FileSystem& fs, Logger& logger)
{
  return parseMda(reader.buffer().stringView()) | kdl::and_then([&](const auto& mda) {
           const auto* profile = defaultProfile(mda);
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
                        std::move(name), baseModelReader, fs, logger, skins);
                    });
         });
}

} // namespace tb::mdl
