#pragma once

#include "../../IdtMain.h"

#include "../../IdtD2DPreparation.h"

class IdtFontFileLoader;
class IdtFontFileEnumerator;
class IdtFontCollectionLoader;

class IdtFontFileStream : public IDWriteFontFileStream
{
public:
	IdtFontFileStream(const BYTE* fontData, UINT32 fontDataSize) :
		m_cRefCount(1),
		m_fontData(fontData),
		m_fontDataSize(fontDataSize)
	{
	}

	// --- IUnknown methods ---
	STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&m_cRefCount); }
	STDMETHOD_(ULONG, Release)() override
	{
		ULONG newCount = InterlockedDecrement(&m_cRefCount);
		if (newCount == 0)
		{
			delete this;
		}
		return newCount;
	}
	STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IDWriteFontFileStream) || riid == __uuidof(IUnknown))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	// --- IDWriteFontFileStream methods ---
	STDMETHOD(GetFileSize)(UINT64* fileSize) override
	{
		// --- 【添加日志 2】 ---
		cout << "IdtFontFileStream::GetFileSize CALLED" << endl;

		*fileSize = m_fontDataSize;
		return S_OK;
	}

	STDMETHOD(GetLastWriteTime)(UINT64* lastWriteTime) override
	{
		// 内存中的数据没有“最后写入时间”
		*lastWriteTime = 0;
		return S_OK;
	}

	// 【关键修改】这里不再进行内存拷贝
	STDMETHOD(ReadFileFragment)(
		void const** fragmentStart,
		UINT64 fileOffset,
		UINT64 fragmentSize,
		void** fragmentContext
		) override
	{
		// --- 【添加日志 3】 ---
		wchar_t buffer[256];
		swprintf_s(buffer, L"IdtFontFileStream::ReadFileFragment CALLED: Offset = %llu, Size = %llu\n", fileOffset, fragmentSize);
		wcout << buffer << endl;

		if (fileOffset > m_fontDataSize || fileOffset + fragmentSize > m_fontDataSize)
		{
			cout << "   -> ERROR: Invalid read range!" << endl;

			return E_FAIL;
		}

		*fragmentStart = m_fontData + fileOffset;
		*fragmentContext = nullptr;

		return S_OK;
	}

	// 【关键修改】由于 ReadFileFragment 没有分配新内存，这里什么都不用做
	STDMETHOD_(void, ReleaseFileFragment)(void* fragmentContext) override
	{
		// No-op
	}

private:
	LONG m_cRefCount;
	const BYTE* m_fontData;
	UINT32 m_fontDataSize;
};
class IdtFontFileLoader : public IDWriteFontFileLoader
{
public:
	IdtFontFileLoader() : m_cRefCount(1) {}
	~IdtFontFileLoader()
	{
		cout << "--- ~IdtFontFileLoader() DESTRUCTOR CALLED! ---" << endl;
		Testi(123);
	}

	// --- IUnknown methods ---
	STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&m_cRefCount); }
	STDMETHOD_(ULONG, Release)() override
	{
		ULONG newCount = InterlockedDecrement(&m_cRefCount);
		if (newCount == 0)
		{
			delete this;
		}
		return newCount;
	}
	STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IDWriteFontFileLoader) || riid == __uuidof(IUnknown))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	// --- IDWriteFontFileLoader methods ---
	// 【关键】这个方法的 key 就是我们传入的字体数据指针
	STDMETHOD(CreateStreamFromKey)(
		void const* fontFileReferenceKey,      // 字体数据的指针
		UINT32 fontFileReferenceKeySize,       // 字体数据的大小
		IDWriteFontFileStream** fontFileStream
		) override
	{
		// ******** 添加调试日志 ********
		wchar_t buffer[256];
		swprintf_s(buffer, L"IdtFontFileLoader::CreateStreamFromKey 被调用！KeySize = %u\n", fontFileReferenceKeySize);
		wcout << buffer << endl;
		// *****************************

		*fontFileStream = nullptr; // 初始化
		const BYTE* bytePtr = reinterpret_cast<const BYTE*>(fontFileReferenceKey);

		IdtFontFileStream* stream = new(std::nothrow) IdtFontFileStream(bytePtr, fontFileReferenceKeySize);
		if (stream == nullptr)
		{
			return E_OUTOFMEMORY;
			Testi(152);
		}

		// 【核心修复】
		// 将新创建的对象（引用计数为1）赋给输出参数。
		// 然后为调用者增加一个引用。
		// 这样 DirectWrite 拿到的对象引用计数至少为1，即使它内部 Release 了一次，对象依然存活。
		*fontFileStream = stream;
		(*fontFileStream)->AddRef(); // <-- 这行就是缺失的关键代码

		// 释放我们通过 new 创建的那个引用，因为现在它被 *fontFileStream 持有，并且我们为调用者 AddRef 了一次。
		stream->Release();

		return S_OK;
	}

private:
	LONG m_cRefCount;
};

