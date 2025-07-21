#pragma once

#include "IdtMain.h"

#include <d2d1_1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

extern CComPtr<ID2D1Factory> D2DFactory;
extern D2D1_RENDER_TARGET_PROPERTIES D2DProperty;

extern CComPtr<IDWriteFactory> D2DTextFactory;
extern CComPtr<IDWriteFontCollection> D2DFontCollection;

/*
* Dwrite win7 支持版本不能从内存中加载字体，故只能从本地字体文件加载
*
class IdtFontFileStream :public IDWriteFontFileStream
{
public:
	// IDWriteFontFileLoader methods
	STDMETHOD(GetFileSize)(UINT64* fileSize) override
	{
		//Testi(1);

		*fileSize = m_collectionKeySize;

		return S_OK;
	}
	STDMETHOD(GetLastWriteTime)(UINT64* lastWriteTime) override
	{
		//Testi(2);

		*lastWriteTime = 0;

		return S_OK;
	}
	STDMETHOD(ReadFileFragment)(void const** fragmentStart, UINT64 fileOffset, UINT64 fragmentSize, void** fragmentContext) override
	{
		//Testi(3);

		//Testi(fileOffset);
		//Testi(fragmentSize);

		void const* offsetAddress = reinterpret_cast<void const*>(reinterpret_cast<uintptr_t>(m_collectionKey) + fileOffset);

		vector<BYTE>* fontData = new vector<BYTE>(fragmentSize);
		memcpy(fontData->data(), offsetAddress, fragmentSize);

		//Testw(to_wstring((*fontData)[0]) + L" " + to_wstring((*fontData)[1]) + L" " + to_wstring((*fontData)[2]) + L" " + to_wstring((*fontData)[3]) + L" " + to_wstring((*fontData)[4]) + L" " + to_wstring((*fontData)[5]) + L" " + to_wstring((*fontData)[6]) + L" " + to_wstring((*fontData)[7]));

		*fragmentStart = fontData->data();
		*fragmentContext = fontData;

		return S_OK;
	}
	void STDMETHODCALLTYPE ReleaseFileFragment(void* fragmentContext) override
	{
		//Testi(4);

		delete fragmentContext;

		return;
	}

	// Idt methods
	STDMETHOD(SetFont)(void const* collectionKey, UINT32 collectionKeySize)
	{
		//Testi(5);

		m_collectionKey = collectionKey;
		m_collectionKeySize = collectionKeySize;

		return S_OK;
	}

	// IUnknown methods
	STDMETHOD_(ULONG, AddRef)()
	{
		return InterlockedIncrement(&m_cRefCount);
	}
	STDMETHOD_(ULONG, Release)()
	{
		ULONG cNewRefCount = InterlockedDecrement(&m_cRefCount);
		if (cNewRefCount == 0)
		{
			delete this;
		}
		return cNewRefCount;
	}
	STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj)
	{
		if ((riid == IID_IStylusSyncPlugin) || (riid == IID_IUnknown))
		{
			*ppvObj = this;
			AddRef();
			return S_OK;
		}
		else if ((riid == IID_IMarshal) && (m_punkFTMarshaller != NULL))
		{
			return m_punkFTMarshaller->QueryInterface(riid, ppvObj);
		}

		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

private:
	void const* m_collectionKey;
	UINT32 m_collectionKeySize;

	LONG m_cRefCount;
	IUnknown* m_punkFTMarshaller;
};
class IdtFontFileLoader :public IDWriteFontFileLoader
{
public:
	// IDWriteFontFileLoader methods
	STDMETHOD(CreateStreamFromKey)(void const* fontFileReferenceKey, UINT32 fontFileReferenceKeySize, IDWriteFontFileStream** fontFileStream) override
	{
		//Testi(6);

		IdtFontFileStream* D2DFontFileStream = new IdtFontFileStream;
		D2DFontFileStream->SetFont(fontFileReferenceKey, fontFileReferenceKeySize);

		*fontFileStream = D2DFontFileStream;

		return S_OK;
	}

	// IUnknown methods
	STDMETHOD_(ULONG, AddRef)()
	{
		return InterlockedIncrement(&m_cRefCount);
	}
	STDMETHOD_(ULONG, Release)()
	{
		ULONG cNewRefCount = InterlockedDecrement(&m_cRefCount);
		if (cNewRefCount == 0)
		{
			delete this;
		}
		return cNewRefCount;
	}
	STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj)
	{
		if ((riid == IID_IStylusSyncPlugin) || (riid == IID_IUnknown))
		{
			*ppvObj = this;
			AddRef();
			return S_OK;
		}
		else if ((riid == IID_IMarshal) && (m_punkFTMarshaller != NULL))
		{
			return m_punkFTMarshaller->QueryInterface(riid, ppvObj);
		}

		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

private:
	LONG m_cRefCount;
	IUnknown* m_punkFTMarshaller;
};
*/
class IdtFontFileEnumerator : public IDWriteFontFileEnumerator
{
public:

