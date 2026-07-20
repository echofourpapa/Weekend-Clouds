#include "Resource.h"
#include "Awesome.h"
#include "Util.h"

// things?

using namespace Awesome;


template <class T>
void hash_combine(size_t& s, const T& v)
{
    std::hash<T> h;
    s ^= h(v) + 0x9e3779b9 + (s << 6) + (s >> 2);
}

size_t ResourceDesc::GetHash(const ResourceDesc& desc)
{
    /*
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;
    */
    size_t res = 0;
    hash_combine(res, desc.desc.Dimension);
    hash_combine(res, desc.desc.Format);
    hash_combine(res, desc.desc.MipLevels);
    hash_combine(res, desc.desc.Alignment);
    hash_combine(res, desc.desc.DepthOrArraySize);
    hash_combine(res, desc.desc.Flags);
    hash_combine(res, desc.desc.Height);
    hash_combine(res, desc.desc.Layout);
    hash_combine(res, desc.desc.SampleDesc.Count);
    hash_combine(res, desc.desc.SampleDesc.Quality);
    hash_combine(res, desc.desc.Width);
    
    //hash_combine(res, desc.state);

    //hash_combine(res, desc.clear.Format);
    //hash_combine(res, desc.clear.Color[0]);
    //hash_combine(res, desc.clear.Color[1]);
    //hash_combine(res, desc.clear.Color[2]);
    //hash_combine(res, desc.clear.Color[3]);

    return res;
}
