#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../include/nvof-motion-provider.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include <d3dcompiler.h>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr unsigned int kPrepDescriptorsPerSlot = 2;
constexpr unsigned int kConversionDescriptorsPerSlot = 8;
constexpr unsigned int kVisualizationDescriptorsPerSlot = 4;

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource *resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

bool Contains(const std::vector<DXGI_FORMAT> &formats, DXGI_FORMAT format)
{
    return std::find(formats.begin(), formats.end(), format) != formats.end();
}

HRESULT CompileShader(const char *source, const char *name, const char *entry,
    ComPtr<ID3DBlob> &bytecode, std::string &error_text)
{
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source, std::strlen(source), name, nullptr,
        nullptr, entry, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &bytecode, &errors);
    if (FAILED(hr) && errors)
        error_text.assign(static_cast<const char *>(errors->GetBufferPointer()),
            errors->GetBufferSize());
    return hr;
}

bool CreateTexture(ID3D12Device *device, unsigned int width, unsigned int height,
    DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource> &resource)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return SUCCEEDED(device->CreateCommittedResource(&heap,
        D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)));
}

D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptor(ID3D12DescriptorHeap *heap,
    unsigned int stride, unsigned int index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(stride) * index;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptor(ID3D12DescriptorHeap *heap,
    unsigned int stride, unsigned int index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(stride) * index;
    return handle;
}
}

NvofMotionProvider::~NvofMotionProvider()
{
    Shutdown();
}

void NvofMotionProvider::SetStatus(const char *format, ...)
{
    char message[384] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    status_ = message;
}

void NvofMotionProvider::Log(const char *format, ...) const
{
    if (!log_) return;
    char message[512] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    log_(message);
}

bool NvofMotionProvider::LoadApi()
{
    module_ = LoadLibraryW(L"nvofapi64.dll");
    if (!module_)
    {
        SetStatus("NVIDIA Optical Flow driver library not found (error %lu)",
            GetLastError());
        return false;
    }
    using CreateInstance = NV_OF_STATUS (NVOFAPI *)(uint32_t,
        NV_OF_D3D12_API_FUNCTION_LIST *);
    const auto create = reinterpret_cast<CreateInstance>(
        GetProcAddress(module_, "NvOFAPICreateInstanceD3D12"));
    if (!create)
    {
        SetStatus("NVIDIA Optical Flow D3D12 entry point is unavailable");
        return false;
    }
    const NV_OF_STATUS status = create(NV_OF_API_VERSION, &api_);
    if (status != NV_OF_SUCCESS)
    {
        SetStatus("NVIDIA Optical Flow API initialization failed (%d)", status);
        return false;
    }
    return true;
}

