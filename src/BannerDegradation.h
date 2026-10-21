#ifndef BANNER_DEGRADATION_H
#define BANNER_DEGRADATION_H

#include "Utility.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <string>

namespace hdt {
    struct BannerAlphaImage {
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;
    };

    inline ID3DBlob* BannerAlphaShaderBytecode() {
        using Microsoft::WRL::ComPtr;
        const char code[] = R"(
 Texture2D<float4> original:register(t0);Texture2D<float4> damage:register(t1);
 SamplerState linearClamp:register(s0);RWTexture2D<float4> output:register(u0);
 cbuffer Params:register(b0){uint width;uint height;uint encodeSRGB;uint dirtStage;};
 float grain(float2 p){float3 q=frac(float3(p.xyx)*.1031);q+=dot(q,q.yzx+33.33);return frac((q.x+q.y)*q.z);}
 float grime(float2 uv){float2 p=uv*float2(9,13),cell=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(grain(cell),grain(cell+float2(1,0)),f.x),lerp(grain(cell+float2(0,1)),grain(cell+1),f.x),f.y);}
 float3 encode(float3 c){return float3(c.x<=.0031308?12.92*c.x:1.055*pow(max(c.x,0),1/2.4)-.055,
 c.y<=.0031308?12.92*c.y:1.055*pow(max(c.y,0),1/2.4)-.055,c.z<=.0031308?12.92*c.z:1.055*pow(max(c.z,0),1/2.4)-.055);}
 [numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(id.x>=width||id.y>=height)return;
 float2 uv=(float2(id.xy)+.5)/float2(width,height);float4 color=original.SampleLevel(linearClamp,uv,0);
 if(dirtStage & 1024u){
   if(encodeSRGB)color.rgb=encode(color.rgb);
   color.a=color.a>((dirtStage>>24)&255u)/255.0?1:0;
   output[id.xy]=color;return;
 }
 float4 masks=damage.SampleLevel(linearClamp,uv,0);
 float mask=(dirtStage & 512u)?masks.b:masks.r;
 if(dirtStage & 256u){
   float rimWidth=max(1.0,float((dirtStage>>16)&255u));
   float fabric=mask*color.a,nearest=rimWidth+1,edge=0;
   [loop]for(uint radius=1;radius<=8;radius++){
     if(radius>rimWidth)break;
     float2 stepUV=max(float(radius)/1024,1.0/float2(width,height));
     float2 a=uv+float2(stepUV.x,0),b=uv-float2(stepUV.x,0);
     float2 c=uv+float2(0,stepUV.y),d=uv-float2(0,stepUV.y);
     float nearMask=min(min(damage.SampleLevel(linearClamp,a,0).r*original.SampleLevel(linearClamp,a,0).a,
                            damage.SampleLevel(linearClamp,b,0).r*original.SampleLevel(linearClamp,b,0).a),
                        min(damage.SampleLevel(linearClamp,c,0).r*original.SampleLevel(linearClamp,c,0).a,
                            damage.SampleLevel(linearClamp,d,0).r*original.SampleLevel(linearClamp,d,0).a));
     float rim=saturate((fabric-nearMask)*4)*step(86.0/255,fabric);
     if(rim>.01)nearest=min(nearest,float(radius));
     edge=max(edge,rim);
   }
   float distance=saturate((nearest-1)/max(rimWidth-1,1));
   float stage=float(dirtStage & 3u)/3;
   float3 core=lerp(float3(1,.72,.12),float3(1,.88,.22),stage);
   float3 orange=float3(1,.19,.008),red=float3(.38,.009,.0005);
   float3 glow=distance<.6?lerp(core,orange,smoothstep(0,.6,distance)):
                                  lerp(orange,red,smoothstep(.6,1,distance));
   glow*=edge*(1-.75*smoothstep(.65,1,distance));
   if(encodeSRGB)glow=encode(glow);
   output[id.xy]=float4(glow,1);return;
 }
 float fringe=smoothstep(.28,.55,mask)*(1-smoothstep(.82,1,mask));
 if(encodeSRGB)color.rgb=encode(color.rgb);color.rgb*=1-.10*fringe;
 if(dirtStage & 3u){float dirt=min(dirtStage & 3u,3u)*.14*smoothstep(.15,.7,grime(uv));
 color.rgb*=1-dirt*float3(.72,.91,1.18);}
 if(dirtStage & 512u){
   float reference=((dirtStage>>16)&255u)/255.0;
   bool before=color.a*masks.g>reference,after=color.a*masks.b>reference;
   color.a=!before?0:after?1:clamp(masks.r,1.0/255,254.0/255);
 }else color.a*=mask;
 output[id.xy]=color;}
 )";
        static const auto bytecode = [&]() {
            ComPtr<ID3DBlob> blob, errors;
            if (FAILED(D3DCompile(code, sizeof(code) - 1, nullptr, nullptr, nullptr, "main", "cs_5_0",
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
                blob.Reset();
            }
            return blob;
        }();
        return bytecode.Get();
    }

    inline bool ComposeBannerAlpha(ID3D11DeviceContext* immediate, ID3D11ShaderResourceView* source,
                                   const std::vector<uint8_t>& mask, BannerAlphaImage& image, unsigned dirtStage = 0,
                                   Microsoft::WRL::ComPtr<ID3D11Texture2D>* gpuResult = nullptr,
                                   ID3D11ShaderResourceView* preparedMask = nullptr, bool edgeGlow = false,
                                   bool alphaRamp = false, unsigned alphaReference = 85, int binaryCutoff = -1,
                                   unsigned edgeWidth = 2) {
        using Microsoft::WRL::ComPtr;
        if (!immediate || !source || mask.size() != 1024 * 1024 * (alphaRamp ? 4u : 1u)) {
            return false;
        }
        ComPtr<ID3D11Device> device;
        immediate->GetDevice(&device);
        ComPtr<ID3D11Resource> input;
        source->GetResource(&input);
        ComPtr<ID3D11Texture2D> inputImage;
        if (FAILED(input.As(&inputImage))) {
            return false;
        }
        D3D11_TEXTURE2D_DESC inputDesc{};
        inputImage->GetDesc(&inputDesc);
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        source->GetDesc(&viewDesc);
        if (viewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
            return false;
        }
        const uint32_t mip = viewDesc.Texture2D.MostDetailedMip;
        const uint32_t width = std::max(1u, inputDesc.Width >> mip), height = std::max(1u, inputDesc.Height >> mip);
        if (width > 8192 || height > 8192) {
            return false;
        }
        const bool srgb =
            viewDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || viewDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            viewDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || viewDesc.Format == DXGI_FORMAT_BC1_UNORM_SRGB ||
            viewDesc.Format == DXGI_FORMAT_BC2_UNORM_SRGB || viewDesc.Format == DXGI_FORMAT_BC3_UNORM_SRGB ||
            viewDesc.Format == DXGI_FORMAT_BC7_UNORM_SRGB;
        auto* bytecode = BannerAlphaShaderBytecode();
        if (!bytecode) {
            return false;
        }
        static ComPtr<ID3D11Device> shaderDevice;
        static ComPtr<ID3D11ComputeShader> shader;
        if (shaderDevice.Get() != device.Get()) {
            shader.Reset();
            if (FAILED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr,
                                                   &shader))) {
                return false;
            }
            shaderDevice = device;
        }
        D3D11_TEXTURE2D_DESC maskDesc{};
        maskDesc.Width = 1024;
        maskDesc.Height = 1024;
        maskDesc.MipLevels = 1;
        maskDesc.ArraySize = 1;
        maskDesc.Format = alphaRamp ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;
        maskDesc.SampleDesc.Count = 1;
        maskDesc.Usage = D3D11_USAGE_IMMUTABLE;
        maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA maskData{mask.data(), 1024u * (alphaRamp ? 4u : 1u), 0};
        ComPtr<ID3D11Texture2D> maskTexture;
        if (!preparedMask && FAILED(device->CreateTexture2D(&maskDesc, &maskData, &maskTexture))) {
            return false;
        }
        ComPtr<ID3D11ShaderResourceView> maskView;
        if (!preparedMask && FAILED(device->CreateShaderResourceView(maskTexture.Get(), nullptr, &maskView))) {
            return false;
        }
        D3D11_TEXTURE2D_DESC outputDesc = maskDesc;
        outputDesc.Width = width;
        outputDesc.Height = height;
        outputDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        outputDesc.Usage = D3D11_USAGE_DEFAULT;
        outputDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> output;
        if (FAILED(device->CreateTexture2D(&outputDesc, nullptr, &output))) {
            return false;
        }
        ComPtr<ID3D11UnorderedAccessView> outputView;
        if (FAILED(device->CreateUnorderedAccessView(output.Get(), nullptr, &outputView))) {
            return false;
        }
        D3D11_TEXTURE2D_DESC stagingDesc = outputDesc;
        stagingDesc.BindFlags = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (!gpuResult && FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging))) {
            return false;
        }

        struct Params {
            uint32_t width, height, srgb, dirtStage;
        } params{width, height, srgb ? 1u : 0u,
                 ((edgeGlow ? std::min(dirtStage > 0 ? dirtStage - 1 : 0u, 3u) : std::min(dirtStage, 3u)) |
                  (edgeGlow ? 256u : 0u) | (alphaRamp ? 512u : 0u) |
                  (((edgeGlow ? std::clamp(edgeWidth, 1u, 8u) : alphaReference) & 255u) << 16) |
                  (binaryCutoff >= 0 ? 1024u | (uint32_t(binaryCutoff) << 24) : 0u))};

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = 16;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA bufferData{&params, 0, 0};
        ComPtr<ID3D11Buffer> constants;
        if (FAILED(device->CreateBuffer(&bufferDesc, &bufferData, &constants))) {
            return false;
        }
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        if (FAILED(device->CreateSamplerState(&samplerDesc, &sampler))) {
            return false;
        }
        ComPtr<ID3D11DeviceContext> deferred;
        if (FAILED(device->CreateDeferredContext(0, &deferred))) {
            return false;
        }
        auto* cb = constants.Get();
        auto* sm = sampler.Get();
        auto* uav = outputView.Get();
        ID3D11ShaderResourceView* views[]{source, preparedMask ? preparedMask : maskView.Get()};
        deferred->CSSetShader(shader.Get(), nullptr, 0);
        deferred->CSSetConstantBuffers(0, 1, &cb);
        deferred->CSSetSamplers(0, 1, &sm);
        deferred->CSSetShaderResources(0, 2, views);
        deferred->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        deferred->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        uav = nullptr;
        deferred->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        if (!gpuResult) {
            deferred->CopyResource(staging.Get(), output.Get());
        }
        ComPtr<ID3D11CommandList> commands;
        if (FAILED(deferred->FinishCommandList(FALSE, &commands))) {
            return false;
        }
        immediate->ExecuteCommandList(commands.Get(), TRUE);
        image.width = width;
        image.height = height;
        if (gpuResult) {
            *gpuResult = output;
            return true;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return false;
        }
        image.width = width;
        image.height = height;
        image.rgba.resize(size_t(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(image.rgba.data() + size_t(y) * width * 4,
                        static_cast<uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(width) * 4);
        }
        immediate->Unmap(staging.Get(), 0);
        return true;
    }
}

