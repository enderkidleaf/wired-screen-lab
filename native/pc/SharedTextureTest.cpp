#include "SharedTexturePool.h"
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
using namespace WiredScreenGpu;

static void Check(HRESULT hr, const char* what) {
    if(hr!=S_OK) { char message[200]; sprintf_s(message,"%s: 0x%08lx",what,static_cast<unsigned long>(hr)); throw std::runtime_error(message); }
}
struct Mapping {
    PoolInfo* info=nullptr;
    ~Mapping(){if(info)UnmapViewOfFile(info);}
};
static ComPtr<ID3D11Device> Device(const LUID* luid=nullptr) {
    ComPtr<IDXGIFactory1> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;;++i) {
        Check(factory->EnumAdapters1(i,&adapter),"adapter enumeration");
        DXGI_ADAPTER_DESC1 desc{}; Check(adapter->GetDesc1(&desc),"adapter description");
        if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && (!luid || (desc.AdapterLuid.HighPart==luid->HighPart && desc.AdapterLuid.LowPart==luid->LowPart))) break;
        adapter.Reset();
    }
    ComPtr<ID3D11Device> result;
    Check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&result,nullptr,nullptr),"D3D11 device");
    return result;
}
static int Child(int argc,wchar_t** argv) {
    if(argc!=6) return 2;
    HANDLE handles[4]{};
    for(int i=0;i<4;++i) handles[i]=reinterpret_cast<HANDLE>(_wcstoui64(argv[i+2],nullptr,10));
    Mapping map; map.info=static_cast<PoolInfo*>(MapViewOfFile(handles[0],FILE_MAP_ALL_ACCESS,0,0,sizeof(PoolInfo)));
    if(!map.info) throw std::runtime_error("child map");
    auto device=Device(&map.info->adapter);
    Consumer consumer; Check(consumer.Init(device.Get(),map.info,handles+1),"consumer init");
    ID3D11Texture2D* texture=nullptr; FrameInfo frame{};
    Check(consumer.TakeLatest(&texture,&frame),"take latest");
    if(frame.sequence!=3 || frame.capturedQpc<=0) throw std::runtime_error("latest frame metadata");
    if(wcscmp(argv[1],L"--abandon")==0) {
        // Simulate a peer crash while it owns a texture: bypass destructors.
        TerminateProcess(GetCurrentProcess(),0);
        return 3;
    }
    // CPU readback is ONLY test verification. The production handoff exposes
    // an ID3D11Texture2D to the future GPU converter/encoder.
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; Check(device->CreateTexture2D(&desc,nullptr,&staging),"staging");
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE pixels{}; Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels),"readback");
    bool correct=true;
    for(UINT y=0;y<desc.Height;++y) {
        auto row=reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(pixels.pData)+y*pixels.RowPitch);
        for(UINT x=0;x<desc.Width;++x) if(row[x]!=0xff336699) correct=false;
    }
    context->Unmap(staging.Get(),0);
    Check(consumer.Release(),"release latest");
    if(!correct) throw std::runtime_error("shared pixels differ");
    if(consumer.TakeLatest(&texture,&frame)!=S_FALSE) throw std::runtime_error("stale frame remained");
    for(auto h:handles) CloseHandle(h);
    return 0;
}
static int Parent(bool abandon) {
    Handle mapping;
    *mapping.put()=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(PoolInfo),nullptr);
    if(!mapping.get()) throw std::runtime_error("mapping allocation");
    Mapping map; map.info=static_cast<PoolInfo*>(MapViewOfFile(mapping.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(PoolInfo)));
    if(!map.info) throw std::runtime_error("mapping view");
    auto device=Device(); Producer producer; Check(producer.Init(device.Get(),1920,1080,map.info),"producer init");
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=1920;desc.Height=1080;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    std::vector<uint32_t> pixels(desc.Width*desc.Height,0xff336699);
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem=pixels.data();initial.SysMemPitch=desc.Width*4;
    ComPtr<ID3D11Texture2D> source; Check(device->CreateTexture2D(&desc,&initial,&source),"source");
    LARGE_INTEGER stamp; QueryPerformanceCounter(&stamp);
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    for(uint64_t i=1;i<=SlotCount;++i) {
        std::fill(pixels.begin(),pixels.end(),0xff000000u+static_cast<uint32_t>(i)*0x112233u);
        context->UpdateSubresource(source.Get(),0,nullptr,pixels.data(),desc.Width*4,0);
        Check(producer.Publish(source.Get(),i,stamp.QuadPart),"publish");
    }
    if(producer.Publish(source.Get(),4,stamp.QuadPart)!=S_FALSE) throw std::runtime_error("full pool did not reject source frame");

    // Explicit handle list avoids publishing names or inheriting unrelated
    // process handles. This harness is not a driver/client authentication API.
    std::array<Handle,4> inherited;
    HANDLE list[4]{}; HANDLE originals[]={mapping.get(),producer.SharedHandle(0),producer.SharedHandle(1),producer.SharedHandle(2)};
    std::wstring command=L"\"";
    wchar_t executable[32768]; if(!GetModuleFileNameW(nullptr,executable,32768)) throw std::runtime_error("executable path");
    command+=executable;command+=abandon ? L"\" --abandon" : L"\" --child";
    for(unsigned i=0;i<4;++i) {
        if(!DuplicateHandle(GetCurrentProcess(),originals[i],GetCurrentProcess(),inherited[i].put(),0,TRUE,DUPLICATE_SAME_ACCESS)) throw std::runtime_error("duplicate");
        list[i]=inherited[i].get();command+=L" "+std::to_wstring(reinterpret_cast<uintptr_t>(list[i]));
    }
    SIZE_T bytes=0; InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<BYTE> attributes(bytes);
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
    startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)) throw std::runtime_error("attribute init");
    if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,list,sizeof(list),nullptr,nullptr)) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);throw std::runtime_error("handle list");
    }
    PROCESS_INFORMATION process{};
    BOOL created=CreateProcessW(executable,&command[0],nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,nullptr,nullptr,&startup.StartupInfo,&process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if(!created) throw std::runtime_error("child launch");
    Handle child,thread;*child.put()=process.hProcess;*thread.put()=process.hThread;
    if(WaitForSingleObject(child.get(),15000)!=WAIT_OBJECT_0) {
        TerminateProcess(child.get(),3);WaitForSingleObject(child.get(),5000);throw std::runtime_error("child timed out");
    }
    DWORD code=1;GetExitCodeProcess(child.get(),&code);
    if(code!=0) throw std::runtime_error("child verification failed");
    if(abandon) {
        bool lost=false;
        for(uint64_t i=4;i<=6;++i) {
            HRESULT hr=producer.Publish(source.Get(),i,stamp.QuadPart);
            if(hr==DXGI_ERROR_DEVICE_REMOVED) { lost=true;break; }
            Check(hr,"publish after crash");
        }
        if(!lost) throw std::runtime_error("abandoned texture did not invalidate session");
        puts("PASS: peer crash while holding GPU texture invalidates session without a consumer wait.");
        return 0;
    }
    for(uint64_t i=4;i<=6;++i) Check(producer.Publish(source.Get(),i,stamp.QuadPart),"reuse after peer exit");
    if(producer.Publish(source.Get(),7,stamp.QuadPart)!=S_FALSE) throw std::runtime_error("disconnected pool was not bounded");
    puts("PASS: 1080p GPU texture shared across processes; latest frame and pixels verified; full-pool rejection; slots reusable after peer exit.");
    return 0;
}
int wmain(int argc,wchar_t** argv) {
    try { if(argc>1) return Child(argc,argv); Parent(false); return Parent(true); }
    catch(const std::exception& ex) { fprintf(stderr,"FAIL: %s\n",ex.what());return 1; }
}
