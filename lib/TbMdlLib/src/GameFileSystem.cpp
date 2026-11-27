/*
 Copyright (C) 2010 Kristian Duske

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

#include "mdl/GameFileSystem.h"

#include "base/Logger.h"
#include "fs/AnoxDatFileSystem.h"
#include "fs/DiskFileSystem.h"
#include "fs/DiskIO.h"
#include "fs/DkPakFileSystem.h"
#include "fs/IdPakFileSystem.h"
#include "fs/PathInfo.h"
#include "fs/TraversalMode.h"
#include "fs/WadFileSystem.h"
#include "fs/ZipFileSystem.h"
#include "mdl/EnvironmentConfig.h"
#include "mdl/GameConfig.h"

#include "kd/path_utils.h"
#include "kd/ranges/as_rvalue_view.h"
#include "kd/result_fold.h"
#include "kd/string_compare.h"
#include "kd/string_compare_natural.h"

#include <memory>
#include <ranges>

namespace tb::mdl
{

namespace
{

bool isAnachronoxPackageFormat(const std::string& packageFormat)
{
  return kdl::ci::str_is_equal(packageFormat, "anox");
}

bool isAnachronoxDataPackage(const std::filesystem::path& path)
{
  return kdl::path_has_extension(kdl::path_to_lower(path), ".dat");
}

/**
 * Returns the format of the package at the given path.
 *
 * Anachronox's file system mounts both its own data packages (.dat) and zip files.
 */
std::string packageFormatForPath(
  const std::string& packageFormat, const std::filesystem::path& path)
{
  if (isAnachronoxPackageFormat(packageFormat))
  {
    return isAnachronoxDataPackage(path) ? "anoxdat" : "zip";
  }
  return packageFormat;
}

/**
 * Returns the path at which a package is mounted in the game file system.
 *
 * Anachronox maps each data package to the directory named after the package's
 * lowercase file name stem, e.g., MODELS.dat is mounted at models/. All other packages
 * are mounted at the root.
 */
std::filesystem::path packageMountPoint(
  const std::string& packageFormat, const std::filesystem::path& path)
{
  if (kdl::ci::str_is_equal(packageFormat, "anoxdat"))
  {
    return kdl::path_to_lower(path.stem());
  }
  return {};
}

/**
 * Sorts the given packages into the order in which they are mounted. Packages that are
 * mounted later take precedence over packages that are mounted earlier.
 *
 * Packages are mounted in natural order of their file names. For Anachronox, the data
 * packages (.dat) are mounted first, so that the files in zip files, such as the
 * unofficial patch's anox1.zip, take precedence over them.
 */
void sortPackages(
  const std::string& packageFormat, std::vector<std::filesystem::path>& packagePaths)
{
  std::ranges::sort(packagePaths, kdl::ci::string_less_natural{}, [](const auto& p) {
    return p.string();
  });

  if (isAnachronoxPackageFormat(packageFormat))
  {
    std::ranges::stable_partition(packagePaths, isAnachronoxDataPackage);
  }
}

} // namespace

void GameFileSystem::initialize(
  const EnvironmentConfig& environmentConfig,
  const GameConfig& gameConfig,
  const std::filesystem::path& gamePath,
  const std::vector<std::filesystem::path>& additionalSearchPaths,
  Logger& logger)
{
  unmountAll();

  addDefaultAssetPaths(environmentConfig, gameConfig, logger);

  if (!gamePath.empty() && fs::Disk::pathInfo(gamePath) == fs::PathInfo::Directory)
  {
    addGameFileSystems(gameConfig, gamePath, additionalSearchPaths, logger);
  }
}

void GameFileSystem::reloadWads(
  const std::filesystem::path& rootPath,
  const std::vector<std::filesystem::path>& wadSearchPaths,
  const std::vector<std::filesystem::path>& wadPaths,
  Logger& logger)
{
  unmountWads();
  mountWads(rootPath, wadSearchPaths, wadPaths, logger);
}

void GameFileSystem::addDefaultAssetPaths(
  const EnvironmentConfig& environmentConfig, const GameConfig& config, Logger& logger)
{
  // There are two ways of providing default assets: The 'defaults/assets' folder in
  // TrenchBroom's resources folder, and the 'assets' folder in the game configuration
  // folders. We add filesystems for both types here.

  auto defaultFolderPaths = environmentConfig.defaultAssetFolderPaths;
  if (!config.path.empty())
  {
    defaultFolderPaths.push_back(config.path.parent_path());
  }

  for (const auto& defaultFolderPath : defaultFolderPaths)
  {
    const auto defaultAssetsPath = defaultFolderPath / std::filesystem::path("assets");
    if (fs::Disk::pathInfo(defaultAssetsPath) == fs::PathInfo::Directory)
    {
      addFileSystemPath(defaultAssetsPath, logger);
    }
  }
}

void GameFileSystem::addGameFileSystems(
  const GameConfig& config,
  const std::filesystem::path& gamePath,
  const std::vector<std::filesystem::path>& additionalSearchPaths,
  Logger& logger)
{
  const auto& fileSystemConfig = config.fileSystemConfig;
  addSearchPath(config, gamePath, fileSystemConfig.searchPath, logger);

  for (const auto& searchPath : additionalSearchPaths)
  {
    addSearchPath(config, gamePath, searchPath, logger);
  }
}

