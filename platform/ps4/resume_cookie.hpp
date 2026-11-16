// SPDX-License-Identifier: MIT
#pragma once
#include "port_hash.h"
#include <cstdio>
#include <string>
#include <string_view>
namespace prime {
inline std::string resume_cookie(std::string_view bytes) {
    if (bytes.empty()) return {};
    char token[96];
    std::snprintf(token,sizeof(token),"PRIME_SAFE_RESUME_V1 %016llx %zu",
        static_cast<unsigned long long>(port::Fnv1a64(bytes)),bytes.size());
    return token;
}
inline bool resume_cookie_matches(std::string_view bytes,std::string_view token) {
    return !bytes.empty() && token==resume_cookie(bytes);
}
}
