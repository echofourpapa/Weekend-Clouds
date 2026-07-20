#pragma once
#include "types.h"
#include <d3d12.h>
#include <string>
#include <vector>

namespace Awesome
{
    class AwesomeGraphics;

    struct ProfileData
    {
        std::string name;
        float timeMs;
        float avgTimeMs;
        bool active;
        bool valid;
        uint32 startIndex;
        uint32 endIndex;
        uint32 framesSinceUpdate;
    };

    class AwesomeProfiler
    {
    public:
        AwesomeProfiler(AwesomeGraphics* Awesome);
        ~AwesomeProfiler();

        bool StartUp();
        void TearDown();

        void BeginProfile(ID3D12GraphicsCommandList* cmdList, const std::string& name, uint32 idx);
        void EndProfile(ID3D12GraphicsCommandList* cmdList, const std::string& name, uint32 idx);
        
        void Readback();

        void Resolve(ID3D12GraphicsCommandList* cmdList);

        const std::vector<ProfileData>& GetProfiles() const { return m_profiles; }

    private:
        AwesomeGraphics* m_Awesome;
        
        ID3D12QueryHeap* m_queryHeap;
        ID3D12Resource* m_queryReadbackBuffer;
        
        uint64 m_timestampFrequency;
        std::vector<ProfileData> m_profiles;

        static const uint32 c_maxProfileQueries = 128; // Must be even
        static const uint32 c_querySize = sizeof(uint64);

        void FreeResources();
    };
    
    class ScopedGPUProfile
    {
    public:
        ScopedGPUProfile(AwesomeProfiler* profiler, ID3D12GraphicsCommandList* cmdList, const std::string& name, uint32 idx)
            : m_profiler(profiler)
            , m_cmdList(cmdList)
            , m_name(name)
            , m_idx(idx)
        {
            if (m_profiler)
                m_profiler->BeginProfile(m_cmdList, m_name, m_idx);
        }

        ~ScopedGPUProfile()
        {
            if (m_profiler)
                m_profiler->EndProfile(m_cmdList, m_name, m_idx);
        }
        
        // Disable copying
        ScopedGPUProfile(const ScopedGPUProfile&) = delete;
        ScopedGPUProfile& operator=(const ScopedGPUProfile&) = delete;

    private:
        AwesomeProfiler* m_profiler;
        ID3D12GraphicsCommandList* m_cmdList;
        std::string m_name;
        uint32 m_idx;
    };
}

#define AWESOME_CONCAT_IMPL(x, y) x##y
#define AWESOME_CONCAT(x, y) AWESOME_CONCAT_IMPL(x, y)

#define PROFILE_SCOPE(profiler, cmdList, name, id) \
    Awesome::ScopedGPUProfile AWESOME_CONCAT(_profile_, __LINE__)(profiler, cmdList, name, id)