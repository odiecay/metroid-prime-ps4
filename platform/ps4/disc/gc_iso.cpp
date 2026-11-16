// SPDX-License-Identifier: MIT
// The nod C ABI subset used by Aurora, backed by a PC-prepared raw GameCube
// image. This is an actual reader, not the upstream Android nod build stub.
// Compressed/Wii images are rejected; RVZ is converted by tools/prepare_data.py.
#include <nod.h>
#include "gc_iso_cache.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {
constexpr size_t kReadCacheBlockBytes = 64 * 1024;
// Leave one block's allowance for metadata: lazy data + metadata stays <8 MiB.
constexpr size_t kReadCacheSlots = 127;
struct ReadCacheCounters {
    std::atomic<uint64_t> hits{}, misses{}, calls{}, bytes{}, retained{}, fallbacks{}, bulk{};
} cache_counters;
thread_local char error_text[160];
NodResult fail(NodResult r, const char *message) {
    std::strncpy(error_text, message, sizeof(error_text)-1);
    error_text[sizeof(error_text)-1] = 0;
    return r;
}
uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
}
struct Entry { std::string name; uint32_t offset, length; bool directory; };
struct Disc {
    NodDiscStream stream;
    uint64_t size = 0;
    std::mutex io;
    NodDiscHeader header{};
    std::vector<uint8_t> boot_bytes, bi2, apploader, fst, dol;
    std::vector<Entry> entries;
    struct CachedBlock {
        std::unique_ptr<uint8_t[]> data;
        uint64_t offset=UINT64_MAX, stamp=0;
        size_t length=0;
    };
    std::array<CachedBlock,kReadCacheSlots> read_cache;
    uint64_t cache_clock=0;
    bool cache_enabled=false; // Metadata validation uses exact, uncached reads.
    explicit Disc(NodDiscStream s) : stream(s) {}
    ~Disc() {
        for (const auto& block:read_cache) if (block.data)
            cache_counters.retained.fetch_sub(kReadCacheBlockBytes,std::memory_order_relaxed);
        stream.close(stream.user_data);
    }
    bool read_exact_locked(uint64_t offset, void *dst, size_t length) {
        size_t done = 0;
        while (done < length) {
            cache_counters.calls.fetch_add(1,std::memory_order_relaxed);
            int64_t n = stream.read_at(stream.user_data, offset+done,
                static_cast<uint8_t *>(dst)+done, length-done);
            if (n <= 0 || uint64_t(n) > length-done) return false;
            cache_counters.bytes.fetch_add(uint64_t(n),std::memory_order_relaxed);
            done += size_t(n);
        }
        return true;
    }
    bool read_at(uint64_t offset, void *dst, size_t length) {
        if (offset > size || length > size-offset) return false;
        std::lock_guard<std::mutex> guard(io);
        // Large streaming reads already amortise the seek. Do not evict the
        // small-resource working set for movie/music/whole-area transfers.
        if (!cache_enabled || length > kReadCacheBlockBytes) {
            if(cache_enabled) cache_counters.bulk.fetch_add(1,std::memory_order_relaxed);
            return read_exact_locked(offset,dst,length);
        }
        size_t done=0;
        while(done<length) {
            const uint64_t at=offset+done, base=at&~uint64_t(kReadCacheBlockBytes-1);
            CachedBlock* found=nullptr;
            CachedBlock* victim=&read_cache.front();
            for(auto& block:read_cache) {
                if(block.offset==base) { found=&block;break; }
                if(block.stamp<victim->stamp) victim=&block;
            }
            if(found) {
                cache_counters.hits.fetch_add(1,std::memory_order_relaxed);
            } else {
                cache_counters.misses.fetch_add(1,std::memory_order_relaxed);
                if(!victim->data) {
                    victim->data.reset(new(std::nothrow) uint8_t[kReadCacheBlockBytes]);
                    if(victim->data) cache_counters.retained.fetch_add(kReadCacheBlockBytes,std::memory_order_relaxed);
                }
                victim->offset=UINT64_MAX;
                const size_t amount=size_t(std::min<uint64_t>(kReadCacheBlockBytes,size-base));
                // A speculative tail failure must not turn a valid exact read
                // into an error. Publish a block only after the complete fill.
                if(!victim->data || !read_exact_locked(base,victim->data.get(),amount)) {
                    cache_counters.fallbacks.fetch_add(1,std::memory_order_relaxed);
                    return read_exact_locked(offset,dst,length);
                }
                victim->offset=base;victim->length=amount;
                found=victim;
            }
            found->stamp=++cache_clock;
            const size_t within=size_t(at-base);
            const size_t count=std::min(length-done,found->length-within);
            std::memcpy(static_cast<uint8_t*>(dst)+done,found->data.get()+within,count);
            done+=count;
        }
        return true;
    }
    bool initialize() {
        int64_t length = stream.stream_len(stream.user_data);
        if (length < 0x440) return false;
        size = uint64_t(length);
        uint8_t boot[0x440];
        if (!read_at(0, boot, sizeof(boot)) || be32(boot+0x1c) != 0xc2339f3d) return false;
        if (boot[6] || !((!std::memcmp(boot,"GM8E01",6) && boot[7]<=2) ||
                         (!std::memcmp(boot,"GM8P01",6) && !boot[7]))) return false;
        std::memcpy(&header, boot, sizeof(header));
        boot_bytes.assign(boot,boot+sizeof(boot));
        bi2.resize(0x2000);
        if (!read_at(0x440,bi2.data(),bi2.size())) return false;
        uint8_t app_header[0x20];
        if (!read_at(0x2440,app_header,sizeof(app_header))) return false;
        uint64_t app_size=0x20ull+be32(app_header+0x14)+be32(app_header+0x18);
        if (app_size>4u*1024*1024) return false;
        apploader.resize(size_t(app_size));
        if (!read_at(0x2440,apploader.data(),apploader.size())) return false;
        uint32_t dol_offset=be32(boot+0x420), fst_offset=be32(boot+0x424), fst_size=be32(boot+0x428);
        if (fst_size < 12 || fst_size > 32u*1024*1024 || fst_offset < 0x440 || dol_offset < 0x440) return false;
        fst.resize(fst_size);
        if (!read_at(fst_offset,fst.data(),fst.size()) || fst[0]!=1) return false;
        uint32_t count=be32(fst.data()+8);
        if (!count || uint64_t(count)*12 > fst.size()) return false;
        const uint8_t *names=fst.data()+size_t(count)*12;
        size_t names_size=fst.size()-size_t(count)*12;
        entries.reserve(count);
        // Validate nesting, not only individual extents, before exposing names
        // to Aurora's FST iterator (which trusts parent/child boundaries).
        std::vector<uint32_t> ends{count};
        std::vector<uint32_t> parents{0};
        for (uint32_t i=0;i<count;++i) {
            const uint8_t *node=fst.data()+i*12;
            uint32_t type_name=be32(node), off=be32(node+4), len=be32(node+8), name=type_name&0xffffff;
            uint32_t kind=type_name>>24;
            if (kind>1 || name>=names_size) return false;
            const void *end=std::memchr(names+name,0,names_size-name);
            if (!end) return false;
            if (i) {
                while (!ends.empty() && i>=ends.back()) { ends.pop_back(); parents.pop_back(); }
                if (ends.empty()) return false;
                if (kind) {
                    if (off!=parents.back() || len<=i || len>ends.back()) return false;
                } else if (off>size || len>size-off) return false;
            } else if (!kind || off || len!=count) return false;
            entries.push_back({std::string(reinterpret_cast<const char *>(names+name),
                static_cast<const uint8_t *>(end)-(names+name)),off,len,kind==1});
            if (i && kind) { ends.push_back(len); parents.push_back(i); }
        }
        uint8_t dh[0x100];
        if (!read_at(dol_offset,dh,sizeof(dh))) return false;
        uint64_t dol_size=sizeof(dh);
        for (unsigned i=0;i<18;++i) {
            uint32_t off=be32(dh+i*4), len=be32(dh+0x90+i*4);
            if (len) {
                if (off<sizeof(dh) || uint64_t(off)+len>64u*1024*1024) return false;
                dol_size=std::max(dol_size,uint64_t(off)+len);
            }
        }
        dol.resize(size_t(dol_size));
        return read_at(dol_offset,dol.data(),dol.size());
    }
};
}

