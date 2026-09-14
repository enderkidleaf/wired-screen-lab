#pragma once
#include "SharedTexturePool.h"
#include "H264Color.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <codecapi.h>
#include <strmif.h>
#include <mftransform.h>
#include <d3d10.h>
#include <map>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdio>
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mf.lib")
#pragma comment(lib,"mfuuid.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
namespace WiredScreenGpu {
inline void VideoCheck(HRESULT hr,const char* operation) {
    if(FAILED(hr)){char text[200];sprintf_s(text,"%s: 0x%08lx",operation,static_cast<unsigned long>(hr));throw std::runtime_error(text);}
}
class MediaRuntime {
    bool com=false,media=false;
public:
    MediaRuntime(){VideoCheck(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM startup");com=true;
        HRESULT hr=MFStartup(MF_VERSION);if(FAILED(hr)){CoUninitialize();com=false;VideoCheck(hr,"MF startup");}media=true;}
    ~MediaRuntime(){if(media)MFShutdown();if(com)CoUninitialize();}
};
class GpuColorConverter {
public:
    struct Surface { ID3D11Texture2D* texture=nullptr; size_t slot=0; };
private:
    static constexpr size_t SurfaceCount=3;
    struct Slot { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11VideoProcessorOutputView> outputView; bool busy=false; };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VideoDevice> video;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    std::vector<Slot> slots; size_t nextSlot=0;
public:
    void Init(ID3D11Device* sourceDevice){
        device=sourceDevice;device->GetImmediateContext(&context);
        VideoCheck(device.As(&video),"video device");VideoCheck(context.As(&videoContext),"video context");
        ComPtr<ID3D10Multithread> lock;VideoCheck(context.As(&lock),"D3D multithread lock");lock->SetMultithreadProtected(TRUE);
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};desc.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        desc.InputFrameRate={60,1};desc.OutputFrameRate={60,1};desc.InputWidth=desc.OutputWidth=1920;desc.InputHeight=desc.OutputHeight=1080;desc.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        VideoCheck(video->CreateVideoProcessorEnumerator(&desc,&enumerator),"video processor enumerator");
        UINT flags=0;VideoCheck(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12,&flags),"NV12 support");
        if(!(flags&D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))throw std::runtime_error("GPU cannot output NV12");
        VideoCheck(video->CreateVideoProcessor(enumerator.Get(),0,&processor),"video processor");
        videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(),0,FALSE);
        videoContext->VideoProcessorSetStreamFrameFormat(processor.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        // Desktop RGB full range -> BT.709 limited-range NV12, also declared
        // on the encoder input/output media types below.
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE input{};input.RGB_Range=0;input.YCbCr_Matrix=1;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outputColor{};outputColor.YCbCr_Matrix=1;outputColor.Nominal_Range=1;
        videoContext->VideoProcessorSetStreamColorSpace(processor.Get(),0,&input);
        videoContext->VideoProcessorSetOutputColorSpace(processor.Get(),&outputColor);
        D3D11_TEXTURE2D_DESC output{};output.Width=1920;output.Height=1080;output.MipLevels=1;output.ArraySize=1;output.Format=DXGI_FORMAT_NV12;
        output.SampleDesc.Count=1;output.Usage=D3D11_USAGE_DEFAULT;output.BindFlags=D3D11_BIND_RENDER_TARGET;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{};ov.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
        slots.resize(SurfaceCount);
        for(auto& slot:slots){VideoCheck(device->CreateTexture2D(&output,nullptr,&slot.texture),"NV12 pool texture");VideoCheck(video->CreateVideoProcessorOutputView(slot.texture.Get(),enumerator.Get(),&ov,&slot.outputView),"NV12 pool view");}
    }
    Surface Convert(ID3D11Texture2D* source){
        D3D11_TEXTURE2D_DESC input{};source->GetDesc(&input);
        if(input.Width!=1920||input.Height!=1080||input.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||input.SampleDesc.Count!=1)throw std::runtime_error("Unexpected source texture");
        size_t selected=slots.size();for(size_t count=0;count<slots.size();++count){size_t candidate=(nextSlot+count)%slots.size();if(!slots[candidate].busy){selected=candidate;break;}}
        if(selected==slots.size())throw std::runtime_error("NV12 conversion pool exhausted");
        Slot& slot=slots[selected];slot.busy=true;nextSlot=(selected+1)%slots.size();
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};iv.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
        try{ComPtr<ID3D11VideoProcessorInputView> inView;VideoCheck(video->CreateVideoProcessorInputView(source,enumerator.Get(),&iv,&inView),"RGB view");
            D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=inView.Get();
            VideoCheck(videoContext->VideoProcessorBlt(processor.Get(),slot.outputView.Get(),0,1,&stream),"GPU color conversion");
            // The keyed-mutex release below must not let the IDD producer reuse its source before the GPU consumes it.
            context->Flush();return Surface{slot.texture.Get(),selected};
        }catch(...){slot.busy=false;throw;}
    }
    void Release(size_t slot){if(slot>=slots.size()||!slots[slot].busy)throw std::runtime_error("Invalid NV12 pool release");slots[slot].busy=false;}
};
struct NativeVideoPacket { std::vector<BYTE> bytes; FrameInfo frame{}; int64_t encodedQpc=0; };
// Event credits are independent of the application's in-flight frame limit.
class EncoderInputCredits {
    uint64_t available=0;
public:
    void Grant(){if(available==UINT64_MAX)throw std::runtime_error("Input credit overflow");++available;}
    bool Any()const{return available!=0;}
    void Consume(){if(!available)throw std::runtime_error("Input without event credit");--available;}
};
inline ComPtr<IMFMediaBuffer> AllocateEncoderOutput(DWORD size,DWORD alignment){
    if(alignment && (alignment&(alignment-1)))throw std::runtime_error("Invalid output buffer alignment");
    ComPtr<IMFMediaBuffer> memory;
    // MFT reports bytes; MFCreateAlignedMemoryBuffer takes an alignment mask.
    VideoCheck(MFCreateAlignedMemoryBuffer(size,alignment?alignment-1:0,&memory),"output allocation");
    return memory;
}
class GpuVideoEncoder {
    // Prefer a current desktop frame over encoder throughput: only one frame
    // may remain in the hardware MFT at a time.
    static constexpr size_t MaxInFlight=1;
    ComPtr<IMFTransform> encoder;
    ComPtr<IMFMediaEventGenerator> events;
    ComPtr<IMFDXGIDeviceManager> manager;
    struct Pending { FrameInfo frame; ComPtr<IMFSample> sample; size_t surfaceSlot=0; };
    std::map<LONGLONG,Pending> pending;
    EncoderInputCredits credits;LONGLONG nextPts=0;
    bool draining=false,drained=false,outputReady=false;
    static ComPtr<IMFMediaType> Type(const GUID& subtype){
        ComPtr<IMFMediaType> type;VideoCheck(MFCreateMediaType(&type),"media type");
        VideoCheck(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"major type");VideoCheck(type->SetGUID(MF_MT_SUBTYPE,subtype),"subtype");
        VideoCheck(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,1920,1080),"size");
        VideoCheck(MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,60,1),"frame rate");
        VideoCheck(MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1),"pixel aspect");
        VideoCheck(type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),"interlace");
        VideoCheck(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235),"range");
        VideoCheck(type->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT709),"matrix");
        VideoCheck(type->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT709),"primaries");return type;
    }
