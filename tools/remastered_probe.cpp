// Read-only PC input check using the actual pinned upstream Switch reader.
#include "port_remastered_nsp.h"
#include "port_remastered_pak.h"
#include <algorithm>
#include <cstdio>
#include <openssl/evp.h>
#include <string>
#include <vector>
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    PortRemastered::Nsp image;std::string error;
    if(!image.Open(argv[1],argv[2],error)) {
        std::fprintf(stderr,"Remastered input rejected: %s\n",error.c_str());return 3;
    }
    if(image.ProgramId()!=PortRemastered::kRemasteredProgramId) {
        std::fprintf(stderr,"Unexpected program ID: %016llx\n",(unsigned long long)image.ProgramId());return 4;
    }
    std::vector<uint8_t> brdf,noise;
    if(!PortRemastered::ExtractBrdfLut(image,brdf,error)) {
        std::fprintf(stderr,"Unsupported Remastered BRDF/version data: %s\n",error.c_str());return 8;
    }
    if(!PortRemastered::ExtractThermalNoise(image,noise,error)) {
        std::fprintf(stderr,"Thermal data read failed: %s\n",error.c_str());return 9;
    }
    unsigned paks=0,sampled=0,decoded=0;uint64_t assets=0;
    for(const auto &file:image.Files()) {
        if(!file.path.ends_with(".pak")) continue;
        ++paks;
        PortRemastered::Pak pak;
        if(!pak.Open([&](uint64_t offset,void *out,size_t size) {
            return image.Read(file,offset,out,size,error);
        },file.size,error)) {
            std::fprintf(stderr,"PAK table rejected: %s: %s\n",file.path.c_str(),error.c_str());return 10;
        }
        assets+=pak.Assets().size();
        for(const auto& asset:pak.Assets()) {
            if(decoded>=16 || asset.decompressedSize>32*1024*1024) break;
            std::vector<uint8_t> raw;
            if(!pak.ReadAsset(asset,raw,error)) {
                std::fprintf(stderr,"Asset decompression rejected: %s\n",error.c_str());return 11;
            }
            ++decoded;
        }
        if(sampled>=8 || !file.size) continue;
        const size_t bytes=std::min<uint64_t>(file.size,65536);
        std::vector<unsigned char> data(bytes);
        if(!image.Read(file,0,data.data(),data.size(),error)) {
            std::fprintf(stderr,"Read failed: %s\n",error.c_str());return 5;
        }
        unsigned char digest[EVP_MAX_MD_SIZE];unsigned length=0;
        if(!EVP_Digest(data.data(),data.size(),digest,&length,EVP_sha256(),nullptr)) return 6;
        std::printf("{\"sample\":%u,\"bytes\":%zu,\"sha256\":\"",sampled++,bytes);
        for(unsigned n=0;n<length;++n) std::printf("%02x",digest[n]);
        std::puts("\"}");
    }
    std::printf("{\"program_id\":\"%016llx\",\"romfs_files\":%zu,\"pak_files\":%u,\"pak_samples\":%u,\"asset_records\":%llu,\"raw_assets_decoded\":%u,\"decryption_verified\":%s,\"brdf_version_verified\":true,\"thermal_bytes\":%zu,\"assets_converted\":false}\n",
        (unsigned long long)image.ProgramId(),image.Files().size(),paks,sampled,
        (unsigned long long)assets,decoded,sampled?"true":"false",noise.size());
    return sampled?0:7;
}
