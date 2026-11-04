#pragma once

#include <Windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <cstddef>
#include <limits>
#include <algorithm>

namespace Inkeys::Launch
{
	class Handle
	{
		HANDLE value = nullptr;
	public:
		Handle() = default;
		explicit Handle(HANDLE h) : value(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
		~Handle() { if (value) CloseHandle(value); }
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		HANDLE Get() const { return value; }
		HANDLE Release() { HANDLE h = value; value = nullptr; return h; }
		void Reset(HANDLE h = nullptr) { if (value) CloseHandle(value); value = h; }
	};

	inline std::wstring Image(HANDLE process = GetCurrentProcess())
	{
		std::wstring text(32768, L'\0'); DWORD length = static_cast<DWORD>(text.size());
		if (!QueryFullProcessImageNameW(process, 0, text.data(), &length)) return {};
		text.resize(length); return text;
	}
	inline std::wstring Directory(const std::wstring& image) { return std::filesystem::path(image).parent_path().wstring(); }
	inline bool Equal(const std::wstring& a, const std::wstring& b)
	{ auto left=a, right=b;
		while (left.size()>3 && (left.back()==L'\\' || left.back()==L'/')) left.pop_back();
		while (right.size()>3 && (right.back()==L'\\' || right.back()==L'/')) right.pop_back();
		return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL; }
	inline std::wstring PresenceName(const std::wstring& root)
	{
		Handle directory(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
		BY_HANDLE_FILE_INFORMATION info{};
		if (!directory.Get() || !GetFileInformationByHandle(directory.Get(), &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
		return L"Inkeys.Instance." + std::to_wstring(info.dwVolumeSerialNumber) + L"." +
			std::to_wstring(info.nFileIndexHigh) + L"." + std::to_wstring(info.nFileIndexLow);
	}
	inline bool IsPresence(HANDLE object, const std::wstring& name)
	{
		// 查询只用于核对接力句柄的对象名；不静态依赖ntdll，查询不可用时拒绝交接。
		using Query = LONG (NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
		auto module = GetModuleHandleW(L"ntdll.dll");
		auto query = module ? reinterpret_cast<Query>(GetProcAddress(module, "NtQueryObject")) : nullptr;
		if (!query || name.empty()) return false;
		struct Unicode { USHORT length, maximum; PWSTR buffer; };
		alignas(void*) unsigned char bytes[4096]{}; ULONG returned = 0;
		if (query(object, 2, bytes, sizeof(bytes), &returned) < 0 || returned > sizeof(bytes) || returned < sizeof(Unicode)) return false;
		const auto* type = reinterpret_cast<const Unicode*>(bytes);
		const auto address = reinterpret_cast<std::uintptr_t>(bytes), typePointer = reinterpret_cast<std::uintptr_t>(type->buffer);
		if (type->length % sizeof(wchar_t) || typePointer < address || typePointer > address + sizeof(bytes)
			|| type->length > address + sizeof(bytes) - typePointer || std::wstring(type->buffer, type->length / sizeof(wchar_t)) != L"Mutant") return false;
		if (query(object, 1, bytes, sizeof(bytes), &returned) < 0 || returned > sizeof(bytes) || returned < sizeof(Unicode)) return false;
		const auto* value = reinterpret_cast<const Unicode*>(bytes);
		const auto first = reinterpret_cast<std::uintptr_t>(bytes), pointer = reinterpret_cast<std::uintptr_t>(value->buffer);
		if (value->length % sizeof(wchar_t) || pointer < first || pointer > first + sizeof(bytes)
			|| value->length > first + sizeof(bytes) - pointer) return false;
		std::wstring actual(value->buffer, value->length / sizeof(wchar_t));
		// Local在受限runner中可映射到AppContainer命名空间；以系统实际解析的同名对象为准。
		Handle reference(OpenMutexW(SYNCHRONIZE,FALSE,(L"Local\\"+name).c_str()));
		if (!reference.Get() || query(reference.Get(),1,bytes,sizeof(bytes),&returned)<0
			|| returned>sizeof(bytes) || returned<sizeof(Unicode))return false;
		value=reinterpret_cast<const Unicode*>(bytes);
		const auto expectedPointer=reinterpret_cast<std::uintptr_t>(value->buffer);
		if(value->length%sizeof(wchar_t) || expectedPointer<first || expectedPointer>first+sizeof(bytes)
			|| value->length>first+sizeof(bytes)-expectedPointer)return false;
		return actual==std::wstring(value->buffer,value->length/sizeof(wchar_t));
	}

	inline constexpr wchar_t HandoffPrefix[] = L"--inkeys-startup-handoff=";
	struct alignas(8) Packet
	{
		DWORD version = 1, parentPid = 0, childPid = 0, byteSize = 65576;
		std::uint64_t parent = 0, presence = 0, ready = 0;
		wchar_t root[32768]{};
	};
	static_assert(sizeof(Packet) == 65576 && offsetof(Packet, parent) == 16 && offsetof(Packet, root) == 40);
	inline Handle presence;
	inline Handle physicalPresence; // installer还需排斥同物理目录的独立并发更新。
	inline std::wstring logicalRoot;
	inline bool adopted = false;

	// 只允许根目录与已校验installer目录之间的启动接力；不会仅因目录名而自动上溯。
	inline bool WithinRoot(const std::wstring& image, const std::wstring& root)
	{
		const auto directory = Directory(image);
		auto normalized = root;
		while (normalized.size()>3 && (normalized.back()==L'\\' || normalized.back()==L'/')) normalized.pop_back();
		const auto ownIdentity=PresenceName(directory), rootIdentity=PresenceName(normalized);
		if (ownIdentity.empty() || rootIdentity.empty()) return false;
		if (ownIdentity==rootIdentity) return true;
		const auto installer=normalized+L"\\installer";
		const DWORD attributes=GetFileAttributesW(installer.c_str());
		return attributes!=INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)
			&& !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) && ownIdentity==PresenceName(installer);
	}
	inline int Adopt()
	{
		int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!args) return -1;
		std::wstring name;
		for (int i = 1; i < count; ++i)
			if (wcsncmp(args[i], HandoffPrefix, ARRAYSIZE(HandoffPrefix) - 1) == 0)
			{
				if (!name.empty()) { LocalFree(args); return -1; }
				name = args[i] + ARRAYSIZE(HandoffPrefix) - 1;
			}
		LocalFree(args);
		if (name.empty()) return 0;
		if (name.rfind(L"Local\\Inkeys.Handoff.", 0) != 0) return -1;
		Handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
		if (!mapping.Get()) return -1;
		const auto* view = static_cast<const Packet*>(MapViewOfFile(mapping.Get(), FILE_MAP_READ, 0, 0, sizeof(Packet)));
		if (!view) return -1;
		Packet packet; std::memcpy(&packet, view, sizeof(packet)); UnmapViewOfFile(view);
		if (packet.version != 1 || packet.byteSize != sizeof(Packet) || packet.childPid != GetCurrentProcessId() || !packet.root[0]
			|| packet.root[ARRAYSIZE(packet.root) - 1] || !packet.parent || !packet.presence || !packet.ready) return -1;
		if (packet.parent > (std::numeric_limits<std::uintptr_t>::max)() || packet.presence > (std::numeric_limits<std::uintptr_t>::max)()
			|| packet.ready > (std::numeric_limits<std::uintptr_t>::max)()) return -1;
		Handle parent(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(packet.parent))),
			lease(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(packet.presence))), ready(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(packet.ready)));
		const std::wstring root(packet.root);
		if (GetProcessId(parent.Get()) != packet.parentPid || packet.parentPid == GetCurrentProcessId()
			|| WaitForSingleObject(parent.Get(), 0) != WAIT_TIMEOUT
			|| !WithinRoot(Image(parent.Get()), root) || !WithinRoot(Image(), root)
			|| !IsPresence(lease.Get(), PresenceName(root))) return -1;
		// installer的物理目录准入必须在Ready之前完成；否则父已退出后再发现冲突会丢失整个交接。
		const auto physicalName=PresenceName(Directory(Image()));
		if(physicalName.empty())return -1;
		if(physicalName!=PresenceName(root))
		{
			Handle physical(CreateMutexW(nullptr,FALSE,(L"Local\\"+physicalName).c_str()));const DWORD error=GetLastError();
			if(!physical.Get() || error==ERROR_ALREADY_EXISTS)return -1;
			physicalPresence.Reset(physical.Release());
		}
		// 先确认已验真的连续占位，再等精确父进程退出；不能把Ready放在等待之后。
		if (!SetEvent(ready.Get())) return -1;
		if (WaitForSingleObject(parent.Get(), INFINITE) != WAIT_OBJECT_0) return -1;
		logicalRoot = root; presence.Reset(lease.Release()); adopted = true; return 1;
	}
	enum class Admission { Accepted, Duplicate, Error };
	inline Admission Acquire(const std::wstring& root, bool allow)
	{
		if (adopted)
		{
			if (PresenceName(root)==PresenceName(logicalRoot)) return Admission::Accepted;
			const auto physicalName=PresenceName(root); if(physicalName.empty())return Admission::Error;
			if(physicalPresence.Get())return IsPresence(physicalPresence.Get(),physicalName)?Admission::Accepted:Admission::Error;
			Handle physical(CreateMutexW(nullptr,FALSE,(L"Local\\"+physicalName).c_str()));const DWORD error=GetLastError();
			if(!physical.Get())return Admission::Error;
			if(error==ERROR_ALREADY_EXISTS)return Admission::Duplicate;
			physicalPresence.Reset(physical.Release());return Admission::Accepted;
		}
		const auto name = PresenceName(root); if (name.empty()) return Admission::Error;
		HANDLE object = CreateMutexW(nullptr, FALSE, (L"Local\\" + name).c_str());
		DWORD error = GetLastError();
		if (!object && error == ERROR_ACCESS_DENIED)
		{
			object = OpenMutexW(SYNCHRONIZE, FALSE, (L"Local\\" + name).c_str());
			if (object) error = ERROR_ALREADY_EXISTS;
		}
		if (!object) return Admission::Error;
		Handle candidate(object);
		if (error == ERROR_ALREADY_EXISTS && !allow) return Admission::Duplicate;
		if(presence.Get() && PresenceName(logicalRoot)!=name) physicalPresence.Reset(presence.Release());
		logicalRoot = root;
		while (logicalRoot.size()>3 && (logicalRoot.back()==L'\\' || logicalRoot.back()==L'/')) logicalRoot.pop_back();
		presence.Reset(candidate.Release()); return Admission::Accepted;
	}

	// 受控关闭一旦被接受就放开占位：清理耗时再久，也不会让用户此时重新打开程序时被判为重复实例。
	// 只给纯关闭使用；重启、崩溃重启与更新需要把占位连续接力给新实例，不能调用。
	inline void ReleaseForClose()
	{
		presence.Reset();
		physicalPresence.Reset();
	}

	inline bool CopyParentPresence(HANDLE parent, HANDLE lease, const std::wstring& root)
	{
		HANDLE copy = nullptr;
		if (!WithinRoot(Image(parent), root) || !DuplicateHandle(parent, lease, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) return false;
		Handle candidate(copy);
		if (!IsPresence(copy, PresenceName(root))) return false;
		presence.Reset(candidate.Release()); logicalRoot = root; return true;
	}

	class Handoff
	{
		Handle mapping, ready;
		Packet* view = nullptr;
		std::wstring name;
		HANDLE sourceParent = nullptr;
	public:
		Handoff(HANDLE parent = GetCurrentProcess()) : sourceParent(parent)
		{
			if (!presence.Get() || logicalRoot.empty() || logicalRoot.size() >= 32768) return;
			GUID guid{}; wchar_t text[40]{};
			if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, text, ARRAYSIZE(text))) return;
			name = L"Local\\Inkeys.Handoff." + std::wstring(text);
			mapping.Reset(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Packet), name.c_str()));
			if (!mapping.Get() || GetLastError() == ERROR_ALREADY_EXISTS) { name.clear(); return; }
			view = static_cast<Packet*>(MapViewOfFile(mapping.Get(), FILE_MAP_WRITE, 0, 0, sizeof(Packet)));
			ready.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
			if (!view || !ready.Get()) name.clear();
		}
		~Handoff() { if (view) UnmapViewOfFile(view); }
		bool Valid() const { return !name.empty(); }
		std::wstring Arguments() const { return std::wstring(HandoffPrefix) + name; }
		bool Publish(HANDLE child)
		{
			if (!Valid()) return false;
			HANDLE remoteParent = nullptr, remoteLease = nullptr, remoteReady = nullptr;
			if (!DuplicateHandle(GetCurrentProcess(), sourceParent, child, &remoteParent, SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)
				|| !DuplicateHandle(GetCurrentProcess(), presence.Get(), child, &remoteLease, 0, FALSE, DUPLICATE_SAME_ACCESS)
				|| !DuplicateHandle(GetCurrentProcess(), ready.Get(), child, &remoteReady, EVENT_MODIFY_STATE, FALSE, 0)) return false;
			Packet packet; packet.parentPid = GetProcessId(sourceParent); packet.childPid = GetProcessId(child);
			packet.parent = reinterpret_cast<std::uintptr_t>(remoteParent); packet.presence = reinterpret_cast<std::uintptr_t>(remoteLease);
			packet.ready = reinterpret_cast<std::uintptr_t>(remoteReady);
			wcscpy_s(packet.root, logicalRoot.c_str()); std::memcpy(view, &packet, sizeof(packet)); MemoryBarrier(); return true;
		}
		bool Wait(HANDLE child)
		{
			const HANDLE waits[]{ ready.Get(), child };
			return WaitForMultipleObjects(2, waits, FALSE, 10000) == WAIT_OBJECT_0;
		}
	};

	enum class Protocol { Legacy, Current, Unsupported };
	inline Protocol HandoffProtocol(const std::wstring& image)
	{
		wchar_t system[32768]{}; const UINT count=GetSystemDirectoryW(system,ARRAYSIZE(system));
		if(!count || count>=ARRAYSIZE(system))return Protocol::Unsupported;
		HMODULE library=LoadLibraryW((std::wstring(system)+L"\\version.dll").c_str()); if(!library)return Protocol::Unsupported;
		using Size=DWORD(WINAPI*)(LPCWSTR,LPDWORD); using Read=BOOL(WINAPI*)(LPCWSTR,DWORD,DWORD,LPVOID);
		using Query=BOOL(WINAPI*)(LPCVOID,LPCWSTR,LPVOID*,PUINT);
		auto size=reinterpret_cast<Size>(GetProcAddress(library,"GetFileVersionInfoSizeW"));
		auto read=reinterpret_cast<Read>(GetProcAddress(library,"GetFileVersionInfoW"));
		auto query=reinterpret_cast<Query>(GetProcAddress(library,"VerQueryValueW"));
		DWORD ignored=0; SetLastError(ERROR_SUCCESS); const DWORD bytes=size?size(image.c_str(),&ignored):0; const DWORD error=GetLastError();
		Protocol protocol=Protocol::Unsupported;
		if(!bytes && (error==ERROR_RESOURCE_DATA_NOT_FOUND || error==ERROR_RESOURCE_TYPE_NOT_FOUND || error==ERROR_RESOURCE_NAME_NOT_FOUND))
			protocol=Protocol::Legacy;
		if(bytes && bytes<=1024*1024 && read && query)
		{
			std::vector<unsigned char> buffer(bytes);
			if(read(image.c_str(),0,bytes,buffer.data()))
			{
				void* value=nullptr; UINT length=0;
				const auto start=reinterpret_cast<std::uintptr_t>(buffer.data());
				auto inside=[&](std::size_t size) {const auto pointer=reinterpret_cast<std::uintptr_t>(value);
					return value && pointer>=start && pointer<=start+bytes && size<=start+bytes-pointer;};
				DWORD signature=0;
				if(query(buffer.data(),L"\\",&value,&length) && length>=sizeof(signature) && inside(length))
				{
					std::memcpy(&signature,value,sizeof(signature));
					if(signature==0xfeef04bd)
					{
						if(!query(buffer.data(),L"\\StringFileInfo\\080404b0\\StartupHandoffProtocol",&value,&length)) protocol=Protocol::Legacy;
						else if(length==2 && inside(length*sizeof(wchar_t)) && std::memcmp(value,L"1",length*sizeof(wchar_t))==0) protocol=Protocol::Current;
					}
				}
			}
		}
		FreeLibrary(library); return protocol;
	}
	inline bool StartCompatibleReplacement(const std::wstring& image, std::wstring_view extraArguments = {})
	{
		if((!extraArguments.empty() && extraArguments != L"-Whiteboard") || !presence.Get()
			|| !WithinRoot(image,logicalRoot) || HandoffProtocol(image)!=Protocol::Legacy)return false;
		std::wstring command=L"\""+image+L"\"";
		if (!extraArguments.empty()) command += L" " + std::wstring(extraArguments);
		STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION created{};
		if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&created))return false;
		Handle process(created.hProcess),thread(created.hThread); HANDLE remote=nullptr;
		// 旧版本不识别Ready协议；复制的占位随旧进程寿命关闭，同时保留旧版正常入口互斥。
		if(DuplicateHandle(GetCurrentProcess(),presence.Get(),process.Get(),&remote,0,FALSE,DUPLICATE_SAME_ACCESS)
			&& ResumeThread(thread.Get())!=static_cast<DWORD>(-1))return true;
		TerminateProcess(process.Get(),ERROR_CANCELLED);(void)WaitForSingleObject(process.Get(),1000);return false;
	}

	inline bool StartReplacement(const std::wstring& image, std::wstring_view extraArguments = {})
	{
		// 更新交接仅允许传递这一个一次性意图，不能成为任意命令行转发入口。
		if (!extraArguments.empty() && extraArguments != L"-Whiteboard") return false;
		const auto protocol=HandoffProtocol(image);
		if (protocol==Protocol::Unsupported) return false;
		if (protocol==Protocol::Legacy) return StartCompatibleReplacement(image, extraArguments);
		Handoff handoff; if (!handoff.Valid() || !WithinRoot(image, logicalRoot)) return false;
		std::wstring command = L"\"" + image + L"\" " + handoff.Arguments();
		if (!extraArguments.empty()) command += L" " + std::wstring(extraArguments);
		STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION created{};
		if (!CreateProcessW(image.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &created)) return false;
		Handle process(created.hProcess), thread(created.hThread);
		if (handoff.Publish(process.Get()) && ResumeThread(thread.Get()) != static_cast<DWORD>(-1) && handoff.Wait(process.Get())) return true;
		TerminateProcess(process.Get(), ERROR_CANCELLED); (void)WaitForSingleObject(process.Get(), 1000); return false;
	}
	// 专项只在复制到私有临时目录的EXE中运行产品准入/接力，不创建窗口或读取用户配置。
	inline bool PrivateTestRoot(const std::wstring& root)
	{
		wchar_t temporary[32768]{}; const DWORD length=GetTempPathW(ARRAYSIZE(temporary),temporary);
		if (!length || length>=ARRAYSIZE(temporary)) return false;
		const std::wstring prefix=std::wstring(temporary)+L"Inkeys Instance Test ";
		return root.rfind(prefix,0)==0 && Directory(root)==std::filesystem::path(temporary).lexically_normal().parent_path().wstring();
	}
	inline bool TryRunTests(int& result)
	{
		int count=0; LPWSTR* raw=CommandLineToArgvW(GetCommandLineW(),&count);
		if (!raw) return false;
		std::vector<std::wstring> args; for(int i=0;i<count;++i) args.emplace_back(raw[i]); LocalFree(raw);
		if (count<2) return false;
		const auto mode=args[1];
		const bool parent=mode==L"--single-instance-test-parent", relay=mode==L"--single-instance-test-relay",
			child=mode==L"--single-instance-test-child" || relay, race=mode==L"--single-instance-test-race", probe=mode==L"--single-instance-test-probe";
		if (!parent && !child && !probe && !race && mode!=L"--single-instance-tests") return false;
		result=1;
		if (parent || child || probe || race)
		{
			const auto physicalRoot=Directory(Image());
			const auto root=PrivateTestRoot(physicalRoot)?physicalRoot:Directory(physicalRoot);
			if (!PrivateTestRoot(root)) return true;
			if (!Equal(physicalRoot,root))
			{
				const DWORD attributes=GetFileAttributesW(physicalRoot.c_str());
				if (!Equal(physicalRoot,root+L"\\installer") || attributes==INVALID_FILE_ATTRIBUTES
					|| !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
			}
			auto trace=[&](const char* stage,unsigned long code) { std::ofstream log(std::filesystem::path(root)/L"trace.txt",std::ios::app);log<<stage<<" code="<<code<<" error="<<GetLastError()<<"\n"; };
			if (race)
			{
				Handle start(OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\"+PresenceName(root)+L".TestStart").c_str()));
				Handle stop(OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\"+PresenceName(root)+L".TestRelease").c_str()));
				if(!start.Get() || !stop.Get() || WaitForSingleObject(start.Get(),10000)!=WAIT_OBJECT_0)return true;
				const auto admission=Acquire(root,false);
				std::ofstream verdict(std::filesystem::path(root)/(L"race."+std::to_wstring(GetCurrentProcessId())+L".txt"));verdict<<static_cast<int>(admission);verdict.close();
				if(admission==Admission::Accepted) (void)WaitForSingleObject(stop.Get(),10000);
				result=admission==Admission::Accepted?0:admission==Admission::Duplicate?10:20;return true;
			}
			if (probe)
			{
				const auto admission=Acquire(root,count==3 && args[2]==L"allow");
				result=admission==Admission::Accepted?0:admission==Admission::Duplicate?10:20; return true;
			}
			if(child)
			{
				const int adoption=Adopt(); trace("child Adopt",static_cast<unsigned long>(adoption));
				if(adoption!=1) { result=30; return true; }
				if (!relay)
				{
				std::ofstream marker(std::filesystem::path(root)/L"adopted.pid"); marker<<GetCurrentProcessId(); marker.close();
				Handle release(OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\"+PresenceName(root)+L".TestRelease").c_str()));
				result=release.Get() && WaitForSingleObject(release.Get(),15000)==WAIT_OBJECT_0?0:31; return true;
				}
			}
			const auto admission=Acquire(root,false); trace("parent Acquire",static_cast<unsigned long>(admission));
			if(admission!=Admission::Accepted) return true;
			Handoff handoff; trace("parent Handoff.Valid",handoff.Valid()); if(!handoff.Valid()) return true;
			const auto target=parent?root+L"\\installer\\Inkeys.exe":root+L"\\Inkeys.exe";
			std::wstring command=L"\""+target+L"\" "+(parent?L"--single-instance-test-relay ":L"--single-instance-test-child ")+handoff.Arguments();
			STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION created{};
			if(!CreateProcessW(target.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created)) return true;
			Handle process(created.hProcess), thread(created.hThread);
			const bool published=handoff.Publish(process.Get()); trace("parent Publish",published);
			const bool resumed=published && ResumeThread(thread.Get())!=static_cast<DWORD>(-1);trace("parent Resume",resumed);
			const bool ready=resumed && handoff.Wait(process.Get());trace("parent Ready",ready);
			if(ready) {result=0;return true;}
			trace("parent retained after rejected Ready",IsPresence(presence.Get(),PresenceName(root)));
			TerminateProcess(process.Get(),ERROR_CANCELLED); (void)WaitForSingleObject(process.Get(),1000); return true;
		}
		wchar_t temporary[32768]{}; if(!GetTempPathW(ARRAYSIZE(temporary),temporary)) return true;
		const auto root=std::wstring(temporary)+L"Inkeys Instance Test "+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
		if(!PrivateTestRoot(root)) return true;
		std::error_code error; if(!std::filesystem::create_directory(root,error)) return true;
		const auto executable=root+L"\\Inkeys.exe";
		if(!std::filesystem::copy_file(Image(),executable,std::filesystem::copy_options::none,error)) return true;
		const auto installer=root+L"\\installer";
		if(!std::filesystem::create_directory(installer,error)
			|| !std::filesystem::copy_file(Image(),installer+L"\\Inkeys.exe",std::filesystem::copy_options::none,error))return true;
		std::wstring runExecutable=executable;
		auto run=[&](const std::wstring& arguments,DWORD& exitCode)->bool {
			std::wstring command=L"\""+runExecutable+L"\" "+arguments;
			STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION created{};
			if(!CreateProcessW(runExecutable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created))return false;
			Handle process(created.hProcess),thread(created.hThread);
			if(WaitForSingleObject(process.Get(),15000)!=WAIT_OBJECT_0) {TerminateProcess(process.Get(),ERROR_CANCELLED);(void)WaitForSingleObject(process.Get(),1000);return false;}
			const bool read=GetExitCodeProcess(process.Get(),&exitCode)!=FALSE;
			std::fwprintf(stderr,L"[SingleInstanceCase] %ls exit=%lu read=%d\n",arguments.c_str(),exitCode,read);
			return read;
		};
		Handle release(CreateEventW(nullptr,TRUE,FALSE,(L"Local\\"+PresenceName(root)+L".TestRelease").c_str()));
		DWORD code=0;
		// 先制造真实installer物理占位冲突：子进程必须在Ready前拒绝，父仍持根lease。
		Handle physicalBlocker(CreateMutexW(nullptr,FALSE,(L"Local\\"+PresenceName(installer)).c_str()));
		bool ok=release.Get() && physicalBlocker.Get() && run(L"--single-instance-test-parent",code) && code==1;
		bool retained=false;
		{std::ifstream trace(std::filesystem::path(root)/L"trace.txt");std::string line;
			while(std::getline(trace,line))retained|=line.find("parent retained after rejected Ready code=1 ")!=std::string::npos;}
		ok=retained && !std::filesystem::exists(std::filesystem::path(root)/L"adopted.pid",error) && ok;
		std::fprintf(stderr,"[SingleInstanceCase] installer conflict retained-parent=%d ok=%d\n",retained,ok);
		physicalBlocker.Reset();
		ok=ok && run(L"--single-instance-test-parent",code) && code==0;
		DWORD childPid=0;
		for(unsigned attempt=0;ok && !childPid && attempt<100;++attempt)
		{
			std::ifstream marker(std::filesystem::path(root)/L"adopted.pid"); marker>>childPid;
			if(!childPid) Sleep(20); // 仅私有测试等marker；生产交接从不按固定延迟猜旧进程退出。
		}
		Handle replacement(childPid?OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,childPid):nullptr);
		ok=ok && replacement.Get() && run(L"--single-instance-test-probe",code) && code==10;
		const auto otherRoot=root+L" other";
		if(ok && std::filesystem::create_directory(otherRoot,error) && std::filesystem::copy_file(Image(),otherRoot+L"\\Inkeys.exe",std::filesystem::copy_options::none,error))
		{
			runExecutable=otherRoot+L"\\Inkeys.exe";ok=run(L"--single-instance-test-probe",code) && code==0;runExecutable=executable;
		}
		else ok=false;
		if(PrivateTestRoot(otherRoot))std::filesystem::remove_all(otherRoot,error);
		ok=ok && run(L"--single-instance-test-probe allow",code) && code==0;
		ok=ok && run(L"--single-instance-test-probe",code) && code==10;
		ok=ok && run(L"--single-instance-test-child --inkeys-startup-handoff=Local\\Inkeys.Handoff.invalid",code) && code==30;
		SetEvent(release.Get());
		if(replacement.Get()) ok=WaitForSingleObject(replacement.Get(),15000)==WAIT_OBJECT_0 && ok;
		ok=ok && run(L"--single-instance-test-probe",code) && code==0;
		// 两个真实子进程同一barrier竞争，获准者持占位到driver收齐结果。
		Handle start(CreateEventW(nullptr,TRUE,FALSE,(L"Local\\"+PresenceName(root)+L".TestStart").c_str()));
		ResetEvent(release.Get()); HANDLE racers[2]{}; DWORD racerPids[2]{};
		for(unsigned i=0;ok && i<2;++i)
		{
			std::wstring command=L"\""+executable+L"\" --single-instance-test-race";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION created{};
			if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created)){ok=false;break;}
			racers[i]=created.hProcess;racerPids[i]=created.dwProcessId;CloseHandle(created.hThread);
		}
		SetEvent(start.Get());int verdicts[2]{-1,-1};
		for(unsigned attempt=0;ok && (verdicts[0]<0 || verdicts[1]<0) && attempt<500;++attempt)
		{
			for(unsigned i=0;i<2;++i){std::ifstream verdict(std::filesystem::path(root)/(L"race."+std::to_wstring(racerPids[i])+L".txt"));verdict>>verdicts[i];}
			if(verdicts[0]<0 || verdicts[1]<0)Sleep(20);
		}
		ok=ok && ((verdicts[0]==0 && verdicts[1]==1)||(verdicts[0]==1 && verdicts[1]==0));
		SetEvent(release.Get());for(auto process:racers)if(process){if(WaitForSingleObject(process,15000)!=WAIT_OBJECT_0){TerminateProcess(process,ERROR_CANCELLED);ok=false;}CloseHandle(process);}
		std::fprintf(stderr,"[SingleInstanceCase] concurrent admissions=%d/%d\n",verdicts[0],verdicts[1]);
		replacement.Reset(); release.Reset();
		// 修改仅属本次私有copy的VERSIONINFO，真实验证三态能力读取而非镜像分流算法。
		std::fprintf(stderr,"[SingleInstanceCase] before capability ok=%d\n",ok);
		HMODULE resource=LoadLibraryExW(executable.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE);
		HRSRC entry=resource?FindResourceW(resource,MAKEINTRESOURCEW(1),RT_VERSION):nullptr;
		const DWORD resourceSize=entry?SizeofResource(resource,entry):0;
		const void* resourceData=entry?LockResource(LoadResource(resource,entry)):nullptr;
		std::vector<unsigned char> original;
		if(resourceData && resourceSize)original.assign(static_cast<const unsigned char*>(resourceData),static_cast<const unsigned char*>(resourceData)+resourceSize);
		std::fprintf(stderr,"[SingleInstanceCapability] resource=%d entry=%d bytes=%lu copied=%zu error=%lu\n",resource!=nullptr,entry!=nullptr,resourceSize,original.size(),GetLastError());
		if(resource)FreeLibrary(resource);
		const wchar_t key[]=L"StartupHandoffProtocol";
		auto keyStart=std::search(original.begin(),original.end(),reinterpret_cast<const unsigned char*>(key),reinterpret_cast<const unsigned char*>(key)+sizeof(key));
		if(keyStart==original.end())ok=false;
		else
		{
			const auto keyOffset=static_cast<std::size_t>(keyStart-original.begin()), valueOffset=(keyOffset+sizeof(key)+3)&~std::size_t(3);
			// 已执行/查询过的image仍可能保留资源引用；每个变体使用未加载的新copy，修改后才读取。
			auto variant=[&](const wchar_t* label,std::vector<unsigned char>& bytes,Protocol expected) {
				const auto target=root+L"\\metadata-"+label+L".exe";
				if(!CopyFileW(Image().c_str(),target.c_str(),TRUE))return false;
				HANDLE write=BeginUpdateResourceW(target.c_str(),FALSE);if(!write)return false;
				if(!UpdateResourceW(write,RT_VERSION,MAKEINTRESOURCEW(1),0x804,bytes.data(),static_cast<DWORD>(bytes.size())))
					{EndUpdateResourceW(write,TRUE);return false;}
				const bool saved=EndUpdateResourceW(write,FALSE)!=FALSE;const DWORD writeError=GetLastError();
				const auto actual=HandoffProtocol(target);
				std::fprintf(stderr,"[SingleInstanceCapability] %ls write=%d protocol=%d expected=%d error=%lu\n",label,saved,static_cast<int>(actual),static_cast<int>(expected),writeError);
				return saved && actual==expected;
			};
			const auto current=HandoffProtocol(executable);std::fprintf(stderr,"[SingleInstanceCapability] current=%d offset=%zu/%zu error=%lu\n",static_cast<int>(current),keyOffset,valueOffset,GetLastError());
			ok=current==Protocol::Current && ok;
			if(valueOffset+2>=original.size())ok=false;
			else
			{
				auto unknown=original;unknown[valueOffset]='2';ok=variant(L"unknown",unknown,Protocol::Unsupported) && ok;
				auto legacy=original;legacy[keyOffset]='X';ok=variant(L"absent",legacy,Protocol::Legacy) && ok;
				ok=variant(L"current",original,Protocol::Current) && ok;
			}
		}

		if(!ok) { std::ifstream trace(std::filesystem::path(root)/L"trace.txt");std::string line;while(std::getline(trace,line))std::fprintf(stderr,"[SingleInstanceTrace] %s\n",line.c_str()); }
		if(PrivateTestRoot(root)) std::filesystem::remove_all(root,error);
		std::fprintf(stderr,"[SingleInstance] exact-parent handoff, presence lifetime, allow-to-deny, forged packet: %s\n",ok?"PASS":"FAIL");
		result=ok?0:1; return true;
	}

}
