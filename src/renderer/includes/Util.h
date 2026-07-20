#pragma once
#include <Windows.h>
#include <stdio.h>
#include <cassert>

void DebugPrint(const char* fmt, ...);

template <class T>
void SafeRelease(T* ppT)
{
    if (ppT)
    {
        ppT->Release();
        ppT = nullptr;
    }
}