bool NvofMotionProvider::CreatePipelineState()
{
    D3D12_DESCRIPTOR_HEAP_DESC heap = {};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = kSlotCount * kPrepDescriptorsPerSlot;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device_->CreateDescriptorHeap(&heap,
        IID_PPV_ARGS(&prep_descriptors_));
    heap.NumDescriptors = kSlotCount * kConversionDescriptorsPerSlot;
    if (SUCCEEDED(hr))
        hr = device_->CreateDescriptorHeap(&heap,
            IID_PPV_ARGS(&conversion_descriptors_));
    heap.NumDescriptors = kSlotCount * kVisualizationDescriptorsPerSlot;
    if (SUCCEEDED(hr))
        hr = device_->CreateDescriptorHeap(&heap,
            IID_PPV_ARGS(&visualization_descriptors_));
    if (FAILED(hr))
    {
        SetStatus("Optical Flow descriptor allocation failed (0x%08X)",
            static_cast<unsigned int>(hr));
        return false;
    }
    prep_descriptor_stride_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    conversion_descriptor_stride_ = prep_descriptor_stride_;
    visualization_descriptor_stride_ = prep_descriptor_stride_;

    D3D12_DESCRIPTOR_RANGE prep_ranges[2] = {};
    prep_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    prep_ranges[0].NumDescriptors = 1;
    prep_ranges[0].BaseShaderRegister = 0;
    prep_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    prep_ranges[1].NumDescriptors = 1;
    prep_ranges[1].BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER prep_params[2] = {};
    for (unsigned int index = 0; index < 2; ++index)
    {
        prep_params[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        prep_params[index].DescriptorTable.NumDescriptorRanges = 1;
        prep_params[index].DescriptorTable.pDescriptorRanges = &prep_ranges[index];
        prep_params[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC prep_desc = {};
    prep_desc.NumParameters = 2;
    prep_desc.pParameters = prep_params;
    ComPtr<ID3DBlob> signature, errors, prep_cs;
    hr = D3D12SerializeRootSignature(&prep_desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &errors);
    if (SUCCEEDED(hr))
        hr = device_->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&prep_root_));
    static const char prep_shader[] =
        "Texture2D<float4> Source:register(t0);"
        "RWTexture2D<float> OpticalInput:register(u0);"
        "[numthreads(8,8,1)] void CS(uint3 id:SV_DispatchThreadID){"
        "uint w,h;OpticalInput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;"
        "float4 c=Source.Load(int3(id.xy,0));"
        "float peak=max(max(c.r,c.g),max(c.b,1.0));"
        "c.rgb=saturate(c.rgb/peak);"
        "OpticalInput[id.xy]=dot(c.rgb,float3(0.2126,0.7152,0.0722));}";
    std::string compile_error;
    if (SUCCEEDED(hr))
        hr = CompileShader(prep_shader, "nvof-input", "CS", prep_cs,
            compile_error);
    D3D12_COMPUTE_PIPELINE_STATE_DESC prep_pso = {};
    prep_pso.pRootSignature = prep_root_.Get();
    if (prep_cs)
        prep_pso.CS = {prep_cs->GetBufferPointer(), prep_cs->GetBufferSize()};
    if (SUCCEEDED(hr))
        hr = device_->CreateComputePipelineState(&prep_pso,
            IID_PPV_ARGS(&prep_pipeline_));

    D3D12_DESCRIPTOR_RANGE conversion_ranges[2] = {};
    conversion_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    conversion_ranges[0].NumDescriptors = 5;
    conversion_ranges[0].BaseShaderRegister = 0;
    conversion_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    conversion_ranges[1].NumDescriptors = 3;
    conversion_ranges[1].BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER conversion_params[3] = {};
    for (unsigned int index = 0; index < 2; ++index)
    {
        conversion_params[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        conversion_params[index].DescriptorTable.NumDescriptorRanges = 1;
        conversion_params[index].DescriptorTable.pDescriptorRanges =
            &conversion_ranges[index];
        conversion_params[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    conversion_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    conversion_params[2].Constants.ShaderRegister = 0;
    conversion_params[2].Constants.Num32BitValues = 13;
    D3D12_ROOT_SIGNATURE_DESC conversion_desc = {};
    conversion_desc.NumParameters = 3;
    conversion_desc.pParameters = conversion_params;
    signature.Reset();
    errors.Reset();
    if (SUCCEEDED(hr))
        hr = D3D12SerializeRootSignature(&conversion_desc,
            D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (SUCCEEDED(hr))
        hr = device_->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&conversion_root_));
    static const char conversion_shader[] =
        "Texture2D<int2> Forward:register(t0);"
        "Texture2D<int2> Backward:register(t1);"
        "Texture2D<uint> ForwardCost:register(t2);"
        "Texture2D<uint> BackwardCost:register(t3);"
        "Texture2D<float> SceneDepth:register(t4);"
        "RWTexture2D<float2> Motion:register(u0);"
        "RWTexture2D<float> HistoryMask:register(u1);"
        "RWTexture2D<float> GeometryDepth:register(u2);"
        "cbuffer C:register(b0){uint Width;uint Height;uint Grid;float Consistency;"
        "float CostThreshold;float CostScale;uint Reset;uint UseDepth;"
        "uint DepthWidth;uint DepthHeight;uint DepthReversed;float DepthSensitivity;"
        "float MotionRepairStrength;}"
        "[numthreads(8,8,1)] void CS(uint3 id:SV_DispatchThreadID){"
        "if(id.x>=Width||id.y>=Height)return;uint2 cell=id.xy/Grid;"
        "float2 f=float2(Forward.Load(int3(cell,0)))/32.0;"
        "float2 previous=float2(id.xy)+f;bool outside=any(previous<0.0)||"
        "previous.x>=Width||previous.y>=Height;"
        "uint2 priorCell=uint2(clamp(previous,float2(0,0),"
        "float2(Width-1,Height-1)))/Grid;"
        "float2 b=float2(Backward.Load(int3(priorCell,0)))/32.0;"
        "float consistency=length(f+b);"
        "float cf=ForwardCost.Load(int3(cell,0))*CostScale;"
        "float cb=BackwardCost.Load(int3(priorCell,0))*CostScale;"
        "float badFlow=smoothstep(Consistency,Consistency*2.0,consistency);"
        "float badCost=smoothstep(CostThreshold,1.0,max(cf,cb));"
        "float depthReject=0.0;float sceneDepth=DepthReversed!=0?0.0:1.0;"
        "float2 depthScale=float2(DepthWidth,DepthHeight)/float2(Width,Height);"
        "float clearDepth=DepthReversed!=0?0.0:1.0;float valid=0.0;"
        "if(UseDepth!=0){"
        "uint2 dp=min(uint2((float2(id.xy)+0.5)*depthScale),uint2(DepthWidth-1,DepthHeight-1));"
        "sceneDepth=SceneDepth.Load(int3(dp,0));"
        "uint2 dx0=uint2(dp.x>0?dp.x-1:dp.x,dp.y);uint2 dx1=uint2(min(dp.x+1,DepthWidth-1),dp.y);"
        "uint2 dy0=uint2(dp.x,dp.y>0?dp.y-1:dp.y);uint2 dy1=uint2(dp.x,min(dp.y+1,DepthHeight-1));"
        "float localDelta=max(max(abs(sceneDepth-SceneDepth.Load(int3(dx0,0))),abs(sceneDepth-SceneDepth.Load(int3(dx1,0)))),"
        "max(abs(sceneDepth-SceneDepth.Load(int3(dy0,0))),abs(sceneDepth-SceneDepth.Load(int3(dy1,0)))));"
        "float2 endpoint=clamp(previous,float2(0,0),float2(Width-1,Height-1));"
        "uint2 ep=min(uint2((endpoint+0.5)*depthScale),uint2(DepthWidth-1,DepthHeight-1));"
        "float endpointDelta=abs(sceneDepth-SceneDepth.Load(int3(ep,0)));"
        "valid=abs(sceneDepth-clearDepth)>1e-7?1.0:0.0;"
        "depthReject=valid*saturate(max(smoothstep(DepthSensitivity*0.25,DepthSensitivity,localDelta),"
        "smoothstep(DepthSensitivity*0.5,DepthSensitivity*2.0,endpointDelta)));}"
        "float rejection=saturate(max(max(badFlow,badCost),depthReject));float rawRejection=rejection;"
        "float2 repaired=f;float2 bestFlow=f;float bestScore=1000.0;"
        "float2 motionSum=float2(0,0);float motionWeight=0.0;float motionEnergy=0.0;"
        "float rejectionSum=0.0;float surfaceWeightSum=0.0;float coverage=0.0;"
        "float foregroundEvidence=0.0;float2 foregroundMotionSum=float2(0,0);"
        "float foregroundMotionWeight=0.0;float foregroundRejectionSum=0.0;"
        "float foregroundSurfaceWeight=0.0;"
        "if(UseDepth!=0&&valid>0.5&&MotionRepairStrength>0.0){"
        "static const int2 offsets[20]={int2(-4,0),int2(4,0),int2(0,-4),int2(0,4),"
        "int2(-8,0),int2(8,0),int2(0,-8),int2(0,8),"
        "int2(-16,0),int2(16,0),int2(0,-16),int2(0,16),"
        "int2(-32,0),int2(32,0),int2(0,-32),int2(0,32),"
        "int2(-8,-8),int2(8,-8),int2(-8,8),int2(8,8)};"
        "[unroll]for(int i=0;i<20;i++){int2 candidate=int2(id.xy)+offsets[i];"
        "if(any(candidate<0)||candidate.x>=int(Width)||candidate.y>=int(Height))continue;"
        "uint2 cp=uint2(candidate);uint2 cdp=min(uint2((float2(cp)+0.5)*depthScale),uint2(DepthWidth-1,DepthHeight-1));"
        "float cd=SceneDepth.Load(int3(cdp,0));if(abs(cd-clearDepth)<=1e-7)continue;"
        "float depthGap=abs(cd-sceneDepth)/max(max(abs(cd),abs(sceneDepth)),1e-4);"
        "float signedDepthGap=(DepthReversed!=0?sceneDepth-cd:cd-sceneDepth)/"
        "max(max(abs(cd),abs(sceneDepth)),1e-4);"
        "uint2 ccell=cp/Grid;float2 candidateFlow=float2(Forward.Load(int3(ccell,0)))/32.0;"
        "float2 candidatePrevious=float2(cp)+candidateFlow;bool candidateOutside=any(candidatePrevious<0.0)||"
        "candidatePrevious.x>=Width||candidatePrevious.y>=Height;"
        "uint2 cbcell=uint2(clamp(candidatePrevious,float2(0,0),float2(Width-1,Height-1)))/Grid;"
        "float2 candidateBackward=float2(Backward.Load(int3(cbcell,0)))/32.0;"
        "float candidateFlowBad=smoothstep(Consistency,Consistency*2.0,length(candidateFlow+candidateBackward));"
        "float candidateCost=max(ForwardCost.Load(int3(ccell,0))*CostScale,"
        "BackwardCost.Load(int3(cbcell,0))*CostScale);"
        "float candidateBad=max(candidateFlowBad,smoothstep(CostThreshold,1.0,candidateCost));"
        "float surfaceMismatch=smoothstep(DepthSensitivity,DepthSensitivity*4.0,depthGap);"
        "float distanceWeight=i<4?1.0:(i<8?0.8:(i<12?0.6:(i<16?0.4:0.65)));"
        "float reliability=(candidateOutside?0.0:1.0)*saturate(1.0-candidateBad);"
        "float surfaceWeight=(candidateOutside?0.0:1.0)*(1.0-surfaceMismatch)*distanceWeight;"
        "rejectionSum+=candidateBad*surfaceWeight;surfaceWeightSum+=surfaceWeight;"
        "float weight=reliability*surfaceWeight;"
        "motionSum+=candidateFlow*weight;motionWeight+=weight;motionEnergy+=length(candidateFlow)*weight;"
        "float fartherBackground=smoothstep(DepthSensitivity,DepthSensitivity*4.0,signedDepthGap);"
        "foregroundEvidence+=fartherBackground*distanceWeight;"
        "float relaxedSurface=1.0-smoothstep(DepthSensitivity*2.0,DepthSensitivity*12.0,depthGap);"
        "float notFarBackground=1.0-smoothstep(DepthSensitivity,DepthSensitivity*3.0,signedDepthGap);"
        "float foregroundWeight=(candidateOutside?0.0:1.0)*relaxedSurface*notFarBackground*distanceWeight;"
        "foregroundRejectionSum+=candidateBad*foregroundWeight;"
        "foregroundSurfaceWeight+=foregroundWeight;"
        "float trustedForegroundWeight=foregroundWeight*reliability;"
        "foregroundMotionSum+=candidateFlow*trustedForegroundWeight;"
        "foregroundMotionWeight+=trustedForegroundWeight;"
        "float score=(candidateOutside?1.0:candidateBad)+surfaceMismatch*2.0+(1.0-distanceWeight)*0.1;"
        "if(score<bestScore){bestScore=score;bestFlow=candidateFlow;}}"
        "if(bestScore<rawRejection)repaired=bestFlow;"
        "float2 consensus=motionWeight>0.5?motionSum/motionWeight:f;"
        "float coherence=motionEnergy>0.01?length(motionSum)/motionEnergy:0.0;"
        "float coverageGap=length(consensus)-length(f);"
        "coverage=smoothstep(0.5,2.0,coverageGap)*smoothstep(0.55,0.82,coherence)*"
        "smoothstep(0.5,2.0,motionWeight);"
        "repaired=lerp(repaired,consensus,coverage);"
        "float surfaceRejection=surfaceWeightSum>0.5?rejectionSum/surfaceWeightSum:rawRejection;"
        "float interiorTrust=saturate(1.0-surfaceRejection);"
        "float confidenceFill=smoothstep(0.45,0.85,interiorTrust)*smoothstep(1.0,3.0,surfaceWeightSum);"
        "rejection=lerp(rawRejection,min(rawRejection,surfaceRejection),"
        "saturate(MotionRepairStrength*confidenceFill));"
        "float foregroundRejection=foregroundSurfaceWeight>0.5?"
        "foregroundRejectionSum/foregroundSurfaceWeight:rawRejection;"
        "float foregroundTrust=saturate(1.0-foregroundRejection);"
        "float foregroundHalo=smoothstep(0.5,1.5,foregroundEvidence)*"
        "smoothstep(0.5,1.5,foregroundMotionWeight)*smoothstep(0.45,0.85,foregroundTrust);"
        "float2 foregroundMotion=foregroundMotionWeight>0.5?"
        "foregroundMotionSum/foregroundMotionWeight:repaired;"
        "repaired=lerp(repaired,foregroundMotion,foregroundHalo);"
        "rejection=lerp(rejection,min(rejection,foregroundRejection),"
        "saturate(MotionRepairStrength*foregroundHalo));coverage=max(coverage,foregroundHalo);}"
        "float repairBlend=saturate(MotionRepairStrength*max(rawRejection,coverage));"
        "Motion[id.xy]=Reset!=0?float2(0,0):lerp(f,repaired,repairBlend);"
        "GeometryDepth[id.xy]=sceneDepth;"
        "HistoryMask[id.xy]=Reset!=0||outside?1.0:rejection;}";
    ComPtr<ID3DBlob> conversion_cs;
    if (SUCCEEDED(hr))
        hr = CompileShader(conversion_shader, "nvof-conversion", "CS",
            conversion_cs, compile_error);
    D3D12_COMPUTE_PIPELINE_STATE_DESC conversion_pso = {};
    conversion_pso.pRootSignature = conversion_root_.Get();
    if (conversion_cs)
        conversion_pso.CS = {conversion_cs->GetBufferPointer(),
            conversion_cs->GetBufferSize()};
    if (SUCCEEDED(hr))
        hr = device_->CreateComputePipelineState(&conversion_pso,
            IID_PPV_ARGS(&conversion_pipeline_));

    D3D12_DESCRIPTOR_RANGE visualization_ranges[2] = {};
    visualization_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    visualization_ranges[0].NumDescriptors = 3;
    visualization_ranges[0].BaseShaderRegister = 0;
    visualization_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    visualization_ranges[1].NumDescriptors = 1;
    visualization_ranges[1].BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER visualization_params[3] = {};
    for (unsigned int index = 0; index < 2; ++index)
    {
        visualization_params[index].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        visualization_params[index].DescriptorTable.NumDescriptorRanges = 1;
        visualization_params[index].DescriptorTable.pDescriptorRanges =
            &visualization_ranges[index];
        visualization_params[index].ShaderVisibility =
            D3D12_SHADER_VISIBILITY_ALL;
    }
    visualization_params[2].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    visualization_params[2].Constants.ShaderRegister = 0;
    visualization_params[2].Constants.Num32BitValues = 8;
    D3D12_ROOT_SIGNATURE_DESC visualization_desc = {};
    visualization_desc.NumParameters = 3;
    visualization_desc.pParameters = visualization_params;
    signature.Reset();
    errors.Reset();
    if (SUCCEEDED(hr))
        hr = D3D12SerializeRootSignature(&visualization_desc,
            D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (SUCCEEDED(hr))
        hr = device_->CreateRootSignature(0, signature->GetBufferPointer(),
            signature->GetBufferSize(), IID_PPV_ARGS(&visualization_root_));
    static const char visualization_shader[] =
        "Texture2D<float2> Motion:register(t0);"
        "Texture2D<float> Rejection:register(t1);"
        "Texture2D<float> GeometryDepth:register(t2);"
        "RWTexture2D<float4> Output:register(u0);"
        "cbuffer C:register(b0){uint SourceWidth;uint SourceHeight;"
        "uint OutputWidth;uint OutputHeight;uint Mode;float MagnitudeScale;"
        "uint Reserved0;uint Reserved1;}"
        "[numthreads(8,8,1)] void CS(uint3 id:SV_DispatchThreadID){"
        "if(id.x>=OutputWidth||id.y>=OutputHeight)return;"
        "uint2 p=min(uint2((float2(id.xy)+0.5)*float2(SourceWidth,SourceHeight)/"
        "float2(OutputWidth,OutputHeight)),uint2(SourceWidth-1,SourceHeight-1));"
        "float2 mv=Motion.Load(int3(p,0));float rejection=Rejection.Load(int3(p,0));"
        "float3 color;if(Mode==1){float angle=atan2(mv.y,mv.x)/6.2831853+0.5;"
        "float3 hue=saturate(abs(frac(angle+float3(0,0.6666667,0.3333333))*6-3)-1);"
        "float magnitude=1-exp(-length(mv)/max(MagnitudeScale,0.01));"
        "color=lerp(float3(0.01,0.01,0.01),hue,magnitude);"
        "color+=saturate(length(mv)/(MagnitudeScale*8))*0.2;}else{"
        "color=lerp(float3(0.02,0.65,0.02),float3(1,0.02,0.02),rejection);"
        "color+=smoothstep(0.45,0.55,rejection)*float3(0.2,0.1,0);"
        "if(Mode==3){uint2 qx=uint2(min(p.x+1,SourceWidth-1),p.y);"
        "uint2 qy=uint2(p.x,min(p.y+1,SourceHeight-1));"
        "float d=GeometryDepth.Load(int3(p,0));float ex=abs(d-GeometryDepth.Load(int3(qx,0)));"
        "float ey=abs(d-GeometryDepth.Load(int3(qy,0)));float edge=max(ex,ey);"
        "color=lerp(float3(d,d,d)*0.15,float3(0,0.8,1),saturate(edge*80));}}"
        "Output[id.xy]=float4(color,1);}";
    ComPtr<ID3DBlob> visualization_cs;
    if (SUCCEEDED(hr))
        hr = CompileShader(visualization_shader, "nvof-visualization", "CS",
            visualization_cs, compile_error);
    D3D12_COMPUTE_PIPELINE_STATE_DESC visualization_pso = {};
    visualization_pso.pRootSignature = visualization_root_.Get();
    if (visualization_cs)
        visualization_pso.CS = {visualization_cs->GetBufferPointer(),
            visualization_cs->GetBufferSize()};
    if (SUCCEEDED(hr))
        hr = device_->CreateComputePipelineState(&visualization_pso,
            IID_PPV_ARGS(&visualization_pipeline_));
    if (FAILED(hr))
    {
        if (!compile_error.empty()) Log("shader compiler: %s", compile_error.c_str());
        SetStatus("Optical Flow conversion pipeline failed (0x%08X)",
            static_cast<unsigned int>(hr));
        return false;
    }
    return true;
}

bool NvofMotionProvider::RegisterResource(ID3D12Resource *resource,
    NvOFGPUBufferHandle &handle)
{
    NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 params = {};
    params.resource = resource;
    params.inputFencePoint.fence = registration_fence_.Get();
    params.inputFencePoint.value = registration_fence_value_;
    params.hOFGpuBuffer = &handle;
    params.outputFencePoint.fence = registration_fence_.Get();
    params.outputFencePoint.value = ++registration_fence_value_;
    const NV_OF_STATUS status = api_.nvOFRegisterResourceD3D12(session_, &params);
    if (status != NV_OF_SUCCESS)
    {
        SetStatus("Optical Flow resource registration failed (%d)", status);
        return false;
    }
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event || FAILED(registration_fence_->SetEventOnCompletion(
            registration_fence_value_, event)) ||
        WaitForSingleObject(event, 5000) != WAIT_OBJECT_0)
    {
        if (event) CloseHandle(event);
        SetStatus("Optical Flow resource registration timed out");
        return false;
    }
    CloseHandle(event);
    return handle != nullptr;
}

void NvofMotionProvider::UnregisterResource(NvOFGPUBufferHandle &handle)
{
    if (!handle || !api_.nvOFUnregisterResourceD3D12) return;
    NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 params = {};
    params.hOFGpuBuffer = handle;
    api_.nvOFUnregisterResourceD3D12(&params);
    handle = nullptr;
}

bool NvofMotionProvider::CreateSessionResources()
{
    auto formats = [&](NV_OF_BUFFER_USAGE usage)
    {
        std::vector<DXGI_FORMAT> result;
        uint32_t count = 0;
        if (api_.nvOFGetSurfaceFormatCountD3D12(session_, usage,
                NV_OF_MODE_OPTICALFLOW, &count) != NV_OF_SUCCESS || count == 0)
            return result;
        result.resize(count);
        if (api_.nvOFGetSurfaceFormatD3D12(session_, usage,
                NV_OF_MODE_OPTICALFLOW, result.data()) != NV_OF_SUCCESS)
            result.clear();
        return result;
    };
    const std::vector<DXGI_FORMAT> input_formats = formats(NV_OF_BUFFER_USAGE_INPUT);
    const std::vector<DXGI_FORMAT> output_formats = formats(NV_OF_BUFFER_USAGE_OUTPUT);
    const std::vector<DXGI_FORMAT> cost_formats = formats(NV_OF_BUFFER_USAGE_COST);
    input_format_ = Contains(input_formats, DXGI_FORMAT_R8_UNORM) ?
        DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_UNKNOWN;
    flow_format_ = Contains(output_formats, DXGI_FORMAT_R16G16_SINT) ?
        DXGI_FORMAT_R16G16_SINT : DXGI_FORMAT_UNKNOWN;
    cost_format_ = Contains(cost_formats, DXGI_FORMAT_R8_UINT) ?
        DXGI_FORMAT_R8_UINT :
        (Contains(cost_formats, DXGI_FORMAT_R32_UINT) ?
            DXGI_FORMAT_R32_UINT : DXGI_FORMAT_UNKNOWN);
    if (input_format_ == DXGI_FORMAT_UNKNOWN ||
        flow_format_ == DXGI_FORMAT_UNKNOWN || cost_format_ == DXGI_FORMAT_UNKNOWN)
    {
        SetStatus("Optical Flow driver lacks required grayscale/SHORT2/cost formats");
        return false;
    }

    const unsigned int flow_width = (width_ + grid_size_ - 1) / grid_size_;
    const unsigned int flow_height = (height_ + grid_size_ - 1) / grid_size_;
    for (unsigned int index = 0; index < kSlotCount; ++index)
    {
        Slot &slot = slots_[index];
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                IID_PPV_ARGS(&slot.prep_allocator))) ||
            FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                slot.prep_allocator.Get(), nullptr, IID_PPV_ARGS(&slot.prep_list))) ||
            FAILED(slot.prep_list->Close()))
        {
            SetStatus("Optical Flow preparation command allocation failed");
            return false;
        }
        if (!CreateTexture(device_.Get(), width_, height_, input_format_,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COMMON, slot.input) ||
            !CreateTexture(device_.Get(), flow_width, flow_height, flow_format_,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON,
                slot.forward) ||
            !CreateTexture(device_.Get(), flow_width, flow_height, flow_format_,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON,
                slot.backward) ||
            !CreateTexture(device_.Get(), flow_width, flow_height, cost_format_,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON,
                slot.forward_cost) ||
            !CreateTexture(device_.Get(), flow_width, flow_height, cost_format_,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON,
                slot.backward_cost) ||
            !CreateTexture(device_.Get(), width_, height_, DXGI_FORMAT_R16G16_FLOAT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COMMON, slot.motion) ||
            !CreateTexture(device_.Get(), width_, height_, DXGI_FORMAT_R8_UNORM,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COMMON, slot.history_mask) ||
            !CreateTexture(device_.Get(), width_, height_, DXGI_FORMAT_R32_FLOAT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COMMON, slot.geometry_depth))
        {
            SetStatus("Optical Flow texture allocation failed");
            return false;
        }
        if (!RegisterResource(slot.input.Get(), slot.input_handle) ||
            !RegisterResource(slot.forward.Get(), slot.forward_handle) ||
            !RegisterResource(slot.backward.Get(), slot.backward_handle) ||
            !RegisterResource(slot.forward_cost.Get(), slot.forward_cost_handle) ||
            !RegisterResource(slot.backward_cost.Get(), slot.backward_cost_handle))
            return false;

        const unsigned int prep_base = index * kPrepDescriptorsPerSlot;
        D3D12_UNORDERED_ACCESS_VIEW_DESC input_uav = {};
        input_uav.Format = input_format_;
        input_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(slot.input.Get(), nullptr, &input_uav,
            CpuDescriptor(prep_descriptors_.Get(), prep_descriptor_stride_,
                prep_base + 1));

        const unsigned int base = index * kConversionDescriptorsPerSlot;
        D3D12_SHADER_RESOURCE_VIEW_DESC flow_srv = {};
        flow_srv.Format = flow_format_;
        flow_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        flow_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        flow_srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(slot.forward.Get(), &flow_srv,
            CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base));
        device_->CreateShaderResourceView(slot.backward.Get(), &flow_srv,
            CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base + 1));
        D3D12_SHADER_RESOURCE_VIEW_DESC cost_srv = flow_srv;
        cost_srv.Format = cost_format_;
        device_->CreateShaderResourceView(slot.forward_cost.Get(), &cost_srv,
            CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base + 2));
        device_->CreateShaderResourceView(slot.backward_cost.Get(), &cost_srv,
            CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base + 3));
        D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv = flow_srv;
        depth_srv.Format = DXGI_FORMAT_R32_FLOAT;
        device_->CreateShaderResourceView(nullptr, &depth_srv,
            CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base + 4));
        D3D12_UNORDERED_ACCESS_VIEW_DESC motion_uav = {};
        motion_uav.Format = DXGI_FORMAT_R16G16_FLOAT;
        motion_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(slot.motion.Get(), nullptr, &motion_uav,
            CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base + 5));
        D3D12_UNORDERED_ACCESS_VIEW_DESC mask_uav = motion_uav;
        mask_uav.Format = DXGI_FORMAT_R8_UNORM;
        device_->CreateUnorderedAccessView(slot.history_mask.Get(), nullptr,
            &mask_uav, CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base + 6));
        D3D12_UNORDERED_ACCESS_VIEW_DESC depth_uav = motion_uav;
        depth_uav.Format = DXGI_FORMAT_R32_FLOAT;
        device_->CreateUnorderedAccessView(slot.geometry_depth.Get(), nullptr,
            &depth_uav, CpuDescriptor(conversion_descriptors_.Get(),
                conversion_descriptor_stride_, base + 7));

        const unsigned int visualization_base =
            index * kVisualizationDescriptorsPerSlot;
        D3D12_SHADER_RESOURCE_VIEW_DESC motion_srv = {};
        motion_srv.Format = DXGI_FORMAT_R16G16_FLOAT;
        motion_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        motion_srv.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        motion_srv.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(slot.motion.Get(), &motion_srv,
            CpuDescriptor(visualization_descriptors_.Get(),
                visualization_descriptor_stride_, visualization_base));
        D3D12_SHADER_RESOURCE_VIEW_DESC mask_srv = motion_srv;
        mask_srv.Format = DXGI_FORMAT_R8_UNORM;
        device_->CreateShaderResourceView(slot.history_mask.Get(), &mask_srv,
            CpuDescriptor(visualization_descriptors_.Get(),
                visualization_descriptor_stride_, visualization_base + 1));
        D3D12_SHADER_RESOURCE_VIEW_DESC geometry_srv = motion_srv;
        geometry_srv.Format = DXGI_FORMAT_R32_FLOAT;
        device_->CreateShaderResourceView(slot.geometry_depth.Get(), &geometry_srv,
            CpuDescriptor(visualization_descriptors_.Get(),
                visualization_descriptor_stride_, visualization_base + 2));
    }
    return true;
}

