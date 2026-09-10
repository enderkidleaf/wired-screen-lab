#include <windows.h>
#include <swdevice.h>
#include <new>
#include <dxgi1_2.h>

extern "C" __declspec(dllexport) HRESULT WINAPI WiredScreenFindOutput(PCWSTR name, UINT* adapterIndex, UINT* outputIndex) {
    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) return hr;
    for (UINT a=0;;a++) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(a,&adapter) == DXGI_ERROR_NOT_FOUND || !adapter) break;
        for (UINT o=0;;o++) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(o,&output) == DXGI_ERROR_NOT_FOUND || !output) break;
            DXGI_OUTPUT_DESC desc = {};
            hr=output->GetDesc(&desc); output->Release();
            if (SUCCEEDED(hr) && wcscmp(desc.DeviceName,name)==0 && desc.AttachedToDesktop) {
                *adapterIndex=a; *outputIndex=o;
                adapter->Release(); factory->Release(); return S_OK;
            }
        }
        adapter->Release();
    }
    factory->Release(); return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

struct CallbackContext {
    SW_DEVICE_CREATE_CALLBACK callback;
    void* context;
};

// SwDeviceCreate retains the callback's module. A CLR delegate thunk has no
// PE module, so give Windows a callback implemented inside this native DLL.
static void CALLBACK Created(HSWDEVICE device, HRESULT result, void* context, PCWSTR id) {
    CallbackContext* state = static_cast<CallbackContext*>(context);
    state->callback(device, result, state->context, id);
    delete state;
}

extern "C" __declspec(dllexport) HRESULT WINAPI WiredScreenSwDeviceCreate(
    PCWSTR enumerator, PCWSTR parent, const SW_DEVICE_CREATE_INFO* info,
    ULONG count, const DEVPROPERTY* properties, SW_DEVICE_CREATE_CALLBACK callback,
    void* context, PHSWDEVICE device) {
    CallbackContext* state = new(std::nothrow) CallbackContext{callback, context};
    if (!state) return E_OUTOFMEMORY;
    HRESULT result = SwDeviceCreate(enumerator, parent, info, count, properties, Created, state, device);
    if (FAILED(result)) delete state;
    return result;
}
