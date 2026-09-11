#include "GpuHandoffClient.h"
#include "GpuVideoEncoder.h"
#include <cstdio>
#include <string>
#include <stdexcept>
#include <vector>
using namespace WiredScreenGpu;
static void Check(HRESULT hr,const char* what) {
    if(hr!=S_OK){char text[200];sprintf_s(text,"%s: 0x%08lx",what,static_cast<unsigned long>(hr));throw std::runtime_error(text);}
}
static ComPtr<ID3D11Device> Device(const LUID* luid=nullptr) {
    ComPtr<IDXGIFactory1> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;;++i){Check(factory->EnumAdapters1(i,&adapter),"adapter");DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc),"description");
        if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&(!luid||(desc.AdapterLuid.HighPart==luid->HighPart&&desc.AdapterLuid.LowPart==luid->LowPart)))break;adapter.Reset();}
    ComPtr<ID3D11Device> device;
    Check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr),"device");return device;
}
static void Receive(const wchar_t* name,DWORD pid,bool test,bool (*verifyHost)(DWORD)=nullptr) {
    HandoffClient client;Check(client.Connect(name,pid,verifyHost),"connect authenticated host");
    auto device=Device(&client.Info()->adapter);Consumer consumer;Check(client.InitConsumer(device.Get(),consumer),"open GPU textures");
    ULONGLONG deadline=GetTickCount64()+8000;uint64_t last=0;unsigned count=0;
    while(GetTickCount64()<deadline && count<(test?1u:3u)) {
        ID3D11Texture2D* texture=nullptr;FrameInfo frame{};HRESULT hr=consumer.TakeLatest(&texture,&frame);
        if(hr==S_FALSE){Sleep(1);continue;}Check(hr,"take texture");
        if(frame.sequence<=last || frame.capturedQpc<=0)throw std::runtime_error("non-increasing frame metadata");
        last=frame.sequence;
        // Probe only: verify the shared GPU copy completes via staging read.
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Check(device->CreateTexture2D(&desc,nullptr,&staging),"staging");
        ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);context->CopyResource(staging.Get(),texture);
        D3D11_MAPPED_SUBRESOURCE pixels{};Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels),"GPU completion");
        bool correct=true;
        if(test)for(UINT y=0;y<desc.Height;++y){auto row=reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(pixels.pData)+y*pixels.RowPitch);for(UINT x=0;x<desc.Width;++x)if(row[x]!=0xff2468ac)correct=false;}
        context->Unmap(staging.Get(),0);Check(consumer.Release(),"release");
        if(!correct)throw std::runtime_error("pixel mismatch");
        ++count;printf("frame=%llu size=%ux%u captureQpc=%lld\n",static_cast<unsigned long long>(frame.sequence),desc.Width,desc.Height,static_cast<long long>(frame.capturedQpc));
    }
    if(count<(test?1u:3u))throw std::runtime_error("frame deadline exceeded");
    puts("PASS: authenticated handle handoff and GPU frame read.");
}
static void SelfTest() {
    auto device=Device();std::wstring name=std::wstring(HandoffPipe)+L".test."+std::to_wstring(GetCurrentProcessId());
    {
        HandoffServer denied;Check(denied.Init(device.Get(),name.c_str()),"server init");
        HandoffClient client;
        if(client.Connect(name.c_str(),GetCurrentProcessId()+1)!=E_ACCESSDENIED)throw std::runtime_error("wrong server PID accepted");
    }
    if(!ElevatedAdministrator(GetCurrentProcess())) {
        HandoffServer denied;Check(denied.Init(device.Get(),name.c_str()),"denial server");HandoffClient client;
        if(client.Connect(name.c_str(),GetCurrentProcessId())==S_OK)throw std::runtime_error("non-admin accepted");
        puts("PASS: wrong server PID and non-elevated client rejected; pending handshake cancelled.");return;
    }
    HandoffServer server;Check(server.Init(device.Get(),name.c_str()),"server init");
    wchar_t path[32768];if(!GetModuleFileNameW(nullptr,path,32768))throw std::runtime_error("path");
    std::wstring command=L"\""+std::wstring(path)+L"\" --client "+std::to_wstring(GetCurrentProcessId())+L" "+name;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(path,&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("child launch");
    Handle child,thread;*child.put()=process.hProcess;*thread.put()=process.hThread;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=1920;desc.Height=1080;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
    std::vector<uint32_t> pixels(desc.Width*desc.Height,0xff2468ac);D3D11_SUBRESOURCE_DATA initial{pixels.data(),desc.Width*4,0};
    ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&desc,&initial,&texture),"source");
    ULONGLONG deadline=GetTickCount64()+12000;uint64_t sequence=0;
    while(GetTickCount64()<deadline && WaitForSingleObject(child.get(),0)==WAIT_TIMEOUT){LARGE_INTEGER qpc;QueryPerformanceCounter(&qpc);HRESULT hr=server.Publish(texture.Get(),++sequence,qpc.QuadPart);if(FAILED(hr))break;Sleep(5);}
    if(WaitForSingleObject(child.get(),0)!=WAIT_OBJECT_0){TerminateProcess(child.get(),3);WaitForSingleObject(child.get(),3000);throw std::runtime_error("child timeout");}
    DWORD exitCode=1;GetExitCodeProcess(child.get(),&exitCode);if(exitCode)throw std::runtime_error("child failed");
    for(unsigned i=0;i<100 && server.Connected();++i)Sleep(1);
    if(server.Connected())throw std::runtime_error("disconnect not observed");
    puts("PASS: wrong PID rejected; elevated independent client authenticated; 1080p pixels verified; disconnect detected.");
}
static void VerifyDriver(DWORD pid) {
    Handle process;*process.put()=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process.get())throw std::runtime_error("driver process");
    wchar_t path[32768];DWORD size=32768;if(!QueryFullProcessImageNameW(process.get(),0,path,&size))throw std::runtime_error("driver image");
    wchar_t system[32768];if(!GetSystemDirectoryW(system,32768))throw std::runtime_error("system directory");
    std::wstring expected=std::wstring(system)+L"\\WUDFHost.exe";if(_wcsicmp(path,expected.c_str())!=0)throw std::runtime_error("unexpected driver host image");
    Handle token;if(!OpenProcessToken(process.get(),TOKEN_QUERY,token.put()))throw std::runtime_error("driver token");
    BYTE buffer[1024];DWORD length=0;if(!GetTokenInformation(token.get(),TokenUser,buffer,sizeof(buffer),&length))throw std::runtime_error("driver user");
    PSID sid=reinterpret_cast<TOKEN_USER*>(buffer)->User.Sid;
    if(!IsWellKnownSid(sid,WinLocalServiceSid)&&!IsWellKnownSid(sid,WinLocalSystemSid))throw std::runtime_error("unexpected driver account");
}
class DebugPrivilege {
    Handle token;TOKEN_PRIVILEGES previous{};bool changed=false;
public:
    DebugPrivilege(){
        if(!ElevatedAdministrator(GetCurrentProcess()))throw std::runtime_error("Run driver probe as administrator");
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,token.put()))throw std::runtime_error("probe token");
        TOKEN_PRIVILEGES requested{};requested.PrivilegeCount=1;
        if(!LookupPrivilegeValueW(nullptr,L"SeDebugPrivilege",&requested.Privileges[0].Luid))throw std::runtime_error("debug privilege");
        requested.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;DWORD bytes=sizeof(previous);
        if(!AdjustTokenPrivileges(token.get(),FALSE,&requested,sizeof(previous),&previous,&bytes)||GetLastError()!=ERROR_SUCCESS)throw std::runtime_error("debug privilege unavailable");
        changed=true;
    }
    ~DebugPrivilege(){if(changed)AdjustTokenPrivileges(token.get(),FALSE,&previous,0,nullptr,nullptr);}
};
static void CheckColorSyntax(){
    // Baseline 16x16 SPS, POC type 2, no VUI. Also exercise an existing VUI
    // after the first edit, and ensure unrelated NAL payloads are untouched.
    std::vector<uint8_t> plain{0,0,0,1,0x67,0x42,0,0x1e,0xda,0x79};
    auto marked=H264Bt709(plain);
    if(marked==plain||H264Bt709(marked)!=marked)throw std::runtime_error("SPS VUI edit is not idempotent");
    std::vector<uint8_t> other{0,0,1,0x68,0xc0,0,0,0,1,0x65,0x88,0x80};
    if(H264Bt709(other)!=other)throw std::runtime_error("Non-SPS bytes changed");
    for(const auto& malformed:std::vector<std::vector<uint8_t>>{{0,0,1,0x67,66},{0,0,1,0x67,100,0,0,0x80},{1,2,3}}){
        bool rejected=false;try{H264Bt709(malformed);}catch(const std::runtime_error&){rejected=true;}
        if(!rejected)throw std::runtime_error("Malformed/unsupported SPS accepted");
    }
    puts("PASS: absent/existing VUI, unchanged non-SPS NALs, malformed input and unsupported profile rejection.");
}
static void EncodeThree(const wchar_t* outputPath,bool fromDriver){
    MediaRuntime runtime;
    HandoffClient client;ComPtr<ID3D11Device> device;Consumer consumer;
    if(fromDriver){Check(client.Connect(HandoffPipe,0,[](DWORD pid){try{VerifyDriver(pid);return true;}catch(...){return false;}}),"driver connection");
        device=Device(&client.Info()->adapter);}
    else device=Device();
    ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
    Check(device.As(&dxgi),"DXGI device");Check(dxgi->GetAdapter(&adapter),"adapter");Check(adapter->GetDesc(&desc),"LUID");
    GpuColorConverter converter;converter.Init(device.Get());GpuVideoEncoder encoder;encoder.Init(device.Get(),desc.AdapterLuid);
    // Acknowledge only after encoder setup so its startup cannot fill the
    // shared pool with old desktop frames before we're ready to consume them.
    if(fromDriver)Check(client.InitConsumer(device.Get(),consumer),"driver consumer");
    int nameSize=WideCharToMultiByte(CP_UTF8,0,encoder.name.c_str(),-1,nullptr,0,nullptr,nullptr);std::vector<char> name(nameSize);
    WideCharToMultiByte(CP_UTF8,0,encoder.name.c_str(),-1,name.data(),nameSize,nullptr,nullptr);
    printf("Hardware encoder: %s; lowLatencyAccepted=%d bFramesDisabled=%d colorMetadataAccepted=%d\n",name.data(),encoder.lowLatencyAccepted,encoder.bFramesDisabled,encoder.colorMetadataAccepted);
    ComPtr<ID3D11Texture2D> synthetic;
    if(!fromDriver){D3D11_TEXTURE2D_DESC d{};d.Width=1920;d.Height=1080;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
        std::vector<uint32_t> pixels(d.Width*d.Height,0xffcc4422);D3D11_SUBRESOURCE_DATA initial{pixels.data(),d.Width*4,0};Check(device->CreateTexture2D(&d,&initial,&synthetic),"synthetic GPU source");}
    FILE* file=nullptr;if(_wfopen_s(&file,outputPath,L"wb")!=0)throw std::runtime_error("output file");
    unsigned submitted=0,received=0;uint64_t last=0;ULONGLONG deadline=GetTickCount64()+15000;
    try{
        while(!encoder.Drained() && GetTickCount64()<deadline){
            NativeVideoPacket packet;
            if(encoder.Poll(packet)){
                // Native MF H.264 samples are expected to be Annex B. Do not
                // silently treat length-prefixed bytes as a bytestream.
                const auto& bytes=packet.bytes;
                if(bytes.size()<4 || bytes[0]!=0 || bytes[1]!=0 || (bytes[2]!=1 && !(bytes[2]==0&&bytes[3]==1)))throw std::runtime_error("Encoder output is not Annex B");
                if(fwrite(bytes.data(),1,bytes.size(),file)!=bytes.size())throw std::runtime_error("write encoded packet");
                printf("encoded source=%llu bytes=%zu capturedQpc=%lld encodedQpc=%lld\n",static_cast<unsigned long long>(packet.frame.sequence),bytes.size(),static_cast<long long>(packet.frame.capturedQpc),static_cast<long long>(packet.encodedQpc));++received;
            }
            if(submitted<3 && encoder.CanSubmit()){
                ID3D11Texture2D* source=synthetic.Get();FrameInfo frame{};
                if(fromDriver){HRESULT hr=consumer.TakeLatest(&source,&frame);if(hr==S_FALSE){Sleep(1);continue;}Check(hr,"take driver texture");}
                else{LARGE_INTEGER qpc;QueryPerformanceCounter(&qpc);frame={submitted+1,qpc.QuadPart};}
                if(frame.sequence<=last)throw std::runtime_error("source sequence order");last=frame.sequence;
                ComPtr<ID3D11Texture2D> nv12;
                try{nv12=converter.Convert(source);}catch(...){if(fromDriver)consumer.Release();throw;}
                // ReleaseSync orders the queued GPU conversion's source read;
                // encoder owns a separate NV12 surface and never holds the slot.
                if(fromDriver)Check(consumer.Release(),"release driver source");
                encoder.Submit(nv12.Get(),frame);++submitted;if(submitted==3)encoder.Drain();
            }
            Sleep(1);
        }
        if(!encoder.Drained()||received!=3)throw std::runtime_error("Expected three native encoded packets before deadline");
        if(fclose(file)!=0){file=nullptr;throw std::runtime_error("close output");}file=nullptr;
        puts("PASS: three source textures converted on GPU and encoded as native H264 packets; external decode verification still required.");
    }catch(...){if(file)fclose(file);throw;}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==1){SelfTest();return 0;}
        if(argc==2 && wcscmp(argv[1],L"--color-test")==0){CheckColorSyntax();return 0;}
        if(argc==3 && wcscmp(argv[1],L"--encode-test")==0){EncodeThree(argv[2],false);return 0;}
        if(argc==3 && wcscmp(argv[1],L"--encode-driver")==0){DebugPrivilege privilege;EncodeThree(argv[2],true);return 0;}
        if(argc==4 && wcscmp(argv[1],L"--client")==0){Receive(argv[3],wcstoul(argv[2],nullptr,10),true);return 0;}
        if(argc==2 && wcscmp(argv[1],L"--driver")==0){DebugPrivilege privilege;Receive(HandoffPipe,0,false,[](DWORD pid){try{VerifyDriver(pid);return true;}catch(...){return false;}});return 0;}
        if(argc==3 && wcscmp(argv[1],L"--driver-pid")==0){DWORD pid=wcstoul(argv[2],nullptr,10);VerifyDriver(pid);Receive(HandoffPipe,pid,false);return 0;}
        throw std::runtime_error("Usage: GpuHandoffProbe [--driver-pid verified-UMDF-host-pid]");
    }catch(const std::exception& ex){fprintf(stderr,"FAIL: %s\n",ex.what());return 1;}
}