bool NvofMotionProvider::Initialize(ID3D12Device *device,
    ID3D12CommandQueue *compute_queue, ID3D12Fence *neural_fence,
    unsigned int width, unsigned int height, DXGI_FORMAT source_format,
    LogCallback log)
{
    Shutdown();
    log_ = log;
    if (!device || !compute_queue || !neural_fence || width == 0 || height == 0)
    {
        SetStatus("Optical Flow requires the asynchronous D3D12 host queue");
        return false;
    }
    device_ = device;
    compute_queue_ = compute_queue;
    neural_fence_ = neural_fence;
    width_ = width;
    height_ = height;
    source_format_ = source_format;
    if (!LoadApi())
    {
        Log("%s", Status());
        Shutdown();
        return false;
    }
    NV_OF_STATUS status = api_.nvCreateOpticalFlowD3D12(device_.Get(), &session_);
    if (status != NV_OF_SUCCESS || !session_)
    {
        SetStatus("Optical Flow session creation failed (%d)", status);
        Log("%s", Status());
        Shutdown();
        return false;
    }
    uint32_t grid_count = 0;
    std::vector<uint32_t> grids;
    if (api_.nvOFGetCaps(session_, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,
            nullptr, &grid_count) == NV_OF_SUCCESS && grid_count != 0)
    {
        grids.resize(grid_count);
        if (api_.nvOFGetCaps(session_, NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,
                grids.data(), &grid_count) != NV_OF_SUCCESS)
            grids.clear();
    }
    grid_size_ = std::find(grids.begin(), grids.end(), 1u) != grids.end() ? 1u :
        (std::find(grids.begin(), grids.end(), 2u) != grids.end() ? 2u : 4u);
    NV_OF_INIT_PARAMS init = {};
    init.width = width_;
    init.height = height_;
    init.outGridSize = static_cast<NV_OF_OUTPUT_VECTOR_GRID_SIZE>(grid_size_);
    init.hintGridSize = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
    init.mode = NV_OF_MODE_OPTICALFLOW;
    init.perfLevel = NV_OF_PERF_LEVEL_MEDIUM;
    init.enableExternalHints = NV_OF_FALSE;
    init.enableOutputCost = NV_OF_TRUE;
    init.disparityRange = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
    init.enableRoi = NV_OF_FALSE;
    init.predDirection = NV_OF_PRED_DIRECTION_BOTH;
    init.enableGlobalFlow = NV_OF_FALSE;
    init.inputBufferFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;
    status = api_.nvOFInit(session_, &init);
    if (status != NV_OF_SUCCESS)
    {
        SetStatus("Optical Flow session initialization failed (%d, grid=%u)",
            status, grid_size_);
        Log("%s", Status());
        Shutdown();
        return false;
    }
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&prep_fence_))) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&completion_fence_))) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&registration_fence_))) ||
        !CreatePipelineState() || !CreateSessionResources())
    {
        Log("%s", Status());
        Shutdown();
        return false;
    }
    ready_ = true;
    SetStatus("ready: %ux%u, %ux%u grid, forward/backward + cost",
        width_, height_, grid_size_, grid_size_);
    Log("%s", Status());
    return true;
}

