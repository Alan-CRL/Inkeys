#include "IdtGesture.h"

#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#pragma comment(lib, "propsys.lib")

BOOL IdtGesture::DisableEdgeGestures(HWND hwnd, BOOL disable)
{
	typedef HRESULT(WINAPI* SHGetPropertyStoreForWindowFunc)(HWND, REFIID, void**);

	HMODULE hShcore = LoadLibrary(TEXT("Shell32.dll"));
	if (!hShcore) return FALSE;

	SHGetPropertyStoreForWindowFunc pSHGetPropertyStoreForWindow = (SHGetPropertyStoreForWindowFunc)GetProcAddress(hShcore, "SHGetPropertyStoreForWindow");
	if (!pSHGetPropertyStoreForWindow)
	{
		FreeLibrary(hShcore);
		return FALSE;
	}

	IPropertyStore* pPropStore = NULL;
	HRESULT hr = pSHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&pPropStore));

	if (SUCCEEDED(hr))
	{
		PROPERTYKEY propKey;
		propKey.fmtid = GUID{ 0x32CE38B2, 0x2C9A, 0x41B1, { 0x9B, 0xC5, 0xB3, 0x78, 0x43, 0x94, 0xAA, 0x44 } };
		propKey.pid = 2;

		PROPVARIANT propVar;
		PropVariantInit(&propVar);
		propVar.vt = VT_BOOL;
		propVar.boolVal = (disable ? VARIANT_TRUE : VARIANT_FALSE);

		hr = pPropStore->SetValue(propKey, propVar);

		PropVariantClear(&propVar);
		pPropStore->Release();
	}

	FreeLibrary(hShcore);
	return SUCCEEDED(hr);
}