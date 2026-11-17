// SPDX-License-Identifier: MIT
// PC-only original menu preparation. The encoder and bounded video decoder
// are the port's existing implementations, not another movie implementation.
#include "port_remastered_movie.h"
#include <dolphin/thp.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <time.h>
#endif
#include <vector>

namespace {
using Bytes=std::vector<uint8_t>;
int64_t cpu_now() {
#ifdef _WIN32
  FILETIME created{},exited{},kernel{},user{};
  if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return -1;
  const auto ticks=[](FILETIME value) {return (uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
  return int64_t((ticks(kernel)+ticks(user))*100);
#else
  timespec t{};
  if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))return -1;
  return int64_t(t.tv_sec)*1000000000+t.tv_nsec;
#endif
}
uint32_t word(std::span<const uint8_t> b,size_t n) {
  if(n>b.size() || b.size()-n<4)throw std::runtime_error("Truncated THP word");
  return uint32_t(b[n])<<24|uint32_t(b[n+1])<<16|uint32_t(b[n+2])<<8|b[n+3];
}
struct Frame {size_t offset;uint32_t bytes;};
struct Movie {
  Bytes data;std::vector<Frame> frames;
  uint32_t width{},height{},fpsBits{},maxFrame{};
  float fps{};
};
Movie read_movie(const std::filesystem::path& path) {
  const auto size=std::filesystem::file_size(path);
  if(size<76 || size>(size_t(512)<<20))throw std::runtime_error("Invalid/bounded THP file size");
  Movie m;m.data.resize(size);std::ifstream in(path,std::ios::binary);
  if(!in.read(reinterpret_cast<char*>(m.data.data()),m.data.size()))throw std::runtime_error("THP read failed");
  std::span<const uint8_t> b=m.data;
  if(word(b,0)!=0x54485000 || word(b,4)!=0x10000)throw std::runtime_error("Expected THP version1.0");
  const auto components=word(b,32);
  // The three original menu videos have no audio. Refuse anything else rather
  // than silently strip or re-encode a soundtrack.
  if(word(b,12)!=0 || word(b,components)!=1 || components>b.size()-28 || b[components+4]!=0)
    throw std::runtime_error("Only video-only original THPs are supported; audio is never discarded");
  m.width=word(b,components+20);m.height=word(b,components+24);
  m.fpsBits=word(b,16);m.fps=std::bit_cast<float>(m.fpsBits);
  const auto count=word(b,20);m.maxFrame=word(b,8);
  if(!m.width || !m.height || m.width>4096 || m.height>4096 || (m.width|m.height)&1 ||
     !std::isfinite(m.fps) || m.fps<=0 || m.fps>120 || !count || count>36000)
    throw std::runtime_error("Invalid THP dimensions/rate/frame count");
  size_t offset=word(b,40);uint32_t next=word(b,24),previous=0;
  if(offset<components+28 || size_t(word(b,28))+offset!=b.size())throw std::runtime_error("Invalid THP data extent");
  for(uint32_t i=0;i<count;++i) {
    if(next<12 || next>(16u<<20) || next>m.maxFrame || offset>b.size() || next>b.size()-offset)
      throw std::runtime_error("Truncated/oversized THP frame");
    const auto payload=word(b,offset+8);
    if(payload>next-12 || payload<4 || (i && word(b,offset+4)!=previous))throw std::runtime_error("Invalid THP frame chain");
    m.frames.push_back({offset+12,payload});previous=next;next=word(b,offset);offset+=previous;
  }
  if(offset!=b.size() || next!=word(b,24) ||
     word(b,40)+word(b,28)-previous!=word(b,44) || word(b,word(b,40)+4)!=previous)
    throw std::runtime_error("THP loop/previous/last-frame link mismatch");
  return m;
}
void jpeg_dimensions(std::span<const uint8_t> b,unsigned width,unsigned height) {
  if(b.size()<4 || b[0]!=255 || b[1]!=216)throw std::runtime_error("Expected baseline JPEG frame");
  size_t p=2;
  while(p+4<=b.size()) {
    if(b[p]!=255)throw std::runtime_error("Invalid JPEG header");
    const auto marker=b[p+1];const size_t len=(unsigned(b[p+2])<<8)|b[p+3];
    if(len<2 || len>b.size()-p-2)throw std::runtime_error("Truncated JPEG segment");
    if(marker==0xc0) {
      if(len<8 || b[p+4]!=8 || ((unsigned(b[p+5])<<8)|b[p+6])!=height ||
         ((unsigned(b[p+7])<<8)|b[p+8])!=width)throw std::runtime_error("JPEG/frame dimensions differ");
      return;
    }
    if(marker==0xda || marker==0xd9)break;
    p+=len+2;
  }
  throw std::runtime_error("Missing baseline JPEG dimensions");
}
size_t plane_bytes(unsigned w,unsigned h) {return size_t((w+7)&~7u)*((h+3)&~3u);}
struct Plane {
  Bytes bytes;size_t size;
  explicit Plane(size_t n):bytes(n+128,0xa5),size(n){}
  uint8_t* data(){return bytes.data()+64;}
  void guard()const {
    if(!std::all_of(bytes.begin(),bytes.begin()+64,[](auto x){return x==0xa5;}) ||
       !std::all_of(bytes.end()-64,bytes.end(),[](auto x){return x==0xa5;}))throw std::runtime_error("Decoded plane guard was overwritten");
  }
  void dump(std::ostream& out,unsigned w,unsigned h)const {
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col) {
      const size_t index=(size_t(row/4)*((w+7)/8)+col/8)*32+(row%4)*8+col%8;
      out.put(char(bytes[index+64]));
    }
  }
};
void verify(const Movie& m,int dumpFrame=-1,const std::filesystem::path& dumpPath={}) {
  Plane y(plane_bytes(m.width,m.height)),u(plane_bytes(m.width/2,m.height/2)),v(u.size);
  std::vector<double> times,cpuTimes;uint64_t hash=1469598103934665603ull;
  for(size_t i=0;i<m.frames.size();++i) {
    const auto& f=m.frames[i];const std::span<const uint8_t> jpg(m.data.data()+f.offset,f.bytes);
    jpeg_dimensions(jpg,m.width,m.height);
    const auto cpuStart=cpu_now();
    const auto start=std::chrono::steady_clock::now();
    const auto error=THPVideoDecodeBounded(jpg.data(),jpg.size(),y.data(),u.data(),v.data(),m.width,m.height);
    times.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
    const auto cpuEnd=cpu_now();
    if(cpuStart>=0 && cpuEnd>=cpuStart)cpuTimes.push_back(double(cpuEnd-cpuStart)/1000.0);
    if(error)throw std::runtime_error("Actual portable THP decoder rejected frame "+std::to_string(i)+" with error "+std::to_string(error));
    y.guard();u.guard();v.guard();
    for(auto* p:{&y,&u,&v})for(size_t n=0;n<p->size;++n){hash^=p->data()[n];hash*=1099511628211ull;}
    if(int(i)==dumpFrame) {
      if(std::filesystem::exists(dumpPath))throw std::runtime_error("Refusing to overwrite decoded dump");
      std::ofstream out(dumpPath,std::ios::binary);y.dump(out,m.width,m.height);u.dump(out,m.width/2,m.height/2);v.dump(out,m.width/2,m.height/2);
      if(!out)throw std::runtime_error("Decoded dump failed");
    }
  }
  std::sort(times.begin(),times.end());
  std::sort(cpuTimes.begin(),cpuTimes.end());
  const auto q=[&](double p){return times[std::min(size_t(p*(times.size()-1)),times.size()-1)];};
  std::cout<<"{\"width\":"<<m.width<<",\"height\":"<<m.height<<",\"frames\":"<<m.frames.size()
    <<",\"fps_bits\":"<<m.fpsBits<<",\"max_frame_bytes\":"<<m.maxFrame<<",\"file_bytes\":"<<m.data.size()
    <<",\"audio_components\":0,\"audio_bytes\":0,\"audio_crc32\":\"00000000\""
    <<",\"decoded_planes_bytes\":"<<(y.size+u.size+v.size)<<",\"triple_buffer_planes_bytes\":"<<(y.size+u.size+v.size)*3
    <<",\"decoded_fnv64\":"<<hash<<",\"all_frames_decoded\":true,\"plane_guards_intact\":true"
    <<",\"host_decode_median_us\":"<<q(.5)<<",\"host_decode_p95_us\":"<<q(.95)<<",\"host_decode_max_us\":"<<times.back()
#ifdef _WIN32
    <<",\"cpu_clock\":\"GetThreadTimes\",\"host_decode_cpu_samples\":"<<cpuTimes.size()
#else
    <<",\"cpu_clock\":\"CLOCK_THREAD_CPUTIME_ID\",\"host_decode_cpu_samples\":"<<cpuTimes.size()
#endif
    <<",\"host_decode_cpu_median_us\":";
  if(cpuTimes.size()==times.size())std::cout<<cpuTimes[cpuTimes.size()/2];else std::cout<<"null";
  std::cout<<"}\n";
}
} // namespace