namespace hdt {
    inline std::vector<uint8_t> DecodeBannerMask(const std::vector<uint8_t>& bytes, int stage) {
        if (stage < 1 || stage > 4 || bytes.size() < 4 || std::string(bytes.begin(), bytes.begin() + 4) != "HCBM") {
            return {};
        }
        size_t offset = 4;
        for (int current = 1; current <= 4; ++current) {
            if (offset + 4 > bytes.size()) {
                return {};
            }
            uint32_t size = 0;
            for (int i = 0; i < 4; ++i) {
                size |= uint32_t(bytes[offset + i]) << (i * 8);
            }
            offset += 4;
            if (size % 3 || size > bytes.size() - offset) {
                return {};
            }
            if (current == stage) {
                std::vector<uint8_t> result;
                result.reserve(1024 * 1024);
                for (size_t i = offset; i < offset + size; i += 3) {
                    size_t run = bytes[i] | (uint16_t(bytes[i + 1]) << 8);
                    if (!run || run > 1024 * 1024 - result.size()) {
                        return {};
                    }
                    result.insert(result.end(), run, bytes[i + 2]);
                }
                return result.size() == 1024 * 1024 ? result : std::vector<uint8_t>{};
            }
            offset += size;
        }
        return {};
    }

    inline std::vector<uint8_t> LoadBannerMask(unsigned layout, int stage, bool erosion = false) {
        const auto digits = std::to_string(layout);
        const auto name = std::string(3 - digits.size(), '0') + digits;
        std::ifstream file("Data/SKSE/Plugins/FSMPBanners/masks/layout_" + name + (erosion ? "_erosion.bin" : ".bin"),
                           std::ios::binary | std::ios::ate);
        const auto size = file.tellg();
        if (size <= 0 || size > 16 * 1024 * 1024) {
            return {};
        }
        file.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
            return {};
        }
        return DecodeBannerMask(bytes, stage);
    }
}