public:
    std::wstring name;
    bool lowLatencyAccepted=false,bFramesDisabled=false,colorMetadataAccepted=false;
    ~GpuVideoEncoder(){if(encoder){encoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,0);ComPtr<IMFShutdown> shutdown;if(SUCCEEDED(encoder.As(&shutdown)))shutdown->Shutdown();}}
    void Init(ID3D11Device* device,const LUID& luid){
        ComPtr<IMFAttributes> selection;VideoCheck(MFCreateAttributes(&selection,1),"selection attributes");
        UINT64 adapter=0;memcpy(&adapter,&luid,sizeof(adapter));VideoCheck(selection->SetUINT64(MFT_ENUM_ADAPTER_LUID,adapter),"adapter filter");
        MFT_REGISTER_TYPE_INFO input{MFMediaType_Video,MFVideoFormat_NV12},output{MFMediaType_Video,MFVideoFormat_H264};
        IMFActivate** available=nullptr;UINT32 count=0;
        VideoCheck(MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER,MFT_ENUM_FLAG_HARDWARE|MFT_ENUM_FLAG_SORTANDFILTER,&input,&output,selection.Get(),&available,&count),"hardware encoder enumeration");
        HRESULT activation=MF_E_TOPO_CODEC_NOT_FOUND;
        for(UINT32 i=0;i<count;++i){if(!encoder){activation=available[i]->ActivateObject(IID_PPV_ARGS(&encoder));
                if(SUCCEEDED(activation)){wchar_t* label=nullptr;UINT32 length=0;if(SUCCEEDED(available[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute,&label,&length))){name.assign(label,length);CoTaskMemFree(label);}}}
            available[i]->Release();}CoTaskMemFree(available);VideoCheck(activation,"activate same-adapter hardware encoder");
        if(!encoder)throw std::runtime_error("No same-adapter hardware H264 encoder");
        ComPtr<IMFAttributes> attributes;VideoCheck(encoder->GetAttributes(&attributes),"encoder attributes");
        VideoCheck(attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK,TRUE),"async unlock");
        UINT32 aware=0;if(FAILED(attributes->GetUINT32(MF_SA_D3D11_AWARE,&aware))||!aware)throw std::runtime_error("Encoder is not D3D11-aware");
        attributes->SetUINT32(MF_LOW_LATENCY,TRUE);
        UINT token=0;VideoCheck(MFCreateDXGIDeviceManager(&token,&manager),"device manager");VideoCheck(manager->ResetDevice(device,token),"manager device");
        VideoCheck(encoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,reinterpret_cast<ULONG_PTR>(manager.Get())),"encoder GPU device");
        auto out=Type(MFVideoFormat_H264);VideoCheck(out->SetUINT32(MF_MT_AVG_BITRATE,20000000),"bitrate");
        VideoCheck(out->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_Base),"baseline profile");
        VideoCheck(encoder->SetOutputType(0,out.Get(),0),"encoder output type");auto in=Type(MFVideoFormat_NV12);VideoCheck(encoder->SetInputType(0,in.Get(),0),"encoder input type");
        ComPtr<ICodecAPI> codec;
        if(SUCCEEDED(encoder.As(&codec))){VARIANT value;VariantInit(&value);value.vt=VT_BOOL;value.boolVal=VARIANT_TRUE;
            lowLatencyAccepted=SUCCEEDED(codec->SetValue(&CODECAPI_AVLowLatencyMode,&value));value.vt=VT_UI4;value.ulVal=0;
            bFramesDisabled=SUCCEEDED(codec->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount,&value));value.ulVal=60;codec->SetValue(&CODECAPI_AVEncMPVGOPSize,&value);
            value.ulVal=eAVEncVideoColorPrimaries_BT709;HRESULT color=codec->SetValue(&CODECAPI_AVEncVideoOutputColorPrimaries,&value);
            value.ulVal=eAVEncVideoColorTransferFunction_22_709;HRESULT transfer=codec->SetValue(&CODECAPI_AVEncVideoOutputColorTransferFunction,&value);
            value.ulVal=eAVEncVideoColorTransferMatrix_BT709;HRESULT matrix=codec->SetValue(&CODECAPI_AVEncVideoOutputColorTransferMatrix,&value);
            value.ulVal=eAVEncVideoColorNominalRange_16_235;HRESULT range=codec->SetValue(&CODECAPI_AVEncVideoOutputColorNominalRange,&value);
            colorMetadataAccepted=SUCCEEDED(color)&&SUCCEEDED(transfer)&&SUCCEEDED(matrix)&&SUCCEEDED(range);}
        VideoCheck(encoder.As(&events),"async encoder events");
        VideoCheck(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0),"begin streaming");
        VideoCheck(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0),"start streaming");
    }
    bool CanSubmit()const{return !draining && credits.Any() && pending.size()<MaxInFlight;}
    void Submit(const GpuColorConverter::Surface& surface,const FrameInfo& frame){
        if(!CanSubmit())throw std::runtime_error("Encoder input would exceed bounded capacity");
        ComPtr<IMFMediaBuffer> buffer;VideoCheck(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D),surface.texture,0,FALSE,&buffer),"GPU media buffer");
        ComPtr<IMFSample> sample;VideoCheck(MFCreateSample(&sample),"input sample");VideoCheck(sample->AddBuffer(buffer.Get()),"input buffer");
        const LONGLONG pts=nextPts++*10000000/60;
        VideoCheck(sample->SetSampleTime(pts),"input PTS");VideoCheck(sample->SetSampleDuration(10000000/60),"input duration");
        VideoCheck(encoder->ProcessInput(0,sample.Get(),0),"submit GPU sample");credits.Consume();pending.emplace(pts,Pending{frame,sample,surface.slot});
    }
    void Drain(){if(!draining){VideoCheck(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0),"end input");VideoCheck(encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0),"drain");draining=true;}}
    bool Drained()const{return drained;}
    bool Poll(NativeVideoPacket& packet,size_t* releasedSurface=nullptr){
        if(!outputReady){
        ComPtr<IMFMediaEvent> event;HRESULT hr=events->GetEvent(MF_EVENT_FLAG_NO_WAIT,&event);
        if(hr==MF_E_NO_EVENTS_AVAILABLE)return false;VideoCheck(hr,"encoder event");
        HRESULT status=S_OK;VideoCheck(event->GetStatus(&status),"event status");VideoCheck(status,"encoder asynchronous failure");
        MediaEventType type=MEUnknown;VideoCheck(event->GetType(&type),"event type");
        if(type==METransformNeedInput){credits.Grant();return false;}
        if(type==METransformDrainComplete){drained=true;if(!pending.empty())throw std::runtime_error("Encoder drained without all submitted frames");return false;}
        if(type!=METransformHaveOutput)return false;
        outputReady=true;
        }
        MFT_OUTPUT_STREAM_INFO stream{};VideoCheck(encoder->GetOutputStreamInfo(0,&stream),"output stream");
        ComPtr<IMFSample> supplied;
        if(!(stream.dwFlags&MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)){
            VideoCheck(MFCreateSample(&supplied),"output sample");auto memory=AllocateEncoderOutput(stream.cbSize,stream.cbAlignment);
            VideoCheck(supplied->AddBuffer(memory.Get()),"output buffer");}
        MFT_OUTPUT_DATA_BUFFER result{};result.pSample=supplied.Get();DWORD flags=0;
        HRESULT hr=encoder->ProcessOutput(0,1,&result,&flags);
        ComPtr<IMFCollection> outputEvents;outputEvents.Attach(result.pEvents);
        ComPtr<IMFSample> sample;if(supplied)sample=supplied;else sample.Attach(result.pSample);
        if(hr==MF_E_TRANSFORM_STREAM_CHANGE){
            ComPtr<IMFMediaType> changed;VideoCheck(encoder->GetOutputAvailableType(0,0,&changed),"changed output type");
            GUID subtype{};VideoCheck(changed->GetGUID(MF_MT_SUBTYPE,&subtype),"changed subtype");UINT32 width=0,height=0;
            VideoCheck(MFGetAttributeSize(changed.Get(),MF_MT_FRAME_SIZE,&width,&height),"changed dimensions");
            if(subtype!=MFVideoFormat_H264||width!=1920||height!=1080)throw std::runtime_error("Unexpected encoded stream change");
            VideoCheck(encoder->SetOutputType(0,changed.Get(),0),"accept encoded stream change");outputReady=false;return false;
        }
        outputReady=false;
        VideoCheck(hr,"encoded output");if(!sample)throw std::runtime_error("Empty encoded sample");
        LONGLONG pts=0;VideoCheck(sample->GetSampleTime(&pts),"output PTS");auto found=pending.find(pts);
        if(found==pending.end())throw std::runtime_error("Output PTS does not match submitted source frame");
        packet.frame=found->second.frame;
        ComPtr<IMFMediaBuffer> bytes;VideoCheck(sample->ConvertToContiguousBuffer(&bytes),"encoded bytes");BYTE* data=nullptr;DWORD size=0;
        VideoCheck(bytes->Lock(&data,nullptr,&size),"encoded buffer lock");
        if(!size||size>16*1024*1024){bytes->Unlock();throw std::runtime_error("Invalid encoded packet size");}
        try{packet.bytes.assign(data,data+size);}catch(...){bytes->Unlock();throw;}
        VideoCheck(bytes->Unlock(),"encoded buffer unlock");packet.bytes=H264Bt709(packet.bytes);
        LARGE_INTEGER qpc;QueryPerformanceCounter(&qpc);packet.encodedQpc=qpc.QuadPart;
        if(releasedSurface)*releasedSurface=found->second.surfaceSlot;pending.erase(found);return true;
    }
};
}