struct NodHandle {
    enum Kind { DiscHandle, PartitionHandle, FileHandle } kind;
    std::shared_ptr<Disc> disc;
    uint64_t offset=0, size=0, position=0;
    std::mutex position_mutex;
    NodHandle(Kind k, std::shared_ptr<Disc> d, uint64_t o, uint64_t s)
        : kind(k),disc(std::move(d)),offset(o),size(s) {}
};

extern "C" {
void prime_iso_read_cache_metrics(PrimeIsoReadCacheMetrics *out) {
    if(!out) return;
    *out={cache_counters.hits.load(std::memory_order_relaxed),cache_counters.misses.load(std::memory_order_relaxed),
          cache_counters.calls.load(std::memory_order_relaxed),cache_counters.bytes.load(std::memory_order_relaxed),
          cache_counters.retained.load(std::memory_order_relaxed),cache_counters.fallbacks.load(std::memory_order_relaxed),
          cache_counters.bulk.load(std::memory_order_relaxed)};
}
const char *nod_error_message() { return error_text; }
NodResult nod_disc_open_stream(const NodDiscStream *stream, const NodDiscOptions *, NodHandle **out) {
    if (out) *out=nullptr;
    if (!out || !stream || !stream->read_at || !stream->stream_len || !stream->close)
        return fail(NOD_RESULT_ERR_INVALID_HANDLE,"Invalid disc stream");
    std::shared_ptr<Disc> disc;
    try {
        disc=std::make_shared<Disc>(*stream);
        if (!disc->initialize()) return fail(NOD_RESULT_ERR_FORMAT,"Expected a complete supported raw GameCube ISO; convert RVZ on the PC");
        const char* cache=getenv("MP_DVD_READ_CACHE");
        disc->cache_enabled=cache==nullptr || cache[0]!='0';
        *out=new NodHandle(NodHandle::DiscHandle,disc,0,disc->size);
        return NOD_RESULT_OK;
    } catch (...) {
        if (!disc) stream->close(stream->user_data);
        return fail(NOD_RESULT_ERR_OTHER,"Disc reader allocation failed");
    }
}
void nod_free(NodHandle *h) { delete h; }
NodResult nod_disc_open_partition_kind(NodHandle *disc, uint32_t kind, const NodPartitionOptions *, NodHandle **out) {
    if (out) *out=nullptr;
    if (!out || !disc || disc->kind!=NodHandle::DiscHandle) return NOD_RESULT_ERR_INVALID_HANDLE;
    if (kind!=NOD_PARTITION_KIND_DATA) return NOD_RESULT_ERR_NOT_FOUND;
    try { *out=new NodHandle(NodHandle::PartitionHandle,disc->disc,0,disc->size); }
    catch (...) { return NOD_RESULT_ERR_OTHER; }
    return NOD_RESULT_OK;
}
NodResult nod_partition_open_file(NodHandle *partition,uint32_t index,NodHandle **out) {
    if (out) *out=nullptr;
    if (!out || !partition || partition->kind!=NodHandle::PartitionHandle) return NOD_RESULT_ERR_INVALID_HANDLE;
    if (index>=partition->disc->entries.size() || partition->disc->entries[index].directory) return NOD_RESULT_ERR_NOT_FOUND;
    const Entry &e=partition->disc->entries[index];
    try { *out=new NodHandle(NodHandle::FileHandle,partition->disc,e.offset,e.length); }
    catch (...) { return NOD_RESULT_ERR_OTHER; }
    return NOD_RESULT_OK;
}
int64_t nod_read(NodHandle *h,uint8_t *buf,size_t length) {
    if (!h || (!buf && length)) return -1;
    std::lock_guard<std::mutex> guard(h->position_mutex);
    if (h->position>=h->size) return 0;
    size_t count=size_t(std::min(uint64_t(length),h->size-h->position));
    if (!h->disc->read_at(h->offset+h->position,buf,count)) return -1;
    h->position+=count;
    return int64_t(count);
}
int64_t nod_seek(NodHandle *h,int64_t offset,int32_t whence) {
    if (!h) return -1;
    std::lock_guard<std::mutex> guard(h->position_mutex);
    uint64_t base=whence==0 ? 0 : whence==1 ? h->position : whence==2 ? h->size : UINT64_MAX;
    if (base==UINT64_MAX) return -1;
    uint64_t position;
    if (offset<0) {
        uint64_t magnitude=uint64_t(-(offset+1))+1;
        if (magnitude>base) return -1;
        position=base-magnitude;
    } else {
        if (uint64_t(offset)>uint64_t(INT64_MAX)-base) return -1;
        position=base+uint64_t(offset);
    }
    h->position=position;
    return int64_t(position);
}
NodResult nod_disc_header(const NodHandle *h,NodDiscHeader *out) {
    if (!h || h->kind!=NodHandle::DiscHandle || !out) return NOD_RESULT_ERR_INVALID_HANDLE;
    *out=h->disc->header; return NOD_RESULT_OK;
}
bool nod_partition_is_wii(const NodHandle *) { return false; }
NodResult nod_partition_meta(const NodHandle *h,NodPartitionMeta *out) {
    if (!h || h->kind!=NodHandle::PartitionHandle || !out) return NOD_RESULT_ERR_INVALID_HANDLE;
    *out={};
    out->raw_boot={h->disc->boot_bytes.data(),h->disc->boot_bytes.size()};
    out->raw_bi2={h->disc->bi2.data(),h->disc->bi2.size()};
    out->raw_apploader={h->disc->apploader.data(),h->disc->apploader.size()};
    out->raw_fst={h->disc->fst.data(),h->disc->fst.size()};
    out->raw_dol={h->disc->dol.data(),h->disc->dol.size()};
    return NOD_RESULT_OK;
}
void nod_partition_iterate_fst(const NodHandle *h,NodFstCallback callback,void *user) {
    if (!h || h->kind!=NodHandle::PartitionHandle || !callback) return;
    for (uint32_t i=1;i<h->disc->entries.size();) {
        const Entry &e=h->disc->entries[i];
        uint32_t next=callback(i,e.directory ? NOD_NODE_KIND_DIRECTORY : NOD_NODE_KIND_FILE,e.name.c_str(),e.length,user);
        if (next==NOD_FST_STOP || next<=i) return;
        i=next;
    }
}
uint32_t nod_partition_find_file(const NodHandle *h,const char *path,NodNodeKind *kind,uint32_t *length) {
    if (!h || h->kind!=NodHandle::PartitionHandle || !path) return NOD_FST_STOP;
    uint32_t dir=0;
    while (*path=='/') ++path;
    while (*path) {
        const char *slash=std::strchr(path,'/');
        size_t size=slash ? size_t(slash-path) : std::strlen(path);
        if (!size || (size==2 && path[0]=='.' && path[1]=='.')) return NOD_FST_STOP;
        uint32_t found=NOD_FST_STOP;
        const auto &entries=h->disc->entries;
        for (uint32_t i=dir+1;i<entries[dir].length;) {
            const Entry &e=entries[i];
            if (e.name.size()==size && std::equal(e.name.begin(),e.name.end(),path,[](char a,char b) {
                auto lower=[](unsigned char c) { return c>='A' && c<='Z' ? c+('a'-'A') : c; };
                return lower(a)==lower(b);
            })) { found=i; break; }
            i=e.directory ? e.length : i+1;
        }
        if (found==NOD_FST_STOP) return found;
        dir=found;
        path=slash ? slash+1 : path+size;
        if (*path && !entries[dir].directory) return NOD_FST_STOP;
    }
    const Entry &e=h->disc->entries[dir];
    if (kind) *kind=e.directory ? NOD_NODE_KIND_DIRECTORY : NOD_NODE_KIND_FILE;
    if (length) *length=e.length;
    return dir;
}
}
