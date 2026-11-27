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
#include "fs/ImageFileSystem.h"

namespace tb::fs
{
class CFile;

/**
 * Reads Anachronox data packages (ADAT, version 9), e.g. MODELS.dat.
 *
 * Entry names are relative to the directory the game maps the package to, i.e., the
 * package's lowercase file name stem. This file system does not add that prefix; the
 * caller is expected to mount it at the right location.
 */
class AnoxDatFileSystem : public ImageFileSystem<CFile>
{
public:
  using ImageFileSystem::ImageFileSystem;

private:
  Result<void> doReadDirectory() override;
};
} // namespace tb::fs