namespace hdt {
    inline bool BannerHasErosionMask(unsigned layout) {
        const auto digits = std::to_string(layout);
        const auto name = std::string(digits.size() < 3 ? 3 - digits.size() : 0, '0') + digits;
        std::ifstream file("Data/SKSE/Plugins/FSMPBanners/masks/layout_" + name + "_erosion.bin", std::ios::binary);
        char magic[4]{};
        return file.read(magic, 4) && std::memcmp(magic, "HCBM", 4) == 0;
    }

    inline std::vector<uint8_t> LoadBannerTransitionMask(unsigned layout, int stage) {
        auto order = LoadBannerMask(layout, stage, true);
        const auto after = LoadBannerMask(layout, stage);
        const auto before = stage > 1 ? LoadBannerMask(layout, stage - 1) : std::vector<uint8_t>(1024 * 1024, 255);
        if (order.size() != 1024 * 1024 || before.size() != order.size() || after.size() != order.size()) {
            return {};
        }
        std::vector<uint8_t> packed(order.size() * 4);
        for (size_t i = 0; i < order.size(); ++i) {
            packed[i * 4] = order[i];
            packed[i * 4 + 1] = before[i];
            packed[i * 4 + 2] = after[i];
            packed[i * 4 + 3] = 255;
        }
        return packed;
    }
}

