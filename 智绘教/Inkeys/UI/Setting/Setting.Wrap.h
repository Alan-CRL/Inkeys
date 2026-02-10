#pragma once

#include "../../../IdtMain.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"

#include "imgui/imgui_toggle.h"
#include "imgui/imgui_toggle_presets.h"

#include <d3d9.h>
#pragma comment(lib, "d3d9")

// D3dx9tex.h 副本位于项目中
// 如果出现问题也可以选用 SDK 所属的默认路径
#include "Microsoft DirectX SDK (June 2010)/Include/D3dx9tex.h"