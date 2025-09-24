#include "IdtGuid.h"

#include "IdtText.h"

struct
{
	wstring BoardUUID;
	wstring BoardSerialNumber;
	wstring MainHardDiskSerialNumber;
}BoardInfo;
void GetBoardInfo()
{
	// 初始化COM
	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// 初始化WMI
	IWbemLocator* pLoc = NULL;
	IWbemServices* pSvc = NULL;
	HRESULT hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
	if (SUCCEEDED(hres))
	{
		hres = pLoc->ConnectServer((_bstr_t)L"ROOT\\CIMV2", NULL, NULL, 0, NULL, 0, 0, &pSvc);
		if (SUCCEEDED(hres))
		{
			hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
			if (SUCCEEDED(hres))
			{
				IEnumWbemClassObject* pEnumerator = NULL;

				hres = pSvc->ExecQuery((_bstr_t)L"WQL", (_bstr_t)L"SELECT UUID FROM Win32_ComputerSystemProduct", WBEM_FLAG_FORWARD_ONLY, NULL, &pEnumerator);
				if (SUCCEEDED(hres))
				{
					IWbemClassObject* pclsObj = NULL;
					ULONG uReturn = 0;
					hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
					if (SUCCEEDED(hres) && uReturn == 1)
					{
						VARIANT vtProp;
						hres = pclsObj->Get(L"UUID", 0, &vtProp, 0, 0);
						if (SUCCEEDED(hres))
						{
							if (vtProp.vt == VT_BSTR) BoardInfo.BoardUUID = vtProp.bstrVal;
							VariantClear(&vtProp);
						}
						pclsObj->Release();
					}
					pEnumerator->Release();
				}

				hres = pSvc->ExecQuery((_bstr_t)L"WQL", (_bstr_t)L"SELECT SerialNumber FROM Win32_Bios", WBEM_FLAG_FORWARD_ONLY, NULL, &pEnumerator);
				if (SUCCEEDED(hres))
				{
					IWbemClassObject* pclsObj = NULL;
					ULONG uReturn = 0;
					hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
					if (SUCCEEDED(hres) && uReturn == 1)
					{
						VARIANT vtProp;
						hres = pclsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0);
						if (SUCCEEDED(hres))
						{
							if (vtProp.vt == VT_BSTR) BoardInfo.BoardSerialNumber = vtProp.bstrVal;
							VariantClear(&vtProp);
						}
						pclsObj->Release();
					}
					pEnumerator->Release();
				}

				hres = pSvc->ExecQuery((_bstr_t)L"WQL", (_bstr_t)L"SELECT SerialNumber FROM Win32_DiskDrive WHERE Index = 0", WBEM_FLAG_FORWARD_ONLY, NULL, &pEnumerator);
				if (SUCCEEDED(hres))
				{
					IWbemClassObject* pclsObj = NULL;
					ULONG uReturn = 0;
					hres = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
					if (SUCCEEDED(hres) && uReturn == 1)
					{
						VARIANT vtProp;
						hres = pclsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0);
						if (SUCCEEDED(hres))
						{
							if (vtProp.vt == VT_BSTR) BoardInfo.MainHardDiskSerialNumber = vtProp.bstrVal;
							VariantClear(&vtProp);
						}
						pclsObj->Release();
					}
					pEnumerator->Release();
				}
			}
			pSvc->Release();
		}
		pLoc->Release();
	}

	// 释放COM
	CoUninitialize();
}

string getBiosCpuDiskHashID()
{
	GetBoardInfo();

	string Guid;
	string BoardUUID = utf16ToUtf8(BoardInfo.BoardUUID);
	string BoardSerialNumber = utf16ToUtf8(BoardInfo.BoardSerialNumber);
	string MainHardDiskSerialNumber = utf16ToUtf8(BoardInfo.MainHardDiskSerialNumber);

	if (!BoardUUID.empty() && BoardUUID.compare("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF") != 0) Guid.append(BoardUUID + "\n");
	if (!BoardSerialNumber.empty()) Guid.append(BoardSerialNumber + "\n");
	if (!MainHardDiskSerialNumber.empty()) Guid.append(MainHardDiskSerialNumber + "\n");

	return Guid;
}
string generateGUID(const string& input)
{
	md5wrapper hasher;
	string hash = hasher.getHashFromString(input);

	string guid = "{" + hash.substr(0, 8) + "-" +
		hash.substr(8, 4) + "-" +
		hash.substr(12, 4) + "-" +
		hash.substr(16, 4) + "-" +
		hash.substr(20, 12) + "}";

	return guid;
}

string getDeviceGUID()
{
	string deviceGuid = getBiosCpuDiskHashID();
	if (deviceGuid.empty()) return "Error";
	return generateGUID(deviceGuid);
}