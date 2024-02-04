#pragma once

#include <winsdkver.h>
#ifndef _WIN32_WINNT
	#define _WIN32_WINNT 0x0A00
#endif
#include <sdkddkver.h>

#define NOMINMAX
#define NODRAWTEXT
#define NOGDI
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP
#define WIN32_LEAN_AND_MEAN

#define EPSILON 1e-12

#include <Windows.h>

#include <wrl/client.h>
#include <wrl/event.h>

#include <d3d12.h>
#include "d3dx12.h"
#include "shellapi.h"

#include <dxgi1_4.h>

#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>
#include <list>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <windowsx.h>

#ifdef _DEBUG
	#include <dxgidebug.h>
#endif

// DX Helper
#include "StepTimer.h"
#include "DDSTextureLoader12.h"
#include "ReadData.h"
#include "SimpleMath.h"

// imgui
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

// assimp
#include <Importer.hpp>
#include <scene.h>
#include <postprocess.h>

#pragma warning(disable : 4061)

#include <stdarg.h>

namespace DX
{
    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw std::exception();
        }
    }
}

static void DebugOut(const wchar_t* fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	wchar_t dbg_out[4096];
	vswprintf_s(dbg_out, fmt, argp);
	va_end(argp);
	OutputDebugString(dbg_out);
}

template <typename T>
static void Unique(const std::vector<T>& dupVec, std::vector<T>& uniqueVec)
{
	std::copy_if(dupVec.begin(), dupVec.end(), std::back_inserter(uniqueVec),
		[&](const T& e) { return uniqueVec.end() == std::find(uniqueVec.begin(), uniqueVec.end(), e); });
}

#define OutputDebugStringWFormat(fmt, ...) DebugOut(fmt, __VA_ARGS__);

#define TIMER_INIT \
    LARGE_INTEGER freq; \
    LARGE_INTEGER st,en; \
    double el; \
    QueryPerformanceFrequency(&freq)

#define TIMER_START QueryPerformanceCounter(&st)

#define TIMER_STOP \
    QueryPerformanceCounter(&en); \
    el=(float)(en.QuadPart-st.QuadPart)/freq.QuadPart

#define TIMER_STOP_PRINT \
    QueryPerformanceCounter(&en); \
    el=(float)(en.QuadPart-st.QuadPart)/freq.QuadPart; \
    OutputDebugStringWFormat(L"%f ms\n", el*1000)