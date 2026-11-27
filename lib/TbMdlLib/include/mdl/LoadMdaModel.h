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

#include "base/Result.h"
#include "fs/Reader.h"
#include "mdl/EntityModel.h"

#include "kd/reflection_decl.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tb
{
class Logger;

namespace fs
{
class FileSystem;
} // namespace fs

namespace mdl
{

/**
 * An Anachronox model description (MDA). It refers to a base model and contains one or
 * more profiles. Every profile describes how to render each of the base model's skins.
 */
struct MdaProfile
{
  /** The profile's tag, empty for the default profile. */
  std::string tag;

  /**
   * For every skin of the base model, the texture of its first pass, or an empty path if
   * that skin has no texture.
   */
  std::vector<std::filesystem::path> skins;

  kdl_reflect_decl(MdaProfile, tag, skins);
};

struct Mda
{
  std::filesystem::path baseModel;
  std::vector<MdaProfile> profiles;

  kdl_reflect_decl(Mda, baseModel, profiles);
};

// exposed for testing
Result<Mda> parseMda(std::string_view str);

bool canLoadMdaModel(const std::filesystem::path& path, fs::Reader reader);

/**
 * Loads the base model of an Anachronox model description and applies the skins of its
 * default profile. Only Anachronox MD2 base models are supported.
 */
Result<EntityModelData> loadMdaModel(
  fs::Reader reader, const fs::FileSystem& fs, Logger& logger);

} // namespace mdl
} // namespace tb
