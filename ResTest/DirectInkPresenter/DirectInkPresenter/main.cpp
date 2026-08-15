#include "pch.h"
#include "Application.h"

HINSTANCE g_hInstance = nullptr;

int WINAPI _tWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow
)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	int nResult = 0;
	if(HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0))
	{
		DirectInkPresenter::Utils::AutoCoInitialize autoCoInitialize;
		DirectInkPresenter::Application app;

		app.Initialize(hInstance);
		nResult = app.Run();
		
	}
	return nResult;
}
