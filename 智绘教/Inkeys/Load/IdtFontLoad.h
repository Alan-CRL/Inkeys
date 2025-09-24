#pragma once

#include "../../IdtMain.h"

#include "../../IdtD2DPreparation.h"

template <typename InterfaceType>
inline void SafeRelease(InterfaceType** currentObject)
{
	if (*currentObject != NULL)
	{
		(*currentObject)->Release();
		*currentObject = NULL;
	}
}
template <typename InterfaceType>
inline InterfaceType* SafeAcquire(InterfaceType* newObject)
{
	if (newObject != NULL)
		newObject->AddRef();

	return newObject;
}
template <typename InterfaceType>
inline void SafeSet(InterfaceType** currentObject, InterfaceType* newObject)
{
	SafeAcquire(newObject);
	SafeRelease(&currentObject);
	currentObject = newObject;
}

inline HRESULT ExceptionToHResult() throw()
{
	try
	{
		throw;  // Rethrow previous exception.
	}
	catch (std::bad_alloc&)
	{
		return E_OUTOFMEMORY;
	}
	catch (...)
	{
		return E_FAIL;
	}
}

class IdtFontFileStream : public IDWriteFontFileStream
{
public:
	explicit IdtFontFileStream(UINT resourceID);

	// IUnknown methods
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject)
	{
		if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileStream))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		else
		{
			*ppvObject = NULL;
			return E_NOINTERFACE;
		}
	}
	virtual ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&refCount_);
	}
	virtual ULONG STDMETHODCALLTYPE Release()
	{
		ULONG newCount = InterlockedDecrement(&refCount_);
		if (newCount == 0)
			delete this;

		return newCount;
	}

	// IDWriteFontFileStream methods
	virtual HRESULT STDMETHODCALLTYPE ReadFileFragment(void const** fragmentStart, UINT64 fileOffset, UINT64 fragmentSize, OUT void** fragmentContext)
	{
		if (fileOffset <= resourceSize_ &&
			fragmentSize <= resourceSize_ - fileOffset)
		{
			*fragmentStart = static_cast<BYTE const*>(resourcePtr_) + static_cast<size_t>(fileOffset);
			*fragmentContext = NULL;
			return S_OK;
		}
		else
		{
			*fragmentStart = NULL;
			*fragmentContext = NULL;
			return E_FAIL;
		}
	}
	virtual void STDMETHODCALLTYPE ReleaseFileFragment(void* fragmentContext) {}
	virtual HRESULT STDMETHODCALLTYPE GetFileSize(OUT UINT64* fileSize)
	{
		*fileSize = resourceSize_;
		return S_OK;
	}
	virtual HRESULT STDMETHODCALLTYPE GetLastWriteTime(OUT UINT64* lastWriteTime)
	{
		*lastWriteTime = 0;
		return E_NOTIMPL;
	}

	bool IsInitialized()
	{
		return resourcePtr_ != NULL;
	}

private:
	ULONG refCount_;
	void const* resourcePtr_;
	DWORD resourceSize_;

	static HMODULE const moduleHandle_;
	static HMODULE GetCurrentModule();
};
class IdtFontFileLoader : public IDWriteFontFileLoader
{
public:
	IdtFontFileLoader() : refCount_(0) {}

	// IUnknown methods
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject)
	{
		if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileLoader))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		else
		{
			*ppvObject = NULL;
			return E_NOINTERFACE;
		}
	}
	virtual ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&refCount_);
	}
	virtual ULONG STDMETHODCALLTYPE Release()
	{
		ULONG newCount = InterlockedDecrement(&refCount_);
		if (newCount == 0)
			delete this;

		return newCount;
	}

	// IDWriteFontFileLoader methods
	virtual HRESULT STDMETHODCALLTYPE CreateStreamFromKey(void const* fontFileReferenceKey, UINT32 fontFileReferenceKeySize, OUT IDWriteFontFileStream** fontFileStream)
	{
		*fontFileStream = NULL;

		// Make sure the key is the right size.
		if (fontFileReferenceKeySize != sizeof(UINT))
			return E_INVALIDARG;

		UINT resourceID = *static_cast<UINT const*>(fontFileReferenceKey);

		// Create the stream object.
		IdtFontFileStream* stream = new(std::nothrow) IdtFontFileStream(resourceID);
		if (stream == NULL)
			return E_OUTOFMEMORY;

		if (!stream->IsInitialized())
		{
			delete stream;
			return E_FAIL;
		}

		*fontFileStream = SafeAcquire(stream);

		return S_OK;
	}

	// Gets the singleton loader instance.
	static IDWriteFontFileLoader* GetLoader()
	{
		return instance_;
	}

	static bool IsLoaderInitialized()
	{
		return instance_ != NULL;
	}

