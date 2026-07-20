#include "DescriptorHeap.h"
#include "Awesome.h"
#include "Util.h"
#include <algorithm>

using namespace Awesome;

DescriptorHeap::DescriptorHeap(AwesomeGraphics* awesome)
    : m_Awesome(awesome)
    , m_heap(nullptr)
    , m_desc{}
    , m_descSize(0)
    , m_cpuHandle{0}
    , m_gpuHandle{0}
    , m_handlesSystem{}
    , m_handlesAssets{}
    , m_handlesToClear{}
{
    m_handlesToClear.resize(c_frameBufferCount);
}

DescriptorHeap::~DescriptorHeap()
{
    Release();
}

void DescriptorHeap::Release()
{
    SafeRelease(m_heap);
}

bool DescriptorHeap::Create(D3D12_DESCRIPTOR_HEAP_DESC desc, LPCWSTR name)
{
    m_desc = desc;
    if (FAILED(m_Awesome->Device()->CreateDescriptorHeap(&m_desc, IID_PPV_ARGS(&m_heap))))
        return false;
    m_heap->SetName(name);

    m_handlesToClear.reserve(desc.NumDescriptors);

    for (uint32 i = 0; i < desc.NumDescriptors; i++)
    {
        if (i < DescriptorSection::Assets)
            m_handlesSystem.push_back(i);
        else
            m_handlesAssets.push_back(i);
    }

    m_cpuHandle = m_heap->GetCPUDescriptorHandleForHeapStart();
    m_gpuHandle = m_heap->GetGPUDescriptorHandleForHeapStart();
    m_descSize = m_Awesome->Device()->GetDescriptorHandleIncrementSize(desc.Type);
    return true;
}

void DescriptorHeap::SortHeaps()
{
    std::sort(m_handlesSystem.begin(), m_handlesSystem.end());
    std::sort(m_handlesAssets.begin(), m_handlesAssets.end());
}

void DescriptorHeap::ReleaseHandles()
{
    std::sort(m_handlesToClear[m_Awesome->GetCurrentFrameIndex()].begin(), m_handlesToClear[m_Awesome->GetCurrentFrameIndex()].end());

    for (uint32 index : m_handlesToClear[m_Awesome->GetCurrentFrameIndex()])
    {
        if (index < DescriptorSection::Assets)
        {
            auto iter = std::find(m_handlesSystem.begin(), m_handlesSystem.end(), index);
            if(iter == m_handlesSystem.end())
                m_handlesSystem.push_back(index);
        }
        else
        {
            auto iter = std::find(m_handlesAssets.begin(), m_handlesAssets.end(), index);
            if (iter == m_handlesAssets.end())
                m_handlesAssets.push_back(index);
        }
    }
    DescriptorHeap::SortHeaps();
    m_handlesToClear[m_Awesome->GetCurrentFrameIndex()].clear();
}

DescriptorHandle DescriptorHeap::Allocate(DescriptorSection section)
{

    std::deque<uint32>& handles = (section == DescriptorSection::System) ? m_handlesSystem : m_handlesAssets;

    uint32 index = handles.front();
    handles.pop_front();

    DescriptorHandle handle = {};
    handle.index = index;
    handle.cpuHandle = m_cpuHandle;
    handle.gpuHandle = m_gpuHandle;

    handle.cpuHandle.ptr += index * m_descSize;
    handle.gpuHandle.ptr += index * m_descSize;
    return handle;
}

void DescriptorHeap::AllocateBlock(std::vector<DescriptorHandle>& outHandles, uint32 size, DescriptorSection section)
{
    uint32 start = 0;

    std::deque<uint32>& handles = (section == DescriptorSection::System) ? m_handlesSystem : m_handlesAssets;

    uint32 index = handles.front();
    uint32 end = handles[size-1];
    bool looking = (end-index) != (size - 1);
    while(looking)
    {
        if (++start + size > handles.size())
        {
            // I don't know, figure out something smart to do here
            start = 0;
        }

        index = handles[start];
        end = handles[start+size-1];

        if ((end - index) == (size - 1))
        {
            bool all = true;
            for (uint32 i = start; i < start + size; i++)
            {
                if (handles[i + 1] - handles[i] > 1)
                    all = false;
            }

            if (all)
                looking = false;
        }

    }

    for (uint32 i = start; i < start + size; i++)
    {
        index = handles[i];
        DescriptorHandle handle = {};
        handle.index = index;
        handle.cpuHandle = m_cpuHandle;
        handle.gpuHandle = m_gpuHandle;

        handle.cpuHandle.ptr += index * m_descSize;
        handle.gpuHandle.ptr += index * m_descSize;
        outHandles.push_back(handle);
    }

    auto iter = handles.begin() + start;
    handles.erase(iter, iter + size );
}

void DescriptorHeap::Free(DescriptorHandle handle)
{
    m_handlesToClear[m_Awesome->GetCurrentFrameIndex()].push_back(handle.index);
}


