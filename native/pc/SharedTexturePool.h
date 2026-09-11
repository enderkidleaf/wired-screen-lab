#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

namespace WiredScreenGpu {
using Microsoft::WRL::ComPtr;
constexpr unsigned SlotCount = 3;
// CPU metadata lives in a shared mapping. Access a slot only while holding its
// texture's keyed mutex. No process-local pointers or handles in this layout.
struct FrameInfo { uint64_t sequence; int64_t capturedQpc; };
struct PoolInfo {
    uint32_t version, width, height, format;
    LUID adapter;
    int64_t qpcFrequency;
    FrameInfo frames[SlotCount];
};
static_assert(sizeof(FrameInfo) == 16, "Stable IPC frame layout");
static_assert(sizeof(PoolInfo) == 80, "Stable IPC pool layout");

class Handle {
    HANDLE value = nullptr;
public:
    Handle() = default;
    ~Handle() { if (value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value; }
    HANDLE* put() { return &value; }
};

// Single producer, single consumer on the SAME adapter. Key 0 means writable;
// key 1 means a complete submitted copy is available. Only S_OK grants access:
// WAIT_TIMEOUT and WAIT_ABANDONED are positive HRESULT values, not success.
// The caller must tear down the session on abandonment/device loss.
class Producer {
    struct Slot { ComPtr<ID3D11Texture2D> texture; ComPtr<IDXGIKeyedMutex> mutex; Handle handle; };
    std::array<Slot, SlotCount> slots;
    ComPtr<ID3D11DeviceContext> context;
    D3D11_TEXTURE2D_DESC desc{};
    PoolInfo* info = nullptr;
    unsigned next = 0;
public:
    HRESULT Init(ID3D11Device* device, UINT width, UINT height, PoolInfo* shared) {
        if (!device || !shared || !width || !height || info) return E_INVALIDARG;
        desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        for (auto& slot : slots) {
            HRESULT hr=device->CreateTexture2D(&desc,nullptr,&slot.texture); if (FAILED(hr)) return hr;
            hr=slot.texture.As(&slot.mutex); if (FAILED(hr)) return hr;
            ComPtr<IDXGIResource1> resource;
            hr=slot.texture.As(&resource); if (FAILED(hr)) return hr;
            // Unnamed NT handles: hand off only to an authenticated peer using
            // DuplicateHandle (or an explicit inheritance list in the test).
            hr=resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,nullptr,slot.handle.put());
            if (FAILED(hr)) return hr;
        }
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC adapterDesc{};
        HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&dxgi)); if (FAILED(hr)) return hr;
        hr=dxgi->GetAdapter(&adapter); if (FAILED(hr)) return hr;
        hr=adapter->GetDesc(&adapterDesc); if (FAILED(hr)) return hr;
        device->GetImmediateContext(&context);
        *shared={}; shared->version=1; shared->width=width; shared->height=height;
        shared->format=desc.Format; shared->adapter=adapterDesc.AdapterLuid;
        LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency); shared->qpcFrequency=frequency.QuadPart;
        info=shared;
        return S_OK;
    }
    HANDLE SharedHandle(unsigned index) const { return index<SlotCount ? slots[index].handle.get() : nullptr; }
    HRESULT Publish(ID3D11Texture2D* source, uint64_t sequence, int64_t capturedQpc) {
        if (!info || !source || !sequence) return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC input{}; source->GetDesc(&input);
        if(input.Width!=desc.Width || input.Height!=desc.Height || input.Format!=desc.Format ||
           input.SampleDesc.Count!=1 || input.MipLevels!=1 || input.ArraySize!=1) return E_INVALIDARG;
        ComPtr<ID3D11Device> sourceDevice, ownDevice;
        source->GetDevice(&sourceDevice); context->GetDevice(&ownDevice);
        if(sourceDevice.Get()!=ownDevice.Get()) return E_INVALIDARG;
        for(unsigned n=0;n<SlotCount;++n) {
            unsigned index=(next+n)%SlotCount; auto& slot=slots[index];
            HRESULT hr=slot.mutex->AcquireSync(0,0);
            if(hr==WAIT_TIMEOUT) continue;
            if(hr!=S_OK) return hr==WAIT_ABANDONED ? DXGI_ERROR_DEVICE_REMOVED : hr;
            context->CopyResource(slot.texture.Get(),source);
            info->frames[index]={sequence,capturedQpc}; MemoryBarrier();
            context->Flush();
            hr=slot.mutex->ReleaseSync(1);
            if(FAILED(hr)) return hr;
            next=(index+1)%SlotCount; return S_OK;
        }
        // Backpressure drops only an unencoded source frame, never H.264 data.
        // Never wait on the consumer while owning an IddCx system buffer.
        return S_FALSE;
    }
};

