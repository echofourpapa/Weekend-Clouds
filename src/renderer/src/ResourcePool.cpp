#include "ResourcePool.h"
#include "Awesome.h"
#include "Util.h"

using namespace Awesome;

ResourcePool::ResourcePool(AwesomeGraphics* awesome) 
    : m_Awesome(awesome)
    , m_pool{}
{
}

ResourcePool::~ResourcePool()
{
    TearDown();
}

Resource* ResourcePool::Allocate(ResourceDesc desc, LPCWSTR name, D3D12_HEAP_TYPE type)
{
    uint64 frame = m_Awesome->GetCurrentFrame();
    size_t desc_hash = ResourceDesc::GetHash(desc);
    auto found = m_pool.find(desc_hash);
    if (found != m_pool.end())
    {
        auto& reses = m_pool[desc_hash];
        for (auto& res : reses)
        {
            if (!(res.cpuInUse || res.gpuInUse))
            {
                res.cpuInUse = true;
                res.gpuInUse = true;
                res.lastFrameActive = frame;
                return &res;
            }
        }
    }

    auto& reses = m_pool[desc_hash];
    reses.reserve(10);
    Resource res = {};

    DebugPrint("New buffer ! %S on frame %d, hash %zu, total %d\n", name, frame, desc_hash, m_pool[desc_hash].size());

    D3D12_CLEAR_VALUE* cVal = nullptr;
    if ((desc.desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ||
        (desc.desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        cVal = &desc.clear;
    res.resource = m_Awesome->CreateBuffer(desc.desc, name, type, desc.state, cVal);
    assert(res.resource != nullptr);
    res.cpuInUse = true;
    res.gpuInUse = true;
    res.lastFrameActive = frame;

    reses.push_back(res);

    return &reses.back();

}

void ResourcePool::Free(Resource* buffer)
{
    if (buffer)
    {
        buffer->cpuInUse = false;
        buffer->gpuInUse = true;
    }
}

void ResourcePool::StartFrame()
{
    uint64 currentFrame = m_Awesome->GetCurrentFrame();
    for (auto& reses : m_pool)
    {
        for (auto& res : reses.second)
        {
            if (res.gpuInUse && !res.cpuInUse)
            {
                if (currentFrame - res.lastFrameActive >= c_frameBufferCount)
                {
                    res.cpuInUse = false;
                    res.gpuInUse = false;
                }
            }
        }
    }
}

void ResourcePool::TearDown()
{
    for (auto& reses : m_pool)
    {
        for (auto& res : reses.second)
        {
            SafeRelease(res.resource);
        }
        reses.second.clear();
    }
    m_pool.clear();
}