namespace hdt {
    inline bool FillRuntimeBannerTexture(RE::NiSourceTexture* source, RE::NiSourceTexture* slot, int stage,
                                         unsigned layout, bool edgeGlow = false, bool alphaRamp = false,
                                         unsigned alphaReference = 85, unsigned edgeWidth = 2) {
        if (!source || !slot || source == slot || !source->rendererTexture || !slot->rendererTexture ||
            !source->rendererTexture->resourceView || !slot->rendererTexture->resourceView) {
            return false;
        }
        if (source->rendererTexture == slot->rendererTexture) {
            return false;
        }
        const std::string slotName = slot->name.c_str();
        if (slotName.find("slot_") == std::string::npos || slotName.size() < 4 ||
            slotName.substr(slotName.size() - 4) != ".dds") {
            return false;
        }
        const auto mask = edgeGlow    ? std::vector<uint8_t>(1024 * 1024, 255)
                          : alphaRamp ? LoadBannerTransitionMask(layout, stage)
                                      : LoadBannerMask(layout, stage);
        if (mask.size() != 1024 * 1024 * (alphaRamp ? 4u : 1u)) {
            return false;
        }
        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        source->rendererTexture->resourceView->GetDevice(&device);
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(&renderer->GetLock());

        struct Guard {
            CRITICAL_SECTION* lock;

            explicit Guard(CRITICAL_SECTION* p) : lock(p) {
                EnterCriticalSection(lock);
            }

            ~Guard() {
                LeaveCriticalSection(lock);
            }
        } guard(lock);

        D3D11_SHADER_RESOURCE_VIEW_DESC sourceView{};
        source->rendererTexture->resourceView->GetDesc(&sourceView);
        if (sourceView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
            return false;
        }
        const bool srgb =
            sourceView.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || sourceView.Format == DXGI_FORMAT_BC1_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_BC2_UNORM_SRGB || sourceView.Format == DXGI_FORMAT_BC3_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_BC7_UNORM_SRGB;
        Microsoft::WRL::ComPtr<ID3D11Resource> sourceResource;
        source->rendererTexture->resourceView->GetResource(&sourceResource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceImage;
        if (FAILED(sourceResource.As(&sourceImage))) {
            return false;
        }
        D3D11_TEXTURE2D_DESC sourceDesc{};
        sourceImage->GetDesc(&sourceDesc);
        unsigned firstMip = sourceView.Texture2D.MostDetailedMip;
        if (firstMip >= sourceDesc.MipLevels) {
            return false;
        }
        while (edgeGlow && firstMip + 1 < sourceDesc.MipLevels &&
               (std::max(sourceDesc.Width, sourceDesc.Height) >> firstMip) > 1024) {
            ++firstMip;
        }
        const unsigned mipCount = std::min(sourceView.Texture2D.MipLevels, sourceDesc.MipLevels - firstMip);
        if (!mipCount) {
            return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = std::max(1u, sourceDesc.Width >> firstMip);
        desc.Height = std::max(1u, sourceDesc.Height >> firstMip);
        desc.MipLevels = mipCount;
        desc.ArraySize = 1;
        desc.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) {
            return false;
        }
        D3D11_TEXTURE2D_DESC maskDesc{};
        maskDesc.Width = maskDesc.Height = 1024;
        maskDesc.MipLevels = maskDesc.ArraySize = 1;
        maskDesc.Format = alphaRamp ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8_UNORM;
        maskDesc.SampleDesc.Count = 1;
        maskDesc.Usage = D3D11_USAGE_IMMUTABLE;
        maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA maskData{mask.data(), 1024u * (alphaRamp ? 4u : 1u), 0};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> maskTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> maskView;
        if (FAILED(device->CreateTexture2D(&maskDesc, &maskData, &maskTexture)) ||
            FAILED(device->CreateShaderResourceView(maskTexture.Get(), nullptr, &maskView))) {
            return false;
        }
        for (unsigned mip = 0; mip < mipCount; ++mip) {
            auto mipDesc = sourceView;
            mipDesc.Texture2D.MostDetailedMip = firstMip + mip;
            mipDesc.Texture2D.MipLevels = 1;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mipView;
            if (FAILED(device->CreateShaderResourceView(sourceResource.Get(), &mipDesc, &mipView))) {
                return false;
            }
            BannerAlphaImage dimensions;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> composed;
            if (!ComposeBannerAlpha(context.Get(), mipView.Get(), mask, dimensions, alphaRamp ? 0 : stage, &composed,
                                    maskView.Get(), edgeGlow, alphaRamp, alphaReference, -1, edgeWidth)) {
                return false;
            }
            context->CopySubresourceRegion(texture.Get(), mip, 0, 0, 0, composed.Get(), 0, nullptr);
        }
        if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            return false;
        }
        auto* target = slot->rendererTexture;
        auto* oldView = target->resourceView;
        auto* oldResource = target->texture;
        target->texture = texture.Detach();
        target->resourceView = view.Detach();
        target->width = uint16_t(desc.Width);
        target->height = uint16_t(desc.Height);
        target->mips = uint8_t(desc.MipLevels);
        target->format = uint8_t(desc.Format);
        if (oldView) {
            oldView->Release();
        }
        if (oldResource) {
            oldResource->Release();
        }
        return true;
    }

