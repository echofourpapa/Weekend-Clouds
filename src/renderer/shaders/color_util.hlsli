
#if !defined( COLOR_UTILS_H )
#define COLOR_UTILS_H

# define ST2084MAX 10000.0f

// https://en.wikipedia.org/wiki/SRGB
float3 sRGBToLinear(float3 color)
{
    return select(color > 0.04045f, pow((color + 0.055f) / 1.055f, 2.4f), color / 12.92f);
}

float4 sRGBToLinear(float4 color)
{
    return float4(sRGBToLinear(color.rgb), color.a);

}

float3 LinearTosRGB(float3 color)
{
    return select(color > 0.0031308f, 1.055f * pow(color, 1.f / 2.4f) - 0.055f, color * 12.92f);
}

float4 LinearTosRGB(float4 color)
{
    return float4(LinearTosRGB(color.rgb), color.a);

}


float3 Rec709ToRec2020(float3 color)
{
    static const float3x3 conversion =
    {
        0.627402, 0.329292, 0.043306,
        0.069095, 0.919544, 0.011360,
        0.016394, 0.088028, 0.895578
    };
    return mul(conversion, color);
}

float3 Rec2020ToRec709(float3 color)
{
    static const float3x3 conversion =
    {
        1.660496, -0.587656, -0.072840,
        -0.124547, 1.132895, -0.008348,
        -0.018154, -0.100597, 1.118751
    };
    return mul(conversion, color);
}

static const float m1 = 2610.0 / 4096.0 / 4;
static const float m2 = 2523.0 / 4096.0 * 128;
static const float c1 = 3424.0 / 4096.0;
static const float c2 = 2413.0 / 4096.0 * 32;
static const float c3 = 2392.0 / 4096.0 * 32;


float3 LinearToST2084(float3 color)
{
    float3 cp = pow(abs(color), m1);
    return pow((c1 + c2 * cp) / (1 + c3 * cp), m2);
}

float3 ST2084ToLinear(float3 color)
{
    float3 inv_cp = pow(color, 1.0 / m1);
    return pow(max(inv_cp - c1, 0.0) / (c2 - c3 * inv_cp), 1.0 / m2);
}

