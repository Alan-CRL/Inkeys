#include "IdtGuid.h"

#include "IdtText.h"

#include <iomanip>
#include <map>
#pragma comment(lib, "msvcprt.lib")

constexpr auto BUFFER_SIZE = (512);
typedef unsigned int size_t_32;

struct
{
	wstring BoardUUID;
	wstring BoardSerialNumber;
	wstring MainHardDiskSerialNumber;
}BoardInfo;

unsigned int BKDRHash(char* str)
{
	unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
	unsigned int hash = 0;

	while (*str)
	{
		hash = hash * seed + (*str++);
	}

	return (hash & 0x7FFFFFFF);
}
string hash32_to_guid(size_t_32 hash_value)
{
	char chHex_str[BUFFER_SIZE] = { 0 };

	stringstream ss;
	ss << setfill('0') << setw(8) << hex << hash_value;
	string hex_str = ss.str();
	strcpy_s(chHex_str, hex_str.c_str());

	size_t_32 num1 = BKDRHash(chHex_str);
	stringstream ss1;
	ss1 << setfill('0') << setw(8) << hex << num1;
	string hex_str1 = ss1.str();
	memset(chHex_str, 0, sizeof(chHex_str));
	size_t_32 num2 = BKDRHash(chHex_str);
	stringstream ss2;
	ss2 << setfill('0') << setw(8) << hex << num2;
	string hex_str2 = ss2.str();
	memset(chHex_str, 0, sizeof(chHex_str));
	strcpy_s(chHex_str, hex_str2.c_str());

	size_t_32 num3 = BKDRHash(chHex_str);
	stringstream ss3;
	ss3 << setfill('0') << setw(8) << hex << num3;
	string hex_str3 = ss3.str();
	memset(chHex_str, 0, sizeof(chHex_str));
	strcpy_s(chHex_str, hex_str3.c_str());

	// 构造GUID的格式
	string guid_str = "{" + hex_str.substr(0, 8) + "-" + hex_str1.substr(0, 4) + "-" +
		hex_str1.substr(4, 4) + "-" +
		hex_str2.substr(0, 4) + "-" +
		hex_str2.substr(4, 4) + hex_str3.substr(0, 8) + "}";

	return guid_str;
}

wstring GetCPUID()
{
	wstring strCPUID;
	int CPUInfo[4] = { -1 };
	unsigned long s1, s2;
	wchar_t buffer[17];

	__cpuid(CPUInfo, 1);
	s1 = CPUInfo[3];
	s2 = CPUInfo[0];

	swprintf(buffer, 17, L"%08X%08X", s1, s2);
	strCPUID = buffer;
	return strCPUID;
}
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

size_t_32 getBiosCpuDiskHashID()
{
	size_t_32 num;
	char chHex_str[BUFFER_SIZE] = { 0 };

	GetBoardInfo();

	string BoardUUID = wstring_to_string(BoardInfo.BoardUUID);
	if (!BoardUUID.empty() && BoardUUID.compare("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF") != 0)
	{
		strcpy_s(chHex_str, (BoardUUID + "\r\n").c_str());
		num = BKDRHash(chHex_str);

		return num;
	}

	string Guid;

	string CPUID = wstring_to_string(GetCPUID());
	string BoardSerialNumber = wstring_to_string(BoardInfo.BoardSerialNumber);
	string MainHardDiskSerialNumber = wstring_to_string(BoardInfo.MainHardDiskSerialNumber);

	if (!CPUID.empty()) Guid.append(CPUID + "\r\n");
	if (!BoardSerialNumber.empty()) Guid.append(BoardSerialNumber + "\r\n");
	if (!MainHardDiskSerialNumber.empty()) Guid.append(MainHardDiskSerialNumber + "\r\n");

	memset(chHex_str, 0, sizeof(chHex_str));
	strcpy_s(chHex_str, Guid.c_str());
	num = BKDRHash(chHex_str);

	return num;
}

string getDeviceGUID()
{
	size_t_32 num = getBiosCpuDiskHashID();
	return hash32_to_guid(num);
}