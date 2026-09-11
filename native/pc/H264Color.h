#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
namespace WiredScreenGpu {
// Baseline SPS VUI edit only. Preserve all timing/HRD/restriction bits and all
// non-SPS NALs. Refuse unsupported profiles instead of guessing their syntax.
inline std::vector<uint8_t> BaselineSpsBt709(const std::vector<uint8_t>& nal){
    if(nal.size()<5||(nal[0]&31)!=7)throw std::runtime_error("Invalid SPS");
    std::vector<uint8_t> rbsp;unsigned zeros=0;
    for(size_t i=1;i<nal.size();++i){uint8_t b=nal[i];if(zeros>=2&&b==3){if(i+1>=nal.size()||nal[i+1]>3)throw std::runtime_error("Invalid SPS escape");zeros=0;continue;}
        rbsp.push_back(b);zeros=b==0?zeros+1:0;}
    std::vector<uint8_t> bits;
    for(uint8_t b:rbsp)for(int shift=7;shift>=0;--shift)bits.push_back((b>>shift)&1);
    while(!bits.empty()&&!bits.back())bits.pop_back();
    if(bits.empty())throw std::runtime_error("Missing SPS trailing bits");bits.pop_back();
    size_t position=0;
    auto read=[&](unsigned count){if(count>32||position+count>bits.size())throw std::runtime_error("Truncated SPS");uint32_t value=0;while(count--)value=(value<<1)|bits[position++];return value;};
    auto ue=[&](){unsigned count=0;while(read(1)==0){if(++count>30)throw std::runtime_error("Oversized SPS field");}return ((1u<<count)-1)+read(count);};
    if(read(8)!=66)throw std::runtime_error("Color edit requires Baseline H264");
    read(8);read(8);ue();ue();uint32_t order=ue();
    if(order==0)ue();else if(order==1){read(1);ue();ue();uint32_t cycle=ue();if(cycle>256)throw std::runtime_error("Invalid SPS cycle");while(cycle--)ue();}else if(order!=2)throw std::runtime_error("Invalid SPS POC type");
    ue();read(1);ue();ue();if(!read(1))read(1);read(1);
    if(read(1)){ue();ue();ue();ue();}
    size_t vuiFlag=position;bool vui=read(1)!=0;
    size_t videoFlag=0,afterVideo=0;
    if(vui){if(read(1)){uint32_t aspect=read(8);if(aspect==255){read(16);read(16);}}
        if(read(1))read(1);videoFlag=position;
        if(read(1)){read(3);read(1);if(read(1)){read(8);read(8);read(8);}}afterVideo=position;}
    std::vector<uint8_t> edited;
    auto put=[&](uint32_t value,unsigned count){while(count--)edited.push_back((value>>count)&1);};
    if(vui)edited.insert(edited.end(),bits.begin(),bits.begin()+videoFlag);
    else{edited.insert(edited.end(),bits.begin(),bits.begin()+vuiFlag);put(1,1);put(0,1);put(0,1);}
    put(1,1);put(5,3);put(0,1);put(1,1);put(1,8);put(1,8);put(1,8);
    if(vui)edited.insert(edited.end(),bits.begin()+afterVideo,bits.end());
    else{put(0,1);put(0,1);put(0,1);put(0,1);put(0,1);put(0,1);}
    put(1,1);while(edited.size()%8)put(0,1);
    std::vector<uint8_t> result{nal[0]};zeros=0;
    for(size_t i=0;i<edited.size();i+=8){uint8_t byte=0;for(unsigned b=0;b<8;++b)byte=static_cast<uint8_t>((byte<<1)|edited[i+b]);
        if(zeros>=2&&byte<=3){result.push_back(3);zeros=0;}result.push_back(byte);zeros=byte==0?zeros+1:0;}
    return result;
}
inline std::vector<uint8_t> H264Bt709(const std::vector<uint8_t>& packet){
    auto prefix=[&](size_t at)->size_t{if(at+3<=packet.size()&&packet[at]==0&&packet[at+1]==0){if(packet[at+2]==1)return 3;if(at+4<=packet.size()&&packet[at+2]==0&&packet[at+3]==1)return 4;}return 0;};
    if(!prefix(0))throw std::runtime_error("Expected Annex B packet");
    std::vector<uint8_t> output;size_t at=0;
    while(at<packet.size()){
        size_t start=prefix(at),body=at+start;if(!start||body>=packet.size())throw std::runtime_error("Empty H264 NAL");
        size_t end=body+1;while(end<packet.size()&&!prefix(end))++end;
        if((packet[body]&31)==7){auto sps=BaselineSpsBt709(std::vector<uint8_t>(packet.begin()+body,packet.begin()+end));
            output.insert(output.end(),packet.begin()+at,packet.begin()+body);output.insert(output.end(),sps.begin(),sps.end());}
        else output.insert(output.end(),packet.begin()+at,packet.begin()+end);
        at=end;
    }
    return output;
}
}
