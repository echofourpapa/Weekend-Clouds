#pragma once
#include "types.h"
#include "Awesome.h"
#include "pix3.h"
#include <d3d12.h>
#include "Constants.h"
#include <deque>

namespace Awesome
{
    struct DescriptorHandle
    {
        uint32 index;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    };

    enum DescriptorSection
    {
        System,
        Assets = 256
    };

    class DescriptorHeap
    {
    public:
        DescriptorHeap(AwesomeGraphics* awesome);
        ~DescriptorHeap();

        void Release();

        bool Create(D3D12_DESCRIPTOR_HEAP_DESC desc, LPCWSTR name);
        void SortHeaps();
        void ReleaseHandles();

        DescriptorHandle Allocate(DescriptorSection section=DescriptorSection::System);
        void AllocateBlock(std::vector<DescriptorHandle>& outHandles, uint32 size, DescriptorSection section = DescriptorSection::System);
        void Free(DescriptorHandle handle);

        ID3D12DescriptorHeap* GetHeap() { return m_heap; }
        uint32 GetOffset() { return m_descSize; }

    private:

        AwesomeGraphics* m_Awesome;
        ID3D12DescriptorHeap* m_heap;
        D3D12_DESCRIPTOR_HEAP_DESC m_desc;
        uint32 m_descSize;
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle;

        std::deque<uint32> m_handlesSystem;
        std::deque<uint32> m_handlesAssets;
        std::vector<std::vector<uint32>> m_handlesToClear;

    };
}