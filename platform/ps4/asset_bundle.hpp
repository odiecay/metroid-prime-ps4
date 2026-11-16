// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
namespace prime {
// Stream-copy preconverted resources only. No conversion, decryption or game logic.
// Publish the complete directory by rename; never overwrite existing assets.
bool unpack_asset_bundle(const std::filesystem::path& bundle,const std::filesystem::path& destination);
}
