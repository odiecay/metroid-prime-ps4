// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <string>
namespace prime {
struct CandidateProfile {
    std::string key;
    std::filesystem::path root;
    bool remastered{},enhanced{};
    std::filesystem::path assetsRoot;
};
// Missing descriptor is the legacy game, never an implicit migration.
CandidateProfile read_candidate_profile(const std::filesystem::path& descriptor,
                                        const std::filesystem::path& dataRoot,
                                        const std::filesystem::path& appRoot = "/app0");
void prepare_candidate_storage(const CandidateProfile& profile,
                               const std::filesystem::path& baselineUser);
bool candidate_assets_ready(const CandidateProfile& profile);
void apply_candidate_environment(const CandidateProfile& profile);
}