float3 hsv2rgb(float3 c)
{
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float3 UVtoRGB(float2 uv)
{
    float h = uv.x * 2.0 - 1.0;
    // expand V
    float sv = uv.y * 2.0 - 1.0;
    // top half to be sarutation
    float s = 1.0 - saturate(-sv);
    // bottom half to be value
    float v = 1.0 - saturate(sv);
    return hsv2rgb(float3(h, s, v));
}

float GetLuminance(float3 color)
{
    return dot(color, float3(0.2126729, 0.7151522, 0.0721750));
}

// Rec2020 helpers
// https://www.ryanjuckett.com/rgb-color-space-conversion/
// https://gist.github.com/pretzelhammer/22a6e61e31d97c691bbc95627d91ea04
// https://en.wikipedia.org/wiki/Rec._2020
/* 
Rec.2020 primaries:

Red:    [0.708, 0.292]
Green:  [0.170, 0.797]
Blue:   [0.131, 0.046]
WP:     [0.3127,0.3290]


xy to XYZ
x = x * 1 / y,
y = 1,
z =(1 - x - y) * 1 / y

XYZ to xy

x = X / (X + Y + Z)
y = Y / (X + Y + Z)

*/

#define Rec2020_R_x 0.708
#define Rec2020_R_y 0.292
#define Rec2020_G_x 0.170
#define Rec2020_G_y 0.797
#define Rec2020_B_x 0.131
#define Rec2020_B_y 0.046
#define Rec2020_W_x 0.3127
#define Rec2020_W_y 0.3290

static const float3 Rec2020_R_XYZ = float3(Rec2020_R_x * (1 / Rec2020_R_y), 1, (1 - Rec2020_R_x - Rec2020_R_y) * (1 / Rec2020_R_y));
static const float3 Rec2020_G_XYZ = float3(Rec2020_G_x * (1 / Rec2020_G_y), 1, (1 - Rec2020_G_x - Rec2020_G_y) * (1 / Rec2020_G_y));
static const float3 Rec2020_B_XYZ = float3(Rec2020_B_x * (1 / Rec2020_B_y), 1, (1 - Rec2020_B_x - Rec2020_B_y) * (1 / Rec2020_B_y));
static const float3 Rec2020_W_XYZ = float3(Rec2020_W_x * (1 / Rec2020_W_y), 1, (1 - Rec2020_W_x - Rec2020_W_y) * (1 / Rec2020_W_y));

float3 XYZToxyY(float3 XYZ)
{
    float3 xyY = 0;
    xyY.x = XYZ.x / (XYZ.x + XYZ.y + XYZ.z);
    xyY.y = XYZ.y / (XYZ.x + XYZ.y + XYZ.z);
    xyY.z = XYZ.y;
    return xyY;
}

float3 Rec2020ToXYZ(float3 rec2020)
{
    static const float3x3 conversion =
    {
        0.636958, 0.2627, 0,
        0.144617, 0.677998, 0.028073,
        0.168881, 0.059302, 1.060985
    };
    return mul(conversion, rec2020);
}

float3 Rec2020ToxyY(float3 rec2020)
{
    float3 XYZ = Rec2020ToXYZ(rec2020);
    return XYZToxyY(XYZ);
}

float3 xyYToXYZ(float3 xyY)
{
    float3 XYZ = 0;
    XYZ.x = xyY.x * xyY.z / xyY.y;
    XYZ.y = xyY.z;
    XYZ.z = (1 - xyY.x - xyY.y) * xyY.z / xyY.y;
    return XYZ;
}

float3 XYZToRec2020(float3 XYZ)
{
    static const float3x3 conversion =
    {
        1.716651, -0.666684, 0.01764,
        -0.355671, 1.616481, -0.042771,
        -0.253366, 0.015769, 0.942103
    };
    return mul(conversion, XYZ);
}

float3 xyYToRec2020(float3 xyY)
{
    float3 XYZ = xyYToXYZ(xyY);
    return XYZToRec2020(XYZ);
}

float GetLuminanceRec2020(float3 color)
{
    return dot(color, float3(0.262700, 0.677998, 0.059302));
}

// GT7 Tone Mapper Curve
// 
// Parameters:
// input            : Linear HDR color or luminance (nits)
// maxDisplayNits   : The Peak Brightness of the display (e.g. 1000.0)
// contrast         : Controls the slope of the linear section (default 1.0)
// linearStart      : The anchor point where the linear section begins (e.g. paperWhiteNits)
// linearLength     : The length of the linear section (default 0.4)
// blackTightness   : Controls how sharply the shadows fall off (default 1.33)
// minNits          : The black level of the display (e.g. 0.0 or 0.001)

float3 Tonemap_GT7(float3 input, float maxDisplayNits, float contrast, float linearStart, float linearLength, float blackTightness, float minNits)
{
    // --- Precompute Curve Constants ---
    
    // Calculate the geometric segments of the curve based on the parameters
    // These derived values ensure the Toe, Linear, and Shoulder segments join continuously
    
    // 'linearInterval' relates to how much range the linear section covers on the X-axis relative to the slope
    float linearInterval = ((maxDisplayNits - linearStart) * linearLength) / contrast;
    
    float toeEnd = linearStart - linearInterval;
    // float linearEnd = linearStart + (1.0 - linearInterval); // Unused in final equation simplification but part of derivation
    
    float shoulderStart = linearStart + linearInterval;
    float shoulderSplit = linearStart + contrast * linearInterval;
    
    float shoulderScale = (contrast * maxDisplayNits) / (maxDisplayNits - shoulderSplit);
    float shoulderPower = -shoulderScale / maxDisplayNits;

    // --- Calculate Weights ---
    // Determine which section of the curve (Toe, Linear, or Shoulder) applies to the input
    
    float3 toeWeight      = 1.0 - smoothstep(0.0, linearStart, input);
    float3 shoulderWeight = step(shoulderStart, input);
    float3 linearWeight   = 1.0 - toeWeight - shoulderWeight;

    // --- Evaluate Curve Segments ---

    // 1. Toe Segment (Shadows)
    // Uses a power function to roll off shadows smoothly to black
    float3 toeCurve = linearStart * pow(input / linearStart, blackTightness) + minNits;

    // 2. Linear Segment (Midtones)
    // Standard linear equation: y = mx + c
    float3 linearCurve = linearStart + contrast * (input - linearStart);

    // 3. Shoulder Segment (Highlights)
    // Uses an exponential decay to asymptote towards 'maxDisplayNits' without hard clipping
    float3 shoulderCurve = maxDisplayNits - (maxDisplayNits - shoulderSplit) * exp(shoulderPower * (input - shoulderStart));

    // --- Blend ---
    return toeCurve * toeWeight + linearCurve * linearWeight + shoulderCurve * shoulderWeight;
}

// Overload for single float (Luminance)
float Tonemap_GT7(float input, float maxDisplayNits, float contrast, float linearStart, float linearLength, float blackTightness, float minNits)
{
    float linearInterval = ((maxDisplayNits - linearStart) * linearLength) / contrast;
    
    float toeEnd = linearStart - linearInterval;
    float shoulderStart = linearStart + linearInterval;
    float shoulderSplit = linearStart + contrast * linearInterval;
    
    float shoulderScale = (contrast * maxDisplayNits) / (maxDisplayNits - shoulderSplit);
    float shoulderPower = -shoulderScale / maxDisplayNits;

    float toeWeight      = 1.0 - smoothstep(0.0, linearStart, input);
    float shoulderWeight = step(shoulderStart, input);
    float linearWeight   = 1.0 - toeWeight - shoulderWeight;

    float toeCurve = linearStart * pow(input / linearStart, blackTightness) + minNits;
    float linearCurve = linearStart + contrast * (input - linearStart);
    float shoulderCurve = maxDisplayNits - (maxDisplayNits - shoulderSplit) * exp(shoulderPower * (input - shoulderStart));

    return toeCurve * toeWeight + linearCurve * linearWeight + shoulderCurve * shoulderWeight;
}

#endif //COLOR_UTILS_H