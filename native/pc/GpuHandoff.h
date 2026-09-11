#pragma once
#include "SharedTexturePool.h"
#include <sddl.h>
#include <atomic>
#pragma comment(lib,"advapi32.lib")

namespace WiredScreenGpu {
constexpr wchar_t HandoffPipe[]=L"\\\\.\\pipe\\WiredScreen.Gpu.v1";
struct HandoffHello { uint32_t version, bytes; };
struct HandoffReply { uint32_t version, bytes; uint64_t handles[4]; };
static_assert(sizeof(HandoffReply)==40,"IPC layout");

inline bool ElevatedAdminToken(HANDLE token) {
    TOKEN_ELEVATION elevation{}; DWORD length=0;
    if(!GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&length) || !elevation.TokenIsElevated) return false;
    BYTE groupsBuffer[16384];
    if(!GetTokenInformation(token,TokenGroups,groupsBuffer,sizeof(groupsBuffer),&length)) return false;
    BYTE adminBuffer[SECURITY_MAX_SID_SIZE]; DWORD sidSize=sizeof(adminBuffer);
    if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,adminBuffer,&sidSize)) return false;
    auto groups=reinterpret_cast<TOKEN_GROUPS*>(groupsBuffer);
    for(DWORD i=0;i<groups->GroupCount;++i)
        if((groups->Groups[i].Attributes&SE_GROUP_ENABLED) && EqualSid(groups->Groups[i].Sid,adminBuffer)) return true;
    return false;
}
inline bool ElevatedAdministrator(HANDLE process) {
    Handle token;
    return OpenProcessToken(process,TOKEN_QUERY,token.put()) && ElevatedAdminToken(token.get());
}
inline bool AuthenticatePipeClient(HANDLE pipe) {
    // Identification-level token permits inspecting identity but not acting
    // with the client's administrator privileges. No OpenProcessToken on an
    // elevated peer (which a LocalService UMDF host cannot normally perform).
    if(!ImpersonateNamedPipeClient(pipe))return false;
    Handle token;
    bool allowed=OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,token.put()) && ElevatedAdminToken(token.get());
    if(!RevertToSelf())return false;
    return allowed;
}

// Overlapped operations have bounded handshakes and cancellation. The frame
// producer NEVER performs pipe I/O or waits for this worker.
inline bool PipeTransfer(HANDLE pipe,HANDLE stop,void* buffer,DWORD size,bool write,DWORD timeout) {
    Handle event; *event.put()=CreateEventW(nullptr,TRUE,FALSE,nullptr); if(!event.get()) return false;
    OVERLAPPED overlapped{};overlapped.hEvent=event.get(); DWORD done=0;
    BOOL ok=write ? WriteFile(pipe,buffer,size,&done,&overlapped) : ReadFile(pipe,buffer,size,&done,&overlapped);
    if(ok) return done==size;
    if(GetLastError()!=ERROR_IO_PENDING) return false;
    HANDLE waits[]={event.get(),stop};
    if(WaitForMultipleObjects(stop?2:1,waits,FALSE,timeout)!=WAIT_OBJECT_0) {
        CancelIoEx(pipe,&overlapped);GetOverlappedResult(pipe,&overlapped,&done,TRUE);return false;
    }
    return GetOverlappedResult(pipe,&overlapped,&done,FALSE) && done==size;
}

