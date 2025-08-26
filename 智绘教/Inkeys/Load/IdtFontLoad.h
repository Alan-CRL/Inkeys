#pragma once

#include "../../IdtMain.h"

#include "../../IdtD2DPreparation.h"

class IdtFontFileLoader;
class IdtFontFileEnumerator;
class IdtFontCollectionLoader;

class IdtFontFileStream : public IDWriteFontFileStream
{
public:
	IdtFontFileStream(void const* fontData, UINT32 fontDataSize) :
		m_cRefCount(1),
		m_fontData(nullptr),
		m_fontDataSize(fontDataSize)
	{
		// 1. new[] 返回的内存保证是充分对齐的
		m_fontData = new (std::nothrow) BYTE[fontDataSize];
		if (m_fontData)
		{
			// 2. 将未对齐的资源数据复制到我们自己管理的、对齐的内存中
			memcpy(m_fontData, fontData, fontDataSize);
		}
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

			*fragmentStart = nullptr;
			*fragmentContext = nullptr;
			return E_FAIL;
		}

		*fragmentStart = static_cast<const BYTE*>(m_fontData) + fileOffset;
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
	BYTE* m_fontData;
	UINT32 m_fontDataSize;
};
class IdtFontFileLoader : public IDWriteFontFileLoader
{
public:
	IdtFontFileLoader() : m_cRefCount(1) {}
	~IdtFontFileLoader()
	{
		cout << "--- ~IdtFontFileLoader() DESTRUCTOR CALLED! ---" << endl;
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

		IdtFontFileStream* stream = new(std::nothrow) IdtFontFileStream(fontFileReferenceKey, fontFileReferenceKeySize);
		if (stream == nullptr)
		{
			return E_OUTOFMEMORY;
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
	IdtFontFileEnumerator(IDWriteFactory* factory, IDWriteFontFileLoader* loader) :
		m_cRefCount(1),
		m_factory(factory),
		m_loader(loader),
		m_currentFileIndex(0)
	{
		// 保持对 factory 和 loader 的引用
		m_factory->AddRef();
		m_loader->AddRef();
	}
	~IdtFontFileEnumerator()
	{
		for (auto& file : m_fontFiles)
		{
			if (file) file->Release();
		}
		m_loader->Release();
		m_factory->Release();
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
		if (riid == __uuidof(IDWriteFontFileEnumerator) || riid == __uuidof(IUnknown))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	// --- IDWriteFontFileEnumerator methods ---
	STDMETHOD(MoveNext)(BOOL* hasCurrentFile) override
	{
		*hasCurrentFile = FALSE;
		if (m_currentFileIndex < m_fontFiles.size())
		{
			*hasCurrentFile = TRUE;
			m_currentFileIndex++;
		}
		return S_OK;
	}

	STDMETHOD(GetCurrentFontFile)(IDWriteFontFile** fontFile) override
	{
		if (m_currentFileIndex == 0 || m_currentFileIndex > m_fontFiles.size())
		{
			*fontFile = nullptr;
			return E_FAIL;
		}
		*fontFile = m_fontFiles[m_currentFileIndex - 1];
		(*fontFile)->AddRef(); // Caller expects a reference
		return S_OK;
	}

	// 【核心修改】AddFont 方法现在接收内存数据
	STDMETHOD(AddFontFromMemory)(void const* fontData, UINT32 fontDataSize)
	{
		// ******** 添加调试日志 ********
		wchar_t buffer[256];
		swprintf_s(buffer, L"AddFontFromMemory: 准备创建引用，fontDataSize = %u\n", fontDataSize);
		wcout << buffer << endl;
		if (fontData == nullptr)
		{
			cout << "AddFontFromMemory: 错误！fontData 为空！" << endl;
		}
		// *****************************

		IDWriteFontFile* fontFile = nullptr;
		HRESULT hr = S_OK;

		// 使用 CreateCustomFontFileReference 从内存创建字体文件引用
		// 第一个参数 (fontData) 是“Key”，它会被传递给我们自定义的 IDWriteFontFileLoader::CreateStreamFromKey
		hr = m_factory->CreateCustomFontFileReference(
			fontData,       // Key: 字体数据指针
			fontDataSize,   // Key Size: 字体数据大小
			m_loader,       // 我们的自定义加载器
			&fontFile
		);

		if (SUCCEEDED(hr))
		{
			m_fontFiles.push_back(fontFile);
		}

		return hr;
	}

private:
	LONG m_cRefCount;
	size_t m_currentFileIndex;

	IDWriteFactory* m_factory;
	IDWriteFontFileLoader* m_loader; // 需要一个加载器实例来创建字体文件
	vector<IDWriteFontFile*> m_fontFiles;
};
class IdtFontCollectionLoader : public IDWriteFontCollectionLoader
{
public:
	IdtFontCollectionLoader() : m_cRefCount(1)
	{
		// 创建我们自定义的字体文件加载器
		m_fileLoader = new IdtFontFileLoader();
	}
	~IdtFontCollectionLoader()
	{
		cout << "--- ~IdtFontCollectionLoader() DESTRUCTOR CALLED! ---" << endl;

		if (m_fileLoader) m_fileLoader->Release();
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
		if (riid == __uuidof(IDWriteFontCollectionLoader) || riid == __uuidof(IUnknown))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	// --- IDWriteFontCollectionLoader methods ---
	STDMETHOD(CreateEnumeratorFromKey)(
		IDWriteFactory* factory,
		void const* collectionKey,          // 我们将用它来传递字体数据
		UINT32 collectionKeySize,
		IDWriteFontFileEnumerator** fontFileEnumerator
		) override
	{
		// 创建我们的自定义枚举器
		IdtFontFileEnumerator* enumerator = new IdtFontFileEnumerator(factory, m_fileLoader);
		if (enumerator == nullptr)
		{
			return E_OUTOFMEMORY;
		}

		// 将 collectionKey (字体数据) 添加到枚举器中
		// 这里假设一个 Collection 只包含一个字体，如果需要支持多个，逻辑需要调整
		HRESULT hr = enumerator->AddFontFromMemory(collectionKey, collectionKeySize);

		if (SUCCEEDED(hr))
		{
			// 【核心修复】正确地将所有权转移给调用者

			// 1. 将指针赋给 out 参数
			*fontFileEnumerator = enumerator;

			// 2. 为调用者增加一个引用
			(*fontFileEnumerator)->AddRef();
		}

		// 3. 释放我们自己通过 new 创建的那个引用，
		// 因为所有权已经安全地转移给了 DirectWrite。
		enumerator->Release();

		return hr;
	}

	// 获取内部的文件加载器，以便注册它
	IDWriteFontFileLoader* GetFileLoader()
	{
		return m_fileLoader;
	}

private:
	LONG m_cRefCount;
	IdtFontFileLoader* m_fileLoader;
};