private:
	ULONG refCount_;

	static IDWriteFontFileLoader* instance_;
};

class IdtFontFileEnumerator : public IDWriteFontFileEnumerator
{
public:
	IdtFontFileEnumerator(IDWriteFactory* factory);

	HRESULT Initialize(UINT const* resourceIDs, UINT32 resourceCount)
	{
		try
		{
			resourceIDs_.assign(resourceIDs, resourceIDs + resourceCount);
		}
		catch (...)
		{
			return ExceptionToHResult();
		}
		return S_OK;
	}

	~IdtFontFileEnumerator()
	{
		SafeRelease(&currentFile_);
		SafeRelease(&factory_);
	}

	// IUnknown methods
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject)
	{
		if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontFileEnumerator))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		else
		{
			*ppvObject = NULL;
			return E_NOINTERFACE;
		}
	}
	virtual ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&refCount_);
	}
	virtual ULONG STDMETHODCALLTYPE Release()
	{
		ULONG newCount = InterlockedDecrement(&refCount_);
		if (newCount == 0)
			delete this;

		return newCount;
	}

	// IDWriteFontFileEnumerator methods
	virtual HRESULT STDMETHODCALLTYPE MoveNext(OUT BOOL* hasCurrentFile)
	{
		HRESULT hr = S_OK;

		*hasCurrentFile = FALSE;
		SafeRelease(&currentFile_);

		if (nextIndex_ < resourceIDs_.size())
		{
			hr = factory_->CreateCustomFontFileReference(
				&resourceIDs_[nextIndex_],
				sizeof(UINT),
				IdtFontFileLoader::GetLoader(),
				&currentFile_
			);

			if (SUCCEEDED(hr))
			{
				*hasCurrentFile = TRUE;

				++nextIndex_;
			}
		}

		return hr;
	}
	virtual HRESULT STDMETHODCALLTYPE GetCurrentFontFile(OUT IDWriteFontFile** fontFile)
	{
		*fontFile = SafeAcquire(currentFile_);

		return (currentFile_ != NULL) ? S_OK : E_FAIL;
	}

private:
	ULONG refCount_;

	IDWriteFactory* factory_;
	IDWriteFontFile* currentFile_;
	std::vector<UINT> resourceIDs_;
	size_t nextIndex_;
};
class IdtFontCollectionLoader : public IDWriteFontCollectionLoader
{
public:
	IdtFontCollectionLoader() : refCount_(0) {}

	// IUnknown methods
	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject)
	{
		if (iid == IID_IUnknown || iid == __uuidof(IDWriteFontCollectionLoader))
		{
			*ppvObject = this;
			AddRef();
			return S_OK;
		}
		else
		{
			*ppvObject = NULL;
			return E_NOINTERFACE;
		}
	}
	virtual ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&refCount_);
	}
	virtual ULONG STDMETHODCALLTYPE Release()
	{
		ULONG newCount = InterlockedDecrement(&refCount_);
		if (newCount == 0)
			delete this;

		return newCount;
	}

	// IDWriteFontCollectionLoader methods
	virtual HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory, void const* collectionKey, UINT32 collectionKeySize, OUT IDWriteFontFileEnumerator** fontFileEnumerator)
	{
		*fontFileEnumerator = NULL;

		HRESULT hr = S_OK;

		if (collectionKeySize % sizeof(UINT) != 0)
			return E_INVALIDARG;

		IdtFontFileEnumerator* enumerator = new(std::nothrow) IdtFontFileEnumerator(
			factory
		);
		if (enumerator == NULL)
			return E_OUTOFMEMORY;

		UINT const* resourceIDs = static_cast<UINT const*>(collectionKey);
		UINT32 const resourceCount = collectionKeySize / sizeof(UINT);

		hr = enumerator->Initialize(
			resourceIDs,
			resourceCount
		);

		if (FAILED(hr))
		{
			delete enumerator;
			return hr;
		}

		*fontFileEnumerator = SafeAcquire(enumerator);

		return hr;
	}

	// Gets the singleton loader instance.
	static IDWriteFontCollectionLoader* GetLoader()
	{
		return instance_;
	}

	static bool IsLoaderInitialized()
	{
		return instance_ != NULL;
	}

private:
	ULONG refCount_;

	static IDWriteFontCollectionLoader* instance_;
};