class Consumer {
    std::array<ComPtr<ID3D11Texture2D>,SlotCount> textures;
    std::array<ComPtr<IDXGIKeyedMutex>,SlotCount> mutexes;
    PoolInfo* info=nullptr;
    int held=-1;
public:
    ~Consumer() { Release(); }
    HRESULT Init(ID3D11Device* device, PoolInfo* shared, const HANDLE* handles) {
        if(!device || !shared || !handles || info || shared->version!=1 || !shared->width || !shared->height ||
           shared->format!=DXGI_FORMAT_B8G8R8A8_UNORM || shared->qpcFrequency<=0) return E_INVALIDARG;
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC adapterDesc{};
        HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&dxgi)); if(FAILED(hr)) return hr;
        hr=dxgi->GetAdapter(&adapter); if(FAILED(hr)) return hr;
        hr=adapter->GetDesc(&adapterDesc); if(FAILED(hr)) return hr;
        if(adapterDesc.AdapterLuid.HighPart!=shared->adapter.HighPart || adapterDesc.AdapterLuid.LowPart!=shared->adapter.LowPart) return DXGI_ERROR_UNSUPPORTED;
        ComPtr<ID3D11Device1> device1; hr=device->QueryInterface(IID_PPV_ARGS(&device1)); if(FAILED(hr)) return hr;
        for(unsigned i=0;i<SlotCount;++i) {
            hr=device1->OpenSharedResource1(handles[i],IID_PPV_ARGS(&textures[i])); if(FAILED(hr)) return hr;
            D3D11_TEXTURE2D_DESC desc{}; textures[i]->GetDesc(&desc);
            if(desc.Width!=shared->width || desc.Height!=shared->height || static_cast<uint32_t>(desc.Format)!=shared->format || desc.SampleDesc.Count!=1) return E_INVALIDARG;
            hr=textures[i].As(&mutexes[i]); if(FAILED(hr)) return hr;
        }
        info=shared; return S_OK;
    }
    HRESULT TakeLatest(ID3D11Texture2D** texture, FrameInfo* frame) {
        if(!info || !texture || !frame || held>=0) return E_INVALIDARG;
        *texture=nullptr; *frame={};
        for(unsigned i=0;i<SlotCount;++i) {
            HRESULT hr=mutexes[i]->AcquireSync(1,0);
            if(hr==WAIT_TIMEOUT) continue;
            if(hr!=S_OK) { Release(); return hr==WAIT_ABANDONED ? DXGI_ERROR_DEVICE_REMOVED : hr; }
            MemoryBarrier(); FrameInfo candidate=info->frames[i];
            if(held<0 || candidate.sequence>frame->sequence) {
                hr=Release(); if(FAILED(hr)) { mutexes[i]->ReleaseSync(0); return hr; }
                held=static_cast<int>(i); *frame=candidate;
            } else { hr=mutexes[i]->ReleaseSync(0); if(FAILED(hr)) { Release(); return hr; } }
        }
        if(held<0) return S_FALSE;
        // Borrowed pointer, valid until Release. Encoder must finish reading or
        // copy into its own input surface BEFORE releasing this slot.
        *texture=textures[held].Get(); return S_OK;
    }
    HRESULT Release() {
        if(held<0) return S_OK;
        int index=held; held=-1; return mutexes[index]->ReleaseSync(0);
    }
};
}