class HandoffServer {
    Handle mapping,pipe,stop,thread;
    PoolInfo* info=nullptr;
    Producer producer;
    std::atomic<bool> connected{false};
    std::atomic<HRESULT> status{E_PENDING};
    static DWORD WINAPI Entry(void* self) { static_cast<HandoffServer*>(self)->Serve();return 0; }
    void Serve() {
        Handle event;*event.put()=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!event.get()){status=HRESULT_FROM_WIN32(GetLastError());return;}
        OVERLAPPED overlapped{};overlapped.hEvent=event.get();
        BOOL ready=ConnectNamedPipe(pipe.get(),&overlapped);
        DWORD error=ready?ERROR_SUCCESS:GetLastError();
        if(error==ERROR_IO_PENDING) {
            HANDLE waits[]={event.get(),stop.get()};
            if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0) {
                CancelIoEx(pipe.get(),&overlapped);DWORD ignored;GetOverlappedResult(pipe.get(),&overlapped,&ignored,TRUE);return;
            }
            DWORD ignored;
            if(!GetOverlappedResult(pipe.get(),&overlapped,&ignored,FALSE)){status=HRESULT_FROM_WIN32(GetLastError());return;}
        } else if(error!=ERROR_SUCCESS && error!=ERROR_PIPE_CONNECTED) { status=HRESULT_FROM_WIN32(error);return; }
        status=E_ACCESSDENIED;
        HandoffHello hello{};
        if(!PipeTransfer(pipe.get(),stop.get(),&hello,sizeof(hello),false,3000) || hello.version!=1 || hello.bytes!=sizeof(hello)) return;
        ULONG pid=0;
        if(!GetNamedPipeClientProcessId(pipe.get(),&pid)) return;
        if(!AuthenticatePipeClient(pipe.get())) {
            HandoffReply denied{};denied.bytes=sizeof(denied);denied.handles[0]=static_cast<uint32_t>(E_ACCESSDENIED);
            PipeTransfer(pipe.get(),stop.get(),&denied,sizeof(denied),true,3000);return;
        }
        HandoffReply reply{};reply.version=1;reply.bytes=sizeof(reply);
        reply.handles[0]=reinterpret_cast<uintptr_t>(mapping.get());
        for(unsigned i=0;i<SlotCount;++i)reply.handles[i+1]=reinterpret_cast<uintptr_t>(producer.SharedHandle(i));
        if(!PipeTransfer(pipe.get(),stop.get(),&reply,sizeof(reply),true,3000)) return;
        uint32_t ack=0;
        if(!PipeTransfer(pipe.get(),stop.get(),&ack,sizeof(ack),false,3000) || ack!=1) return;
        status=S_OK;connected.store(true,std::memory_order_release);
        // Only one consumer per swapchain generation. On exit, recreate the
        // virtual screen to get a fresh pool; never reuse abandoned textures.
        uint32_t goodbye=0;
        PipeTransfer(pipe.get(),stop.get(),&goodbye,sizeof(goodbye),false,INFINITE);
        connected.store(false,std::memory_order_release);status=HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE);
    }
public:
    ~HandoffServer() {
        connected=false;
        if(stop.get())SetEvent(stop.get());
        if(thread.get())WaitForSingleObject(thread.get(),INFINITE);
        if(info)UnmapViewOfFile(info);
    }
    HRESULT Init(ID3D11Device* device,const wchar_t* name=HandoffPipe) {
        if(info)return E_UNEXPECTED;
        *mapping.put()=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(PoolInfo),nullptr);
        if(!mapping.get())return HRESULT_FROM_WIN32(GetLastError());
        info=static_cast<PoolInfo*>(MapViewOfFile(mapping.get(),FILE_MAP_ALL_ACCESS,0,0,sizeof(PoolInfo)));
        if(!info)return HRESULT_FROM_WIN32(GetLastError());
        HRESULT hr=producer.Init(device,1920,1080,info);if(FAILED(hr))return hr;
        *stop.put()=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop.get())return HRESULT_FROM_WIN32(GetLastError());
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        // No ordinary-user or remote access. OW lets the UMDF owner create
        // the first instance; BA has read/write without create-instance bit.
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;OW)(A;;0x12019b;;;BA)",SDDL_REVISION_1,&descriptor,nullptr))return HRESULT_FROM_WIN32(GetLastError());
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
        HANDLE raw=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,256,256,3000,&security);
        DWORD error=GetLastError();LocalFree(descriptor);
        if(raw==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(error);
        *pipe.put()=raw;*thread.put()=CreateThread(nullptr,0,Entry,this,0,nullptr);
        return thread.get()?S_OK:HRESULT_FROM_WIN32(GetLastError());
    }
    HRESULT Publish(ID3D11Texture2D* texture,uint64_t sequence,int64_t qpc) {
        if(!connected.load(std::memory_order_acquire))return S_FALSE;
        HRESULT hr=producer.Publish(texture,sequence,qpc);
        if(FAILED(hr)){connected=false;status=hr;SetEvent(stop.get());}
        return hr;
    }
    bool Connected()const{return connected.load(std::memory_order_acquire);}
    HRESULT Status()const{return status.load();}
};
}
