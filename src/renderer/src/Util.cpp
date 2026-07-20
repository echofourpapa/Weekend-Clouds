#include "Util.h"

void DebugPrint(const char* fmt, ...)
{
    char str[2048];
    va_list argptr;
    va_start(argptr, fmt);
    vsprintf_s(str, sizeof(str), fmt, argptr);
    va_end(argptr);
    OutputDebugStringA(str);
}