void NvofMotionProvider::Shutdown()
{
    ready_ = false;
    previous_slot_ = -1;
    previous_sequence_ = 0;
    if (session_)
    {
        for (Slot &slot : slots_)
        {
            UnregisterResource(slot.backward_cost_handle);
            UnregisterResource(slot.forward_cost_handle);
            UnregisterResource(slot.backward_handle);
            UnregisterResource(slot.forward_handle);
            UnregisterResource(slot.input_handle);
        }
    }
    for (unsigned int slot_index = 0; slot_index < kSlotCount; ++slot_index)
    {
        Slot &slot = slots_[slot_index];
        slot.prep_list.Reset();
        slot.prep_allocator.Reset();
        slot.consumer_fence.Reset();
        slot.geometry_depth.Reset();
        slot.history_mask.Reset();
        slot.motion.Reset();
        slot.backward_cost.Reset();
        slot.forward_cost.Reset();
        slot.backward.Reset();
        slot.forward.Reset();
        slot.input.Reset();
        slot.prep_fence_value = 0;
        slot.flow_fence_value = 0;
        slot.neural_fence_value = 0;
        slot.consumer_fence_value = 0;
        slot.converted_once = false;
    }
    if (session_)
    {
        if (api_.nvOFDestroy) api_.nvOFDestroy(session_);
        session_ = nullptr;
    }
    visualization_descriptors_.Reset();
    visualization_pipeline_.Reset();
    visualization_root_.Reset();
    conversion_descriptors_.Reset();
    conversion_pipeline_.Reset();
    conversion_root_.Reset();
    prep_descriptors_.Reset();
    prep_pipeline_.Reset();
    prep_root_.Reset();
    registration_fence_.Reset();
    completion_fence_.Reset();
    prep_fence_.Reset();
    neural_fence_.Reset();
    compute_queue_.Reset();
    device_.Reset();
    api_ = {};
    if (module_)
    {
        FreeLibrary(module_);
    }
    module_ = nullptr;
    prep_fence_value_ = completion_fence_value_ = registration_fence_value_ = 0;
    width_ = height_ = 0;
    input_format_ = flow_format_ = cost_format_ = source_format_ = DXGI_FORMAT_UNKNOWN;
}

