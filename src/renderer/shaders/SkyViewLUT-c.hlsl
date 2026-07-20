// Sky-view LUT (docs/PLAN.md 4.1). Entry shader; logic in Sky.hlsli.
// Reads the transmittance LUT (t4); writes the sky-view LUT (u5).
#include "Sky.hlsli"

COMPUTE_MAIN
{
    WriteSkyViewLUT(IN.DispatchThreadID.xy);
}
