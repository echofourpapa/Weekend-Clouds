#pragma once

typedef signed char int8;
typedef signed short int16;
typedef signed int int32;
typedef __int64 int64;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned __int64 uint64;

const uint8 c_frameBufferCount = 3;
const uint8 c_maxCascadeCount = 4;
const float c_clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
const float c_uiClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

const float c_minLightIntesity = 0.03f;

const uint32 invalidIndex32 = -1;