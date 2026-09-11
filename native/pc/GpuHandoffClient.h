#pragma once
#include "GpuHandoff.h"
namespace WiredScreenGpu {
class HandoffClient {
    Handle pipe,sourceProcess;
    std::array<Handle,4> resources;
    PoolInfo* info=nullptr;
    bool started=false;
public:
    ~HandoffClient(){if(info)UnmapViewOfFile(info);}
    // expectedPid must come from a trusted launcher or independently verified
    // driver host. A pipe name alone is not server authentication.
    HRESULT Connect(const wchar_t* name,DWORD expectedPid,bool (*verifyHost)(DWORD)=nullptr) {
        if((!expectedPid && !verifyHost) || started)return E_INVALIDARG;
        started=true;
        HANDLE raw=CreateFileW(name,0x12019b,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
        if(raw==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());
        *pipe.put()=raw;ULONG pid=0;
        if(!GetNamedPipeServerProcessId(pipe.get(),&pid) || (expectedPid && pid!=expectedPid) || (verifyHost && !verifyHost(pid)))return E_ACCESSDENIED;
        *sourceProcess.put()=OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!sourceProcess.get())return HRESULT_FROM_WIN32(GetLastError());
        HandoffHello hello{1,sizeof(HandoffHello)};
        if(!PipeTransfer(pipe.get(),nullptr,&hello,sizeof(hello),true,3000))return E_FAIL;
        HandoffReply reply{};
        if(!PipeTransfer(pipe.get(),nullptr,&reply,sizeof(reply),false,3000))return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        if(reply.version==0 && reply.bytes==sizeof(reply)) {
            HRESULT refusal=static_cast<HRESULT>(reply.handles[0]);
            return FAILED(refusal)?refusal:E_ACCESSDENIED;
        }
        if(reply.version!=1 || reply.bytes!=sizeof(reply))return E_FAIL;
        for(unsigned i=0;i<4;++i) {
            if(!reply.handles[i] || reply.handles[i]>UINTPTR_MAX)return E_INVALIDARG;
            // Only the metadata mapping is narrowed to read-only access. The
            // GPU handles need read/write for keyed mutex synchronization.
            if(!DuplicateHandle(sourceProcess.get(),reinterpret_cast<HANDLE>(static_cast<uintptr_t>(reply.handles[i])),
                GetCurrentProcess(),resources[i].put(),i==0?FILE_MAP_READ:0,FALSE,i==0?0:DUPLICATE_SAME_ACCESS))return HRESULT_FROM_WIN32(GetLastError());
        }
        info=static_cast<PoolInfo*>(MapViewOfFile(resources[0].get(),FILE_MAP_READ,0,0,sizeof(PoolInfo)));
        if(!info)return HRESULT_FROM_WIN32(GetLastError());
        if(info->version!=1 || info->width!=1920 || info->height!=1080 || info->format!=DXGI_FORMAT_B8G8R8A8_UNORM || info->qpcFrequency<=0)return E_INVALIDARG;
        return S_OK;
    }
    const PoolInfo* Info()const{return info;}
    HRESULT InitConsumer(ID3D11Device* device,Consumer& consumer) {
        if(!info)return E_UNEXPECTED;
        HANDLE handles[]={resources[1].get(),resources[2].get(),resources[3].get()};
        HRESULT hr=consumer.Init(device,info,handles);if(FAILED(hr))return hr;
        uint32_t ack=1;
        return PipeTransfer(pipe.get(),nullptr,&ack,sizeof(ack),true,3000)?S_OK:E_FAIL;
    }
};
}
