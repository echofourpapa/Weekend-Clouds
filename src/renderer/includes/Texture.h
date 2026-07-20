#pragma once
#include "types.h"
#include <d3d12.h>
#include <vector>
#include <string>
#include "DescriptorHeap.h"

namespace Awesome
{
	class AwesomeGraphics;
	class MipGenerator;

	struct ImageData
	{
		std::string name;
		uint32 width;
		uint32 height;
		uint32 channels;
		DXGI_FORMAT format;
		uint32 mipCount;
		std::vector<std::vector<uint8>> images;

		uint32 GetRowPitch(uint32 mip) const
		{
			if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
				return width * channels * sizeof(float);
			uint32 block_size = 16;
			if (format == DXGI_FORMAT_BC1_UNORM_SRGB || format == DXGI_FORMAT_BC1_UNORM || format == DXGI_FORMAT_BC4_UNORM)
				block_size = 8;
			uint32 mipWidth = width >> mip;
			uint32 mipHeight = height >> mip;
			uint32 wpitch = std::max(1u, ((mipWidth + 3) / 4));
			return wpitch * block_size;
		}
	};

    struct Texture
    {
        DescriptorHandle handle;
        float sizeInfo[4];
        ID3D12Resource* resource;
    };

	class TextureSystem
	{
	public:
		TextureSystem(AwesomeGraphics* Awesome);
		~TextureSystem();

		bool StartUp();
		bool TearDown();

		//bool LoadAll();

		uint32 AddTexture(const char* path, bool genMips=false);
		void GenerateMips(ID3D12Resource* texture, D3D12_RESOURCE_STATES state);

		static bool ReadDDS(FILE* fp, bool sRGB, ImageData& outImage);
		static bool ReadStb(FILE* fp, bool sRGB, ImageData& outImage);

        Texture GetTexture(uint32 index);

	private:
		AwesomeGraphics* m_Awesome;
		MipGenerator* m_mipGen;
		ID3D12Resource* m_textureBufferUploadHeap;
		std::vector<Texture> m_textures;


	};
};