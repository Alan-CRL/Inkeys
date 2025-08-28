#include "IdtFontLoad.h"

IDWriteFontCollectionLoader* IdtFontCollectionLoader::instance_(new(std::nothrow) IdtFontCollectionLoader());

IdtFontFileEnumerator::IdtFontFileEnumerator(IDWriteFactory* factory) :
	refCount_(0),
	factory_(SafeAcquire(factory)),
	currentFile_(),
	nextIndex_(0)
{
}

IDWriteFontFileLoader* IdtFontFileLoader::instance_(new(std::nothrow) IdtFontFileLoader());

HMODULE const IdtFontFileStream::moduleHandle_(GetCurrentModule());
HMODULE IdtFontFileStream::GetCurrentModule()
{
	HMODULE handle = NULL;

	GetModuleHandleEx(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		reinterpret_cast<LPCTSTR>(&GetCurrentModule),
		&handle
	);

	return handle;
}
IdtFontFileStream::IdtFontFileStream(UINT resourceID) :
	refCount_(0),
	resourcePtr_(NULL),
	resourceSize_(0)
{
	HRSRC resource = FindResourceW(moduleHandle_, MAKEINTRESOURCE(resourceID), L"TTF");
	if (resource != NULL)
	{
		HGLOBAL memHandle = LoadResource(moduleHandle_, resource);
		if (memHandle != NULL)
		{
			resourcePtr_ = LockResource(memHandle);
			if (resourcePtr_ != NULL)
			{
				resourceSize_ = SizeofResource(moduleHandle_, resource);
			}
		}
	}
}