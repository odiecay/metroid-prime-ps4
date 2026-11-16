// SPDX-License-Identifier: MIT
// Real upstream nested scopes; capture API bytes without claiming GPU rendering.
#include <port_fx_debug.h>
#include <cassert>
#include <type_traits>

static std::vector<unsigned> states;
static_assert(std::is_same_v<decltype(&GXPortSetParticleFog),void(*)(u8)>);
extern "C" void GXPortSetParticleFog(u8 on) { states.push_back(on); }
namespace PortFx {
bool gTiming=false;
std::atomic<int64_t> gRenderNs{};
}
int main() {
    {
        PortFx::RenderScope parent;
        { PortFx::RenderScope unfogged(true);{ PortFx::RenderScope inherited(true); } }
    }
    assert((states==std::vector<unsigned>{1,2,1,0}));states.clear();
    {
        PortFx::RenderScope parent(true);
        { PortFx::RenderScope fogged; }
    }
    assert((states==std::vector<unsigned>{2,1,2,0}));
}