void GameFileSystem::addSearchPath(
  const GameConfig& config,
  const std::filesystem::path& gamePath,
  const std::filesystem::path& searchPath,
  Logger& logger)
{
  const auto fixedPath = fs::Disk::fixPath(gamePath / searchPath);
  if (isAnachronoxPackageFormat(config.fileSystemConfig.packageFormat.format))
  {
    // Anachronox gives loose files precedence over files in packages
    addFileSystemPackages(config, fixedPath, logger);
    addFileSystemPath(fixedPath, logger);
  }
  else
  {
    addFileSystemPath(fixedPath, logger);
    addFileSystemPackages(config, fixedPath, logger);
  }
}

void GameFileSystem::addFileSystemPath(const std::filesystem::path& path, Logger& logger)
{
  logger.info() << "Adding file system path " << path;
  mount("", std::make_unique<fs::DiskFileSystem>(path));
}

namespace
{
Result<std::unique_ptr<fs::FileSystem>> createImageFileSystem(
  const std::string& packageFormat, const std::filesystem::path& path)
{
  const auto setMetadataAndCast = [&](auto fs) {
    fs->setMetadata(fs::makeImageFileSystemMetadata(path));
    return std::unique_ptr<fs::FileSystem>{std::move(fs)};
  };

  if (kdl::ci::str_is_equal(packageFormat, "idpak"))
  {
    return fs::Disk::openFile(path) | kdl::and_then([&](auto file) {
             return fs::createImageFileSystem<fs::IdPakFileSystem>(std::move(file));
           })
           | kdl::transform(setMetadataAndCast);
  }
  else if (kdl::ci::str_is_equal(packageFormat, "dkpak"))
  {
    return fs::Disk::openFile(path) | kdl::and_then([&](auto file) {
             return fs::createImageFileSystem<fs::DkPakFileSystem>(std::move(file));
           })
           | kdl::transform(setMetadataAndCast);
  }
  else if (kdl::ci::str_is_equal(packageFormat, "anoxdat"))
  {
    return fs::Disk::openFile(path) | kdl::and_then([&](auto file) {
             return fs::createImageFileSystem<fs::AnoxDatFileSystem>(std::move(file));
           })
           | kdl::transform(setMetadataAndCast);
  }
  else if (kdl::ci::str_is_equal(packageFormat, "zip"))
  {
    return fs::Disk::openFile(path) | kdl::and_then([&](auto file) {
             return fs::createImageFileSystem<fs::ZipFileSystem>(std::move(file));
           })
           | kdl::transform(setMetadataAndCast);
  }
  return Error{"Unknown package format: " + packageFormat};
}
} // namespace

void GameFileSystem::addFileSystemPackages(
  const GameConfig& config, const std::filesystem::path& searchPath, Logger& logger)
{
  const auto& fileSystemConfig = config.fileSystemConfig;
  const auto& packageFormatConfig = fileSystemConfig.packageFormat;

  const auto& packageExtensions = packageFormatConfig.extensions;
  const auto& packageFormat = packageFormatConfig.format;

  if (fs::Disk::pathInfo(searchPath) == fs::PathInfo::Directory)
  {
    fs::Disk::find(
      searchPath,
      fs::TraversalMode::Flat,
      fs::makeExtensionPathMatcher(packageExtensions))
      | kdl::and_then([&](auto packagePaths) {
          sortPackages(packageFormat, packagePaths);
          return packagePaths | kdl::views::as_rvalue
                 | std::views::transform([&](auto absPackagePath) {
                     const auto actualPackageFormat =
                       packageFormatForPath(packageFormat, absPackagePath);
                     return createImageFileSystem(actualPackageFormat, absPackagePath)
                            | kdl::transform([&](auto fs) {
                                const auto mountPoint =
                                  packageMountPoint(actualPackageFormat, absPackagePath);
                                logger.info()
                                  << "Adding file system package "
                                  << absPackagePath.lexically_relative(searchPath);
                                mount(mountPoint, std::move(fs));
                              });
                   })
                 | kdl::fold;
        })
      | kdl::transform_error([&](auto e) {
          logger.error() << "Could not add file system packages: " << e.msg;
        });
  }
}

void GameFileSystem::mountWads(
  const std::filesystem::path& rootPath,
  const std::vector<std::filesystem::path>& wadSearchPaths,
  const std::vector<std::filesystem::path>& wadPaths,
  Logger& logger)
{
  for (const auto& wadPath : wadPaths)
  {
    const auto resolvedWadPath = fs::Disk::resolvePath(wadSearchPaths, wadPath);
    fs::Disk::openFile(resolvedWadPath) | kdl::and_then([](auto file) {
      return fs::createImageFileSystem<fs::WadFileSystem>(std::move(file));
    }) | kdl::transform([&](auto fs) {
      fs->setMetadata(fs::makeImageFileSystemMetadata(resolvedWadPath));
      m_wadMountPoints.push_back(mount(rootPath, std::move(fs)));
    }) | kdl::transform_error([&](auto e) {
      logger.error() << "Could not load wad file at '" << wadPath << "': " << e.msg;
    });
  }
}

void GameFileSystem::unmountWads()
{
  for (const auto& id : m_wadMountPoints)
  {
    unmount(id);
  }
  m_wadMountPoints.clear();
}

} // namespace tb::mdl