    inline bool ApplyBannerWearCutoff(ID3D11ShaderResourceView* ranks, RE::NiSourceTexture* slot, unsigned cutoff) {
        if (!ranks || !slot || !slot->rendererTexture) {
            return false;
        }
        const std::string slotName = slot->name.c_str();
        if (slotName.find("slot_") == std::string::npos || slotName.size() < 4 ||
            slotName.substr(slotName.size() - 4) != ".dds") {
            return false;
        }
        static const std::vector<uint8_t> mask(1024 * 1024, 0);
        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        ranks->GetDevice(&device);
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(&renderer->GetLock());

        struct Guard {
            CRITICAL_SECTION* lock;

            explicit Guard(CRITICAL_SECTION* p) : lock(p) {
                EnterCriticalSection(lock);
            }

            ~Guard() {
                LeaveCriticalSection(lock);
            }
        } guard(lock);

        D3D11_SHADER_RESOURCE_VIEW_DESC sourceView{};
        ranks->GetDesc(&sourceView);
        if (sourceView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D) {
            return false;
        }
        const bool srgb =
            sourceView.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || sourceView.Format == DXGI_FORMAT_BC1_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_BC2_UNORM_SRGB || sourceView.Format == DXGI_FORMAT_BC3_UNORM_SRGB ||
            sourceView.Format == DXGI_FORMAT_BC7_UNORM_SRGB;
        Microsoft::WRL::ComPtr<ID3D11Resource> sourceResource;
        ranks->GetResource(&sourceResource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceImage;
        if (FAILED(sourceResource.As(&sourceImage))) {
            return false;
        }
        D3D11_TEXTURE2D_DESC sourceDesc{};
        sourceImage->GetDesc(&sourceDesc);
        unsigned firstMip = sourceView.Texture2D.MostDetailedMip;
        if (firstMip >= sourceDesc.MipLevels) {
            return false;
        }
        const unsigned mipCount = std::min(sourceView.Texture2D.MipLevels, sourceDesc.MipLevels - firstMip);
        if (!mipCount) {
            return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = std::max(1u, sourceDesc.Width >> firstMip);
        desc.Height = std::max(1u, sourceDesc.Height >> firstMip);
        desc.MipLevels = mipCount;
        desc.ArraySize = 1;
        desc.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) {
            return false;
        }
        for (unsigned mip = 0; mip < mipCount; ++mip) {
            auto mipDesc = sourceView;
            mipDesc.Texture2D.MostDetailedMip = firstMip + mip;
            mipDesc.Texture2D.MipLevels = 1;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> mipView;
            if (FAILED(device->CreateShaderResourceView(sourceResource.Get(), &mipDesc, &mipView))) {
                return false;
            }
            BannerAlphaImage dimensions;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> composed;
            if (!ComposeBannerAlpha(context.Get(), mipView.Get(), mask, dimensions, 0, &composed, ranks, false, false,
                                    85, int(cutoff))) {
                return false;
            }
            context->CopySubresourceRegion(texture.Get(), mip, 0, 0, 0, composed.Get(), 0, nullptr);
        }
        if (FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
            return false;
        }
        auto* target = slot->rendererTexture;
        auto* oldView = target->resourceView;
        auto* oldResource = target->texture;
        target->texture = texture.Detach();
        target->resourceView = view.Detach();
        target->width = uint16_t(desc.Width);
        target->height = uint16_t(desc.Height);
        target->mips = uint8_t(desc.MipLevels);
        target->format = uint8_t(desc.Format);
        if (oldView) {
            oldView->Release();
        }
        if (oldResource) {
            oldResource->Release();
        }
        return true;
    }
}

#endif
