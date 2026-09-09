#include <windows.h>
#include <swdevice.h>
#include <new>

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
