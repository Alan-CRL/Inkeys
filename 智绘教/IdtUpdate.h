#pragma once
#include "IdtMain.h"

class EditionInfoClass
{
public:
	EditionInfoClass() {}
	EditionInfoClass(const EditionInfoClass& other) { cF(other); }
	EditionInfoClass& operator=(const EditionInfoClass& other)
	{
		if (this != &other) cF(other);
		return *this;
	}

private:
	void cF(const EditionInfoClass& other)
	{
		errorCode = other.errorCode;
		channel = other.channel;

		editionDate = other.editionDate;
		editionCode = other.editionCode;
		explain = other.explain;
		representation = other.representation;
		for (int i = 0; i < 10; i++) path[i] = other.path[i];
		path_size = other.path_size;
		fileSize.store(other.fileSize.load());

		hash_md5 = other.hash_md5;
		hash_sha256 = other.hash_sha256;

		isInkeys3 = other.isInkeys3;
	}

public:
	int errorCode = 0;
	string channel;

	wstring editionDate;
	wstring editionCode;
	wstring explain;
	wstring representation;
	string path[10];
	int path_size = 0;
	atomic_ullong fileSize = 0;

	string hash_md5;
	string hash_sha256;

	// extra
	bool isInkeys3 = false;
};
EditionInfoClass GetEditionInfo(string channel, string arch);

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
	UpdateNew = 11, // 发现软件新版本

	UpdateLimit = 12, // 自动更新被阻止
};
extern AutomaticUpdateStateEnum AutomaticUpdateState;

class DownloadNewProgramStateClass
{
public:
	DownloadNewProgramStateClass()
	{
		downloadedSize.store(0);
		fileSize.store(0);
	}

	DownloadNewProgramStateClass(const DownloadNewProgramStateClass& other) { cF(other); }
	DownloadNewProgramStateClass& operator=(const DownloadNewProgramStateClass& other)
	{
		if (this != &other) cF(other);
		return *this;
	}
private:
	void cF(const DownloadNewProgramStateClass& other)
	{
		downloadedSize.store(other.downloadedSize.load());
		fileSize.store(other.fileSize.load());
	}

public:
	atomic_ullong downloadedSize;
	atomic_ullong fileSize;
};
extern DownloadNewProgramStateClass downloadNewProgramState;

void splitUrl(string input_url, string& prefix, string& domain, string& path);
AutomaticUpdateStateEnum DownloadNewProgram(DownloadNewProgramStateClass* state, EditionInfoClass editionInfo, string url, string arch);

//程序自动更新
extern bool mandatoryUpdate;
extern bool inconsistentArchitecture;
wstring get_domain_name(wstring url);
wstring convertToHttp(const wstring& url);

extern bool isWindows8OrGreater;
void AutomaticUpdate();