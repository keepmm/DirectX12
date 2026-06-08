//#include "IconLibrary.hpp"
//#include "DirectX.hpp"
//#include "Logger.hpp"
//#include "d3dx12.h"
//#include "DirectXTex/DirectXTex.h"
//#include <filesystem>
//
//ImTextureID IconLibrary::GetOrLoad(const std::wstring& path)
//{
//    // Ç∑Ç≈Ç…ì«Ç›çûÇ›çœÇ›Ç»ÇÁë¶ï‘Ç∑
//    auto it = m_Cache.find(path);
//    if (it != m_Cache.end()) return it->second;
//
//    ImTextureID id = 0;
//    if (LoadTexture(path, id))
//    {
//        m_Cache[path] = id;
//        return id;
//    }
//    return 0;   // é∏îséûÇÕ0ÅiImGuiÇÕâΩÇ‡ï`Ç©Ç»Ç¢Åj
//}
//
//bool IconLibrary::LoadTexture(const std::wstring& path, ImTextureID& outId)
//{
//    if (m_App == nullptr) return false;
//    auto device = m_App->GetDevice();
//
//    DirectX::TexMetadata meta{};
//	DirectX::ScratchImage image{};
//    if (FAILED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, &meta, image)))
//    {
//        LOG->LogError("IconLibrary: PNG load failed");
//        return false;
//    }
//    const DirectX::Image* srcImage = image.GetImage(0, 0, 0);
//    if (srcImage == nullptr) return false;
//
//    const CD3DX12_RESOURCE_DESC texDesc = CD3DX12_RESOURCE_DESC::Tex2D(
//        meta.format, (UINT64)meta.width, (UINT)meta.height, 1, 1);
//    CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
//    ComPtr<ID3D12Resource> texture;
//    if (FAILED(device->CreateCommittedResource(
//        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
//        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
//        return false;
//
//    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
//    UINT numRows = 0; UINT64 rowSize = 0, uploadSize = 0;
//    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSize, &uploadSize);
//
//    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
//    CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
//    ComPtr<ID3D12Resource> upload;
//    if (FAILED(device->CreateCommittedResource(
//        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
//        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
//        return false;
//
//    void* mapped = nullptr;
//    CD3DX12_RANGE noRead(0, 0);
//    upload->Map(0, &noRead, &mapped);
//    auto* dst = reinterpret_cast<uint8_t*>(mapped);
//    const size_t copyBytes = (srcImage->rowPitch < (size_t)rowSize) ? srcImage->rowPitch : (size_t)rowSize;
//    for (UINT y = 0; y < meta.height; ++y)
//    {
//        std::memcpy(dst + (size_t)y * footprint.Footprint.RowPitch,
//            srcImage->pixels + (size_t)y * srcImage->rowPitch, copyBytes);
//    }
//    upload->Unmap(0, nullptr);
//
//    ComPtr<ID3D12CommandAllocator> alloc;
//    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
//    ComPtr<ID3D12GraphicsCommandList> cmd;
//    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd));
//
//    D3D12_TEXTURE_COPY_LOCATION dstLoc{ texture.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {} };
//    dstLoc.SubresourceIndex = 0;
//    D3D12_TEXTURE_COPY_LOCATION srcLoc{ upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {} };
//    srcLoc.PlacedFootprint = footprint;
//    cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
//
//    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
//        texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
//    cmd->ResourceBarrier(1, &barrier);
//    cmd->Close();
//
//    ID3D12CommandList* lists[] = { cmd.Get() };
//    m_App->GetCommandQueue()->ExecuteCommandLists(1, lists);
//    m_App->WaitForGPUIdle();   // ÉRÉsÅ[äÆóπÇ‹Ç≈ë“Ç¬
//
//    auto& srv = m_App->GetSrvAllocator();
//    UINT index = 0;
//    if (!srv.Allocate(index)) return false;   // íIÇ™ñûîtÇ»ÇÁé∏îs
//
//    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
//    srvDesc.Format = meta.format;
//    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
//    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
//    srvDesc.Texture2D.MipLevels = 1;
//    device->CreateShaderResourceView(texture.Get(), &srvDesc, srv.Cpu(index));
//
//    outId = (ImTextureID)srv.Gpu(index).ptr;
//    m_Textures.push_back(texture);   // âï˙Ç≥ÇÍÇ»Ç¢ÇÊÇ§ï€éù
//    return true;
//}