void NvofMotionProvider::ResetHistory()
{
    previous_slot_ = -1;
    previous_sequence_ = 0;
}

int NvofMotionProvider::AcquireSlot() const
{
    const std::uint64_t flow_completed = completion_fence_ ?
        completion_fence_->GetCompletedValue() : 0;
    const std::uint64_t neural_completed = neural_fence_ ?
        neural_fence_->GetCompletedValue() : 0;
    for (unsigned int index = 0; index < kSlotCount; ++index)
    {
        if (static_cast<int>(index) == previous_slot_) continue;
        const Slot &slot = slots_[index];
        const bool consumer_complete = !slot.consumer_fence ||
            slot.consumer_fence_value == 0 ||
            slot.consumer_fence->GetCompletedValue() >= slot.consumer_fence_value;
        if (flow_completed >= slot.flow_fence_value &&
            neural_completed >= slot.neural_fence_value && consumer_complete)
            return static_cast<int>(index);
    }
    return -1;
}

bool NvofMotionProvider::IsIdle() const
{
    const std::uint64_t prep_completed = prep_fence_ ?
        prep_fence_->GetCompletedValue() : prep_fence_value_;
    const std::uint64_t flow_completed = completion_fence_ ?
        completion_fence_->GetCompletedValue() : completion_fence_value_;
    const std::uint64_t neural_completed = neural_fence_ ?
        neural_fence_->GetCompletedValue() : 0;
    if (prep_completed < prep_fence_value_ ||
        flow_completed < completion_fence_value_)
        return false;
    for (const Slot &slot : slots_)
    {
        if (neural_fence_ && neural_completed < slot.neural_fence_value)
            return false;
        if (slot.consumer_fence && slot.consumer_fence_value != 0 &&
            slot.consumer_fence->GetCompletedValue() < slot.consumer_fence_value)
            return false;
    }
    return true;
}

