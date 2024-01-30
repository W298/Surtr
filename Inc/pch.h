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
#include <tuple>
#include <unordered_map>
#include <windowsx.h>

#ifdef _DEBUG
	#include <dxgidebug.h>
#endif

#include "StepTimer.h"
#include "DDSTextureLoader12.h"
#include "ReadData.h"

#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

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

#define OutputDebugStringWFormat(fmt, ...) DebugOut(fmt, __VA_ARGS__);