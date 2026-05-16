#pragma once

// DirectX
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <d2d1_3.h>
#include <dcomp.h>
#include <dwrite_3.h>
// WIC
#include <wincodec.h>
// Windows Animation Manager
#include <UIAnimation.h>
// Media Foundation
#include <mfapi.h>
#include <mfmediaengine.h>
#include <audiosessiontypes.h>
// Windows Runtime Library
#include <wrl.h>
#include <wrl\client.h>
#include <wrl\module.h>
#include <wrl\implements.h>
#include <wrl\wrappers\corewrappers.h>
// Libraries
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "psapi")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "d2d1")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dwrite")
#pragma comment(lib, "dcomp")