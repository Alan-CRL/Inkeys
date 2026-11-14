#pragma once

#include <windows.h>
#include <directmanipulation.h>

namespace Inkeys::Drawing::Draw3::Detail
{
	// MANUALUPDATE 只控制输出节拍；显式命中和 INPUT 才保护原 RTS/Mouse 输入所有权。
	template<class Viewport>
	HRESULT ConfigureTouchpadViewport(Viewport& viewport)
	{
		const auto configuration = DIRECTMANIPULATION_CONFIGURATION_INTERACTION |
			DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y |
			DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA;
		const HRESULT configured = viewport.ActivateConfiguration(configuration);
		if (FAILED(configured)) return configured;
		return viewport.SetViewportOptions(DIRECTMANIPULATION_VIEWPORT_OPTIONS_MANUALUPDATE |
			DIRECTMANIPULATION_VIEWPORT_OPTIONS_DISABLEPIXELSNAPPING |
			DIRECTMANIPULATION_VIEWPORT_OPTIONS_INPUT | DIRECTMANIPULATION_VIEWPORT_OPTIONS_EXPLICITHITTEST);
	}

	template<class Associate>
	bool DispatchTouchpadHitTest(bool admitted, bool typeKnown, POINTER_INPUT_TYPE type,
		UINT32 pointer, Associate associate)
	{
		return admitted && typeKnown && type == PT_TOUCHPAD && associate(pointer);
	}
}
