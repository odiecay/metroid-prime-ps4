#include "gx/pipeline.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    using namespace aurora::gx;
    unsigned cases = 0;
    for (bool potential : {false, true}) {
        for (unsigned pbr : {0u, 1u, 3u}) {
            for (unsigned decal : {0u, 1u}) {
                for (unsigned pass : {0u, 1u, 2u}) {
                    for (bool shadow : {false, true}) {
                        PipelineConfig config{};
                        config.shaderConfig.pbr = pbr;
                        config.shaderConfig.actorDecal = decal;
                        config.shaderConfig.shadow = shadow;
                        config.shadowPass = pass;
                        const auto before = config;
                        const bool preserved = potential || pbr != 0 || decal != 0;
                        const bool accepted = normalize_cached_sun_shadow(config, potential);
                        assert(accepted == (preserved || pass == 0));
                        auto expected = before;
                        if (!preserved && pass == 0) expected.shaderConfig.shadow = false;
                        assert(std::memcmp(&config, &expected, sizeof(config)) == 0);
                        ++cases;
                    }
                }
            }
        }
    }
    std::printf("cached_sun_policy_cases=%u retail colour variants normalized; PBR/actor bytes preserved\n", cases);
}
