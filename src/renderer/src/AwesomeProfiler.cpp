#include "AwesomeProfiler.h"
#include "Awesome.h"
#include "Util.h" // For DebugPrint and SafeRelease helpers

using namespace Awesome;

AwesomeProfiler::AwesomeProfiler(AwesomeGraphics* Awesome)
    : m_Awesome(Awesome)
    , m_queryHeap(nullptr)
    , m_queryReadbackBuffer(nullptr)
    , m_timestampFrequency(0)
{
    m_profiles.resize(c_maxProfileQueries / 2);
}

AwesomeProfiler::~AwesomeProfiler()
{
}

bool AwesomeProfiler::StartUp()
{
    ID3D12Device* device = m_Awesome->Device();
    ID3D12CommandQueue* queue = m_Awesome->GetCommandQueue(); // Assuming accessor exists or friend class

    if (FAILED(queue->GetTimestampFrequency(&m_timestampFrequency)))
    {
        DebugPrint("Failed to get Timestamp Frequency");
        return false;
    }

    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryHeapDesc.Count = c_maxProfileQueries;
    queryHeapDesc.NodeMask = 0;

    if (FAILED(device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_queryHeap))))
    {
        DebugPrint("Failed to create Query Heap");
        return false;
    }
    m_queryHeap->SetName(L"AwesomeProfiler Query Heap");
    
    uint64 bufferSize = c_maxProfileQueries * c_querySize * c_frameBufferCount;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = bufferSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_queryReadbackBuffer))))
    {
        DebugPrint("Failed to create Profiler Readback Buffer");
        return false;
    }
    m_queryReadbackBuffer->SetName(L"AwesomeProfiler Readback Buffer");

    return true;
}

void AwesomeProfiler::TearDown()
{
    FreeResources();
}

void AwesomeProfiler::FreeResources()
{
    SafeRelease(m_queryHeap);
    SafeRelease(m_queryReadbackBuffer);
}

void AwesomeProfiler::BeginProfile(ID3D12GraphicsCommandList* cmdList, const std::string& name, uint32 idx)
{
    if (idx >= m_profiles.size()) return;

    m_profiles[idx].name = name;
    m_profiles[idx].startIndex = idx * 2;     
    m_profiles[idx].endIndex = idx * 2 + 1;   
    m_profiles[idx].active = true;

    cmdList->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, m_profiles[idx].startIndex);
}

void AwesomeProfiler::EndProfile(ID3D12GraphicsCommandList* cmdList, const std::string& name, uint32 idx)
{
    if (idx >= m_profiles.size()) return;

    cmdList->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, m_profiles[idx].endIndex);
}

void AwesomeProfiler::Resolve(ID3D12GraphicsCommandList* cmdList)
{
    uint32 frameIndex = m_Awesome->GetCurrentFrameIndex();
    uint64 frameBaseOffset = frameIndex * c_maxProfileQueries * c_querySize;


    uint32 startProfileIdx = 0;
    while (startProfileIdx < m_profiles.size())
    {
        if (!m_profiles[startProfileIdx].active)
        {
            startProfileIdx++;
            continue;
        }

        uint32 endProfileIdx = startProfileIdx;
        while (endProfileIdx < m_profiles.size() && m_profiles[endProfileIdx].active)
        {
            endProfileIdx++;
        }

        uint32 startQueryIdx = startProfileIdx * 2;
        uint32 numQueries = (endProfileIdx - startProfileIdx) * 2;
        
        uint64 batchOffset = frameBaseOffset + (startQueryIdx * c_querySize);

        cmdList->ResolveQueryData(
            m_queryHeap,
            D3D12_QUERY_TYPE_TIMESTAMP,
            startQueryIdx,
            numQueries,
            m_queryReadbackBuffer,
            batchOffset
        );

        startProfileIdx = endProfileIdx;
    }
}

void AwesomeProfiler::Readback()
{
  
    uint32 frameIndex = m_Awesome->GetCurrentFrameIndex();
    uint64 offset = frameIndex * c_maxProfileQueries * c_querySize;

    D3D12_RANGE readRange = { offset, offset + (c_maxProfileQueries * c_querySize) };
    
    uint64* pData = nullptr;
    if (FAILED(m_queryReadbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData))))
    {
        return;
    }

    uint64* frameData = pData + (frameIndex * c_maxProfileQueries);

    for (int i = 0; i < m_profiles.size(); ++i)
    {
        if (!m_profiles[i].active) continue;

        uint64 startTick = frameData[m_profiles[i].startIndex];
        uint64 endTick = frameData[m_profiles[i].endIndex];

        if (endTick > startTick)
        {
            uint64 delta = endTick - startTick;
            float currentMs = (static_cast<float>(delta) / static_cast<float>(m_timestampFrequency)) * 1000.0f;
            m_profiles[i].timeMs = currentMs;
            if (m_profiles[i].valid)
            {
                const float alpha = 0.05f; 
                m_profiles[i].avgTimeMs = (m_profiles[i].avgTimeMs * (1.0f - alpha)) + (currentMs * alpha);
            }
            else
            {
                m_profiles[i].avgTimeMs = currentMs;
            }
            m_profiles[i].valid = true;
            m_profiles[i].framesSinceUpdate = 0;
        }
        else
        {
            m_profiles[i].timeMs = 0.0f; 
            
            m_profiles[i].framesSinceUpdate++;
            if (m_profiles[i].framesSinceUpdate > 60)
            {
                m_profiles[i].valid = false;
            }
        }

        m_profiles[i].active = false;
    }

    D3D12_RANGE writeRange = { 0, 0 };
    m_queryReadbackBuffer->Unmap(0, &writeRange);
}