// Compare both production algorithms. Their complete source bodies, and the
// real ByteBuffer/ArrayRef classes, are supplied by the Python test driver.
#include <dolphin/gx/GXEnum.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <vector>

#define UNLIKELY [[unlikely]]
#define FATAL(...) std::abort()
namespace aurora {
#include "actual_index_buffers.inc"
}
using namespace aurora;
namespace reference {
#include "actual_reference_index.inc"
}
namespace candidate {
#include "actual_candidate_index.inc"
}

int main(){
 ByteBuffer referenceBytes;
 std::vector<unsigned> counts;
 for(unsigned count=3;count<=1024;++count)counts.push_back(count);
 for(unsigned count:{2047u,8191u,21847u,21848u,32768u,65535u})counts.push_back(count);
 unsigned cases=0;
 for(unsigned count:counts)for(unsigned base:{0u,1u,17u,1024u,32768u,65000u,65534u,65535u}){
  referenceBytes.clear();
  constexpr std::array<u16,4> prefix={0xa52a,0xc4e1,0x81fe,0xf19a};
  referenceBytes.append(prefix.data(),sizeof(prefix));
  reference::prepare_idx_buffer(referenceBytes,GX_TRIANGLESTRIP,u16(base),u16(count));
  const size_t size=(count-2)*3;
  std::vector<u16> actual(size+32,0xbeef);
  auto* begin=actual.data()+16;
  auto* end=candidate::write_prepared_strip(begin,u16(base),u16(count));
  assert(end==begin+size);
  assert(referenceBytes.size()==sizeof(prefix)+size*sizeof(u16));
  assert(!std::memcmp(referenceBytes.data(),prefix.data(),sizeof(prefix)));
  assert(!std::memcmp(referenceBytes.data()+sizeof(prefix),begin,size*sizeof(u16)));
  for(size_t i=0;i<16;++i)assert(actual[i]==0xbeef&&actual[size+16+i]==0xbeef);
  ++cases;
 }
 std::printf("RAW_INDEX_EQUIVALENCE cases=%u complete_index_bytes_equal=1 exact_end_pointer=1 canaries_intact=1\n",cases);
}