bool NvofMotionProvider::IsComplete(const Submission &submission) const
{
    return submission.valid && submission.slot < kSlotCount && completion_fence_ &&
        completion_fence_->GetCompletedValue() >= submission.completion_value;
}

bool NvofMotionProvider::Submit(ID3D12Resource *source,
    D3D12_RESOURCE_STATES source_state, std::uint64_t source_sequence,
    bool reset, Submission &submission)
{
    submission = {};
    if (!ready_ || !source || !compute_queue_) return false;
    const int selected = AcquireSlot();
    if (selected < 0)
    {
        SetStatus("waiting for a free Optical Flow history slot");
        return false;
    }
    Slot &slot = slots_[selected];
    HRESULT hr = slot.prep_allocator->Reset();
    if (SUCCEEDED(hr)) hr = slot.prep_list->Reset(slot.prep_allocator.Get(),
        prep_pipeline_.Get());
    if (FAILED(hr))
    {
        SetStatus("Optical Flow preparation reset failed (0x%08X)",
            static_cast<unsigned int>(hr));
        return false;
    }
    const unsigned int prep_base = static_cast<unsigned int>(selected) *
        kPrepDescriptorsPerSlot;
    D3D12_SHADER_RESOURCE_VIEW_DESC source_srv = {};
    source_srv.Format = source_format_;
    source_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    source_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    source_srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(source, &source_srv,
        CpuDescriptor(prep_descriptors_.Get(), prep_descriptor_stride_, prep_base));
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    unsigned int barrier_count = 0;
    if (source_state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        barriers[barrier_count++] = Transition(source, source_state,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers[barrier_count++] = Transition(slot.input.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    slot.prep_list->ResourceBarrier(barrier_count, barriers);
    ID3D12DescriptorHeap *heaps[] = {prep_descriptors_.Get()};
    slot.prep_list->SetDescriptorHeaps(1, heaps);
    slot.prep_list->SetComputeRootSignature(prep_root_.Get());
    slot.prep_list->SetComputeRootDescriptorTable(0,
        GpuDescriptor(prep_descriptors_.Get(), prep_descriptor_stride_, prep_base));
    slot.prep_list->SetComputeRootDescriptorTable(1,
        GpuDescriptor(prep_descriptors_.Get(), prep_descriptor_stride_, prep_base + 1));
    slot.prep_list->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    barrier_count = 0;
    barriers[barrier_count++] = Transition(slot.input.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    if (source_state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        barriers[barrier_count++] = Transition(source,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, source_state);
    slot.prep_list->ResourceBarrier(barrier_count, barriers);
    hr = slot.prep_list->Close();
    if (FAILED(hr))
    {
        SetStatus("Optical Flow preparation close failed (0x%08X)",
            static_cast<unsigned int>(hr));
        return false;
    }
    ID3D12CommandList *lists[] = {slot.prep_list.Get()};
    compute_queue_->ExecuteCommandLists(1, lists);
    slot.prep_fence_value = ++prep_fence_value_;
    hr = compute_queue_->Signal(prep_fence_.Get(), slot.prep_fence_value);
    if (FAILED(hr))
    {
        SetStatus("Optical Flow preparation signal failed (0x%08X)",
            static_cast<unsigned int>(hr));
        return false;
    }

    if (reset) ResetHistory();
    if (previous_slot_ < 0)
    {
        previous_slot_ = selected;
        previous_sequence_ = source_sequence;
        SetStatus("history primed; Optical Flow begins on the next source frame");
        return true;
    }

    Slot &previous = slots_[previous_slot_];
    NV_OF_FENCE_POINT input_fence = {prep_fence_.Get(), slot.prep_fence_value};
    NV_OF_EXECUTE_INPUT_PARAMS_D3D12 input = {};
    // NVOFA defines forward flow as input -> reference. Current -> previous is
    // the same convention consumed by the existing VORT/NGX contract.
    input.inputFrame = slot.input_handle;
    input.referenceFrame = previous.input_handle;
    input.disableTemporalHints = reset ||
        (previous_sequence_ != 0 && source_sequence != previous_sequence_ + 1) ?
            NV_OF_TRUE : NV_OF_FALSE;
    input.numFencePoints = 1;
    input.fencePoint = &input_fence;
    const std::uint64_t completion = ++completion_fence_value_;
    NV_OF_FENCE_POINT output_fence = {completion_fence_.Get(), completion};
    NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 output = {};
    output.outputBuffer = slot.forward_handle;
    output.outputCostBuffer = slot.forward_cost_handle;
    output.bwdOutputBuffer = slot.backward_handle;
    output.bwdOutputCostBuffer = slot.backward_cost_handle;
    output.fencePoint = &output_fence;
    const NV_OF_STATUS status = api_.nvOFExecuteD3D12(session_, &input, &output);
    if (status != NV_OF_SUCCESS)
    {
        SetStatus("Optical Flow execute failed (%d)", status);
        Log("%s", Status());
        previous_slot_ = selected;
        previous_sequence_ = source_sequence;
        return false;
    }
    slot.flow_fence_value = completion;
    previous.flow_fence_value = std::max(previous.flow_fence_value, completion);
    submission.valid = true;
    submission.slot = static_cast<unsigned int>(selected);
    submission.completion_value = completion;
    submission.motion = slot.motion.Get();
    submission.history_mask = slot.history_mask.Get();
    submission.depth = slot.geometry_depth.Get();
    previous_slot_ = selected;
    previous_sequence_ = source_sequence;
    SetStatus("active: hardware forward/backward flow, grid %ux%u",
        grid_size_, grid_size_);
    return true;
}

bool NvofMotionProvider::RecordConversion(ID3D12GraphicsCommandList *commands,
    const Submission &submission, float consistency_threshold_pixels,
    float cost_threshold, ID3D12Resource *geometry_depth,
    D3D12_RESOURCE_STATES geometry_depth_state, bool depth_reversed,
    float motion_repair_strength)
{
    if (!ready_ || !commands || !submission.valid ||
        submission.slot >= kSlotCount) return false;
    Slot &slot = slots_[submission.slot];
    const D3D12_RESOURCE_DESC geometry_desc = geometry_depth ?
        geometry_depth->GetDesc() : D3D12_RESOURCE_DESC{};
    const bool use_depth = geometry_depth &&
        geometry_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        geometry_desc.Width > 0 && geometry_desc.Height > 0 &&
        geometry_desc.Format == DXGI_FORMAT_R32_FLOAT;
    const unsigned int base = submission.slot * kConversionDescriptorsPerSlot;
    D3D12_SHADER_RESOURCE_VIEW_DESC depth_srv = {};
    depth_srv.Format = DXGI_FORMAT_R32_FLOAT;
    depth_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depth_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depth_srv.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(use_depth ? geometry_depth : nullptr,
        &depth_srv, CpuDescriptor(conversion_descriptors_.Get(),
            conversion_descriptor_stride_, base + 4));

    D3D12_RESOURCE_BARRIER begin[8] = {};
    unsigned int begin_count = 0;
    begin[begin_count++] = Transition(slot.forward.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    begin[begin_count++] = Transition(slot.backward.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    begin[begin_count++] = Transition(slot.forward_cost.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    begin[begin_count++] = Transition(slot.backward_cost.Get(), D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const D3D12_RESOURCE_STATES converted_state = slot.converted_once ?
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON;
    begin[begin_count++] = Transition(slot.motion.Get(), converted_state,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    begin[begin_count++] = Transition(slot.history_mask.Get(), converted_state,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    begin[begin_count++] = Transition(slot.geometry_depth.Get(), converted_state,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (use_depth && geometry_depth_state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        begin[begin_count++] = Transition(geometry_depth, geometry_depth_state,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commands->ResourceBarrier(begin_count, begin);
    ID3D12DescriptorHeap *heaps[] = {conversion_descriptors_.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(conversion_root_.Get());
    commands->SetPipelineState(conversion_pipeline_.Get());
    commands->SetComputeRootDescriptorTable(0,
        GpuDescriptor(conversion_descriptors_.Get(),
            conversion_descriptor_stride_, base));
    commands->SetComputeRootDescriptorTable(1,
        GpuDescriptor(conversion_descriptors_.Get(),
            conversion_descriptor_stride_, base + 5));
    struct Constants
    {
        unsigned int width;
        unsigned int height;
        unsigned int grid;
        float consistency;
        float cost_threshold;
        float cost_scale;
        unsigned int reset;
        unsigned int use_depth;
        unsigned int depth_width;
        unsigned int depth_height;
        unsigned int depth_reversed;
        float depth_sensitivity;
        float motion_repair_strength;
    } constants = {width_, height_, grid_size_,
        std::max(0.25f, consistency_threshold_pixels),
        std::clamp(cost_threshold, 0.0f, 0.99f),
        cost_format_ == DXGI_FORMAT_R8_UINT ? 1.0f / 255.0f : 1.0f / 65535.0f,
        0u, use_depth ? 1u : 0u,
        use_depth ? static_cast<unsigned int>(geometry_desc.Width) : 1u,
        use_depth ? geometry_desc.Height : 1u,
        depth_reversed ? 1u : 0u, 0.02f,
        std::clamp(motion_repair_strength, 0.0f, 1.0f)};
    commands->SetComputeRoot32BitConstants(2, 13, &constants, 0);
    commands->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER end[8] = {};
    unsigned int end_count = 0;
    end[end_count++] = Transition(slot.forward.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    end[end_count++] = Transition(slot.backward.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    end[end_count++] = Transition(slot.forward_cost.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    end[end_count++] = Transition(slot.backward_cost.Get(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    end[end_count++] = Transition(slot.motion.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    end[end_count++] = Transition(slot.history_mask.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    end[end_count++] = Transition(slot.geometry_depth.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (use_depth && geometry_depth_state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        end[end_count++] = Transition(geometry_depth,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, geometry_depth_state);
    commands->ResourceBarrier(end_count, end);
    slot.converted_once = true;
    return true;
}

bool NvofMotionProvider::RecordVisualization(
    ID3D12GraphicsCommandList *commands, const Submission &submission,
    ID3D12Resource *output, unsigned int mode, float magnitude_scale)
{
    if (!ready_ || !commands || !submission.valid ||
        submission.slot >= kSlotCount || !output || mode < 1 || mode > 3 ||
        !visualization_pipeline_ || !visualization_descriptors_)
        return false;
    const D3D12_RESOURCE_DESC output_desc = output->GetDesc();
    if (output_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        output_desc.Width == 0 || output_desc.Height == 0)
        return false;

    const unsigned int base =
        submission.slot * kVisualizationDescriptorsPerSlot;
    D3D12_UNORDERED_ACCESS_VIEW_DESC output_uav = {};
    output_uav.Format = output_desc.Format;
    output_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(output, nullptr, &output_uav,
        CpuDescriptor(visualization_descriptors_.Get(),
            visualization_descriptor_stride_, base + 3));

    D3D12_RESOURCE_BARRIER output_barrier = {};
    output_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    output_barrier.UAV.pResource = output;
    commands->ResourceBarrier(1, &output_barrier);

    ID3D12DescriptorHeap *heaps[] = {visualization_descriptors_.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetComputeRootSignature(visualization_root_.Get());
    commands->SetPipelineState(visualization_pipeline_.Get());
    commands->SetComputeRootDescriptorTable(0,
        GpuDescriptor(visualization_descriptors_.Get(),
            visualization_descriptor_stride_, base));
    commands->SetComputeRootDescriptorTable(1,
        GpuDescriptor(visualization_descriptors_.Get(),
            visualization_descriptor_stride_, base + 3));
    struct Constants
    {
        unsigned int source_width;
        unsigned int source_height;
        unsigned int output_width;
        unsigned int output_height;
        unsigned int mode;
        float magnitude_scale;
        unsigned int reserved0;
        unsigned int reserved1;
    } constants = {width_, height_, static_cast<unsigned int>(output_desc.Width),
        output_desc.Height, mode, std::max(0.1f, magnitude_scale), 0, 0};
    commands->SetComputeRoot32BitConstants(2, 8, &constants, 0);
    commands->Dispatch((constants.output_width + 7) / 8,
        (constants.output_height + 7) / 8, 1);
    return true;
}

void NvofMotionProvider::MarkNeuralUse(const Submission &submission,
    std::uint64_t neural_fence_value)
{
    if (!submission.valid || submission.slot >= kSlotCount) return;
    slots_[submission.slot].neural_fence_value = neural_fence_value;
}

void NvofMotionProvider::MarkConsumerUse(const Submission &submission,
    ID3D12Fence *fence, std::uint64_t fence_value)
{
    if (!submission.valid || submission.slot >= kSlotCount || !fence ||
        fence_value == 0) return;
    Slot &slot = slots_[submission.slot];
    slot.consumer_fence = fence;
    slot.consumer_fence_value = fence_value;
}
