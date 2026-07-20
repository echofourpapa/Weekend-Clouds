#include "Texture.h"
#include "Material.h"
#include "Awesome.h"
#include "Util.h"
#include "MipGenerator.h"
#include <d3d12.h>
#include "DDS.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include "DescriptorHeap.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace Awesome;

TextureSystem::TextureSystem(AwesomeGraphics* Awesome) 
	: m_Awesome(Awesome)
	, m_mipGen(new MipGenerator(m_Awesome))
	, m_textureBufferUploadHeap(nullptr)
	, m_textures(0)
{

}

TextureSystem::~TextureSystem()
{
	
}

bool TextureSystem::StartUp()
{
	return m_mipGen->StartUp();
}

bool TextureSystem::TearDown()
{
    for (Texture texture : m_textures)
    {
        SafeRelease(texture.resource);
        m_Awesome->GetMainDescHeap()->Free(texture.handle);
    }
	SafeRelease(m_textureBufferUploadHeap);
	m_textures.clear();
	m_mipGen->TearDown(); m_mipGen = nullptr;
	return true;
}

uint32 TextureSystem::AddTexture(const char* path, bool genMips)
{
	uint32 index = uint32(m_textures.size());
	ImageData img = {};
	{
		std::string name = std::string(std::filesystem::path(path).filename().string().c_str());
		DebugPrint("Reading Image: %s\n", name.c_str());

		FILE* fp;
		fopen_s(&fp, path, "rb");
		if (!fp)
		{
			DebugPrint("Couldn't read file: %s\n", path);
			return invalidIndex32;
		}


		bool res = false;
		bool sRGB = name.find("BaseColor") != std::string::npos;
		std::string ext = std::filesystem::path(path).extension().string();
		if (ext == ".dds")
			res = ReadDDS(fp, sRGB, img);
		else if (ext == ".hdr" || ext == ".tga" || ext == ".png")
			res = ReadStb(fp, false, img);

		fclose(fp);
		if (!res)
			return invalidIndex32;
		img.name = name;
	}
	Texture texture;

    texture.sizeInfo[0] = 1.0f / (float)img.width;
    texture.sizeInfo[1] = 1.0f / (float)img.height;
    texture.sizeInfo[2] = (float)img.width;
    texture.sizeInfo[3] = (float)img.height;
	
	m_Awesome->BeginFrame(false);
	{
		DebugPrint("Loading Image: %s\n", img.name.c_str());
		D3D12_RESOURCE_DESC tDesc = {};
		tDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		tDesc.Alignment = 0; // may be 0, 4KB, 64KB, or 4MB. 0 will let runtime decide between 64KB and 4MB (4MB for multi-sampled textures)
		tDesc.Width = img.width; // width of the texture
		tDesc.Height = img.height; // height of the texture
		tDesc.DepthOrArraySize = 1; // if 3d image, depth of 3d image. Otherwise an array of 1D or 2D textures (we only have one image, so we set 1)
		tDesc.MipLevels = genMips ? (uint32)log2(std::max(img.width, img.height)) + 1 : img.mipCount; // Number of mipmaps. We are not generating mipmaps for this texture, so we have only one level
		tDesc.Format = img.format; // This is the dxgi format of the image (format of the pixels)
		tDesc.SampleDesc.Count = 1; // This is the number of samples per pixel, we just want 1 sample
		tDesc.SampleDesc.Quality = 0; // The quality level of the samples. Higher is better quality, but worse performance
		tDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // The arrangement of the pixels. Setting to unknown lets the driver choose the most efficient one
		tDesc.Flags = D3D12_RESOURCE_FLAG_NONE; // no flags

		wchar_t name[256];
		swprintf(name, 256, L"%S", img.name.c_str());
		texture.resource = m_Awesome->CreateBuffer(tDesc, name, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
		

		//// now we create a shader resource view (descriptor that points to the texture and describes it)
		//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		//srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		//srvDesc.Format = img.format;
		//srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		//srvDesc.Texture2D.MipLevels = img.mipCount;
		//uint32 toffset = m_Awesome->Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		//auto handle = m_Awesome->GetMaterialSystem()->GetMainDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
		//handle.ptr += toffset * index;

		//if (m_Awesome->HasImGui())
		//	handle.ptr += toffset;

        texture.handle = m_Awesome->GetMainDescHeap()->Allocate(DescriptorSection::Assets);

		m_Awesome->Device()->CreateShaderResourceView(texture.resource, nullptr, texture.handle.cpuHandle);

		m_Awesome->UploadTexture(texture.resource, tDesc, 0, img);
		m_Awesome->TransitionResource(texture.resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	m_textures.push_back(texture);
	m_Awesome->EndFrame(false);

	if (genMips)
		GenerateMips(texture.resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	return index;
}

void TextureSystem::GenerateMips(ID3D12Resource* texture, D3D12_RESOURCE_STATES state)
{
	m_mipGen->GenerateMips(texture, state);
}

bool TextureSystem::ReadDDS(FILE* fp, bool sRGB, ImageData& outImage)
{
	uint32 magic;
	fread(&magic, sizeof(uint32), 1, fp);
	if (magic != DirectX::DDS_MAGIC)
	{
		DebugPrint("Bad magic\n");
		fclose(fp);
		return false;
	}

	DirectX::DDS_HEADER header;    
	fread(&header, sizeof(DirectX::DDS_HEADER), 1, fp);

    DirectX::DDS_HEADER_DXT10 header_dx10{};
	if (header.ddspf.fourCC == DirectX::DDSPF_DX10.fourCC)
	{
		fread(&header_dx10, sizeof(DirectX::DDS_HEADER_DXT10), 1, fp);
	}

	// Why doesn't non-sRGB work?
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	if ((header.ddspf.flags & DDS_FOURCC) != 0)
	{
        if (header.ddspf.fourCC == DirectX::DDSPF_DXT1.fourCC)
            format = sRGB ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
        else if (header.ddspf.fourCC == DirectX::DDSPF_DXT5.fourCC)
            format = sRGB ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
        else if (header.ddspf.fourCC == MAKEFOURCC('A', 'T', 'I', '2'))
            format = DXGI_FORMAT_BC5_UNORM;
        else if (header.ddspf.fourCC == DirectX::DDSPF_DX10.fourCC)
            format = header_dx10.dxgiFormat;
		else
		{
			char buf[4];
			std::memcpy(buf, &header.ddspf.fourCC, 4);
			DebugPrint("Unknown: %s\n", buf);
		}
	}

	uint32 block_size = 16;
	if(format == DXGI_FORMAT_BC1_UNORM_SRGB || format == DXGI_FORMAT_BC1_UNORM || format == DXGI_FORMAT_BC4_UNORM)
		block_size = 8;

	outImage.width = header.width;
	outImage.height = header.height;
	outImage.mipCount = std::max(1u, header.mipMapCount);
	outImage.format = format;
	outImage.images.resize(outImage.mipCount);

	for (uint32 i = 0; i < outImage.mipCount; ++i)
	{
		uint32 mipWidth = outImage.width >> i;
		uint32 mipHeight = outImage.height >> i;
		uint32 wpitch = std::max(1u, ((mipWidth + 3) / 4));
		uint32 hpitch = std::max(1u, ((mipHeight + 3) / 4));
		uint32 pitch = wpitch * block_size;
		uint32 total_size = wpitch * hpitch * block_size;
		outImage.images[i].resize(total_size);
		fread(&outImage.images[i].front(), sizeof(uint8), total_size, fp);
	}
	return true;
}

bool TextureSystem::ReadStb(FILE* fp, bool sRGB, ImageData& outImage)
{
	bool hdr = stbi_is_hdr_from_file(fp);
	outImage.images.resize(1);
	if (hdr)
	{
		float* readImg = stbi_loadf_from_file(fp, (int*)&outImage.width, (int*)&outImage.height, (int*)&outImage.channels, 4);
		outImage.channels = 4;
		uint32 total_size = outImage.width * outImage.height * outImage.channels * sizeof(float);
		outImage.images[0].resize(total_size);
		uint8* src = reinterpret_cast<uint8*>(readImg);
		memcpy(&outImage.images[0].front(), src, total_size);
	}
	else
	{
		uint8* readImg = stbi_load_from_file(fp, (int*)&outImage.width, (int*)&outImage.height, (int*)&outImage.channels, 4);
		outImage.channels = 4;
		uint32 total_size = outImage.width * outImage.height * outImage.channels * sizeof(uint8);
		outImage.images[0].resize(total_size);
		//uint8* src = reinterpret_cast<uint8*>(readImg);
		memcpy(&outImage.images[0].front(), readImg, total_size);
	}

	bool compressed = false;
	if(hdr)
		outImage.format = compressed ? DXGI_FORMAT_BC6H_UF16 : DXGI_FORMAT_R32G32B32A32_FLOAT;
	else
	{
		outImage.format = compressed ? (sRGB ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM): (sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
	}
	outImage.mipCount = 1;
	return true;
}

Texture TextureSystem::GetTexture(uint32 index)
{
    return m_textures[index];
}
