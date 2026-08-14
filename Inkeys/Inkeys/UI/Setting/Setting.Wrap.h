#pragma once

#include "../../../IdtMain.h"
#include "Setting.Layout.h"
#include "Setting.Theme.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include "imgui/imgui_internal.h"
#include "imgui/imstb_rectpack.h"
#include "imgui/imstb_textedit.h"
#include "imgui/imstb_truetype.h"

#include "imgui/imgui_toggle.h"
#include "imgui/imgui_toggle_presets.h"
#include "imfluent/imfluent.h"

static_assert(IMGUI_VERSION_NUM == 19270,
	"The vendored ImFluent snapshot is validated with Dear ImGui 1.92.7 only.");

#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
