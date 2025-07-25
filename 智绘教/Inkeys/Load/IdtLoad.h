#pragma once

#include <windows.h>
#include <string>

class IdtLoad
{
public:
	static bool ExtractResourceFile(const std::wstring& dstFile, const std::wstring& resType, const std::wstring& resName)
	{
		// 查找和加载资源
		HRSRC hRes = ::FindResourceW(NULL, resName.c_str(), resType.c_str());
		if (!hRes) return false;

		DWORD dwSize = ::SizeofResource(NULL, hRes);
		if (dwSize == 0) return false;

		HGLOBAL hMem = ::LoadResource(NULL, hRes);
		if (!hMem) return false;

		void* pData = ::LockResource(hMem);
		if (!pData) return false;

		// 创建/打开目标文件
		HANDLE hFile = ::CreateFileW(dstFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);

		if (hFile == INVALID_HANDLE_VALUE) return false;

		DWORD dwWritten = 0;
		BOOL bRet = ::WriteFile(hFile, pData, dwSize, &dwWritten, NULL);

		::CloseHandle(hFile);

		return bRet && (dwWritten == dwSize);
	}
	static bool ExtractResourceString(std::string& outData, const std::wstring& resType, const std::wstring& resName)
	{
		// 查找和加载资源
		HRSRC hRes = ::FindResourceW(NULL, resName.c_str(), resType.c_str());
		if (!hRes) return false;

		DWORD dwSize = ::SizeofResource(NULL, hRes);
		if (dwSize == 0) return false;

		HGLOBAL hMem = ::LoadResource(NULL, hRes);
		if (!hMem) return false;

		void* pData = ::LockResource(hMem);
		if (!pData) return false;

		// 拷贝内容到string
		outData.assign(reinterpret_cast<const char*>(pData), dwSize);

		return true;
	}
	static bool ExtractResourcePtr(void*& pLock, DWORD& dwSize, const std::wstring& resType, const std::wstring& resName)
	{
		pLock = nullptr;
		dwSize = 0;

		// 查找资源
		HRSRC hRes = ::FindResourceW(NULL, resName.c_str(), resType.c_str());
		if (!hRes) return false;

		dwSize = ::SizeofResource(NULL, hRes);
		if (dwSize == 0) return false;

		HGLOBAL hMem = ::LoadResource(NULL, hRes);
		if (!hMem) return false;

		pLock = ::LockResource(hMem);
		if (!pLock) return false;

		return true;
	}

private:
	IdtLoad() = delete;
};
