// SPDX-License-Identifier: MIT
#include "asset_bundle.hpp"
#include <array>
#include <algorithm>
#include <streambuf>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <zlib.h>
namespace prime {
namespace fs=std::filesystem;
namespace {
class BundleBuffer : public std::streambuf {
    gzFile file_{};std::array<char,65536> bytes_{};
protected:
    int_type underflow() override {
        if(gptr() && gptr()<egptr()) return traits_type::to_int_type(*gptr());
        const int n=gzread(file_,bytes_.data(),unsigned(bytes_.size()));
        if(n<=0) return traits_type::eof();
        setg(bytes_.data(),bytes_.data(),bytes_.data()+n);
        return traits_type::to_int_type(*gptr());
    }
public:
    explicit BundleBuffer(const fs::path& path):file_(gzopen(path.c_str(),"rb")) {}
    ~BundleBuffer() { if(file_) gzclose(file_); }
    bool open() const { return file_!=nullptr; }
    bool healthy() const { int code=0;gzerror(file_,&code);return code==Z_OK || code==Z_STREAM_END; }
};
void read_exact(std::istream& in,char* data,size_t n) {
    if(!in.read(data,std::streamsize(n))) throw std::runtime_error("Remastered asset bundle is truncated");
}
uint64_t number(std::istream& in,unsigned n) {
    std::array<unsigned char,8> b{};read_exact(in,reinterpret_cast<char*>(b.data()),n);
    uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[i])<<(i*8);return v;
}
}
bool unpack_asset_bundle(const fs::path& bundle,const fs::path& destination) {
    if(fs::is_symlink(bundle) || fs::is_symlink(destination) || fs::is_symlink(destination.parent_path()))
        throw std::runtime_error("Remastered asset storage contains a symbolic link");
    if(fs::exists(destination)) return false;
    BundleBuffer bufferInput(bundle);
    if(!bufferInput.open()) return false;
    std::istream input(&bufferInput);
    std::array<char,12> magic{};read_exact(input,magic.data(),magic.size());
    if(std::string(magic.data(),magic.size())!="PRIMEASSET1\n") throw std::runtime_error("Unsupported Remastered asset bundle");
    const auto count=number(input,4),total=number(input,8);
    if(!count || count>16400 || total>3ull*1024*1024*1024) throw std::runtime_error("Remastered asset bundle exceeds its limits");
    const auto temp=fs::path(destination.string()+".unpacking");
    if(fs::is_symlink(temp)) throw std::runtime_error("Remastered unpack path is a symbolic link");
    fs::remove_all(temp);fs::create_directories(temp);
    std::array<char,65536> buffer{};std::set<std::string> names;uint64_t copied=0;
    try {
        for(uint64_t i=0;i<count;++i) {
            const auto length=number(input,2),size=number(input,8),expected=number(input,4);
            if(!length || length>512 || size>64ull*1024*1024 || copied>total || size>total-copied)
                throw std::runtime_error("Invalid Remastered asset bundle entry");
            std::string name(length,'\0');read_exact(input,name.data(),name.size());
            const fs::path rel(name);
            if(name.find('\0')!=std::string::npos || rel.is_absolute() || !names.insert(name).second ||
               !(name.starts_with("mods/remastered-models/") || name=="profile.json" || name=="prime-assets.txt" || name=="prime-assets-index.txt"))
                throw std::runtime_error("Unsafe Remastered asset bundle path");
            for(const auto& part:rel) if(part==".." || part==".") throw std::runtime_error("Unsafe Remastered asset bundle path");
            const auto out=temp/rel;fs::create_directories(out.parent_path());
            std::ofstream output(out,std::ios::binary|std::ios::trunc);
            if(!output) throw std::runtime_error("Cannot write Remastered assets; check free storage");
            uint64_t remaining=size;uLong crc=crc32(0,nullptr,0);
            while(remaining) {
                const size_t n=size_t(std::min<uint64_t>(remaining,buffer.size()));read_exact(input,buffer.data(),n);
                output.write(buffer.data(),std::streamsize(n));if(!output) throw std::runtime_error("Remastered asset write failed; check free storage");
                crc=crc32(crc,reinterpret_cast<const Bytef*>(buffer.data()),uInt(n));remaining-=n;
            }
            output.close();
            if(!output || uint32_t(crc)!=expected) throw std::runtime_error("Remastered asset checksum failed");
            copied+=size;
        }
        if(copied!=total || input.peek()!=std::char_traits<char>::eof() || !bufferInput.healthy() ||
           !names.contains("prime-assets.txt") || !names.contains("prime-assets-index.txt") || !names.contains("profile.json") ||
           !names.contains("mods/remastered-models/.import-version")) throw std::runtime_error("Remastered asset bundle is incomplete");
        if(fs::exists(destination)) throw std::runtime_error("Asset destination appeared; existing files preserved");
        fs::rename(temp,destination);
    } catch(...) { std::error_code ec;fs::remove_all(temp,ec);throw; }
    return true;
}
}