class IdtFontFileEnumerator : public IDWriteFontFileEnumerator
{
public:
	// data/size: 指向整块字体二进制（.ttf 或 .ttc）
	IdtFontFileEnumerator(
		IDWriteFactory* factory,
		IDWriteFontFileLoader* loader,
		const BYTE* data,
		UINT32                  size
	) :
		m_refCount(1),
		m_factory(factory),
		m_loader(loader),
		m_fontData(data),
		m_fontDataSize(size),
		m_faceCount(0),
		m_currentFace(0),
		m_fontFile(nullptr)
	{
		// 保持 factory/loader 的引用
		m_factory->AddRef();
		m_loader->AddRef();

		// 创建一个文件级别的引用
		HRESULT hr = m_factory->CreateCustomFontFileReference(
			m_fontData,
			m_fontDataSize,
			m_loader,
			&m_fontFile
		);

		if (SUCCEEDED(hr) && m_fontFile)
		{
			// 分析出这是 .ttf 还是 .ttc，以及包含多少 face
			BOOL isSupported = FALSE;
			DWRITE_FONT_FILE_TYPE fileType;
			DWRITE_FONT_FACE_TYPE faceType;
			hr = m_fontFile->Analyze(
				&isSupported,
				&fileType,
				&faceType,
				&m_faceCount
			);

			if (!isSupported || m_faceCount == 0)
			{
				Testi(18);
				// 格式不支持或没 face
				m_fontFile->Release();
				m_fontFile = nullptr;
			}
		}
		else Testi(222);
	}

	~IdtFontFileEnumerator()
	{
		cout << "--- ~IdtFontFileEnumerator() DESTRUCTOR CALLED! ---" << endl;

		if (m_fontFile) m_fontFile->Release();
		m_loader->Release();
		m_factory->Release();
	}

	// IUnknown
	STDMETHOD_(ULONG, AddRef)() override
	{
		return InterlockedIncrement(&m_refCount);
	}
	STDMETHOD_(ULONG, Release)() override
	{
		ULONG u = InterlockedDecrement(&m_refCount);
		if (u == 0) delete this;
		return u;
	}
	STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWriteFontFileEnumerator))
		{
			*ppv = static_cast<IDWriteFontFileEnumerator*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	// IDWriteFontFileEnumerator
	STDMETHOD(MoveNext)(BOOL* hasCurrentFile) override
	{
		// 对于 .ttf，m_faceCount == 1；对于 .ttc，可能 >1
		*hasCurrentFile = (m_currentFace < m_faceCount) ? TRUE : FALSE;

		wprintf(
			L"[MoveNext] idx=%u, hasCurrentFile=%d\n",
			m_currentFace, *hasCurrentFile
		);

		if (*hasCurrentFile) m_currentFace++;
		return S_OK;
	}
	STDMETHOD(GetCurrentFontFile)(IDWriteFontFile** fontFile) override
	{
		UINT32 faceIndex = m_currentFace ? (m_currentFace - 1) : UINT32_MAX;
		wprintf(
			L"[GetCurrentFontFile] faceIndex=%u, m_fontFile=%p\n",
			faceIndex, m_fontFile
		);

		if (!m_fontFile ||
			m_currentFace == 0 ||
			m_currentFace > m_faceCount)
		{
			*fontFile = nullptr;
			return E_FAIL;

			Testi(275);
		}

		// 每次都把同一个文件引用返回给 DirectWrite，
		// CreateFontFace 时 DirectWrite 会传入当前的 faceIndex
		*fontFile = m_fontFile;
		(*fontFile)->AddRef();
		return S_OK;
	}

private:
	LONG                    m_refCount;
	IDWriteFactory* m_factory;
	IDWriteFontFileLoader* m_loader;

	const BYTE* m_fontData;
	UINT32                  m_fontDataSize;

	IDWriteFontFile* m_fontFile;     // CreateCustomFontFileReference 的返回值
	UINT32                  m_faceCount;    // Analyze 得到的 faceCount
	UINT32                  m_currentFace;  // MoveNext/GetCurrentFontFile 用来枚举
};
class IdtFontCollectionLoader : public IDWriteFontCollectionLoader
{
public:
	IdtFontCollectionLoader(
		IDWriteFactory* factory,
		IDWriteFontFileLoader* fileLoader
	) :
		m_refCount(1),
		m_factory(factory),
		m_fileLoader(fileLoader)
	{
		m_factory->AddRef();
		m_fileLoader->AddRef();
	}

	~IdtFontCollectionLoader()
	{
		cout << "--- ~IdtFontCollectionLoader() DESTRUCTOR CALLED! ---" << endl;

		m_fileLoader->Release();
		m_factory->Release();
	}

	// IUnknown
	STDMETHOD_(ULONG, AddRef)() override
	{
		return InterlockedIncrement(&m_refCount);
	}
	STDMETHOD_(ULONG, Release)() override
	{
		ULONG u = InterlockedDecrement(&m_refCount);
		if (u == 0) delete this;
		return u;
	}
	STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWriteFontCollectionLoader))
		{
			*ppv = static_cast<IDWriteFontCollectionLoader*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	// IDWriteFontCollectionLoader
	STDMETHOD(CreateEnumeratorFromKey)(
		IDWriteFactory* factory,
		void const* collectionKey,
		UINT32                        collectionKeySize,
		IDWriteFontFileEnumerator** fontFileEnumerator
		) override
	{
		wprintf(
			L"[CollectionLoader] CreateEnumeratorFromKey: key=%p, size=%u, factory=%p\n",
			collectionKey, collectionKeySize, factory
		);

		// collectionKey 就是 alignedData 指针
		const BYTE* data = reinterpret_cast<const BYTE*>(collectionKey);

		// 直接用我们的枚举器：它会在内部一次性分析出 faceCount 并准备好 m_fontFile
		auto enumerator = new (std::nothrow)
			IdtFontFileEnumerator(
				factory,
				m_fileLoader,
				data,
				collectionKeySize
			);
		if (!enumerator)
			return E_OUTOFMEMORY;

		*fontFileEnumerator = enumerator;
		(*fontFileEnumerator)->AddRef();
		enumerator->Release();
		return S_OK;
	}

private:
	LONG                    m_refCount;
	IDWriteFactory* m_factory;
	IDWriteFontFileLoader* m_fileLoader;
};