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

enum class AutomaticUpdateStateEnum : int
{
	UpdateNotStarted = 0, // 新版本检测未启动
	UpdateObtainInformation = 1, // 获取新版本信息中
	UpdateInformationFail = 2, // 下载版本信息失败
	UpdateInformationDamage = 3, // 下载版本信息损坏
	UpdateInformationUnStandardized = 4, // 下载版本信息不符合规范
	UpdateDownloading = 5, // 新版本正极速下载中
	UpdateDownloadFail = 6, // 下载最新版本软件失败
	UpdateDownloadDamage = 7, // 下载最新版本软件损坏
	UpdateRestart = 8, // 重启软件更新到最新版本
	UpdateLatest = 9, // 软件已经是最新版本
	UpdateNewer = 10, // 软件相对最新版本更新
	UpdateNew = 11 // 发现软件新版本
};
extern AutomaticUpdateStateEnum AutomaticUpdateState;

class DownloadNewProgramStateClass
{
public:
	long long downloadedSize;
	long long fileSize;
};
extern DownloadNewProgramStateClass downloadNewProgramState;

void splitUrl(string input_url, string& prefix, string& domain, string& path);
AutomaticUpdateStateEnum DownloadNewProgram(DownloadNewProgramStateClass* state, EditionInfoClass editionInfo, string url);

//程序自动更新
extern bool mandatoryUpdate;
extern bool inconsistentArchitecture;
wstring get_domain_name(wstring url);
wstring convertToHttp(const wstring& url);

void AutomaticUpdate();