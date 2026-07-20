// Sky transmittance LUT (docs/PLAN.md 4.1). Entry shader; logic in Sky.hlsli.
#include "Sky.hlsli"

COMPUTE_MAIN
{
    WriteTransLUT(IN.DispatchThreadID.xy);
}
