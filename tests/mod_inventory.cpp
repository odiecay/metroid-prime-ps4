// SPDX-License-Identifier: MIT
#include "port_mod_inventory.h"
#include <cassert>
#include <chrono>
#include <iostream>

int main(int argc,char** argv) {
    assert(argc==2);
    namespace fs=std::filesystem;
    const fs::path root=argv[1],index=root/"prime-assets-index.txt",mod=root/"mods/remastered-models";
    fs::create_directories(mod);
    const auto put=[&](const std::string& text) { std::ofstream(index)<<text; };
    const std::string good="4 mods/remastered-models/.import-version\n7 mods/remastered-models/00000001.dds\n";
    put(good);
    PortModInventory inventory;
    assert(inventory.Load(index,mod));
    assert(inventory.Files().size()==2);
    assert(inventory.Size(mod/"00000001.dds")==7);
    assert(!inventory.Size(mod/"unknown"));
    fs::remove(index);
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<100000;++i) assert(inventory.Load(index,mod));
    std::cout<<"100000 immutable inventory reuses without index reopens: "
             <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms (host only)\n";
    assert(!inventory.Load(index,root/"other-mod"));
    assert(!inventory.Size(mod/"00000001.dds"));
    for(const auto& bad:{good+"7 mods/remastered-models/00000001.dds\n",
        std::string("4 mods/remastered-models/../outside\n"),
        std::string("4 mods/remastered-models/00000001.dds\n"),
        std::string("67108865 mods/remastered-models/.import-version\n"),
        good+"broken row\n"}) {
        put(bad);PortModInventory broken;
        assert(!broken.Load(index,mod));
        assert(!broken.Size(mod/"00000001.dds"));
    }
}