int main(int argc,char** argv) try {
#ifdef _WIN32
  // Windows CRT text mode would translate CR/LF and truncate at Ctrl-Z in the
  // piped JPEG stream. The same bytes must reach JpegSplitter on every host.
  if(_setmode(_fileno(stdin),_O_BINARY)==-1)throw std::runtime_error("Could not set binary stdin");
#endif
  if(argc>=3 && std::string(argv[1])=="verify") {
    const auto m=read_movie(argv[2]);
    if(argc==5)verify(m,std::stoi(argv[3]),argv[4]);else if(argc==3)verify(m);else throw std::runtime_error("verify INPUT [FRAME DUMP.yuv]");
    return 0;
  }
  if(argc!=5 || std::string(argv[1])!="encode")throw std::runtime_error("encode ORIGINAL.thp OUTPUT.thp SCALE < frame-stream.mjpeg; or verify INPUT [FRAME DUMP.yuv]");
  const auto source=read_movie(argv[2]);const std::string scale=argv[4];
  const unsigned numerator=scale=="1.5"?3:scale=="2"?2:scale=="3"?3:0;
  const unsigned denominator=scale=="1.5"?2:1;
  if(!numerator)throw std::runtime_error("Scale must be1.5,2 or3");
  const auto width=source.width*numerator/denominator,height=source.height*numerator/denominator;
  if((source.width*numerator)%denominator || (source.height*numerator)%denominator || (width|height)&1)
    throw std::runtime_error("Scale must produce exact even dimensions");
  if(width>4096 || height>4096)throw std::runtime_error("Upscaled dimensions exceed bounded4096 limit");
  const std::filesystem::path output=argv[3];
  if(std::filesystem::exists(output))throw std::runtime_error("Refusing to overwrite output");
  std::ofstream out(output,std::ios::binary);
  try {
    PortRemastered::ThpWriter writer(out,width,height,source.fps);
    PortRemastered::JpegSplitter splitter;std::array<uint8_t,65536> buffer{};
    uint64_t outputBytes=76;
    const auto sink=[&](const Bytes& jpg) {
      if(size_t(writer.Frames())>=source.frames.size())throw std::runtime_error("More output frames than source");
      const uint64_t padded=(uint64_t(jpg.size())+12+31)&~uint64_t(31);
      if(outputBytes+padded>(uint64_t(512)<<20))throw std::runtime_error("Output exceeds512MiB movie preparation limit");
      outputBytes+=padded;
      jpeg_dimensions(jpg,width,height);writer.Add(jpg);return bool(out);
    };
    while(std::cin) {
      std::cin.read(reinterpret_cast<char*>(buffer.data()),buffer.size());const auto size=std::cin.gcount();
      if(size && !splitter.Feed(buffer.data(),size,sink))throw std::runtime_error("Invalid/truncated JPEG stream");
    }
    if(!splitter.Idle() || size_t(writer.Frames())!=source.frames.size() || !writer.Finish())throw std::runtime_error("Output frame count/stream differs from source");
    out.close();const auto converted=read_movie(output);
    if(converted.fpsBits!=source.fpsBits)throw std::runtime_error("Frame rate bits changed");
    verify(converted);
  } catch(...) {out.close();std::filesystem::remove(output);throw;}
  return 0;
} catch(const std::exception& e) {std::cerr<<"prime_menu_movie: "<<e.what()<<'\n';return 1;}
