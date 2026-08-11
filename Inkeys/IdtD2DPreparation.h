#pragma once

#include "IdtMain.h"

#include <d2d1_1.h>
#include <dwrite_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern ComPtr<ID2D1Factory1> d2dFactory1;

extern ComPtr<IDWriteFactory1> dWriteFactory1;
extern ComPtr<IDWriteFontCollection> dWriteFontCollection;

enum class Ui3RenderBackend : unsigned char
{
	Warp,
	Hardware,
};

enum class Ui3RenderPriority : unsigned char
{
	Interactive,
	Cosmetic,
};

struct Ui3RenderDeviceEpoch
{
	Ui3RenderBackend backend = Ui3RenderBackend::Warp;
	unsigned long long generation = 0;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	ComPtr<ID3D11Device> d3dDevice;
	ComPtr<ID3D11Device1> d3dDevice1;
	ComPtr<ID2D1Device> d2dDevice;
};

// UI3 客户端用该租约包住完整绘制和提交区间，保证共享设备上的帧不会交错。
class Ui3RenderPass
{
public:
	Ui3RenderPass() noexcept = default;
	Ui3RenderPass(Ui3RenderPass&&) noexcept = default;
	Ui3RenderPass& operator=(Ui3RenderPass&&) noexcept = default;
	Ui3RenderPass(const Ui3RenderPass&) = delete;
	Ui3RenderPass& operator=(const Ui3RenderPass&) = delete;

	explicit operator bool() const noexcept { return renderLock.owns_lock(); }

private:
	friend Ui3RenderPass AcquireUi3RenderPass(Ui3RenderPriority priority);
	explicit Ui3RenderPass(unique_lock<mutex>&& lock) noexcept : renderLock(move(lock)) {}

	unique_lock<mutex> renderLock;
};

extern ComPtr<ID3D11Device> d3dDevice_UI3;
extern ComPtr<ID2D1Device> d2dDevice_UI3;

Ui3RenderDeviceEpoch GetUi3RenderDeviceEpoch();
Ui3RenderPass AcquireUi3RenderPass(Ui3RenderPriority priority);
HRESULT PrepareUi3RenderBackend(Ui3RenderBackend backend);
bool CommitPreparedUi3RenderBackend();

//class IdtFontFileEnumerator : public IDWriteFontFileEnumerator
//{
//public:
//
//	// IDWriteFontFileEnumerator methods
//	STDMETHOD(GetCurrentFontFile)(IDWriteFontFile** fontFile) override
//	{
//		*fontFile = m_font[m_currentfontCount - 1];
//
//		return S_OK;
//	}
//	STDMETHOD(MoveNext)(BOOL* hasCurrentFile) override
//	{
//		m_currentfontCount++;
//		*hasCurrentFile = m_currentfontCount > (int)m_font.size() ? FALSE : TRUE;
//
//		return S_OK;
//	}
//
//	// Idt methods
//	STDMETHOD(AddFont)(IDWriteFactory* factory, wstring fontPath)
//	{
//		IDWriteFontFile* D2DFont = nullptr;
//
//		// 文件导入方案
//		factory->CreateFontFileReference(fontPath.c_str(), 0, &D2DFont);
//
//		m_font.push_back(D2DFont);
//
//		return S_OK;
//	}
//
//	// IUnknown methods
//	STDMETHOD_(ULONG, AddRef)()
//	{
//		return InterlockedIncrement(&m_cRefCount);
//	}
//	STDMETHOD_(ULONG, Release)()
//	{
//		ULONG cNewRefCount = InterlockedDecrement(&m_cRefCount);
//		if (cNewRefCount == 0)
//		{
//			delete this;
//		}
//		return cNewRefCount;
//	}
//	STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj)
//	{
//		if ((riid == IID_IStylusSyncPlugin) || (riid == IID_IUnknown))
//		{
//			*ppvObj = this;
//			AddRef();
//			return S_OK;
//		}
//		else if ((riid == IID_IMarshal) && (m_punkFTMarshaller != NULL))
//		{
//			return m_punkFTMarshaller->QueryInterface(riid, ppvObj);
//		}
//
//		*ppvObj = NULL;
//		return E_NOINTERFACE;
//	}
//
//private:
//	int m_currentfontCount = 0;
//	IDWriteFactory* m_D2DTextFactory = nullptr;
//
//	vector<IDWriteFontFile*> m_font;
//
//	LONG m_cRefCount;
//	IUnknown* m_punkFTMarshaller;
//};
//class IdtFontCollectionLoader : public IDWriteFontCollectionLoader
//{
//public:
//
//	// IDWriteFontCollectionLoader methods
//	STDMETHOD(CreateEnumeratorFromKey)(IDWriteFactory* factory, void const* /*collectionKey*/, UINT32 /*collectionKeySize*/, IDWriteFontFileEnumerator** fontFileEnumerator) override
//	{
//		*fontFileEnumerator = D2DFontFileEnumerator;
//
//		return S_OK;
//	}
//
//	// Idt methods
//	STDMETHOD(AddFont)(IDWriteFactory* factory, wstring fontPath)
//	{
//		D2DFontFileEnumerator->AddFont(factory, fontPath);
//
//		return S_OK;
//	}
//
//	// IUnknown methods
//	STDMETHOD_(ULONG, AddRef)()
//	{
//		return InterlockedIncrement(&m_cRefCount);
//	}
//	STDMETHOD_(ULONG, Release)()
//	{
//		ULONG cNewRefCount = InterlockedDecrement(&m_cRefCount);
//		if (cNewRefCount == 0)
//		{
//			delete this;
//		}
//		return cNewRefCount;
//	}
//	STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj)
//	{
//		if ((riid == IID_IStylusSyncPlugin) || (riid == IID_IUnknown))
//		{
//			*ppvObj = this;
//			AddRef();
//			return S_OK;
//		}
//		else if ((riid == IID_IMarshal) && (m_punkFTMarshaller != NULL))
//		{
//			return m_punkFTMarshaller->QueryInterface(riid, ppvObj);
//		}
//
//		*ppvObj = NULL;
//		return E_NOINTERFACE;
//	}
//
//private:
//	IdtFontFileEnumerator* D2DFontFileEnumerator = new IdtFontFileEnumerator;
//
//	LONG m_cRefCount;
//	IUnknown* m_punkFTMarshaller;
//};

template <class T>
void DxObjectSafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}
HRESULT D2DStarup();
void D2DShutdown();
