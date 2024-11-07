#pragma once
#include "IdtMain.h"

class EditionInfoClass
{
public:
	EditionInfoClass()
	{
		errorCode = 0;
		channel = "";

		editionDate = L"";
		editionCode = L"";
		explain = L"";
		representation = L"";
		path_size = 0;

		hash_md5 = "";
		hash_sha256 = "";
	}

public:
	int errorCode;
	string channel;

	wstring editionDate;
	wstring editionCode;
	wstring explain;
	wstring representation;
	string path[10];
	int path_size;

	string hash_md5;
	string hash_sha256;
};
EditionInfoClass GetEditionInfo(string channel);

class DownloadNewProgramStateClass
{
public:
	long long downloadedSize;
	long long fileSize;
};
void splitUrl(string input_url, string& prefix, string& domain, string& path);
int DownloadNewProgram(DownloadNewProgramStateClass* state, EditionInfoClass editionInfo, string url);

//程序自动更新
extern int AutomaticUpdateStep;
wstring get_domain_name(wstring url);
wstring convertToHttp(const wstring& url);
void AutomaticUpdate();