	// IDWriteFontFileEnumerator methods
	STDMETHOD(GetCurrentFontFile)(IDWriteFontFile** fontFile) override
	{
		*fontFile = m_font[m_currentfontCount - 1];

		return S_OK;
	}
	STDMETHOD(MoveNext)(BOOL* hasCurrentFile) override
	{
		m_currentfontCount++;
		*hasCurrentFile = m_currentfontCount > (int)m_font.size() ? FALSE : TRUE;

		return S_OK;
	}

	// Idt methods
	STDMETHOD(AddFont)(IDWriteFactory* factory, wstring fontPath)
	{
		IDWriteFontFile* D2DFont = nullptr;

		// 文件导入方案
		factory->CreateFontFileReference(fontPath.c_str(), 0, &D2DFont);

		m_font.push_back(D2DFont);

		return S_OK;
	}

	// IUnknown methods
	STDMETHOD_(ULONG, AddRef)()
	{
		return InterlockedIncrement(&m_cRefCount);
	}
	STDMETHOD_(ULONG, Release)()
	{
		ULONG cNewRefCount = InterlockedDecrement(&m_cRefCount);
		if (cNewRefCount == 0)
		{
			delete this;
		}
		return cNewRefCount;
	}
	STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj)
	{
		if ((riid == IID_IStylusSyncPlugin) || (riid == IID_IUnknown))
		{
			*ppvObj = this;
			AddRef();
			return S_OK;
		}
		else if ((riid == IID_IMarshal) && (m_punkFTMarshaller != NULL))
		{
			return m_punkFTMarshaller->QueryInterface(riid, ppvObj);
		}

		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

private:
	int m_currentfontCount = 0;
	IDWriteFactory* m_D2DTextFactory = nullptr;

	vector<IDWriteFontFile*> m_font;

	LONG m_cRefCount;
	IUnknown* m_punkFTMarshaller;
};
class IdtFontCollectionLoader : public IDWriteFontCollectionLoader
{
public:

	// IDWriteFontCollectionLoader methods
	STDMETHOD(CreateEnumeratorFromKey)(IDWriteFactory* factory, void const* /*collectionKey*/, UINT32 /*collectionKeySize*/, IDWriteFontFileEnumerator** fontFileEnumerator) override
	{
		*fontFileEnumerator = D2DFontFileEnumerator;

		return S_OK;
	}

	// Idt methods
	STDMETHOD(AddFont)(IDWriteFactory* factory, wstring fontPath)
	{
		D2DFontFileEnumerator->AddFont(factory, fontPath);

		return S_OK;
	}

	// IUnknown methods
	STDMETHOD_(ULONG, AddRef)()
	{
		return InterlockedIncrement(&m_cRefCount);
	}
	STDMETHOD_(ULONG, Release)()
	{
		ULONG cNewRefCount = InterlockedDecrement(&m_cRefCount);
		if (cNewRefCount == 0)
		{
			delete this;
		}
		return cNewRefCount;
	}
	STDMETHOD(QueryInterface)(REFIID riid, LPVOID* ppvObj)
	{
		if ((riid == IID_IStylusSyncPlugin) || (riid == IID_IUnknown))
		{
			*ppvObj = this;
			AddRef();
			return S_OK;
		}
		else if ((riid == IID_IMarshal) && (m_punkFTMarshaller != NULL))
		{
			return m_punkFTMarshaller->QueryInterface(riid, ppvObj);
		}

		*ppvObj = NULL;
		return E_NOINTERFACE;
	}

private:
	IdtFontFileEnumerator* D2DFontFileEnumerator = new IdtFontFileEnumerator;

	LONG m_cRefCount;
	IUnknown* m_punkFTMarshaller;
};

template <class T> void DxObjectSafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}
void D2DStarup();
void D2DShutdown();

D2D1::ColorF ConvertToD2DColor(COLORREF Color, bool ReserveAlpha = true);
void SetAlpha(COLORREF& Color, int Alpha);