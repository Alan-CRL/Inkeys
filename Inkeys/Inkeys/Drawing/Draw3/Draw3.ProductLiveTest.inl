// 只在 IdtMagnification.cpp 末尾包含：复用同 TU 已有实屏安全门，不注册另一套 Mag。
#if defined(INKEYS_ENABLE_UI3_PRESENTATION_HARNESS) && INKEYS_ENABLE_UI3_PRESENTATION_HARNESS
// Draw3.Product/ScrollFollow/IdtState头须由宿主在import Window之前包含，避免退出凭据重复定义。

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <windowsx.h>

import Inkeys.Other.Config;
import Inkeys.Drawing.Draw3.auto_save;
import draw3.uink_file;

namespace
{
	namespace ProductLive
	{
		namespace Draw = Inkeys::Drawing::Draw3;
		namespace Business = Inkeys::Business::ScrollFollow;
		namespace LiveCapture = Inkeys::Graphics::FreezeCapture;
		using Role = Inkeys::Window::WindowRole;
		constexpr ULONG_PTR inputTag = 0x53464C56u;
		constexpr int wheelStep = 16;
		constexpr wchar_t sourceClass[] = L"Inkeys.ScrollFollowLive.Texture";
		struct ExternalState
		{
			bool enabled = false;
			std::wstring ipc;
			Json::Value identity;
			HANDLE edge = nullptr, runner = nullptr;
			HWND root = nullptr, content = nullptr;
			DWORD edgePid = 0;
			POINT origin{}; SIZE size{};
			double dx = 0, dy = 0;
			~ExternalState() { if (edge) CloseHandle(edge); if (runner) CloseHandle(runner); }
		};
		class MotionTrace;
		class DebugNumbers;
		namespace DebugWindow = Inkeys::UI::ScrollFollowDebug;
		struct Fixture
		{
			HANDLE log = INVALID_HANDLE_VALUE;
			std::mutex logMutex;
			std::atomic<bool> logFailed{}, stateReturned{}, stateFailed{}, magReturned{}, magFailed{};
			std::atomic<DWORD> stateTid{}, magTid{};
			HWND source = nullptr;
			int width = 0, height = 0, x = 256, y = 512;
			UINT mouseDpiX = 0, mouseDpiY = 0;
			HMONITOR mouseMonitor = nullptr;
			DWORD mouseOrientation = 0;
			int paintedX = 0, paintedY = 0;
			unsigned wheelMessages = 0, taggedWheels = 0, foreignWheels = 0, paintSequence = 0;
			std::uint64_t textureHash = 0;
			LARGE_INTEGER paintedAt{};
			bool sourceCancelled = false, sourceFailed = false, security = false, ownLeftHeld = false;
			Inkeys::Graphics::DibSurface texture;
			std::wstring root;
			ExternalState external;
			std::shared_ptr<MotionTrace> trace;
			bool traceRequested = false, debuggerProbe = false;
			std::shared_ptr<DebugNumbers> debugNumbers;
		};

		void Log(Fixture& f, const char* format, ...) noexcept
		{
			char line[2048]{};
			va_list arguments;
			va_start(arguments, format);
			const int count = vsnprintf(line, sizeof(line), format, arguments);
			va_end(arguments);
			if (count <= 0) return;
			const DWORD size = static_cast<DWORD>((std::min)(count, static_cast<int>(sizeof(line) - 1)));
			std::scoped_lock lock(f.logMutex);
			DWORD written = 0;
			if (f.log != INVALID_HANDLE_VALUE && (!WriteFile(f.log, line, size, &written, nullptr) || written != size))
				f.logFailed.store(true);
			const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
			if (output && output != INVALID_HANDLE_VALUE) (void)WriteFile(output, line, size, &written, nullptr);
		}

		bool ReleaseOwnLeft(Fixture& f) noexcept
		{
			if (!f.ownLeftHeld) return true;
			// 只归还本夹具成功插入的Down；安全层/取消时也不追加Down或点击外部内容。
			INPUT up{}; up.type = INPUT_MOUSE; up.mi.dwFlags = MOUSEEVENTF_LEFTUP; up.mi.dwExtraInfo = inputTag;
			for (unsigned attempt = 1; attempt <= 3; ++attempt)
			{
				if (SendInput(1, &up, sizeof(INPUT)) == 1)
				{
					f.ownLeftHeld = false;
					Log(f, "[ProductLive] own_left_release accepted=1 attempt=%u original_tag=1\n", attempt);
					return true;
				}
				if (attempt < 3) Sleep(10);
			}
			Log(f, "[ProductLive] own_left_release accepted=0 attempts=3 held_unproven=1 error=%lu\n", GetLastError());
			return false;
		}

		std::uint32_t TexturePixel(int x, int y) noexcept
		{
			// 世界坐标的散列纹理不随帧重置；4px细节和不同位置的L角点避免周期棋盘的多解。
			std::uint32_t h = static_cast<std::uint32_t>(x / 4) * 0x9e3779b9u ^
				static_cast<std::uint32_t>(y / 4) * 0x85ebca6bu ^ 0x317bc4edu;
			h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
			const auto g = 30u + h % 196u;
			std::uint32_t a = static_cast<std::uint32_t>(x / 96) * 0x45d9f3bu ^
				static_cast<std::uint32_t>(y / 96) * 0x27d4eb2du;
			a ^= a >> 16;
			const int u = x % 96 - (8 + static_cast<int>(a % 40));
			const int v = y % 96 - (8 + static_cast<int>((a >> 8) % 40));
			const auto gray = ((u >= 0 && u < 3 && v >= 0 && v < 26) ||
				(v >= 0 && v < 3 && u >= 0 && u < 26)) ? 8u : g;
			return 0xff000000u | gray * 0x010101u;
		}

		LRESULT CALLBACK SourceProc(HWND window, UINT message, WPARAM wp, LPARAM lp) noexcept
		{
			auto* f = reinterpret_cast<Fixture*>(GetWindowLongPtrW(window, GWLP_USERDATA));
			if (message == WM_NCCREATE)
			{
				f = static_cast<Fixture*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
				SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(f));
			}
			if (!f) return DefWindowProcW(window, message, wp, lp);
			if (message == WM_CLOSE || (message == WM_KEYDOWN && wp == VK_ESCAPE))
			{
				if (!f->sourceCancelled) Log(*f, "[ProductLive] source_cancel origin=close-or-escape message=%u wp=%llu lp=%lld tag=%llu\n",
					message, static_cast<unsigned long long>(wp), static_cast<long long>(lp), static_cast<unsigned long long>(GetMessageExtraInfo()));
				f->sourceCancelled = true; return 0;
			}
			if (message == WM_ERASEBKGND) return 1;
			if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
			{
				++f->wheelMessages;
				const bool tagged = static_cast<ULONG_PTR>(GetMessageExtraInfo()) == inputTag;
				if (tagged) ++f->taggedWheels;
				else
				{
					if (!f->sourceCancelled) Log(*f, "[ProductLive] source_cancel origin=foreign-wheel message=%u delta=%d tag=%llu expected_tag=%llu\n",
						message, GET_WHEEL_DELTA_WPARAM(wp), static_cast<unsigned long long>(GetMessageExtraInfo()), static_cast<unsigned long long>(inputTag));
					++f->foreignWheels; f->sourceCancelled = true; return 0;
				}
				const int delta = GET_WHEEL_DELTA_WPARAM(wp);
				if (message == WM_MOUSEWHEEL) f->y = std::clamp(f->y - delta * wheelStep / WHEEL_DELTA, 0, 24 * f->height);
				else f->x = std::clamp(f->x + delta * wheelStep / WHEEL_DELTA, 0, 16 * f->width);
				Log(*f, "[ProductLive] source_wheel message=%u tagged=%u delta=%d offset=(%d,%d) screen=(%d,%d)\n",
					message, tagged, delta, f->x, f->y, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
				InvalidateRect(window, nullptr, FALSE);
				return 0;
			}
			if (message == WM_PAINT)
			{
				PAINTSTRUCT paint{};
				const HDC dc = BeginPaint(window, &paint);
				if (f->texture.empty() && !f->texture.resize(f->width, f->height))
				{
					if (!f->sourceFailed) Log(*f, "[ProductLive] source_failure origin=texture-resize width=%d height=%d error=%lu\n", f->width, f->height, GetLastError());
					f->sourceFailed = true;
				}
				if (!f->sourceFailed)
				{
					auto pixels = f->texture.pixels();
					std::uint64_t hash = 1469598103934665603ull;
					for (int y = 0; y < f->height; ++y) for (int x = 0; x < f->width; ++x)
					{
						const auto pixel = TexturePixel(x + f->x, y + f->y);
						pixels[static_cast<std::size_t>(y) * f->width + x] = pixel;
						hash = (hash ^ pixel) * 1099511628211ull;
					}
					const bool copied = dc && BitBlt(dc, 0, 0, f->width, f->height, f->texture.dc(), 0, 0, SRCCOPY);
					const DWORD copyError = copied ? ERROR_SUCCESS : GetLastError();
					const bool flushed = GdiFlush() != FALSE;
					if (!copied || !flushed)
					{
						// 只补失败来源；仍按原门禁停止，GDI错误码不独自证明失败原因。
						const DWORD flushError = flushed ? ERROR_SUCCESS : GetLastError();
						Log(*f, "[ProductLive] source_failure origin=paint-copy dc=%u texture_dc=%u copied=%u copy_error=%lu flushed=%u flush_error=%lu sequence=%u offset=(%d,%d)\n",
							dc != nullptr, f->texture.dc() != nullptr, copied, copyError, flushed, flushError, f->paintSequence, f->x, f->y);
						f->sourceFailed = true;
					}
					else
					{
						f->paintedX = f->x; f->paintedY = f->y; f->textureHash = hash;
						QueryPerformanceCounter(&f->paintedAt); ++f->paintSequence;
						Log(*f, "[ProductLive] source_paint sequence=%u offset=(%d,%d) qpc=%lld hash=%016llx\n",
							f->paintSequence, f->paintedX, f->paintedY, f->paintedAt.QuadPart, f->textureHash);
					}
				}
				EndPaint(window, &paint);
				return 0;
			}
			return DefWindowProcW(window, message, wp, lp);
		}

		// 外部真值只用于断言；有限阶段文件从不进入捕获、跟踪器或Follow写入口。
		std::string ExternalString(const Json::Value& v, const char* name)
		{
			if (!v[name].isString() || v[name].asString().empty() || v[name].asString().size() > 2048)
				throw std::runtime_error("external-string-field");
			return v[name].asString();
		}
		std::uint64_t ExternalId(const Json::Value& v, const char* name)
		{
			const auto text = ExternalString(v,name);
			if (text.size() > 20 || (text.size() > 1 && text[0] == '0') || text.find_first_not_of("0123456789") != std::string::npos)
				throw std::runtime_error("external-decimal-identity");
			return std::stoull(text);
		}
		double ExternalNumber(const Json::Value& v, const char* name)
		{
			if (!v[name].isNumeric() || !std::isfinite(v[name].asDouble()) || std::abs(v[name].asDouble()) > 1e8)
				throw std::runtime_error("external-nonfinite-or-missing-observation");
			return v[name].asDouble();
		}
		std::string ExternalCreated(HANDLE process)
		{
			FILETIME created{}, exited{}, kernel{}, user{};
			if (!GetProcessTimes(process,&created,&exited,&kernel,&user)) throw std::runtime_error("external-process-times");
			return std::to_string((static_cast<std::uint64_t>(created.dwHighDateTime)<<32) | created.dwLowDateTime);
		}
		bool ReadExternal(Fixture& f, const wchar_t* name, Json::Value& value)
		{
			const auto path = f.external.ipc + L"/" + name;
			HANDLE file = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				if (GetLastError() == ERROR_FILE_NOT_FOUND) return false;
				throw std::runtime_error("external-open-phase");
			}
			BY_HANDLE_FILE_INFORMATION info{}; DWORD read = 0; char bytes[16385]{};
			const bool valid = GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
				!info.nFileSizeHigh && info.nFileSizeLow && info.nFileSizeLow <= 16384 &&
				ReadFile(file,bytes,info.nFileSizeLow,&read,nullptr) && read == info.nFileSizeLow;
			CloseHandle(file);
			if (!valid) throw std::runtime_error("external-phase-size-or-file-type");
			Json::CharReaderBuilder builder; builder["collectComments"]=false; builder["allowComments"]=false;
			builder["rejectDupKeys"]=true; builder["failIfExtra"]=true; builder["strictRoot"]=true;
			std::unique_ptr<Json::CharReader> reader(builder.newCharReader()); std::string errors;
			if (!reader->parse(bytes,bytes+read,&value,&errors) || !value.isObject()) throw std::runtime_error("external-phase-json");
			return true;
		}
		void CheckExternalIdentity(Fixture& f, const Json::Value& v, unsigned sequence, std::uint64_t session)
		{
			if (!v["schema"].isUInt() || v["schema"].asUInt()!=1 || !v["seq"].isUInt() || v["seq"].asUInt()!=sequence)
				throw std::runtime_error("external-phase-order");
			for (const char* key : {"run","url","target","edgePid","edgeCreated","edgeImage","runnerPid","runnerCreated","runnerImage","rootHwnd","contentHwnd"})
				if (ExternalString(v,key) != ExternalString(f.external.identity,key)) throw std::runtime_error("external-phase-identity-changed");
			if (ExternalString(v,"session") != std::to_string(session)) throw std::runtime_error("external-session-changed");
			if (ExternalString(v,"inkeysPid") != std::to_string(GetCurrentProcessId()) ||
				ExternalString(v,"inkeysCreated") != ExternalCreated(GetCurrentProcess())) throw std::runtime_error("external-inkeys-identity");
		}
		void CheckExternalWindow(Fixture& f)
		{
			auto& e=f.external; if (!e.enabled) return;
			if (GetFileAttributesW((e.ipc+L"/runner-cancel.json").c_str()) != INVALID_FILE_ATTRIBUTES)
				throw std::runtime_error("external-runner-cancelled");
			DWORD rootPid=0, contentPid=0; POINT origin{}; RECT size{};
			if (!e.edge || !e.runner || WaitForSingleObject(e.edge,0)!=WAIT_TIMEOUT || WaitForSingleObject(e.runner,0)!=WAIT_TIMEOUT ||
				!IsWindow(e.root) || !IsWindow(e.content) || !IsWindowVisible(e.root) || !IsWindowVisible(e.content) ||
				!GetWindowThreadProcessId(e.root,&rootPid) || !GetWindowThreadProcessId(e.content,&contentPid) || rootPid!=e.edgePid || contentPid!=e.edgePid ||
				GetAncestor(e.content,GA_ROOT)!=e.root || !GetClientRect(e.content,&size) || !ClientToScreen(e.content,&origin) ||
				origin.x!=e.origin.x || origin.y!=e.origin.y || size.right!=e.size.cx || size.bottom!=e.size.cy)
				throw std::runtime_error("external-process-window-or-geometry-changed");
		}
		void CheckExternalObservation(Fixture& f, const Json::Value& v)
		{
			const auto& ready=f.external.identity["observation"];
			if (ExternalString(v,"url")!=ExternalString(f.external.identity,"url") || ExternalString(v,"run")!=ExternalString(f.external.identity,"run") ||
				ExternalString(v,"fixtureId")!="inkeys-browser-scroll-v1" || !v["ready"].isBool() || !v["ready"].asBool() ||
				ExternalString(v,"visibility")!="visible" || ExternalNumber(v,"stableFrames")<6 || ExternalNumber(v,"stableMs")<150 ||
				ExternalNumber(v,"visualScale")!=1 || ExternalNumber(v,"dpr")<=0 || ExternalNumber(v,"dpr")>8)
				throw std::runtime_error("external-page-not-stable-or-wrong-target");
			for (const char* key:{"dpr","innerWidth","innerHeight","resizeCount"})
				if (ExternalNumber(v,key)!=ExternalNumber(ready,key)) throw std::runtime_error("external-css-geometry-changed");
			if (std::abs(ExternalNumber(v,"innerWidth")*ExternalNumber(v,"dpr")-f.external.size.cx)>2 ||
				std::abs(ExternalNumber(v,"innerHeight")*ExternalNumber(v,"dpr")-f.external.size.cy)>2)
				throw std::runtime_error("external-content-dpr-mapping");
		}
		void LoadExternal(Fixture& f)
		{
			wchar_t root[MAX_PATH]{}; const DWORD n=GetEnvironmentVariableW(L"INKEYS_SCROLL_EXTERNAL_ROOT",root,MAX_PATH);
			if (!n || n>=MAX_PATH-80) throw std::runtime_error("external-private-root-required");
			const auto path=std::filesystem::path(root).lexically_normal();
			const auto parent=std::filesystem::current_path()/L"Build"/L"ScrollFollow";
			const auto leaf=path.filename().wstring();
			if (!path.is_absolute() || path.parent_path()!=parent || leaf.size()!=41 || leaf.substr(0,9)!=L"external-" ||
				leaf.substr(9).find_first_not_of(L"0123456789abcdef")!=std::wstring::npos) throw std::runtime_error("external-private-root-shape");
			for (auto part=path; !part.empty(); part=part.parent_path())
			{
				const DWORD attr=GetFileAttributesW(part.c_str());
				if (attr==INVALID_FILE_ATTRIBUTES || !(attr&FILE_ATTRIBUTE_DIRECTORY) || (attr&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("external-reparse-or-missing-root");
				if (part==part.root_path()) break;
			}
			auto& e=f.external; e.ipc=path.wstring();
			if (!ReadExternal(f,L"browser-ready.json",e.identity)) throw std::runtime_error("external-ready-missing");
			const auto& v=e.identity;
			const auto run=ExternalString(v,"run");
			if (!v["schema"].isUInt() || v["schema"].asUInt()!=1 || !v["seq"].isUInt() || v["seq"].asUInt()!=1 ||
				std::wstring(run.begin(),run.end())!=leaf.substr(9) || ExternalString(v,"inkeysPid")!="0" || ExternalString(v,"inkeysCreated")!="0" ||
				!v["screenVerified"].isBool() || !v["screenVerified"].asBool()) throw std::runtime_error("external-ready-identity");
			(void)ExternalString(v,"target"); (void)ExternalString(v,"url");
			const auto edgePid=ExternalId(v,"edgePid"), runnerPid=ExternalId(v,"runnerPid");
			if (!edgePid || edgePid>MAXDWORD || !runnerPid || runnerPid>MAXDWORD || edgePid==runnerPid || edgePid==GetCurrentProcessId() || runnerPid==GetCurrentProcessId())
				throw std::runtime_error("external-process-id-range");
			e.edgePid=static_cast<DWORD>(edgePid);
			e.edge=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,e.edgePid);
			e.runner=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,static_cast<DWORD>(runnerPid));
			for (const auto& process : {std::pair{e.edge,"edge"},std::pair{e.runner,"runner"}})
			{
				wchar_t image[MAX_PATH]{}; DWORD size=MAX_PATH;
				const auto expectedImage=ExternalString(v,(std::string(process.second)+"Image").c_str());
				// char8_t保留UTF-8码元，让C++20 path按UTF-8转换；不依赖本地代码页。
				const auto expectedPath=std::filesystem::path(std::u8string(expectedImage.begin(),expectedImage.end()));
				if (!process.first || !QueryFullProcessImageNameW(process.first,0,image,&size) ||
					ExternalCreated(process.first)!=ExternalString(v,(std::string(process.second)+"Created").c_str()) ||
					CompareStringOrdinal(image,-1,expectedPath.c_str(),-1,TRUE)!=CSTR_EQUAL)
					throw std::runtime_error("external-held-process-identity");
			}
			const auto edgeImage=ExternalString(v,"edgeImage");
			if (std::filesystem::path(std::u8string(edgeImage.begin(),edgeImage.end())).filename()!=L"msedge.exe") throw std::runtime_error("external-not-edge");
			const auto rootId=ExternalId(v,"rootHwnd"), contentId=ExternalId(v,"contentHwnd");
			if (!rootId || !contentId || rootId>UINTPTR_MAX || contentId>UINTPTR_MAX) throw std::runtime_error("external-hwnd-range");
			e.root=reinterpret_cast<HWND>(static_cast<UINT_PTR>(rootId)); e.content=reinterpret_cast<HWND>(static_cast<UINT_PTR>(contentId));
			e.origin={static_cast<LONG>(ExternalNumber(v,"contentX")),static_cast<LONG>(ExternalNumber(v,"contentY"))};
			e.size={static_cast<LONG>(ExternalNumber(v,"contentWidth")),static_cast<LONG>(ExternalNumber(v,"contentHeight"))};
			if (e.origin.x<0 || e.origin.y<0 || e.size.cx<320 || e.size.cy<320) throw std::runtime_error("external-content-bounds");
			e.enabled=true; f.source=e.root; CheckExternalWindow(f); CheckExternalObservation(f,v["observation"]);
		}
		void WriteExternal(Fixture& f, const wchar_t* name, unsigned sequence, std::uint64_t session, double baseX, double baseY)
		{
			Json::Value v=f.external.identity; v["seq"]=sequence; v["inkeysPid"]=std::to_string(GetCurrentProcessId());
			v["inkeysCreated"]=ExternalCreated(GetCurrentProcess()); v["session"]=std::to_string(session);
			v["baseX"]=baseX; v["baseY"]=baseY;
			const auto capture=LiveCapture::GetSnapshot(); v["captureSession"]=std::to_string(capture.session);
			v["accepted"]=std::to_string(capture.statistics.accepted);
			Json::StreamWriterBuilder writer; writer["indentation"]=""; const auto bytes=Json::writeString(writer,v);
			if (bytes.size()>16384) throw std::runtime_error("external-output-size");
			const auto path=f.external.ipc+L"/"+name, temporary=path+L".tmp";
			HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
			if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("external-create-phase");
			DWORD written=0; const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size() && FlushFileBuffers(file);
			CloseHandle(file);
			if (!ok || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("external-publish-phase");
			Log(f,"[ProductLiveExternal] phase=%u session=%llu base=(%.4f,%.4f) no_gt_target_write=1\n",sequence,session,baseX,baseY);
		}

		// 所有对照阶段共用同一只读数值观察器；不保存图像、不写盘、不格式化逐帧日志。
		class DebugNumbers final : public Business::LiveTrace::Observer
		{
		public:
			explicit DebugNumbers(Fixture& fixture) : fixture_(fixture), initialX_(fixture.x), initialY_(fixture.y) {}
			struct Numbers
			{
				std::uint64_t frames = 0, calls = 0, accepted = 0, rejected = 0, awaiting = 0;
				std::uint64_t business = 0, capture = 0;
				bool changedSession = false;
				std::array<double, 14> sum{}, maximum{}; // interval/Read/preprocess/LK/fit/refill/coarse/verify/Update/Redraw/PW/Flush/pixels/Analyze。
				std::array<std::uint64_t, 14> count{};
				double previousTimestamp = 0;
				void Add(std::size_t index, double value) noexcept
				{ sum[index] += value; maximum[index] = (std::max)(maximum[index], value); ++count[index]; }
			};
			struct Proof
			{
				RECT area{};
				int sourceX = 0, sourceY = 0, maximumError = 0;
				std::uint64_t session = 0, capture = 0, sequence = 0;
				unsigned checked = 0;
				bool pending = false, completed = false, exact = false;
			};
			void InitialPose(int x, int y) noexcept { std::scoped_lock lock(mutex_); initialX_=x; initialY_=y; }
			void Phase(int phase) noexcept { phase_.store(phase, std::memory_order_release); }
			Numbers Read(int phase) noexcept { std::scoped_lock lock(mutex_); return numbers_[phase]; }
			std::uint64_t Missed() const noexcept { return missed_.load(); }
			void Arm(RECT area, int x, int y) noexcept
			{ std::scoped_lock lock(mutex_); proof_ = {}; proof_.area = area; proof_.sourceX = x; proof_.sourceY = y; proof_.pending = true; }
			Proof ReadProof() noexcept { std::scoped_lock lock(mutex_); return proof_; }
			void Observe(const Business::LiveTrace::Identity& identity, Business::LiveTrace::Operation operation,
				const Inkeys::CV::MotionFrame& input, const Inkeys::CV::MotionResult& result) noexcept override
			{
				std::unique_lock lock(mutex_, std::try_to_lock);
				if (!lock.owns_lock()) { missed_.fetch_add(1); return; }
				// 仅专用实屏夹具的会话首两帧核对独立纹理 GT，定位 F0 新鲜度；不落盘图像。
				if (input.sequence <= 2 && input.image.format == Inkeys::CV::MotionPixelFormat::Bgra32)
				{
					std::uint64_t mismatches = 0; RECT changed{input.image.width,input.image.height,0,0};
					for (int y=0; y<input.image.height; ++y)
					{
						const auto row=reinterpret_cast<const std::uint32_t*>(input.image.data+static_cast<std::size_t>(y)*input.image.strideBytes);
						for (int x=0; x<input.image.width; ++x) if ((row[x]&0xffffffu)!=(TexturePixel(x+identity.roiLeft+initialX_,y+identity.roiTop+initialY_)&0xffffffu))
						{ ++mismatches; changed.left=(std::min)(changed.left,LONG(x));changed.top=(std::min)(changed.top,LONG(y));changed.right=(std::max)(changed.right,LONG(x+1));changed.bottom=(std::max)(changed.bottom,LONG(y+1)); }
					}
					Log(fixture_,"[ScrollFollowDebugProduct] initial_pixel_gt capture=%llu seq=%llu mismatches=%llu bounds=(%ld,%ld,%ld,%ld) templates=%d candidates=%d blocks=%d/%d reason=%s\n",
						identity.captureSession,input.sequence,mismatches,changed.left,changed.top,changed.right,changed.bottom,result.statistics.templateCount,result.statistics.candidateCount,result.statistics.verifiedBlocks,result.statistics.expectedBlocks,Inkeys::CV::MotionReasonName(result.reason));
				}
				const int phase = phase_.load(std::memory_order_acquire);
				if (phase >= 0 && phase < 3)
				{
					auto& n = numbers_[phase];
					if (!n.frames) { n.business = identity.session; n.capture = identity.captureSession; }
					else if (n.business != identity.session || n.capture != identity.captureSession) n.changedSession = true;
					++n.frames;
					if (n.previousTimestamp > 0) n.Add(0, (input.timestampSeconds - n.previousTimestamp) * 1000);
					n.previousTimestamp = input.timestampSeconds;
					const auto capture = LiveCapture::GetSnapshot();
					if (capture.session == identity.captureSession && capture.sequence == input.sequence)
					{
						n.Add(1, capture.statistics.lastReadDuration.count() / 1000.0);
						const auto add = [&](std::size_t index, const LiveCapture::TimedStage& stage) { if (stage.observed) n.Add(index, stage.duration.count()/1000.0); };
						add(8,capture.statistics.update); add(9,capture.statistics.backend.redraw); add(10,capture.statistics.backend.printWindow);
						add(11,capture.statistics.backend.gdiFlush); add(12,capture.statistics.backend.pixelWork); add(13,capture.statistics.analyze);
					}
					if (operation == Business::LiveTrace::Operation::AwaitTracking) ++n.awaiting;
					else
					{
						++n.calls; result.accepted ? ++n.accepted : ++n.rejected;
						const auto& t = result.statistics;
						n.Add(2, t.preprocessMs); n.Add(3, t.opticalFlowMs); n.Add(4, t.fitMs); n.Add(5, t.refillMs); n.Add(6,t.coarseMatchMs); n.Add(7,t.verificationMs);
					}
				}
				if (!proof_.pending) return;
				// 只在计时窗口以外显式请求一次覆盖 ROI 核验，读取实际借用的 Tracker 输入。
				proof_.pending = false; proof_.completed = true;
				proof_.session = identity.session; proof_.capture = identity.captureSession; proof_.sequence = input.sequence;
				const auto& image = input.image;
				const int left = proof_.area.left - identity.roiLeft, top = proof_.area.top - identity.roiTop;
				if (!image.data || image.format != Inkeys::CV::MotionPixelFormat::Bgra32 || left < 0 || top < 0 ||
					left + 256 > image.width || top + 192 > image.height || image.strideBytes < static_cast<std::size_t>(image.width) * 4 ||
					static_cast<std::size_t>(top + 191) * image.strideBytes + static_cast<std::size_t>(left + 256) * 4 > image.sizeBytes) return;
				for (int y = 0; y < 192; ++y) for (int x = 0; x < 256; ++x)
				{
					const auto* pixel = image.data + static_cast<std::size_t>(top + y) * image.strideBytes + static_cast<std::size_t>(left + x) * 4;
					const auto expected = TexturePixel(proof_.area.left + x + proof_.sourceX, proof_.area.top + y + proof_.sourceY);
					const int error = (std::max)({std::abs(int(pixel[0]) - int(expected & 255)),
						std::abs(int(pixel[1]) - int((expected >> 8) & 255)), std::abs(int(pixel[2]) - int((expected >> 16) & 255))});
					proof_.maximumError = (std::max)(proof_.maximumError, error); ++proof_.checked;
				}
				proof_.exact = proof_.checked == 256 * 192 && proof_.maximumError <= 3;
			}
		private:
			std::mutex mutex_;
			std::array<Numbers, 3> numbers_{};
			Proof proof_{};
			Fixture& fixture_; int initialX_=0,initialY_=0;
			std::atomic<int> phase_ = -1;
			std::atomic<std::uint64_t> missed_{};
		};

		class MotionTrace final : public Business::LiveTrace::Observer
		{
			using Operation = Business::LiveTrace::Operation;
			using Identity = Business::LiveTrace::Identity;
			struct Entry
			{
				Identity identity; Operation operation; Inkeys::CV::MotionResult result;
				std::uint64_t sequence = 0; double timestamp = 0, copyMs = 0;
				std::size_t originalStride = 0, originalBytes = 0;
				std::vector<std::uint8_t> gray;
			};
			HANDLE edge_ = nullptr, runner_ = nullptr;
			DWORD edgePid_ = 0;
			std::uint64_t created_ = 0, generation_ = 0;
			std::wstring image_, nonce_, outputRoot_;
			std::string run_;
			HWND main_ = nullptr, content_ = nullptr, drawpad_ = nullptr, presentation_ = nullptr;
			POINT contentOrigin_{}; SIZE contentSize_{}; RECT roi_{}, workArea_{};
			int width_ = 0, height_ = 0;
			UINT dpiX_ = 0, dpiY_ = 0; DWORD orientation_ = 0; HMONITOR monitor_ = nullptr;
			std::vector<Entry> entries_;
			std::size_t pixelBytes_ = 0;
			bool limitReached_ = false; const char* stopReason_ = "SessionEnded";
			std::uint64_t observedCount_ = 0;
			std::mutex mutex_;
			std::atomic<const char*> failure_ = nullptr;
			std::atomic<bool> unsafe_ = false;
			static bool Contains(const RECT& a, const RECT& b) noexcept
			{ return b.left>=a.left && b.top>=a.top && b.right<=a.right && b.bottom<=a.bottom; }
			void Fail(const char* reason) noexcept { const char* empty=nullptr; (void)failure_.compare_exchange_strong(empty,reason); }
			const char* Guard(bool allowOverlay) const noexcept
			{
				// 保存时offSignal已置位；这里独立核实际桌面，不复用会拒绝正常shutdown的Fixture门。
				if (LiveSecurityInterfacePresent()) return "trace-security-interface";
				wchar_t current[128]{}, input[128]{}; DWORD bytes=0;
				const bool currentRead=GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,current,sizeof(current),&bytes)!=FALSE;
				HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
				const bool inputRead=desktop && GetUserObjectInformationW(desktop,UOI_NAME,input,sizeof(input),&bytes)!=FALSE;
				if (desktop) CloseDesktop(desktop);
				if (!currentRead || !inputRead || CompareStringOrdinal(current,-1,input,-1,TRUE)!=CSTR_EQUAL) return "trace-desktop-unavailable";
				if (WaitForSingleObject(edge_,0)!=WAIT_TIMEOUT || WaitForSingleObject(runner_,0)!=WAIT_TIMEOUT) return "trace-held-process-exited";
				FILETIME created{},exited{},kernel{},user{}; wchar_t image[MAX_PATH]{}; DWORD length=MAX_PATH;
				if (!GetProcessTimes(edge_,&created,&exited,&kernel,&user) ||
					((static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime)!=created_ ||
					!QueryFullProcessImageNameW(edge_,0,image,&length) || CompareStringOrdinal(image,-1,image_.c_str(),-1,TRUE)!=CSTR_EQUAL) return "trace-held-edge-identity";
				DWORD mainPid=0,contentPid=0; POINT origin{}; RECT content{},window{},client{};
				if (!IsWindow(main_) || !IsWindow(content_) || !IsWindowVisible(main_) || !IsWindowVisible(content_) ||
					!GetWindowThreadProcessId(main_,&mainPid) || !GetWindowThreadProcessId(content_,&contentPid) || mainPid!=edgePid_ || contentPid!=edgePid_ ||
					GetAncestor(content_,GA_ROOT)!=main_ || !ClientToScreen(content_,&origin) || !GetClientRect(content_,&content) ||
					origin.x!=contentOrigin_.x || origin.y!=contentOrigin_.y || content.right!=contentSize_.cx || content.bottom!=contentSize_.cy ||
					!GetWindowRect(main_,&window) || !GetClientRect(main_,&client)) return "trace-owned-window-geometry";
				POINT clientOrigin{},clientEnd{client.right,client.bottom};
				if (!ClientToScreen(main_,&clientOrigin) || !ClientToScreen(main_,&clientEnd)) return "trace-main-client-geometry";
				const RECT clientScreen{clientOrigin.x,clientOrigin.y,clientEnd.x,clientEnd.y};
				if (!Contains(window,roi_) || !Contains(clientScreen,roi_)) return "trace-roi-outside-owned-main";
				wchar_t title[160]{}; const int titleLength=GetWindowTextW(main_,title,static_cast<int>(std::size(title)));
				const std::wstring_view titleView(title,static_cast<std::size_t>((std::max)(0,titleLength)));
				if (titleView.find(nonce_)==std::wstring_view::npos || !titleView.ends_with(L" - [Guest]")) return "trace-guest-title-unsupported";
				const auto display=Inkeys::Display::GetSnapshot(); const auto* primary=display ? display->Primary() : nullptr;
				if (!primary || display->fallback || primary->fallback || display->generation!=generation_ || primary->handle!=monitor_ ||
					primary->bounds.left || primary->bounds.top || primary->pixelWidth!=width_ || primary->pixelHeight!=height_ ||
					primary->effectiveDpiX!=dpiX_ || primary->effectiveDpiY!=dpiY_ || primary->orientation!=orientation_ ||
					GetSystemMetrics(SM_CXSCREEN)!=width_ || GetSystemMetrics(SM_CYSCREEN)!=height_) return "trace-display-geometry-changed";
				for (int y=0;y<5;++y) for (int x=0;x<5;++x)
				{
					const POINT point{roi_.left+(roi_.right-roi_.left-1)*x/4,roi_.top+(roi_.bottom-roi_.top-1)*y/4};
					const HWND hit=WindowFromPoint(point); DWORD pid=0; GetWindowThreadProcessId(hit,&pid);
					const bool browser=pid==edgePid_ && GetAncestor(hit,GA_ROOT)==main_;
					const bool overlay=allowOverlay && pid==GetCurrentProcessId() && (hit==drawpad_ || hit==presentation_);
					if (!browser && !overlay) return "trace-unknown-roi-occlusion";
				}
				return nullptr;
			}
			static void WriteNew(const std::wstring& path, const void* first, DWORD firstSize, const void* second=nullptr, DWORD secondSize=0)
			{
				const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
				if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("trace-create-new-file");
				DWORD written=0; bool ok=WriteFile(file,first,firstSize,&written,nullptr) && written==firstSize;
				if (ok && secondSize) ok=WriteFile(file,second,secondSize,&written,nullptr) && written==secondSize;
				if (ok) ok=FlushFileBuffers(file)!=FALSE;
				CloseHandle(file); if (!ok) throw std::runtime_error("trace-write-file");
			}
			static Json::Value Stats(const Inkeys::CV::MotionStatistics& s)
			{
				Json::Value v; v["inputSequence"]=std::to_string(s.inputSequence); v["referenceSequence"]=std::to_string(s.referenceSequence);
				v["referenceGeneration"]=std::to_string(s.referenceGeneration); v["relayGeneration"]=std::to_string(s.relayGeneration);
#define TRACE_INT(name) v[#name]=s.name
				TRACE_INT(attempted); TRACE_INT(forward); TRACE_INT(backward); TRACE_INT(fbValid); TRACE_INT(inliers);
				TRACE_INT(referenceCells); TRACE_INT(occupiedCells); TRACE_INT(inlierCells); TRACE_INT(interiorCells);
				TRACE_INT(added); TRACE_INT(retained); TRACE_INT(retired); TRACE_INT(initialSurvivors); TRACE_INT(consecutiveRejects);
#undef TRACE_INT
#define TRACE_REAL(name) v[#name]=s.name
				TRACE_REAL(pointInlierRatio); TRACE_REAL(spatialInlierRatio); TRACE_REAL(survivalRatio); TRACE_REAL(coverageX); TRACE_REAL(coverageY); TRACE_REAL(runnerUpRatio);
				TRACE_REAL(fbMedian); TRACE_REAL(fbP95); TRACE_REAL(residualMedian); TRACE_REAL(residualP95); TRACE_REAL(affineVariation); TRACE_REAL(affineResidual);
				TRACE_REAL(preprocessMs); TRACE_REAL(opticalFlowMs); TRACE_REAL(fitMs); TRACE_REAL(refillMs);
#undef TRACE_REAL
				return v;
			}
		public:
			explicit MotionTrace(const Fixture& f)
			{
				edgePid_=f.external.edgePid; created_=ExternalId(f.external.identity,"edgeCreated");
				const auto image=ExternalString(f.external.identity,"edgeImage"); image_=std::filesystem::path(std::u8string(image.begin(),image.end())).wstring();
				run_=ExternalString(f.external.identity,"run"); nonce_=std::wstring(run_.begin(),run_.end()); outputRoot_=f.root;
				main_=f.external.root; content_=f.external.content; contentOrigin_=f.external.origin; contentSize_=f.external.size;
				drawpad_=Inkeys::Window::GetService().Handle(Role::Drawpad); presentation_=Inkeys::Window::GetService().Handle(Role::DrawpadPresentation);
				const auto filters=BuildCurrentFilterList();
				if (!drawpad_ || !presentation_ || std::find(filters.begin(),filters.end(),drawpad_)==filters.end() || std::find(filters.begin(),filters.end(),presentation_)==filters.end()) throw std::runtime_error("trace-known-overlays-not-excluded");
				const auto display=Inkeys::Display::GetSnapshot(); const auto* primary=display ? display->Primary() : nullptr;
				if (!primary || display->fallback || primary->fallback) throw std::runtime_error("trace-primary-unavailable");
				generation_=display->generation; width_=f.width; height_=f.height; monitor_=f.mouseMonitor; dpiX_=f.mouseDpiX; dpiY_=f.mouseDpiY; orientation_=f.mouseOrientation;
				workArea_=primary->workArea;
				// 原定格buffer高为主屏高减1；不改Mag几何，也不由Source矩形推导缩放。
				roi_={(std::max)(0L,workArea_.left),(std::max)(0L,workArea_.top),(std::min)(static_cast<LONG>(width_),workArea_.right),(std::min)(static_cast<LONG>(height_-1),workArea_.bottom)};
				if (roi_.right<=roi_.left || roi_.bottom<=roi_.top) throw std::runtime_error("trace-empty-roi");
				entries_.reserve(64);
				try
				{
					if (!DuplicateHandle(GetCurrentProcess(),f.external.edge,GetCurrentProcess(),&edge_,0,FALSE,DUPLICATE_SAME_ACCESS) ||
						!DuplicateHandle(GetCurrentProcess(),f.external.runner,GetCurrentProcess(),&runner_,0,FALSE,DUPLICATE_SAME_ACCESS)) throw std::runtime_error("trace-duplicate-owned-handles");
					if (const auto reason=Guard(true)) throw std::runtime_error(reason);
				}
				catch (...) { if(edge_)CloseHandle(edge_); if(runner_)CloseHandle(runner_); throw; }
			}
			~MotionTrace() override { if(edge_)CloseHandle(edge_); if(runner_)CloseHandle(runner_); }
			const char* Failure() const noexcept { return failure_.load(); }
			void Observe(const Identity& identity, Operation operation, const Inkeys::CV::MotionFrame& input, const Inkeys::CV::MotionResult& result) noexcept override
			{
				try
				{
					std::scoped_lock lock(mutex_); if (Failure() || limitReached_) return;
					const auto nextPixels=static_cast<std::uint64_t>((std::max)(0,input.image.width))*static_cast<std::uint64_t>((std::max)(0,input.image.height));
					if (entries_.size()>=64 || nextPixels>256ull*1024*1024-pixelBytes_ || (!entries_.empty() && input.timestampSeconds-entries_.front().timestamp>=10))
					{ limitReached_=true; stopReason_=entries_.size()>=64?"FrameLimit":nextPixels>256ull*1024*1024-pixelBytes_?"ByteLimit":"TimeLimit"; return; }
					++observedCount_;
					const auto begin=std::chrono::steady_clock::now();
					if (const auto reason=Guard(true)) { unsafe_.store(true); Fail(reason); return; }
					const LiveCapture::Geometry geometry{{0,0,width_,height_},
						{identity.sourceLeft,identity.sourceTop,identity.sourceRight,identity.sourceBottom},
						{identity.workAreaLeft,identity.workAreaTop,identity.workAreaRight,identity.workAreaBottom},
						identity.displayGeneration,identity.sourceWidth,identity.sourceHeight};
					if (!LiveCapture::IsValidGeometry(geometry) || identity.displayGeneration!=generation_ ||
						identity.workAreaLeft!=workArea_.left || identity.workAreaTop!=workArea_.top || identity.workAreaRight!=workArea_.right || identity.workAreaBottom!=workArea_.bottom ||
						identity.roiLeft!=roi_.left || identity.roiTop!=roi_.top || identity.pixelsPerPixelX!=1 || identity.pixelsPerPixelY!=1 ||
						input.image.width!=roi_.right-roi_.left || input.image.height!=roi_.bottom-roi_.top || input.image.format!=Inkeys::CV::MotionPixelFormat::Bgra32 ||
						!input.image.data || !std::isfinite(input.timestampSeconds) || input.timestampSeconds<0 || !input.captureValid || !identity.session || !identity.captureSession)
					{ Fail("trace-input-geometry-or-identity"); return; }
					const int left=(std::max)(0,identity.workAreaLeft-identity.sourceLeft), top=(std::max)(0,identity.workAreaTop-identity.sourceTop);
					const int right=(std::min)(identity.sourceWidth,identity.workAreaRight-identity.sourceLeft), bottom=(std::min)(identity.sourceHeight,identity.workAreaBottom-identity.sourceTop);
					const auto stride=static_cast<std::size_t>(identity.sourceWidth)*4;
					const auto offset=static_cast<std::size_t>(top)*stride+static_cast<std::size_t>(left)*4;
					if (identity.roiLeft!=identity.sourceLeft+left || identity.roiTop!=identity.sourceTop+top || input.image.width!=right-left || input.image.height!=bottom-top ||
						input.image.strideBytes!=stride || input.image.sizeBytes!=static_cast<std::size_t>(identity.sourceHeight)*stride-offset)
					{ Fail("trace-original-roi-clamp-or-span"); return; }
					if (entries_.empty() ? (operation!=Operation::Initialize || input.sequence!=1) :
						(input.sequence!=entries_.back().sequence+1 || input.timestampSeconds<entries_.back().timestamp || identity.session!=entries_.front().identity.session || identity.captureSession!=entries_.front().identity.captureSession))
					{ Fail("trace-prefix-sequence-or-session-gap"); return; }
					const auto pixels=static_cast<std::size_t>(input.image.width)*input.image.height;
					if (entries_.size()>=64 || pixels>256ull*1024*1024-pixelBytes_) { Fail("trace-frame-or-memory-limit"); return; }
					const auto row=static_cast<std::size_t>(input.image.width)*4;
					if (input.image.strideBytes<row || input.image.strideBytes>input.image.sizeBytes ||
						static_cast<std::size_t>(input.image.height-1)>(input.image.sizeBytes-row)/input.image.strideBytes)
					{ Fail("trace-input-buffer-size"); return; }
					Entry entry; entry.identity=identity; entry.operation=operation; entry.result=result; entry.sequence=input.sequence; entry.timestamp=input.timestampSeconds;
					entry.originalStride=input.image.strideBytes; entry.originalBytes=input.image.sizeBytes; entry.gray.resize(pixels);
					const cv::Mat source(input.image.height,input.image.width,CV_8UC4,const_cast<std::uint8_t*>(input.image.data),input.image.strideBytes);
					cv::Mat gray(input.image.height,input.image.width,CV_8UC1,entry.gray.data(),static_cast<std::size_t>(input.image.width));
					cv::cvtColor(source,gray,cv::COLOR_BGRA2GRAY); // 与Tracker同版本、同转换，只保留完整分辨率，未预缩小。
					if (const auto reason=Guard(true)) { unsafe_.store(true); Fail(reason); return; }
					entry.copyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
					entries_.push_back(std::move(entry)); pixelBytes_+=pixels;
				}
				catch (...) { Fail("trace-copy-or-conversion-exception"); }
			}
			bool Save(bool drained, int productResult)
			{
				std::scoped_lock lock(mutex_);
				if (!drained) Fail("trace-original-drain-unproven");
				if (productResult==4) { unsafe_.store(true); Fail("trace-fixture-security"); }
				if (entries_.empty()) Fail("trace-no-initialize-prefix");
				const char* saveGuard=Guard(false); if (saveGuard) { unsafe_.store(true); Fail(saveGuard); }
				for (auto path=std::filesystem::path(outputRoot_); !path.empty(); path=path.parent_path())
				{
					const DWORD a=GetFileAttributesW(path.c_str());
					if (a==INVALID_FILE_ATTRIBUTES || !(a&FILE_ATTRIBUTE_DIRECTORY) || (a&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("trace-save-root-not-ordinary");
					if (path==path.root_path()) break;
				}
				Json::Value manifest; manifest["schema"]=1; manifest["kind"]="actual-tracker-input-fullres-gray8"; manifest["productExitBeforeTraceSave"]=productResult;
				manifest["run"]=run_; manifest["frameLimit"]=64; manifest["pixelByteLimit"]=std::to_string(256ull*1024*1024); manifest["secondsLimit"]=10; manifest["stopReason"]=stopReason_; manifest["boundedPrefix"]=true;
				manifest["recordedCount"]=static_cast<unsigned>(entries_.size()); manifest["observedCount"]=std::to_string(observedCount_);
				manifest["skippedInputs"]=std::to_string(observedCount_-entries_.size());
				manifest["pixelBytes"]=std::to_string(pixelBytes_); manifest["originalThreadsDrained"]=drained; manifest["frames"]=Json::arrayValue;
				for (std::size_t index=0;index<entries_.size();++index)
				{
					const auto& e=entries_[index]; Json::Value v; const auto& s=e.result;
					v["session"]=std::to_string(e.identity.session); v["captureSession"]=std::to_string(e.identity.captureSession); v["displayGeneration"]=std::to_string(e.identity.displayGeneration);
					v["sequence"]=std::to_string(e.sequence); v["timestampSeconds"]=e.timestamp; v["operation"]=e.operation==Operation::Initialize ? "Initialize" : e.operation==Operation::Process ? "Process" : "AwaitTracking";
					v["captureValid"]=true; v["sourceLeft"]=e.identity.sourceLeft; v["sourceTop"]=e.identity.sourceTop; v["sourceWidth"]=e.identity.sourceWidth; v["sourceHeight"]=e.identity.sourceHeight; v["roiLeft"]=static_cast<int>(roi_.left); v["roiTop"]=static_cast<int>(roi_.top); v["width"]=static_cast<int>(roi_.right-roi_.left); v["height"]=static_cast<int>(roi_.bottom-roi_.top);
					v["sourceRect"]["left"]=e.identity.sourceLeft; v["sourceRect"]["top"]=e.identity.sourceTop;
					v["sourceRect"]["right"]=e.identity.sourceRight; v["sourceRect"]["bottom"]=e.identity.sourceBottom;
					v["workArea"]["left"]=e.identity.workAreaLeft; v["workArea"]["top"]=e.identity.workAreaTop;
					v["workArea"]["right"]=e.identity.workAreaRight; v["workArea"]["bottom"]=e.identity.workAreaBottom;
					v["sourcePixelsPerPixelX"]=e.identity.pixelsPerPixelX; v["sourcePixelsPerPixelY"]=e.identity.pixelsPerPixelY;
					v["originalFormat"]="Bgra32"; v["recordedFormat"]="Gray8-full-resolution"; v["originalStrideBytes"]=std::to_string(e.originalStride); v["originalSizeBytes"]=std::to_string(e.originalBytes);
					v["observerCopyMilliseconds"]=e.copyMs; v["accepted"]=s.accepted; v["state"]=static_cast<unsigned>(s.state); v["reason"]=Inkeys::CV::MotionReasonName(s.reason);
					v["deltaX"]=s.delta.x; v["deltaY"]=s.delta.y; v["totalX"]=s.total.x; v["totalY"]=s.total.y; v["confidence"]=s.confidence; v["statistics"]=Stats(s.statistics);
					v["fileComplete"]=false;
					if (drained && !saveGuard && !unsafe_.load())
					{
						if (const auto reason=Guard(false)) { unsafe_.store(true); Fail(reason); saveGuard=reason; }
						else try
						{
							const auto name="motion-"+std::to_string(index+1)+".pgm"; const auto header="P5\n"+std::to_string(roi_.right-roi_.left)+" "+std::to_string(roi_.bottom-roi_.top)+"\n255\n";
							WriteNew(outputRoot_+L"/"+std::wstring(name.begin(),name.end()),header.data(),static_cast<DWORD>(header.size()),e.gray.data(),static_cast<DWORD>(e.gray.size()));
							v["file"]=name; v["fileComplete"]=true;
						}
						catch (...) { Fail("trace-image-file-write-failed"); saveGuard="trace-image-file-write-failed"; }
					}
					manifest["frames"].append(v);
				}
				manifest["complete"]=Failure()==nullptr; manifest["failure"]=Failure() ? Failure() : "";
				Json::StreamWriterBuilder writer; writer["indentation"]=""; writer["precision"]=17; const auto json=Json::writeString(writer,manifest);
				if (json.size()>512*1024) throw std::runtime_error("trace-manifest-size-limit");
				WriteNew(outputRoot_+L"/motion-trace.json",json.data(),static_cast<DWORD>(json.size()));
				return Failure()==nullptr;
			}
		};

		void CheckFixture(Fixture& f)
		{
			CheckExternalWindow(f);
			if (f.trace) if (const auto reason=f.trace->Failure()) throw std::runtime_error(reason);
			if (f.mouseDpiX)
			{
				const auto display = Inkeys::Display::GetSnapshot();
				const auto* primary = display ? display->Primary() : nullptr;
				if (!primary || display->fallback || primary->fallback || primary->handle != f.mouseMonitor ||
					primary->bounds.left || primary->bounds.top || primary->pixelWidth != f.width || primary->pixelHeight != f.height ||
					primary->effectiveDpiX != f.mouseDpiX || primary->effectiveDpiY != f.mouseDpiY || primary->orientation != f.mouseOrientation ||
					GetSystemMetrics(SM_CXSCREEN) != f.width || GetSystemMetrics(SM_CYSCREEN) != f.height)
					throw std::runtime_error("native-mouse-geometry-or-dpi-changed");
			}
			if (f.sourceCancelled || f.sourceFailed || f.stateFailed.load() || f.magFailed.load() || offSignal)
			{
				Log(f, "[ProductLive] fixture_failure source_cancelled=%u source_failed=%u state_failed=%u mag_failed=%u off_signal=%d paint_sequence=%u wheels=%u tagged=%u foreign=%u state_tid=%lu mag_tid=%lu\n",
					f.sourceCancelled, f.sourceFailed, f.stateFailed.load(), f.magFailed.load(), static_cast<int>(offSignal),
					f.paintSequence, f.wheelMessages, f.taggedWheels, f.foreignWheels, f.stateTid.load(), f.magTid.load());
				throw std::runtime_error("fixture-owner-failure-or-cancel");
			}
		}

		void CheckDesktop(Fixture& f, const char* stage)
		{
			if (LiveSecurityInterfacePresent() || !LiveDesktopAvailable(stage))
			{
				// 识别安全层即撤销旧显示事务，不等StateMonitoring/同步PW退出。
				f.security = true; Inkeys::Window::GetService().BeginShutdown();
				throw std::runtime_error("interactive-desktop-or-security-layer");
			}
			CheckFixture(f);
		}

		template<class Predicate>
		bool Wait(Fixture& f, Predicate&& predicate, DWORD milliseconds)
		{
			const auto end = GetTickCount64() + milliseconds;
			do
			{
				if (LiveSecurityInterfacePresent())
				{
					f.security = true; Inkeys::Window::GetService().BeginShutdown();
					throw std::runtime_error("security-layer-appeared");
				}
				CheckFixture(f);
				MSG message{};
				while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
				{
					if (message.message == WM_QUIT)
					{
						Log(f, "[ProductLive] source_cancel origin=thread-quit hwnd=%p wp=%llu lp=%lld\n",
							message.hwnd, static_cast<unsigned long long>(message.wParam), static_cast<long long>(message.lParam));
						f.sourceCancelled = true; break;
					}
					TranslateMessage(&message); DispatchMessageW(&message);
				}
				// Close/Esc/外来滚轮可能刚在Dispatch内到达，不能让同次predicate掩盖取消。
				CheckFixture(f);
				if (predicate()) return true;
				(void)MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
			} while (GetTickCount64() < end);
			return false;
		}

		void LogGtHit(Fixture& f, const char* stage, const char* area, int x, int y, COLORREF actual, COLORREF expected)
		{
			// 只记既有取样RGB与窗口元数据，不记录标题、不另截未知区域。
			const HWND hit = WindowFromPoint({x,y});
			DWORD pid = 0; GetWindowThreadProcessId(hit,&pid);
			wchar_t className[128]{}; (void)GetClassNameW(hit,className,static_cast<int>(std::size(className)));
			RECT rect{}; const bool rectOk = GetWindowRect(hit,&rect) != FALSE;
			auto& service = Inkeys::Window::GetService();
			Log(f,"[ProductLive] gt_mismatch stage=%s area=%s point=(%d,%d) actual_rgb=(%u,%u,%u) expected_rgb=(%u,%u,%u) hit_now=%p pid=%lu class=%ls own_source=%u drawpad=%u presentation=%u mag_host=%u mag_child=%u rect_ok=%u rect=(%ld,%ld,%ld,%ld)\n",
				stage,area,x,y,GetRValue(actual),GetGValue(actual),GetBValue(actual),GetRValue(expected),GetGValue(expected),GetBValue(expected),
				hit,pid,className,hit==f.source,hit==service.Handle(Role::Drawpad),hit==service.Handle(Role::DrawpadPresentation),
				hit==service.Handle(Role::MagnifierHost),hit==service.Handle(Role::MagnifierChild),rectOk,rect.left,rect.top,rect.right,rect.bottom);
		}

		bool ScreenSourceGt(Fixture& f, const char* stage, const wchar_t* imageName)
		{
			CheckDesktop(f, stage);
			if (!f.paintSequence || f.paintedX != f.x || f.paintedY != f.y) return false;
			LARGE_INTEGER started{}, copiedAt{}, checkedAt{}, frequency{};
			QueryPerformanceCounter(&started); QueryPerformanceFrequency(&frequency);
			// 全屏仅暂存内存；一次读回后仍核原99点和精确ROI，避免每个GetPixel都同步桌面。
			Inkeys::Graphics::DibSurface snapshot;
			const HDC screen = GetDC(nullptr);
			const bool copied = screen && snapshot.resize(f.width,f.height) &&
				BitBlt(snapshot.dc(),0,0,f.width,f.height,screen,0,0,SRCCOPY) && GdiFlush();
			if (screen) ReleaseDC(nullptr,screen);
			QueryPerformanceCounter(&copiedAt);
			const auto readUs = frequency.QuadPart>0 ? (copiedAt.QuadPart-started.QuadPart)*1000000/frequency.QuadPart : -1;
			if (!copied)
			{
				Log(f,"[ProductLive] screen_gt stage=%s samples=0/99 dedicated_roi_exact=0 roi=(64,64,256,192) batch_copy=0 read_us=%lld verify_us=0\n",stage,readUs);
				return false;
			}
			CheckDesktop(f, "after-gt-screen-read");
			const auto pixels = snapshot.pixels();
			RECT debuggerBounds{}; bool debuggerVisible = false; unsigned debuggerPixels = 0;
			const HWND debugger = f.debuggerProbe ? Inkeys::Window::GetService().Handle(Role::ScrollFollowDebug) : nullptr;
			if (debugger)
			{
				const auto probe = DebugWindow::ReadProbe(); POINT origin{};
				if (!IsCurrentProcessWindow(debugger) || !IsWindowVisible(debugger) || !GetWindowRect(debugger, &debuggerBounds) ||
					!ClientToScreen(debugger, &origin) || !probe.open || !probe.paints ||
					!(GetWindowLongPtrW(debugger, GWL_EXSTYLE) & WS_EX_TOPMOST) || GetWindow(debugger, GW_OWNER)) return false;
				const POINT sample{origin.x + probe.paintSamplePoint.x, origin.y + probe.paintSamplePoint.y};
				if (sample.x < 1 || sample.y < 1 || sample.x + 1 >= f.width || sample.y + 1 >= f.height) return false;
				for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
				{
					const auto pixel = pixels[static_cast<std::size_t>(sample.y + y) * f.width + sample.x + x];
					debuggerPixels += RGB((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255) == probe.paintSampleColor;
				}
				debuggerVisible = debuggerPixels == 9 && WindowFromPoint(sample) == debugger;
			}
			unsigned matched = 0, total = 0, mismatchCount = 0;
			struct Mismatch { int x{}, y{}; COLORREF actual{}, expected{}; };
			Mismatch mismatches[3]{};
			for (int row = 0; row < 9; ++row) for (int col = 0; col < 11; ++col)
			{
				const int x = (2 * col + 1) * f.width / 22, y = (2 * row + 1) * f.height / 18;
				// 只排除当前已证实属于本进程的诊断窗外框，其余专用内容仍逐点核验。
				if (debugger && PtInRect(&debuggerBounds, {x, y})) continue;
				++total;
				const auto p = TexturePixel(x + f.x, y + f.y);
				const auto sampled = pixels[static_cast<std::size_t>(y)*f.width+x];
				const COLORREF actual = RGB((sampled>>16)&255,(sampled>>8)&255,sampled&255), expected = RGB((p >> 16) & 255,(p >> 8) & 255,p & 255);
				matched += actual == expected;
				if (actual != expected && mismatchCount < std::size(mismatches)) mismatches[mismatchCount++] = {x,y,actual,expected};
			}
			bool exact = true;
			Mismatch cropMismatch{}; bool cropPixelMismatch = false;
			for (int y = 0; y < 192 && exact; ++y) for (int x = 0; x < 256; ++x)
			{
				const auto actual = pixels[static_cast<std::size_t>(y+64)*f.width+x+64] & 0xffffffu;
				const auto expected = TexturePixel(x + 64 + f.x,y + 64 + f.y) & 0xffffffu;
				if (actual != expected)
				{
					cropMismatch = {x+64,y+64,RGB((actual>>16)&255,(actual>>8)&255,actual&255),RGB((expected>>16)&255,(expected>>8)&255,expected&255)};
					cropPixelMismatch = true; exact = false; break;
				}
			}
			QueryPerformanceCounter(&checkedAt);
			const auto verifyUs = frequency.QuadPart>0 ? (checkedAt.QuadPart-copiedAt.QuadPart)*1000000/frequency.QuadPart : -1;
			if (f.debuggerProbe)
				Log(f, "[ProductLive] screen_gt stage=%s samples=%u/%u dedicated_roi_exact=%u roi=(64,64,256,192) debugger=%p debugger_visible=%u debugger_pixels=%u/9 excluded_owned_bounds=(%ld,%ld,%ld,%ld) batch_copy=1 read_us=%lld verify_us=%lld bytes=%zu\n", stage, matched, total, exact, debugger, debuggerVisible, debuggerPixels, debuggerBounds.left, debuggerBounds.top, debuggerBounds.right, debuggerBounds.bottom, readUs, verifyUs, pixels.size_bytes());
			else
				Log(f, "[ProductLive] screen_gt stage=%s samples=%u/99 dedicated_roi_exact=%u roi=(64,64,256,192) batch_copy=1 read_us=%lld verify_us=%lld bytes=%zu\n", stage, matched, exact, readUs, verifyUs, pixels.size_bytes());
			// 未知画面不落盘；离散99点只作全屏抽样，另存的ROI必须每个像素都属于专用纹理。
			if (!total || matched != total || !exact || (debugger && !debuggerVisible))
			{
				RECT sourceRect{}; const bool rectOk = GetWindowRect(f.source,&sourceRect) != FALSE;
				Log(f,"[ProductLive] gt_source stage=%s paint_sequence=%u offset=(%d,%d) painted=(%d,%d) painted_qpc=%lld visible_now=%u rect_ok=%u rect=(%ld,%ld,%ld,%ld)\n",
					stage,f.paintSequence,f.x,f.y,f.paintedX,f.paintedY,f.paintedAt.QuadPart,IsWindowVisible(f.source)!=FALSE,rectOk,
					sourceRect.left,sourceRect.top,sourceRect.right,sourceRect.bottom);
				for (unsigned i=0;i<mismatchCount;++i) LogGtHit(f,stage,"grid",mismatches[i].x,mismatches[i].y,mismatches[i].actual,mismatches[i].expected);
				if (cropPixelMismatch) LogGtHit(f,stage,"existing-crop",cropMismatch.x,cropMismatch.y,cropMismatch.actual,cropMismatch.expected);
				return false;
			}
			if (!imageName) return true;
			Inkeys::Graphics::DibSurface crop;
			if (!crop.resize(256,192)) return false;
			for (int y=0;y<192;++y) for (int x=0;x<256;++x)
				crop.pixels()[static_cast<std::size_t>(y)*256+x] = pixels[static_cast<std::size_t>(y+64)*f.width+x+64] | 0xff000000u;
			return crop.savePng(f.root + L"/" + imageName);
		}

		struct InkView
		{
			RECT roi{};
			std::vector<std::uint8_t> core;
			std::size_t count = 0;
		};

		void MoveOnOwnedWindow(Fixture& f, HWND target, POINT destination, bool sourceForeground)
		{
			CheckDesktop(f, "before-ink-os-move");
			if (!IsCurrentProcessWindow(target) || WindowFromPoint(destination) != target ||
				(sourceForeground && GetForegroundWindow() != f.source)) throw std::runtime_error("ink-move-target");
			const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
			if (w < 2 || h < 2) throw std::runtime_error("ink-virtual-screen");
			INPUT move{}; move.type = INPUT_MOUSE; move.mi.dwExtraInfo = inputTag;
			move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
			move.mi.dx = static_cast<LONG>(static_cast<std::int64_t>(destination.x - GetSystemMetrics(SM_XVIRTUALSCREEN)) * 65535 / (w - 1));
			move.mi.dy = static_cast<LONG>(static_cast<std::int64_t>(destination.y - GetSystemMetrics(SM_YVIRTUALSCREEN)) * 65535 / (h - 1));
			if (SendInput(1, &move, sizeof(INPUT)) != 1) throw std::runtime_error("ink-os-move");
			if (!Wait(f, [&] { POINT p{}; return GetCursorPos(&p) && std::abs(p.x - destination.x) <= 2 && std::abs(p.y - destination.y) <= 2; }, 1000))
				throw std::runtime_error("ink-os-cursor");
			CheckDesktop(f, "after-ink-os-move");
			POINT actual{};
			if (!GetCursorPos(&actual) || WindowFromPoint(actual) != target ||
				(sourceForeground && GetForegroundWindow() != f.source)) throw std::runtime_error("ink-actual-hit-changed");
		}

		bool PrimaryReady() noexcept
		{
			const auto r = Draw::ProductRuntimeSnapshot();
			return r.firstFrameReady && !r.selectionMode && r.lastPresentSucceeded &&
				r.requestedOutputTarget == Draw::HostOutputTarget::PrimaryDrawpad && r.readyOutputTarget == r.requestedOutputTarget &&
				r.readyOutputRevision == r.requestedOutputRevision && r.presentedContentRevision == r.contentRevision;
		}

		void InkToolFacts(Fixture& f, const char* stage, bool widthAttempted, bool widthAccepted,
			bool colorAttempted, bool colorAccepted, bool waited, bool ready)
		{
			// 分开记录同步setter和真实ACK，不能把等待失败误报为RTS未消费。
			const auto mode = GetStateModeVersionedSnapshot();
			const auto desired = Draw::ProductHost().ProductBridge().Snapshot();
			const auto runtime = Draw::ProductRuntimeSnapshot();
			Log(f, "[ProductLiveInk] tool stage=%s width_attempted=%u width_accepted=%u color_attempted=%u color_accepted=%u waited=%u ready=%u mode_revision=%llu mode=%u pen=%u state_width=%.4f state_color=0x%08lx bridge_revision=%llu bridge_tool=%u bridge_width=%.4f bridge_color=0x%08x runtime_revision=%llu primary_ready=%u projection=%llu actual_ack=%u\n",
				stage, widthAttempted, widthAccepted, colorAttempted, colorAccepted, waited, ready,
				mode.revision, static_cast<unsigned>(mode.state.StateModeSelect), static_cast<unsigned>(mode.state.Pen.ModeSelect),
				GetPenWidthMetrics(mode.state).resolvedWidthDip, GetPenColor(mode.state), desired.revision, static_cast<unsigned>(desired.tool),
				desired.widthDip, desired.colorRgba, runtime.runtimeRevision, PrimaryReady(), ScrollFollowProjectionEpoch(),
				ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()));
		}

		POINT DrawOsInk(Fixture& f, POINT first, POINT last, const char* label)
		{
			if (!ScreenSourceGt(f,"before-os-ink",nullptr)) throw std::runtime_error("ink-pre-input-source-gt");
			const auto before = Draw::ProductRuntimeSnapshot();
			const HWND drawpad = Inkeys::Window::GetService().Handle(Role::Drawpad);
			if (!PrimaryReady() || f.ownLeftHeld || (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) throw std::runtime_error("ink-stroke-not-idle");
			MoveOnOwnedWindow(f, drawpad, first, false);
			CheckDesktop(f, "before-ink-down");
			POINT actual{};
			if (!GetCursorPos(&actual) || WindowFromPoint(actual) != drawpad || (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) throw std::runtime_error("ink-down-hit-or-existing-held");
			POINT client = actual;
			if (!ScreenToClient(drawpad, &client)) throw std::runtime_error("ink-down-client");
			RECT clientRect{}; POINT clientOrigin{};
			// 此量化模型只适用于专用鼠标窗的1:1物理像素映射，不外推到任意数位板。
			if (!GetClientRect(drawpad,&clientRect) || !ClientToScreen(drawpad,&clientOrigin) || clientOrigin.x || clientOrigin.y ||
				clientRect.left || clientRect.top || clientRect.right != f.width || clientRect.bottom != f.height || client.x != actual.x || client.y != actual.y)
				throw std::runtime_error("native-mouse-client-mapping");
			INPUT down{}; down.type = INPUT_MOUSE; down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; down.mi.dwExtraInfo = inputTag;
			if (SendInput(1, &down, sizeof(INPUT)) != 1) throw std::runtime_error("ink-os-down");
			f.ownLeftHeld = true;
			for (int step = 1; step <= 16; ++step)
			{
				const auto due = GetTickCount64() + 20;
				(void)Wait(f, [&] { return GetTickCount64() >= due; }, 100);
				MoveOnOwnedWindow(f, drawpad, { first.x + (last.x - first.x) * step / 16, first.y + (last.y - first.y) * step / 16 }, false);
			}
			// 正常Up也确认实际命中；异常时仍由原凭据只归还本夹具的held。
			CheckDesktop(f, "before-ink-up");
			if (!GetCursorPos(&actual) || WindowFromPoint(actual) != drawpad) throw std::runtime_error("ink-up-hit");
			if (!ReleaseOwnLeft(f)) throw std::runtime_error("ink-os-up-unproven");
			const bool completed = Wait(f, [&]
			{
				const auto r = Draw::ProductRuntimeSnapshot();
				return PrimaryReady() && r.currentPageHasContent && r.canUndo && r.completedStrokeKind == Draw::Bridge::CompletedStrokeKind::Drawing &&
					r.inputDownPublished == before.inputDownPublished + 1 && r.inputTerminalPublished == before.inputTerminalPublished + 1 &&
					r.inputRecycled == before.inputRecycled + 1 && r.runtimeMetrics.enabled && !r.runtimeMetrics.unavailable &&
					r.runtimeMetrics.contactSeen == before.runtimeMetrics.contactSeen + 1 && r.successfulPresentCount > before.successfulPresentCount;
			}, 5000);
			const auto r = Draw::ProductRuntimeSnapshot();
			Log(f, "[ProductLiveInk] stroke=%s source=os-sendinput synthetic_sent=0 complete=%u mode=%u down=%llu/%llu terminal=%llu/%llu recycled=%llu/%llu contact=%llu/%llu kind=%u undo=%u ready=%u client_down=(%ld,%ld)\n",
				label, completed, static_cast<unsigned>(r.inputMode), r.inputDownPublished, before.inputDownPublished,
				r.inputTerminalPublished, before.inputTerminalPublished, r.inputRecycled, before.inputRecycled,
				r.runtimeMetrics.contactSeen, before.runtimeMetrics.contactSeen, static_cast<unsigned>(r.completedStrokeKind), r.canUndo, PrimaryReady(), client.x, client.y);
			if (!completed) throw std::runtime_error("real-os-ink-not-consumed-no-synthetic-fallback");
			MoveOnOwnedWindow(f, drawpad, { f.width - 64, f.height / 2 + 48 }, false);
			return client;
		}

		void CheckInkRoi(Fixture& f, const RECT& roi)
		{
			if (roi.right - roi.left < 256 || roi.bottom - roi.top < 96) throw std::runtime_error("ink-roi-too-small");
			const RECT precise{ 64,64,320,256 }; RECT overlap{};
			if (IntersectRect(&overlap, &roi, &precise)) throw std::runtime_error("ink-overlaps-precise-source-roi");
			for (int row = 0; row < 9; ++row) for (int col = 0; col < 11; ++col)
			{
				POINT p{ (2 * col + 1) * f.width / 22, (2 * row + 1) * f.height / 18 };
				if (PtInRect(&roi, p)) throw std::runtime_error("ink-overlaps-source-gt-sample");
			}
		}

		bool KnownInkEdge(int r, int g, int b, int expected, bool blue, double& residual) noexcept
		{
			// 同一覆盖率解释灰底和本笔固定颜色；只接纳<=5%的小覆盖边缘，不放宽强core。
			const double direction[3] = { (blue ? 0.0 : 255.0)-expected,(blue ? 48.0 : 16.0)-expected,(blue ? 255.0 : 0.0)-expected };
			const double actual[3] = {static_cast<double>(r),static_cast<double>(g),static_cast<double>(b)};
			double dot=0, norm=0;
			for (unsigned i=0;i<3;++i) { dot+=(actual[i]-expected)*direction[i]; norm+=direction[i]*direction[i]; }
			const double alpha=std::clamp(dot/norm,0.0,0.05);
			residual=0;
			for (unsigned i=0;i<3;++i) residual=(std::max)(residual,std::abs(actual[i]-(expected+alpha*direction[i])));
			return residual<=1.0;
		}

		InkView ReadInkScreen(Fixture& f, RECT roi, int lineY, bool blue, const wchar_t* imageName)
		{
			if (!ScreenSourceGt(f, "ink-source-gt-unchanged", nullptr)) throw std::runtime_error("ink-source-gt");
			CheckDesktop(f, "before-ink-screen");
			const int w = roi.right - roi.left, h = roi.bottom - roi.top;
			Inkeys::Graphics::DibSurface image;
			const HDC screen = GetDC(nullptr);
			const bool copied = screen && image.resize(w,h) && BitBlt(image.dc(),0,0,w,h,screen,roi.left,roi.top,SRCCOPY) && GdiFlush();
			if (screen) ReleaseDC(nullptr,screen);
			if (!copied) throw std::runtime_error("ink-screen-read");
			InkView value; value.roi = roi; value.core.resize(static_cast<std::size_t>(w) * h);
			unsigned unknown = 0, knownEdges = 0;
			double edgeMaxResidual = 0;
			struct UnknownPixel { int x{},y{},r{},g{},b{},expected{}; bool ink{},core{},corridor{}; };
			UnknownPixel unknownPixels[12]{};
			for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
			{
				const auto i = static_cast<std::size_t>(y)*w+x; const auto p = image.pixels()[i];
				const int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
				const bool ink = blue ? b > r + 4 && b > g + 4 : r > g + 4 && r > b + 4;
				const bool core = blue ? b > 180 && b > r + 80 && b > g + 60 : r > 180 && r > g + 80 && r > b + 80;
				const bool corridor = std::abs(roi.top+y-lineY) <= 24 && x >= 16 && x < w-16;
				const int expected = TexturePixel(roi.left+x+f.x,roi.top+y+f.y) & 255;
				const bool backgroundMismatch = std::abs(r-expected)>3 || std::abs(g-expected)>3 || std::abs(b-expected)>3;
				double edgeResidual = 0;
				const bool knownEdge = corridor && !ink && backgroundMismatch && KnownInkEdge(r,g,b,expected,blue,edgeResidual);
				if (knownEdge) { ++knownEdges; edgeMaxResidual=(std::max)(edgeMaxResidual,edgeResidual); }
				if (!(ink && corridor) && !knownEdge && backgroundMismatch)
				{
					// 只保留既有ROI读回的前12个坏像素，不增加截图或放宽颜色门槛。
					if (unknown < std::size(unknownPixels)) unknownPixels[unknown] = {x,y,r,g,b,expected,ink,core,corridor};
					++unknown;
				}
				value.core[i] = core && corridor; value.count += value.core[i];
			}
			Log(f, "[ProductLiveInk] screen name=%ls roi=(%ld,%ld,%ld,%ld) line_y=%d blue=%u core=%zu unknown=%u known_edge=%u edge_max_residual=%.4f source_offset=(%d,%d)\n",
				imageName,roi.left,roi.top,w,h,lineY,blue,value.count,unknown,knownEdges,edgeMaxResidual,f.x,f.y);
			// 全ROI仅允许已知背景或本笔颜色走廊；失败图不保存，不能拿抽样GT遮掉未知像素。
			if (unknown || value.count < 256)
			{
				for (unsigned n=0;n<(std::min)(unknown,static_cast<unsigned>(std::size(unknownPixels)));++n)
				{
					const auto& p = unknownPixels[n];
					Log(f,"[ProductLiveInk] unknown_pixel image=%ls index=%u screen=(%ld,%ld) local=(%d,%d) rgb=(%d,%d,%d) expected_gray=%d ink=%u core=%u corridor=%u blue_minus=(%d,%d) abs_line_y=%ld source_offset=(%d,%d)\n",
						imageName,n,roi.left+p.x,roi.top+p.y,p.x,p.y,p.r,p.g,p.b,p.expected,p.ink,p.core,p.corridor,p.b-p.r,p.b-p.g,
						std::abs(roi.top+p.y-lineY),f.x,f.y);
				}
				throw std::runtime_error("ink-pixels-or-dedicated-roi-unproven");
			}
			for (auto& p : image.pixels()) p |= 0xff000000u;
			if (!image.savePng(f.root+L"/"+imageName)) throw std::runtime_error("ink-png");
			return value;
		}

		void CompareInk(Fixture& f, const InkView& base, const InkView& now, int dy, const char* stage, int dx = 0)
		{
			LARGE_INTEGER compareStart{}, compareEnd{}, frequency{};
			QueryPerformanceCounter(&compareStart); QueryPerformanceFrequency(&frequency);
			const int w = base.roi.right-base.roi.left, h = base.roi.bottom-base.roi.top;
			if (now.core.size()!=base.core.size()) throw std::runtime_error("ink-mask-size");
			std::vector<double> errors;
			const auto oneWay = [&](const InkView& from,const InkView& to,int shiftX,int shiftY)
			{
				for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(from.core[static_cast<std::size_t>(y)*w+x])
				{
					double best=1000;
					for(int oy=-2;oy<=2;++oy)for(int ox=-2;ox<=2;++ox)
					{
						const int tx=x+shiftX+ox,ty=y+shiftY+oy;
						if(tx>=0 && tx<w && ty>=0 && ty<h && to.core[static_cast<std::size_t>(ty)*w+tx]) best=(std::min)(best,std::hypot(static_cast<double>(ox),static_cast<double>(oy)));
					}
					errors.push_back(best);
				}
			};
			oneWay(base,now,dx,dy); oneWay(now,base,-dx,-dy);
			std::sort(errors.begin(),errors.end());
			const double max = errors.empty()?1000:errors.back(), median = errors.empty()?1000:errors[errors.size()/2];
			QueryPerformanceCounter(&compareEnd);
			const auto elapsedUs = frequency.QuadPart > 0 ? (compareEnd.QuadPart-compareStart.QuadPart)*1000000/frequency.QuadPart : -1;
			Log(f,"[ProductLiveInk] pixel_follow stage=%s shift=(%d,%d) core=%zu/%zu bidirectional_n=%zu median_px=%.4f max_px=%.4f compare_us=%lld\n",stage,dx,dy,base.count,now.count,errors.size(),median,max,elapsedUs);
			if(max>2 || now.count<base.count*9/10 || now.count>base.count*11/10) throw std::runtime_error("ink-screen-follow-error");
		}

		void InkWheel(Fixture& f, const std::shared_ptr<Draw::Follow::Channel>& channel, int delta, int initialX, int initialY, const char* stage, bool horizontal = false)
		{
			if (!delta || delta % WHEEL_DELTA || std::abs(delta / WHEEL_DELTA * wheelStep) > 128) throw std::runtime_error("ink-wheel-step-range");
			if (!ScreenSourceGt(f,stage,nullptr)) throw std::runtime_error("ink-wheel-source-gt");
			POINT wheelPoint{f.width/2,f.height/2};
			if (f.debuggerProbe)
			{
				// 完整诊断窗可能盖住主屏中心；只在当前窗口外的专用 Source 左侧注入。
				wheelPoint = {32, f.height/2}; RECT debugBounds{};
				if (!GetWindowRect(DebugWindow::Handle(), &debugBounds) || PtInRect(&debugBounds, wheelPoint))
					throw std::runtime_error("debug-wheel-point-covered");
			}
			MoveOnOwnedWindow(f,f.source,wheelPoint,true);
			CheckDesktop(f,"before-ink-os-wheel"); POINT cursor{};
			if(f.ownLeftHeld || (GetAsyncKeyState(VK_LBUTTON)&0x8000) || !GetCursorPos(&cursor) || WindowFromPoint(cursor)!=f.source || GetForegroundWindow()!=f.source) throw std::runtime_error("ink-wheel-hit-or-held");
			const auto accepted=LiveCapture::GetSnapshot().statistics.accepted;
			const auto messages=f.taggedWheels;
			const int step=delta/WHEEL_DELTA*wheelStep;
			const int expectedX=f.x+(horizontal?step:0), expectedY=f.y-(horizontal?0:step);
			if(expectedX<0 || expectedX>16*f.width || expectedY<0 || expectedY>24*f.height) throw std::runtime_error("ink-wheel-source-boundary");
			INPUT input{};input.type=INPUT_MOUSE;input.mi.mouseData=static_cast<DWORD>(delta);input.mi.dwFlags=horizontal?MOUSEEVENTF_HWHEEL:MOUSEEVENTF_WHEEL;input.mi.dwExtraInfo=inputTag;
			if(SendInput(1,&input,sizeof(INPUT))!=1) throw std::runtime_error("ink-wheel-send");
			if(!Wait(f,[&]{return f.taggedWheels==messages+1 && f.paintedX==expectedX && f.paintedY==expectedY;},3000)) throw std::runtime_error("ink-wheel-not-delivered");
			unsigned settled=0;std::uint64_t seq=0;
			if(!Wait(f,[&]
			{
				if(Business::GetSnapshot().state!=Business::State::Tracking)throw std::runtime_error("ink-tracking-paused");
				const auto v=channel->Read(); if(v.latest.sequence==seq)return false;seq=v.latest.sequence;
				const bool correct=std::abs(v.latest.x+f.x-initialX)<=2 && std::abs(v.latest.y+f.y-initialY)<=2 &&
					std::abs(v.viewportX-v.baseX-(f.x-initialX))<=2 && std::abs(v.viewportY-v.baseY-(f.y-initialY))<=2;
				settled=correct && v.applied>=v.latest.sequence && !v.clamped?settled+1:0;
				return settled>=2 && LiveCapture::GetSnapshot().statistics.accepted>=accepted+3;
			},8000))throw std::runtime_error("ink-wheel-tracker-owner");
			// 必须在真实Tracker/owner收敛后检查相同99点与精确ROI，不能用GT目标推动owner。
			if (!ScreenSourceGt(f,stage,nullptr)) throw std::runtime_error("ink-wheel-settled-source-gt");
			Log(f,"[ProductLiveInk] wheel_settled stage=%s axis=%s delta=%d source_total=(%d,%d) actual_ack=%u\n",stage,horizontal?"x":"y",delta,f.x-initialX,f.y-initialY,ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()));
		}

		void DebugResources(Fixture& f, const char* phase, const char* boundary)
		{
			PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
			const bool measured = GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
			Log(f, "[ScrollFollowDebugProduct] resources phase=%s boundary=%s memory_valid=%u working_set=%zu private_bytes=%zu gdi=%lu user=%lu\n",
				phase, boundary, measured, measured ? memory.WorkingSetSize : 0, measured ? memory.PrivateUsage : 0,
				GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
		}

		void DebugCoveredPixels(Fixture& f, const char* stage)
		{
			if (!ScreenSourceGt(f, stage, nullptr)) throw std::runtime_error("debug-production-screen-gt");
			const HWND debug = Inkeys::Window::GetService().Handle(Role::ScrollFollowDebug);
			const auto probe = DebugWindow::ReadProbe(); POINT origin{};
			if (!debug || !probe.hasFrame || !ClientToScreen(debug, &origin)) throw std::runtime_error("debug-production-preview-not-ready");
			RECT preview = probe.previewRect; OffsetRect(&preview, origin.x, origin.y);
			const int x = (preview.left + preview.right) / 2 - 128, y = (preview.top + preview.bottom) / 2 - 96;
			const RECT covered{x, y, x + 256, y + 192};
			if (covered.left < preview.left || covered.top < preview.top || covered.right > preview.right || covered.bottom > preview.bottom ||
				WindowFromPoint({x + 128, y + 96}) != debug) throw std::runtime_error("debug-covered-preview-roi");
			f.debugNumbers->Arm(covered, f.x, f.y);
			if (!Wait(f, [&] { return f.debugNumbers->ReadProof().completed; }, 3000)) throw std::runtime_error("debug-covered-mag-timeout");
			const auto proof = f.debugNumbers->ReadProof();
			Log(f, "[ScrollFollowDebugProduct] exclusion stage=%s business=%llu capture=%llu sequence=%llu roi=(%ld,%ld,256,192) input_pixels=%u max_channel_error=%d tolerance=3 actual_tracker_input=1 exact=%u\n",
				stage, proof.session, proof.capture, proof.sequence, covered.left, covered.top, proof.checked, proof.maximumError, proof.exact);
			if (!proof.exact || !ScreenSourceGt(f, "after-debug-covered-mag", nullptr)) throw std::runtime_error("debug-covered-mag-pixel-mismatch");
		}

		void DebugPhase(Fixture& f, const std::shared_ptr<Draw::Follow::Channel>& channel, int index, const char* phase)
		{
			const bool open = index == 1;
			// 闭窗阶段已由真实正反向滚动锁定目标，连续 30 秒检验静止累计漂移。
			const unsigned durationMs = index == 2 ? 30000 : 15000; double maximumDrift = 0;
			const auto baseline = channel->Read(); const auto captureSession = LiveCapture::GetSnapshot().session;
			const auto openPhase = f.debugNumbers->Read(1);
			if (index == 2 && (openPhase.business != baseline.session || openPhase.capture != captureSession)) throw std::runtime_error("debug-open-closed-session-changed");
			const auto stable = [&]
			{
				const auto capture = LiveCapture::GetSnapshot(); const auto owner = channel->Read();
				if (Business::GetSnapshot().state != Business::State::Tracking || owner.session != baseline.session ||
					capture.session != captureSession || !capture.publishing || owner.clamped || Business::Debug::Enabled() != open ||
					(DebugWindow::Handle() != nullptr) != open) throw std::runtime_error("debug-phase-session-or-enable-changed");
				if (index == 2) maximumDrift = (std::max)(maximumDrift, std::hypot(owner.latest.x-baseline.latest.x, owner.latest.y-baseline.latest.y));
			};
			Log(f, "[ScrollFollowDebugProduct] warmup phase=%s duration_ms=1000 outside_timing=1\n", phase);
			const auto warm = GetTickCount64();
			if (!Wait(f, [&] { stable(); return GetTickCount64() - warm >= 1000; }, 1500)) throw std::runtime_error("debug-warmup-timeout");
			if (!ScreenSourceGt(f, phase, nullptr)) throw std::runtime_error("debug-phase-screen-gt");
			const auto captureBefore = LiveCapture::GetSnapshot(); const auto uiBefore = DebugWindow::ReadProbe();
			Business::Debug::Counters countersBefore{};
			if (auto current = Business::Debug::Current()) countersBefore = current->ReadLive().counters;
			DebugResources(f, phase, "begin");
			Log(f, "[ScrollFollowDebugProduct] phase_begin phase=%s duration_ms=%u business=%llu capture=%llu stable_within_phase=1 absent_open_same_session=0 performance_causal_comparison=0 controls_hold=0 image_recording=0\n", phase, durationMs, baseline.session, captureSession);
			double ages = 0, copies = 0, totals = 0; unsigned displaySamples = 0;
			std::uint64_t displayed = 0; ULONGLONG nextSample = 0;
			const auto began = LiveCapture::Clock::now(); f.debugNumbers->Phase(index);
			const bool elapsed = Wait(f, [&]
			{
				stable();
				if (open && GetTickCount64() >= nextSample)
				{
					nextSample = GetTickCount64() + 100; const auto ui = DebugWindow::ReadProbe();
					if (!ui.open || !ui.hasFrame || ui.frozen || ui.exportBusy) throw std::runtime_error("debug-controls-during-performance");
					if (ui.sequence != displayed)
					{ displayed = ui.sequence; ages += ui.displayAgeMs; copies += ui.copyMs; totals += ui.totalTrackerMs; ++displaySamples; }
				}
				return LiveCapture::Clock::now() - began >= std::chrono::milliseconds(durationMs);
			}, durationMs + 1000);
			f.debugNumbers->Phase(-1);
			const double seconds = std::chrono::duration<double>(LiveCapture::Clock::now() - began).count();
			const auto captureAfter = LiveCapture::GetSnapshot(); const auto uiAfter = DebugWindow::ReadProbe();
			const auto n = f.debugNumbers->Read(index);
			Business::Debug::Counters countersAfter{};
			if (auto current = Business::Debug::Current()) countersAfter = current->ReadLive().counters;
			DebugResources(f, phase, "end");
			const auto mean = [&](std::size_t i) { return n.count[i] ? n.sum[i] / n.count[i] : -1; };
			Log(f, "[ScrollFollowDebugProduct] phase_end phase=%s seconds=%.6f capture_accepted=%llu capture_fps=%.6f capture_attempts=%llu capture_rejected=%llu capture_api=%llu capture_skipped=%llu capture_sequence=%llu numeric_frames=%llu tracker_calls=%llu tracker_accepted=%llu tracker_rejected=%llu await_tracking=%llu numeric_drops_total=%llu\n",
				phase, seconds, captureAfter.statistics.accepted-captureBefore.statistics.accepted, (captureAfter.statistics.accepted-captureBefore.statistics.accepted)/seconds,
				captureAfter.statistics.attempts-captureBefore.statistics.attempts, captureAfter.statistics.rejected-captureBefore.statistics.rejected, captureAfter.statistics.apiFailures-captureBefore.statistics.apiFailures,
				captureAfter.statistics.skipped-captureBefore.statistics.skipped, captureAfter.sequence-captureBefore.sequence, n.frames, n.calls, n.accepted, n.rejected, n.awaiting, f.debugNumbers->Missed());
			Log(f, "[ScrollFollowDebugProduct] timing phase=%s interval_ms=(mean:%.6f,max:%.6f,n:%llu) capture_Read_ms=(mean:%.6f,max:%.6f,n:%llu) tracker_preprocess_ms=(mean:%.6f,max:%.6f) tracker_LK_ms=(mean:%.6f,max:%.6f) tracker_fit_ms=(mean:%.6f,max:%.6f) tracker_refill_ms=(mean:%.6f,max:%.6f) Capture_Read_is_backend_duration=1 Analyze_included=0 PW_is_not_isolated=1\n",
				phase, mean(0), n.maximum[0], n.count[0], mean(1), n.maximum[1], n.count[1], mean(2), n.maximum[2], mean(3), n.maximum[3], mean(4), n.maximum[4], mean(5), n.maximum[5]);
			const char* stageNames[]={"coarse","verify","Update","Redraw","PrintWindow","GdiFlush","pixelWork","Analyze"};
			for(std::size_t i=6;i<14;++i) Log(f,"[ScrollFollowDebugProduct] stage phase=%s name=%s mean_ms=%.6f max_ms=%.6f samples=%llu same_input_sequence=1\n",phase,stageNames[i-6],mean(i),n.maximum[i],n.count[i]);
			Log(f, "[ScrollFollowDebugProduct] preview phase=%s enabled=%u display_samples=%u copy_ms_mean=%.6f displayed_measured_tracker_total_ms_mean=%.6f display_age_ms_mean=%.6f refresh_hz=%.6f consume_hz=%.6f paint_hz=%.6f publications=%llu overwritten=%llu disabled_metrics_NA=%u finite_same_machine_observation=1\n",
				phase, open, displaySamples, displaySamples ? copies/displaySamples : -1, displaySamples ? totals/displaySamples : -1, displaySamples ? ages/displaySamples : -1,
				(uiAfter.refreshes-uiBefore.refreshes)/seconds, (uiAfter.consumed-uiBefore.consumed)/seconds, (uiAfter.paints-uiBefore.paints)/seconds,
				countersAfter.framesPublished-countersBefore.framesPublished, countersAfter.framesOverwritten-countersBefore.framesOverwritten, !open);
			if (!elapsed || !n.calls || n.changedSession || n.business != baseline.session || n.capture != captureSession ||
				captureAfter.statistics.accepted <= captureBefore.statistics.accepted || (open && !displaySamples)) throw std::runtime_error("debug-phase-insufficient-observations");
			if (index == 2)
			{
				Log(f,"[ScrollFollowDebugProduct] stationary_locked seconds=%.6f max_total_drift_px=%.9f accepted=%llu limit_px=1 independent_source_gt=1\n",seconds,maximumDrift,n.accepted);
				if (maximumDrift > 1 || !n.accepted) throw std::runtime_error("debug-stationary-drift");
			}
			if (!ScreenSourceGt(f, phase, nullptr)) throw std::runtime_error("debug-phase-final-screen-gt");
		}

		bool LongInkPoseSafe(const Fixture& f, POINT aDown, int aLastX, int totalX, int totalY) noexcept
		{
			// 只选择不会让已知A走廊覆盖既有GT点的端点；99点和精确ROI始终全量检查。
			const int left=aDown.x-totalX-24, right=aLastX-totalX+24;
			const int top=aDown.y-totalY-24, bottom=aDown.y-totalY+24;
			if(right>=64 && left<320 && bottom>=64 && top<256) return false;
			for(int row=0;row<9;++row)for(int col=0;col<11;++col)
			{
				const int x=(2*col+1)*f.width/22, y=(2*row+1)*f.height/18;
				if(x>=left && x<=right && y>=top && y<=bottom) return false;
			}
			return true;
		}

		std::vector<int> PlanLongVertical(const Fixture& f, POINT aDown, int aLastX)
		{
			const auto distance64=(static_cast<std::int64_t>(f.height)*10+wheelStep-1)/wheelStep*wheelStep;
			if(distance64<=0 || distance64>24LL*f.height-f.y) throw std::runtime_error("long-source-range");
			int remaining=static_cast<int>(distance64), totalY=wheelStep;
			std::vector<int> steps;
			while(remaining)
			{
				int step=(std::min)(128,remaining);
				for(;step>=wheelStep;step-=wheelStep)
					if(LongInkPoseSafe(f,aDown,aLastX,wheelStep,totalY+step))break;
				if(step<wheelStep) throw std::runtime_error("long-source-no-safe-gt-endpoint");
				steps.push_back(step);remaining-=step;totalY+=step;
			}
			return steps;
		}

		unsigned LongVerticalRoundtrip(Fixture& f, const std::shared_ptr<Draw::Follow::Channel>& channel,
			POINT aDown, int aLastX, int initialX, int initialY)
		{
			const auto steps=PlanLongVertical(f,aDown,aLastX);
			int distance=0;for(const int step:steps)distance+=step;
			Log(f,"[ProductLiveLong] plan screens=10 physical_height=%d rounded_distance_px=%d steps_each_way=%zu max_step_px=128 final_source_total=(16,16) watchdog_ms=480000\n",f.height,distance,steps.size());
			const auto started=GetTickCount64();
			for(std::size_t i=0;i<steps.size();++i)
			{
				InkWheel(f,channel,-steps[i]/wheelStep*WHEEL_DELTA,initialX,initialY,"ten-heights-down");
				Log(f,"[ProductLiveLong] step phase=down index=%zu/%zu px=%d source_total=(%d,%d) elapsed_ms=%llu\n",i+1,steps.size(),steps[i],f.x-initialX,f.y-initialY,GetTickCount64()-started);
			}
			if(f.x-initialX!=wheelStep || f.y-initialY!=wheelStep+distance || distance<10LL*f.height || aDown.y-(f.y-initialY)+24>=0)throw std::runtime_error("long-peak-actual-source-range");
			Log(f,"[ProductLiveLong] peak source_total=(%d,%d) excursion_px=%d birth_viewport_overlap=0 ink_a_offscreen=1 initial_survivor_evidence=matching-session-accepted-frames-required\n",f.x-initialX,f.y-initialY,distance);
			for(std::size_t i=steps.size();i>0;--i)
			{
				InkWheel(f,channel,steps[i-1]/wheelStep*WHEEL_DELTA,initialX,initialY,"ten-heights-return");
				Log(f,"[ProductLiveLong] step phase=return index=%zu/%zu px=%d source_total=(%d,%d) elapsed_ms=%llu\n",steps.size()-i+1,steps.size(),steps[i-1],f.x-initialX,f.y-initialY,GetTickCount64()-started);
			}
			if(f.x-initialX!=wheelStep || f.y-initialY!=wheelStep) throw std::runtime_error("long-return-actual-source-total");
			Log(f,"[ProductLiveLong] returned source_total=(16,16) elapsed_ms=%llu restored_ink_pixels=checked-next\n",GetTickCount64()-started);
			return static_cast<unsigned>(steps.size()*2);
		}

		double NativeMouseDownBound(double cursor, double viewport, UINT dpi)
		{
			// RTS鼠标包为整数HIMETRIC；容许一整单位，再分别计入float比例、乘法和视口相加舍入。
			// DPI仅作此专用OS鼠标夹具的受限模型；产品仍使用真实GetPacketDescriptionData比例。
			if (!dpi || !std::isfinite(cursor) || !std::isfinite(viewport)) throw std::runtime_error("native-mouse-bound-input");
			const double q = static_cast<double>(dpi)/2540.0, scale = static_cast<float>(q);
			const double rawBound = std::ceil(std::abs(cursor)/q+1.0), eps = std::numeric_limits<float>::epsilon();
			if (!std::isfinite(rawBound) || rawBound >= 16777216.0) throw std::runtime_error("native-mouse-raw-not-exact-float");
			const double arithmetic = rawBound*std::abs(scale-q)+eps*(std::max)(1.0,rawBound*std::abs(scale));
			return q+arithmetic+eps*(std::max)(1.0,std::abs(cursor)+std::abs(viewport)+q+arithmetic);
		}

		void ReadInkExit(Fixture& f, const Draw::Follow::Snapshot& final, POINT aDown, POINT bDown, const std::string& initialDate)
		{
			using namespace draw3::uink;
			auto exit=Draw::ReadLastCommittedDesktopAutoSaveFixture(f.root+L"/AutoSave",initialDate);
			const auto date=Draw::CaptureDesktopAutoSaveTimestamp().localDate;
			if(date!=initialDate){auto latest=Draw::ReadLastCommittedDesktopAutoSaveFixture(f.root+L"/AutoSave",date);if(latest.status==Draw::DesktopPersistenceStatus::Loaded)exit=std::move(latest);}
			if(exit.status!=Draw::DesktopPersistenceStatus::Loaded || exit.trigger!=Draw::DesktopAutoSaveTrigger::Exit || !exit.loadedSnapshot || !exit.sourceRevision ||
				exit.sequenceInSession!=1 || exit.dailySequence!=1 || exit.storageSession.empty())throw std::runtime_error("ink-real-exit-committed-index");
			Json::Value index;Json::CharReaderBuilder builder;std::unique_ptr<Json::CharReader> reader(builder.newCharReader());std::string errors;
			if(!reader->parse(exit.indexBytes.data(),exit.indexBytes.data()+exit.indexBytes.size(),&index,&errors) || !index["entries"].isArray() || index["entries"].size()!=1)
				throw std::runtime_error("ink-unique-exit-index");
			const auto& snapshot=*exit.loadedSnapshot;
			if(snapshot.fileGuid!=exit.fileGuid || snapshot.workspaceType!=0 || snapshot.workspaceGuid.Bytes()!=final.identity.workspace || snapshot.canvases.size()!=1)
				throw std::runtime_error("ink-file-workspace-identity");
			const auto& page=snapshot.canvases.front();
			if(page.pageGuid.Bytes()!=final.identity.page || page.strokes.size()!=2 || page.viewport.scale!=1 ||
				std::abs(page.viewport.x-final.viewportX)>.001 || std::abs(page.viewport.y-final.viewportY)>.001)throw std::runtime_error("ink-file-page-viewport");
			const auto& a=page.strokes[0];const auto& b=page.strokes[1];
			if(a.style.kind!=Draw3UInkStrokeKind::Pen || b.style.kind!=Draw3UInkStrokeKind::Pen || a.points.size()<3 || b.points.size()<3)
				throw std::runtime_error("ink-file-two-pen-points");
			CheckFixture(f);
			const double ax=NativeMouseDownBound(aDown.x,0,f.mouseDpiX), ay=NativeMouseDownBound(aDown.y,0,f.mouseDpiY);
			const double bx=NativeMouseDownBound(bDown.x,page.viewport.x,f.mouseDpiX), by=NativeMouseDownBound(bDown.y,page.viewport.y,f.mouseDpiY);
			Log(f,"[ProductLiveInk] down_oracle source=native-os-mouse-one-himetric-plus-float dpi=(%u,%u) geometry_1to1=1 a_stored=(%.9f,%.9f) a_expected=(%.9f,%.9f) a_bound=(%.9f,%.9f) b_stored=(%.9f,%.9f) b_expected=(%.9f,%.9f) b_bound=(%.9f,%.9f) viewport_tolerance=0.001\n",
				f.mouseDpiX,f.mouseDpiY,a.points.front().x,a.points.front().y,static_cast<double>(aDown.x),static_cast<double>(aDown.y),ax,ay,
				b.points.front().x,b.points.front().y,static_cast<double>(bDown.x)+page.viewport.x,static_cast<double>(bDown.y)+page.viewport.y,bx,by);
			if (!(std::abs(static_cast<double>(a.points.front().x)-aDown.x)<=ax && std::abs(static_cast<double>(a.points.front().y)-aDown.y)<=ay &&
				std::abs(static_cast<double>(b.points.front().x)-bDown.x-page.viewport.x)<=bx && std::abs(static_cast<double>(b.points.front().y)-bDown.y-page.viewport.y)<=by))
				throw std::runtime_error("ink-file-world-down-points");
			const auto file=std::filesystem::path(f.root)/L"AutoSave"/L"desktop"/std::filesystem::path(exit.localDate)/exit.relativePath;
			const auto read=ReadUInkFile(file.native());const auto imported=read.document?ImportDraw3UInkDocument(*read.document):Draw3UInkImportResult{};
			if(kUInkVersion!=10 || read.status!=UInkReadStatus::Complete || imported.status!=Draw3UInkImportStatus::Success || !imported.snapshot ||
				imported.snapshot->fileGuid!=snapshot.fileGuid)throw std::runtime_error("ink-v10-read-import");
			Log(f,"[ProductLiveInk] actual_exit=PASS file=%ls storage_session=%s sequence=%llu daily=%llu points=(%zu,%zu) viewport=(%.4f,%.4f) b_client_down=(%ld,%ld) b_stored_down=(%.4f,%.4f) read_complete=1 import_success=1 synthetic_sent=0\n",
				file.c_str(),exit.storageSession.c_str(),exit.sequenceInSession,exit.dailySequence,a.points.size(),b.points.size(),page.viewport.x,page.viewport.y,bDown.x,bDown.y,b.points.front().x,b.points.front().y);
		}

		void Facts(Fixture& f, const char* stage)
		{
			const auto business = Business::GetSnapshot();
			const auto capture = LiveCapture::GetSnapshot();
			const auto host = Draw::ProductRuntimeSnapshot();
			const auto channel = Draw::ProductHost().FollowChannel();
			const auto follow = channel ? channel->Read() : Draw::Follow::Snapshot{};
			auto& service = Inkeys::Window::GetService();
			Log(f, "[ProductLive] facts stage=%s business=%u session=%llu available=%u capture_session=%llu ready=%u publishing=%u occupied=%u inflight=%u stop=%u quality=%u accepted=%llu rejected=%llu api=%llu pw_us=%lld phase=%u latest=%llu applied=%llu total=(%.4f,%.4f) base=(%.4f,%.4f) viewport=(%.4f,%.4f) clamp=%u selection=%u content=%u output=%u/%llu ready_output=%u/%llu content_revision=%llu presented=%llu full_clean=%u projection=%llu actual_ack=%u visible=(%u,%u) input_mode=%u\n",
				stage, static_cast<unsigned>(business.state), business.session, business.available, capture.session,
				capture.ready, capture.publishing, capture.occupied, capture.inFlight, static_cast<unsigned>(capture.reason),
				static_cast<unsigned>(capture.quality.status), capture.statistics.accepted, capture.statistics.rejected,
				capture.statistics.apiFailures, static_cast<long long>(capture.statistics.lastReadDuration.count()),
				static_cast<unsigned>(follow.phase), follow.latest.sequence, follow.applied, follow.latest.x, follow.latest.y,
				follow.baseX, follow.baseY, follow.viewportX, follow.viewportY, follow.clamped, host.selectionMode, host.currentPageHasContent,
				static_cast<unsigned>(host.requestedOutputTarget), host.requestedOutputRevision,
				static_cast<unsigned>(host.readyOutputTarget), host.readyOutputRevision, host.contentRevision, host.presentedContentRevision,
				host.auxiliaryFullFrameClean, ScrollFollowProjectionEpoch(), ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()),
				IsWindowVisible(service.Handle(Role::Drawpad)), IsWindowVisible(service.Handle(Role::DrawpadPresentation)), static_cast<unsigned>(host.inputMode));
		}

		// 仅失败时读取现有有界数值，不能重跑 Tracker、复制图像或改变原失败条件。
		void DebugFailureFacts(Fixture& f, const char* stage) noexcept
		{
			try
			{
				DWORD session = 0; const bool sessionValid = ProcessIdToSessionId(GetCurrentProcessId(), &session) != FALSE;
				const auto probe = DebugWindow::ReadProbe();
				Log(f, "[ScrollFollowDebugProduct] failure_probe stage=%s pid=%lu session_valid=%u windows_session=%lu open=%u frozen=%u export_busy=%u painted=%llu generation=%llu business=%llu capture=%llu sequence=%llu reference_before=%llu\n",
					stage, GetCurrentProcessId(), sessionValid, session, probe.open, probe.frozen, probe.exportBusy, probe.paints,
					probe.generation, probe.businessSession, probe.captureSession, probe.sequence, probe.referenceSequence);
				const auto channel = Business::Debug::Current();
				if (!channel) { Log(f, "[ScrollFollowDebugProduct] failure_live channel=unavailable\n"); return; }
				const auto snapshot = channel->ReadLive(); const auto& status = snapshot.status;
				Log(f, "[ScrollFollowDebugProduct] failure_live state=%u pause_cause=%s business=%llu capture=%llu owns_capture=%u capture_stage=%s capture_reason=%u native_valid=%u native_error=%lu quality_observed=%u tracker_calls=%llu tracker_A=%llu tracker_R=%llu history=%zu summary_drops=%llu\n",
					static_cast<unsigned>(status.state), Business::Debug::PauseCauseName(status.pauseCause), status.businessSession, status.captureSession,
					status.ownsCapture, LiveCapture::FailureStageName(status.capture.failureStage), static_cast<unsigned>(status.capture.reason),
					status.capture.nativeErrorValid, static_cast<unsigned long>(status.capture.nativeError), status.capture.qualityObserved,
					snapshot.counters.trackerCalls, snapshot.counters.trackerAccepted, snapshot.counters.trackerRejected, snapshot.count, snapshot.counters.summaryDrops);
				const auto count = (std::min)(snapshot.count, snapshot.summaries.size()); std::size_t begin = count, motions = 0;
				while (begin && motions < 6) if (snapshot.summaries[--begin].hasMotion) ++motions;
				for (std::size_t i = 0; i < count; ++i)
				{
					const auto& event = snapshot.summaries[i]; const auto& failure = event.captureFailure;
					if (failure.valid)
						Log(f, "[ScrollFollowDebugProduct] failure_capture_event index=%zu business=%llu capture=%llu stage=%s reason=%u at_us=%lld native_valid=%u native_error=%lu quality_valid=%u quality_status=%d\n",
							i, event.identity.businessSession, event.identity.captureSession, LiveCapture::FailureStageName(failure.stage), static_cast<unsigned>(failure.reason),
							static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(failure.at.time_since_epoch()).count()), failure.nativeErrorValid,
							static_cast<unsigned long>(failure.nativeError), failure.stage == LiveCapture::FailureStage::Quality,
							failure.stage == LiveCapture::FailureStage::Quality ? static_cast<int>(failure.quality.status) : -1);
					if (!event.hasMotion || i < begin) continue;
					const auto& motion = event.motion; const auto& stats = motion.statistics;
					Log(f, "[ScrollFollowDebugProduct] failure_motion index=%zu business=%llu capture=%llu sequence=%llu accepted=%u state=%u reason=%s consecutive_rejects=%d input=%llu reference_after=%llu reference_generation_after=%llu relay_generation=%llu total=(%.6f,%.6f) raw_counts=(attempted:%d,forward:%d,backward:%d,fb:%d,inliers:%d) raw_ratios=(point:%.6f,spatial:%.6f,survival:%.6f) raw_p95=(fb:%.6f,residual:%.6f) stage_validity=not_stored_in_numeric_summary raw_values_may_be_uncomputed=1\n",
						i, event.identity.businessSession, event.identity.captureSession, event.identity.sequence, motion.accepted, static_cast<unsigned>(motion.state), Inkeys::CV::MotionReasonName(motion.reason),
						stats.consecutiveRejects, stats.inputSequence, stats.referenceSequence, stats.referenceGeneration, stats.relayGeneration, motion.total.x, motion.total.y,
						stats.attempted, stats.forward, stats.backward, stats.fbValid, stats.inliers, stats.pointInlierRatio, stats.spatialInlierRatio, stats.survivalRatio, stats.fbP95, stats.residualP95);
				}
			}
			catch (...) { Log(f, "[ScrollFollowDebugProduct] failure_numeric_log_unavailable=1\n"); }
		}

		void CreateRoot(Fixture& f)
		{
			const auto cwd = std::filesystem::current_path();
			const auto parent = cwd / L"Build" / L"ScrollFollow";
			const auto safe = [](const std::filesystem::path& path)
			{
				const DWORD a = GetFileAttributesW(path.c_str());
				return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
			};
			auto checked = parent.root_path();
			if (!parent.is_absolute() || !safe(checked) || GetFileAttributesW((cwd / L"InkeysRepo.sln").c_str()) == INVALID_FILE_ATTRIBUTES)
				throw std::runtime_error("repository-private-root");
			for (const auto& part : parent.relative_path()) { checked /= part; if (!safe(checked)) throw std::runtime_error("reparse-root"); }
			f.root = (parent / (L"product-live-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()))).native();
			if (f.root.size() >= MAX_PATH - 112 || !CreateDirectoryW(f.root.c_str(), nullptr)) throw std::runtime_error("create-new-private-root");
			f.log = CreateFileW((f.root + L"/result.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f.log == INVALID_HANDLE_VALUE) throw std::runtime_error("create-new-log");
		}
	}
}

static int RunScrollFollowProductLiveCase(bool ink, bool longScroll = false, bool externalEmpty = false, bool debuggerProbe = false)
{
	using namespace ProductLive;
	if ((longScroll && !ink) || (externalEmpty && (ink || longScroll)) || (debuggerProbe && (ink || longScroll || externalEmpty))) return 2;
	wchar_t traceToken[34]{};
	SetLastError(ERROR_SUCCESS);
	const DWORD traceLength=GetEnvironmentVariableW(L"INKEYS_SCROLL_RECORD_MOTION_FRAMES",traceToken,static_cast<DWORD>(std::size(traceToken)));
	if (!traceLength && GetLastError()!=ERROR_ENVVAR_NOT_FOUND) return 2;
	if (traceLength && (!externalEmpty || traceLength!=32 || std::wstring_view(traceToken,32).find_first_not_of(L"0123456789abcdef")!=std::wstring_view::npos)) return 2;
	// 早于Display、COM、source和所有产品HWND；仅查询，不尝试越过安全界面。
	if (LiveSecurityInterfacePresent()) { std::fputs("[ProductLive] blocked=security-before-initialization\n", stderr); return 4; }
	Fixture f; f.debuggerProbe = debuggerProbe;
	const auto oldLogger = IDTLogger;
	const auto oldGlobalPath = globalPath;
	std::jthread stateThread, magThread, watchdog;
	HANDLE cleanupDone = nullptr;
	bool comStarted = false, displayStarted = false, serviceStarted = false, classRegistered = false, productStarted = false, businessBegun = false;
	int result = 1;
	InkView aBaseline; RECT aRoi{}, bRoi{}; POINT aDown{}, bDown{};
	std::string initialDate;
	unsigned expectedWheelCount = ink || debuggerProbe ? 2u : 1u;
	const char* stage = "create-root";
	try
	{
		if (offSignal || Draw::ProductRunning() || Inkeys::Window::GetService().Running()) throw std::runtime_error("early-fixture-only");
		if (externalEmpty) LoadExternal(f);
		if (traceLength)
		{
			const auto run=ExternalString(f.external.identity,"run");
			if (std::wstring(run.begin(),run.end())!=traceToken) throw std::runtime_error("trace-nonce-mismatch");
			f.traceRequested=true;
		}
		CreateRoot(f);
		globalPath = f.root + L"/";
		IDTLogger = std::make_shared<spdlog::logger>("ScrollFollowProductLive", std::make_shared<spdlog::sinks::null_sink_mt>());
		Log(f, "[ProductLive] root=%ls pid=%lu main_tid=%lu scope=%s no_test_ports=1 no_manual_follow_target=1 no_manual_ack=1\n", f.root.c_str(), GetCurrentProcessId(), GetCurrentThreadId(), debuggerProbe ? "debugger-three-phases-noninteractive-forward-reverse-off" : externalEmpty ? "external-edge-empty-page-wheel-off" : longScroll ? "os-ink-long-2d-ten-primary-heights-off-b-exit" : ink ? "os-ink-two-wheel-off-b-exit" : "empty-page-tracking-wheel-off");
		cleanupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!cleanupDone) throw std::runtime_error("cleanup-event");
		watchdog = std::jthread([cleanupDone, longScroll, debuggerProbe]
		{
			// 同步PW/owner若永久不返回，只结束本专用进程；这不是正常清理或有界join证明。
			if (WaitForSingleObject(cleanupDone, longScroll ? 480000 : debuggerProbe ? 210000 : 90000) != WAIT_OBJECT_0) TerminateProcess(GetCurrentProcess(), 3);
		});
		CheckDesktop(f, "before-initialization");
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(initialized)) throw std::runtime_error("com-initialize");
		comStarted = true;
		if (!Inkeys::Display::Initialize()) throw std::runtime_error("display-initialize");
		displayStarted = true;
		f.width = GetSystemMetrics(SM_CXSCREEN); f.height = GetSystemMetrics(SM_CYSCREEN);
		if (f.width < 640 || f.height < 480) throw std::runtime_error("primary-too-small");
		const auto display = Inkeys::Display::GetSnapshot();
		const auto* primary = display ? display->Primary() : nullptr;
		if (!primary || display->fallback || primary->fallback || primary->bounds.left || primary->bounds.top ||
			primary->pixelWidth != f.width || primary->pixelHeight != f.height) throw std::runtime_error("primary-geometry");
		if (!primary->effectiveDpiX || !primary->effectiveDpiY) throw std::runtime_error("primary-dpi");
		f.mouseDpiX=primary->effectiveDpiX; f.mouseDpiY=primary->effectiveDpiY;
		f.mouseMonitor=primary->handle; f.mouseOrientation=primary->orientation;
		Log(f,"[ProductLiveInk] native_mouse_model dpi=(%u,%u) primary=(%d,%d) source=Display-effectiveDpi restricted_os_mouse=1 arbitrary_digitizer_scale=not_assumed\n",f.mouseDpiX,f.mouseDpiY,f.width,f.height);
		if (!externalEmpty)
		{
		WNDCLASSW source{}; source.lpfnWndProc = SourceProc; source.hInstance = GetModuleHandleW(nullptr);
		source.lpszClassName = sourceClass; source.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		if (!RegisterClassW(&source)) throw std::runtime_error("source-class");
		classRegistered = true;
		f.source = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, sourceClass, L"Inkeys dedicated scroll-follow texture", WS_POPUP,
			0, 0, f.width, f.height, nullptr, nullptr, source.hInstance, &f);
		if (!f.source) throw std::runtime_error("source-window");
		ShowWindow(f.source, SW_SHOW); UpdateWindow(f.source);
		}
		stage = "window-service";
		const auto make = [&](Role role, const wchar_t* name, WNDPROC proc)
		{
			Inkeys::Window::WindowSpec spec; spec.role = role; spec.className = name;
			spec.width = f.width; spec.height = f.height; spec.style = WS_POPUP | WS_CLIPCHILDREN;
			spec.exStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
			spec.windowProc = proc; spec.bindMessages = false;
			return spec;
		};
		auto host = make(Role::MagnifierHost, L"Inkeys.ProductLive.Magnifier", MagnifierHostWindowWndProc);
		host.height -= 1; host.beforeCreate = PrepareMagnifierWindow; host.created = MagnifierHostCreated; host.destroyed = ShutdownMagnifierWindow;
		auto child = make(Role::MagnifierChild, WC_MAGNIFIER, nullptr);
		child.height -= 1; child.style = WS_CHILD | WS_VISIBLE | MS_CLIPAROUNDCURSOR; child.exStyle = WS_EX_NOACTIVATE; child.created = MagnifierChildCreated;
		auto freeze = make(Role::Freeze, L"Inkeys.ProductLive.Freeze", DefWindowProcW); freeze.exStyle |= WS_EX_TRANSPARENT;
		auto selection = make(Role::DrawpadPresentation, L"Inkeys.ProductLive.Selection", DefWindowProcW); selection.exStyle |= WS_EX_TRANSPARENT;
		auto drawpad = make(Role::Drawpad, L"Inkeys.ProductLive.Drawpad", DrawpadMsgCallback); drawpad.bindMessages = true;
		const bool preferDcomp = Inkeys::config.Experimental.Inkeys3.Draw3.EnableDirectComposition && Draw::ShouldPreconfigureNoRedirectionBitmap();
		drawpad.exStyle &= ~(WS_EX_LAYERED | WS_EX_TRANSPARENT);
		if (preferDcomp) drawpad.exStyle |= WS_EX_NOREDIRECTIONBITMAP;
		auto setting = make(Role::Setting, L"Inkeys.ProductLive.HiddenSetting", DefWindowProcW);
		setting.width = setting.height = 1; setting.exStyle = WS_EX_APPWINDOW;
		auto observer = make(Role::DisplayObserver, L"Inkeys.ProductLive.Display", Inkeys::Display::WindowProc());
		observer.width = observer.height = 1; observer.exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
		auto& service = Inkeys::Window::GetService();
		if (!service.Start({ host, child, freeze, selection, drawpad, setting, observer }) || !magnificationCreateReady)
			throw std::runtime_error("original-window-service");
		serviceStarted = true;
		const auto filters = BuildCurrentFilterList();
		if (std::find(filters.begin(), filters.end(), f.source) != filters.end() ||
			(externalEmpty && std::find(filters.begin(), filters.end(), f.external.content) != filters.end())) throw std::runtime_error("source-filtered");
		Log(f, "[ProductLive] original_mag owner_tid=%lu host=%p child=%p source_unfiltered=1 filters=%zu selection_transparent=%u prefer_dcomp=%u\n",
			service.OwnerThreadId(Role::MagnifierHost), service.Handle(Role::MagnifierHost), service.Handle(Role::MagnifierChild), filters.size(),
			(GetWindowLongPtrW(service.Handle(Role::DrawpadPresentation), GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0, preferDcomp);
		if (!externalEmpty)
		{
		const bool sourceForegroundRequested = SetForegroundWindow(f.source) != FALSE;
		Log(f, "[ProductLive] source_foreground api=%u actual_own=%u\n", sourceForegroundRequested, GetForegroundWindow() == f.source);
		if (!service.SetOverlayTopmost(true) || !SetWindowPos(f.source, service.OverlayRoot(), 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) throw std::runtime_error("own-source-order");
		if (!Wait(f, [&] { return f.paintSequence != 0; }, 2000) || !ScreenSourceGt(f, "before-product", L"source-before-product.png"))
			throw std::runtime_error("dedicated-screen-gt");
		stage = "source-click-activation";
		// 仅在专用内容GT和真实命中通过后，用正常系统单击激活自己的源，不绕前台限制。
		CheckDesktop(f, "before-source-activation-move");
		const POINT activationCenter{ f.width / 2, f.height / 2 };
		if (WindowFromPoint(activationCenter) != f.source) throw std::runtime_error("activation-target-is-not-own-source");
		const int activationWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN), activationHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		if (activationWidth < 2 || activationHeight < 2) throw std::runtime_error("activation-virtual-screen");
		INPUT activationMove{}; activationMove.type = INPUT_MOUSE;
		activationMove.mi.dx = static_cast<LONG>((static_cast<std::int64_t>(activationCenter.x - GetSystemMetrics(SM_XVIRTUALSCREEN)) * 65535) / (activationWidth - 1));
		activationMove.mi.dy = static_cast<LONG>((static_cast<std::int64_t>(activationCenter.y - GetSystemMetrics(SM_YVIRTUALSCREEN)) * 65535) / (activationHeight - 1));
		activationMove.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK; activationMove.mi.dwExtraInfo = inputTag;
		if (SendInput(1, &activationMove, sizeof(INPUT)) != 1) throw std::runtime_error("activation-os-move");
		if (!Wait(f, [&] { POINT p{}; return GetCursorPos(&p) && std::abs(p.x - activationCenter.x) <= 2 && std::abs(p.y - activationCenter.y) <= 2; }, 1000))
			throw std::runtime_error("activation-os-cursor-position");
		CheckDesktop(f, "before-source-activation-click");
		POINT activationCursor{};
		if (!GetCursorPos(&activationCursor) || std::abs(activationCursor.x - activationCenter.x) > 2 || std::abs(activationCursor.y - activationCenter.y) > 2 ||
			WindowFromPoint(activationCursor) != f.source) throw std::runtime_error("activation-cursor-target-changed");
		if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) throw std::runtime_error("left-already-held-before-fixture-click");
		INPUT activationDown{}; activationDown.type = INPUT_MOUSE; activationDown.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; activationDown.mi.dwExtraInfo = inputTag;
		if (SendInput(1, &activationDown, sizeof(INPUT)) != 1) throw std::runtime_error("activation-os-left-down");
		f.ownLeftHeld = true;
		// 不以批量返回2假设完整插入；Down成功后必须确认自身Up，异常清理仍保留归还凭据。
		if (!ReleaseOwnLeft(f)) throw std::runtime_error("activation-os-left-up-unproven");
		if (!Wait(f, [&]
		{
			POINT p{}; return GetCursorPos(&p) && std::abs(p.x - activationCenter.x) <= 2 && std::abs(p.y - activationCenter.y) <= 2 &&
				WindowFromPoint(p) == f.source && GetForegroundWindow() == f.source;
		}, 2000)) throw std::runtime_error("own-source-click-did-not-activate");
		Log(f, "[ProductLive] source_activation os_move=1 os_click=1 actual_cursor_hit_own=1 actual_foreground_own=1 before_product=1\n");
		}
		else
		{
			CheckDesktop(f,"external-before-product");
			if (!service.SetOverlayTopmost(true)) throw std::runtime_error("external-overlay-order");
		}
		stage = "product-start";
		// 仅内存默认配置。root显式私有，绝不进入普通Main的配置/自启动/Hook/PPT分支。
		setlist.saveSetting.enable = true; setlist.paintDevice = 1; setlist.performanceSetting.drawpadFps = 60;
		Draw::HostStartOptions options;
		options.allowDirectComposition = preferDcomp;
		options.enableHardwareAcceleration = Inkeys::config.Experimental.Inkeys3.Draw3.EnableHardwareAcceleration;
		options.enableZhaoxinCompatibility = Inkeys::config.Experimental.Inkeys3.Draw3.EnableZhaoxinCompatibility;
		options.autoSaveRoot = f.root + L"/AutoSave";
		if (ink)
		{
			// 仅启用有限只读指标；实际OS首例不打开或发送HiddenTestContact，不影响RTS选择。
			options.enableRuntimeMetrics = true; options.runtimeMetricsMaximumSamples = 128;
			options.enableRtsTrace = true;
			initialDate = Draw::CaptureDesktopAutoSaveTimestamp().localDate;
			Log(f,"[ProductLiveInk] source=os-sendinput contact_seam_enabled=0 synthetic_sent=0 runtime_metrics=128 rts_trace=1\n");
		}
		options.startupContext = &f;
		options.startupMilestone = [](void* context, Draw::HostStartupStage value) noexcept
		{ Log(*static_cast<Fixture*>(context), "[ProductLive] host_startup stage=%u tid=%lu\n", static_cast<unsigned>(value), GetCurrentThreadId()); };
		Draw::HostStyleCallbacks style{ &service, [](void* context, DWORD set, DWORD clear) -> bool
		{ return static_cast<Inkeys::Window::Service*>(context)->SetExtendedStyleFlags(Role::Drawpad, set, clear); } };
		Draw::HostRuntimeCallbacks runtime;
		runtime.penWidthContextChanged = [](void*, const Inkeys::Business::PenWidth::Context& value) noexcept
		{ auto actual = value; actual.paintDevice = setlist.paintDevice; if (!ApplyPenWidthContext(actual)) SyncDraw3State(); };
		if (!Draw::StartProduct(service.Handle(Role::Drawpad), service.Handle(Role::DrawpadPresentation), style, options, runtime))
			throw std::runtime_error("real-product-host");
		productStarted = true;
		if (!ChangeStateModeToPen()) throw std::runtime_error("real-pen-mode");
		SyncDraw3State();
		Business::SetDiagnostics(true, true);
		stateThread = std::jthread([&]
		{
			f.stateTid.store(GetCurrentThreadId());
			try { StateMonitoring(); f.stateReturned.store(true); }
			catch (...) { f.stateFailed.store(true); }
		});
		magThread = std::jthread([&]
		{
			f.magTid.store(GetCurrentThreadId());
			try { MagnifierThread(); f.magReturned.store(true); }
			catch (...) { f.magFailed.store(true); }
		});
		if (!Wait(f, [&]
		{
			const auto r = Draw::ProductRuntimeSnapshot();
			return r.firstFrameReady && !r.selectionMode && !r.currentPageHasContent &&
				ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()) &&
				IsWindowVisible(service.Handle(Role::Drawpad)) && !IsWindowVisible(service.Handle(Role::DrawpadPresentation)) && Business::Available();
		}, 8000)) throw std::runtime_error("initial-real-primary-and-capture-ready");
		Facts(f, "pen-ready");
		if (ink)
		{
			stage = "real-os-ink-a";
			aRoi = { f.width/4,5*f.height/18+8,3*f.width/4,7*f.height/18-8 };
			bRoi = { f.width/4,11*f.height/18+8,3*f.width/4,13*f.height/18-8 };
			// 长程A上移经过精确ROI的高度时，整个笔走廊仍须在其右方。
			if (longScroll) { aRoi.left = (std::max)(aRoi.left,336L); bRoi.left = (std::max)(bRoi.left,336L); }
			CheckInkRoi(f,aRoi); CheckInkRoi(f,bRoi);
			const bool widthAccepted = SetPenWidth(8.0,false);
			const bool colorAccepted = widthAccepted && SetPenColor(RGBA(255,16,0,255),false);
			InkToolFacts(f,"A-after-setters",true,widthAccepted,widthAccepted,colorAccepted,false,false);
			const bool toolReady = widthAccepted && colorAccepted &&
				Wait(f,[&]{return PrimaryReady() && ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch());},3000);
			InkToolFacts(f,"A-after-wait",true,widthAccepted,widthAccepted,colorAccepted,widthAccepted && colorAccepted,toolReady);
			if (!toolReady) throw std::runtime_error("ink-a-real-tool-ready");
			const int margin = longScroll ? 96 : 48;
			aDown=DrawOsInk(f,{aRoi.left+margin,aRoi.bottom-32},{aRoi.right-margin,aRoi.bottom-32},"A");
			aBaseline=ReadInkScreen(f,aRoi,aDown.y,false,L"ink-a-primary.png");
		}
		const auto realTool = GetStateModeVersionedSnapshot();
		const auto initialContent = Draw::ProductRuntimeSnapshot().contentRevision;
		if (!externalEmpty && !ScreenSourceGt(f, "before-toggle", nullptr)) throw std::runtime_error("primary-transparency-gt");
		stage = "business-tracking";
		if (f.traceRequested)
		{
			// 在F0之前安装；只复制真实Tracker输入，不改变来源、ROI、目标或暂停规则。
			f.trace=std::make_shared<MotionTrace>(f);
			Business::LiveTrace::Install(f.trace);
			Log(f,"[MotionTrace] armed=1 limit_frames=64 limit_pixel_bytes=268435456 limit_seconds=10 format=fullres-gray8 observer_overhead_not_normal_capture_performance=1\n");
		}
		if (debuggerProbe)
		{
			f.debugNumbers = std::make_shared<DebugNumbers>(f);
			Business::LiveTrace::Install(f.debugNumbers);
			Log(f, "[ScrollFollowDebugProduct] numeric_observer=armed_all_phases image_recording=0 watchdog_ms=210000\n");
		}
		Business::RequestToggle(); businessBegun = true;
		if (!Wait(f, [&] { return Business::GetSnapshot().state == Business::State::Tracking; }, 8000))
			throw std::runtime_error("real-business-initialize-tracker-ack");
		auto channel = Draw::ProductHost().FollowChannel();
		if (!channel) throw std::runtime_error("real-channel");
		auto base = channel->Read();
		if (!ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()) || IsWindowVisible(service.Handle(Role::Drawpad)) ||
			(IsWindowVisible(service.Handle(Role::DrawpadPresentation)) != FALSE) != ink || Draw::ProductRuntimeSnapshot().currentPageHasContent != ink)
			throw std::runtime_error("actual-tracking-window-pair");
		Facts(f, ink ? "tracking-real-ack-ink-pair" : "tracking-real-ack-empty-pair");
		const int beforeX = f.x, beforeY = f.y;
		if (debuggerProbe)
		{
			stage = "debug-absent-phase";
			DebugPhase(f, channel, 0, "absent");
			const auto absentBusiness = base.session, absentCapture = LiveCapture::GetSnapshot().session;
			// 正式启动先开诊断窗再启动 Mag；夹具也先真实 Off/反向 ACK/drain，不能在旧 Read 中动态 Show。
			stage = "debug-real-off-before-open";
			Business::RequestStop();
			if (!Wait(f, [&]
			{
				const auto business = Business::GetSnapshot(); const auto capture = LiveCapture::GetSnapshot(); const auto ended = channel->Read();
				return business.state == Business::State::Off && business.available && !channel->Blocked() &&
					ended.phase == Draw::Follow::Phase::Complete && ended.handoff && ended.applied == ended.final.sequence && !ended.clamped &&
					ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()) && IsWindowVisible(service.Handle(Role::Drawpad)) &&
					!IsWindowVisible(service.Handle(Role::DrawpadPresentation)) && !capture.publishing && !capture.occupied && !capture.inFlight;
			}, 10000)) throw std::runtime_error("debug-before-open-real-off-ack-drain");
			Log(f, "[ScrollFollowDebugProduct] before_open real_off=1 reverse_ack=1 capture_drained=1 previous_business=%llu previous_capture=%llu\n", absentBusiness, absentCapture);
			stage = "debug-production-open";
			if (f.width < 1344 || f.height < 720) throw std::runtime_error("debug-primary-too-small");
			// 贴右下边将下/右阴影移出屏内 GT；后续仍严格校验真实 WorkArea 内的完整覆盖 ROI。
			// 两行特征图例和高 DPI 标题栏也要留出原有 256x192 完整像素验收区。
			const RECT bounds{f.width - 960, f.height - 640, f.width, f.height};
			CheckDesktop(f, "before-debug-open");
			// 按独立置顶合同显示诊断窗；专用 Source 保持非置顶带。
			if (!SetWindowPos(f.source, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) ||
				!DebugWindow::Open(nullptr, &bounds)) throw std::runtime_error("debug-production-open");
			const HWND initialDebug = service.Handle(Role::ScrollFollowDebug);
			if (!SetWindowPos(initialDebug, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) ||
				!SetWindowPos(f.source, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER))
				throw std::runtime_error("debug-production-visible-order");
			Log(f, "[ScrollFollowDebugProduct] own_source_behind_debug=1 stage=initial-open foreground_source=%u source_previous=%p debug_next=%p\n", GetForegroundWindow() == f.source, GetWindow(f.source, GW_HWNDPREV), GetWindow(initialDebug, GW_HWNDNEXT));
			const auto beforeRestart = LiveCapture::GetSnapshot();
			if (Business::GetSnapshot().state != Business::State::Off || beforeRestart.publishing || beforeRestart.occupied || beforeRestart.inFlight)
				throw std::runtime_error("debug-open-must-precede-capture");
			stage = "debug-real-restart-after-open";
			Business::RequestToggle();
			if (!Wait(f, [&]
			{
				const auto business = Business::GetSnapshot(); const auto capture = LiveCapture::GetSnapshot();
				return business.state == Business::State::Tracking && business.session != absentBusiness && capture.session != absentCapture && capture.publishing &&
					ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()) && !IsWindowVisible(service.Handle(Role::Drawpad)) &&
					!IsWindowVisible(service.Handle(Role::DrawpadPresentation));
			}, 8000)) throw std::runtime_error("debug-restart-real-f0-tracking-ack");
			channel = Draw::ProductHost().FollowChannel();
			if (!channel) throw std::runtime_error("debug-restarted-channel");
			base = channel->Read(); const auto openCapture = LiveCapture::GetSnapshot().session;
			if (base.session == absentBusiness || base.session != Business::GetSnapshot().session || base.phase != Draw::Follow::Phase::Active)
				throw std::runtime_error("debug-restarted-channel-identity");
			Log(f, "[ScrollFollowDebugProduct] comparison_sessions absent_business=%llu absent_capture=%llu open_business=%llu open_capture=%llu open_closed_same_session_required=1 absent_open_same_session=0 performance_causal_comparison=0 startup_open_before_capture=1\n",
				absentBusiness, absentCapture, base.session, openCapture);
			if (!Wait(f, [&] { const auto ui = DebugWindow::ReadProbe(); return ui.open && ui.hasFrame && ui.paints > 0 && ui.businessSession == base.session && ui.captureSession == openCapture; }, 5000))
				throw std::runtime_error("debug-first-tracker-packet");
			DebugCoveredPixels(f, "open-before-performance");
			stage = "debug-open-phase";
			DebugPhase(f, channel, 1, "open");
			stage = "debug-open-real-wheel-source-focus";
			CheckDesktop(f, stage);
			const HWND openDebug = service.Handle(Role::ScrollFollowDebug);
			(void)SetForegroundWindow(f.source);
			// 只摆放本进程置顶诊断窗，保留 Source 前台；不改变外部窗口或绕过激活限制。
			if (!SetWindowPos(openDebug, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) ||
				!SetWindowPos(f.source, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) ||
				!Wait(f, [&] { return GetForegroundWindow() == f.source && WindowFromPoint({32, f.height/2}) == f.source; }, 2000))
				throw std::runtime_error("debug-open-source-focus-or-hit");
			stage = "debug-open-wheel-forward-32";
			InkWheel(f, channel, -2 * WHEEL_DELTA, beforeX, beforeY, "debug-open-forward32");
			DebugCoveredPixels(f, "open-after-forward32");
			stage = "debug-open-wheel-reverse-16";
			InkWheel(f, channel, WHEEL_DELTA, beforeX, beforeY, "debug-open-reverse16");
			DebugCoveredPixels(f, "open-after-reverse16");
			// 此入口验证性能和生命周期；交互控件通过正常启动入口另行验证。
			Log(f, "[ScrollFollowDebugProduct] interactive_controls_tested=0 manual_entry=normal_startup\n");
			DebugCoveredPixels(f, "open-before-close");
			stage = "debug-production-close";
			std::weak_ptr<Business::Debug::Channel> retired = Business::Debug::Current();
			const HWND debug = service.Handle(Role::ScrollFollowDebug);
			SendMessageW(debug, WM_CLOSE, 0, 0);
			if (!Wait(f, [&] { return !service.Handle(Role::ScrollFollowDebug) && !DebugWindow::Handle() &&
				!Business::Debug::Enabled() && retired.expired(); }, 4000) || IsWindow(debug)) throw std::runtime_error("debug-close-not-drained");
			// 回到原夹具专用源的层级，所有闭窗阶段恢复完整99点真值检查。
			if (!SetWindowPos(f.source, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER) ||
				!service.SetOverlayTopmost(true) || !SetWindowPos(f.source, service.OverlayRoot(), 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) throw std::runtime_error("debug-closed-source-order");
			(void)SetForegroundWindow(f.source);
			if (!Wait(f, [&] { return GetForegroundWindow() == f.source; }, 2000)) throw std::runtime_error("debug-closed-source-foreground");
			Log(f, "[ScrollFollowDebugProduct] close actual_destroy=1 publisher_disabled=1 old_channel_released=1 old_channel_storage_released=1 same_follow_session=%llu\n", channel->Read().session);
			stage = "debug-closed-phase";
			DebugPhase(f, channel, 2, "closed");
		}
		// GT只比较真实系统滚轮结果，不向Capture/Tracker/Channel写目标。
		if (externalEmpty)
		{
			stage="external-real-os-wheel";
			const auto acceptedBefore=LiveCapture::GetSnapshot().statistics.accepted;
			WriteExternal(f,L"inkeys-tracking-ready.json",2,base.session,base.baseX,base.baseY);
			Json::Value motion;
			if (!Wait(f,[&]{return ReadExternal(f,L"browser-wheel-result.json",motion);},20000)) throw std::runtime_error("external-wheel-result-timeout");
			CheckExternalIdentity(f,motion,3,base.session); CheckExternalObservation(f,motion["before"]); CheckExternalObservation(f,motion["after"]);
			if (!motion["screenVerified"].isBool() || !motion["screenVerified"].asBool() || !motion["osWheel"].isUInt() || motion["osWheel"].asUInt()!=1 ||
				ExternalNumber(motion["after"],"wheelCount")!=ExternalNumber(motion["before"],"wheelCount")+1 ||
				ExternalNumber(motion["after"],"scrollCount")<=ExternalNumber(motion["before"],"scrollCount")) throw std::runtime_error("external-wheel-observation");
			for (const char* key:{"scrollX","scrollY","wheelCount","scrollCount"})
				if (ExternalNumber(motion["before"],key)!=ExternalNumber(f.external.identity["observation"],key)) throw std::runtime_error("external-page-moved-before-wheel");
			const double dpr=ExternalNumber(motion["after"],"dpr");
			f.external.dx=(ExternalNumber(motion["after"],"scrollX")-ExternalNumber(motion["before"],"scrollX"))*dpr;
			f.external.dy=(ExternalNumber(motion["after"],"scrollY")-ExternalNumber(motion["before"],"scrollY"))*dpr;
			if (std::abs(f.external.dx)>0.01 || f.external.dy<=0 || f.external.dy>f.height) throw std::runtime_error("external-unexpected-wheel-motion");
			Log(f,"[ProductLiveExternal] independent_gt delta_physical=(%.6f,%.6f) dpr=%.6f os_wheel=1 screen_marker=1\n",f.external.dx,f.external.dy,dpr);
			unsigned settled=0; std::uint64_t lastSequence=0;
			if (!Wait(f,[&]
			{
				if (Business::GetSnapshot().state!=Business::State::Tracking) throw std::runtime_error("external-paused-or-ended");
				const auto value=channel->Read();
				if (value.latest.sequence==lastSequence) return false;
				lastSequence=value.latest.sequence;
				const bool right=std::abs(value.latest.x+f.external.dx)<=2 && std::abs(value.latest.y+f.external.dy)<=2 &&
					std::abs(value.viewportX-base.baseX-f.external.dx)<=2 && std::abs(value.viewportY-base.baseY-f.external.dy)<=2;
				settled=right && value.applied>=value.latest.sequence && !value.clamped ? settled+1 : 0;
				return settled>=2 && LiveCapture::GetSnapshot().statistics.accepted>=acceptedBefore+3;
			},8000)) throw std::runtime_error("external-real-tracker-owner-motion");
			f.external.identity["after"]=motion["after"];
		}
		else if (ink)
		{
			stage = "ink-pixels-tracking";
			const auto handoff=ReadInkScreen(f,aRoi,aDown.y,false,L"ink-a-selection.png");
			CompareInk(f,aBaseline,handoff,0,"real-handoff");
			stage = "ink-wheel-forward-32";
			InkWheel(f,channel,-2*WHEEL_DELTA,beforeX,beforeY,"forward32");
			const auto forward=ReadInkScreen(f,aRoi,aDown.y-32,false,L"ink-a-forward32.png");
			CompareInk(f,aBaseline,forward,-32,"forward32");
			stage = "ink-wheel-reverse-16";
			InkWheel(f,channel,WHEEL_DELTA,beforeX,beforeY,"reverse16");
			const auto reverse=ReadInkScreen(f,aRoi,aDown.y-16,false,L"ink-a-reverse16.png");
			CompareInk(f,aBaseline,reverse,-16,"reverse16-final-offset");
			if (longScroll)
			{
				stage="ink-hwheel-forward-32";
				InkWheel(f,channel,2*WHEEL_DELTA,beforeX,beforeY,"horizontal-forward32",true);
				const auto hforward=ReadInkScreen(f,aRoi,aDown.y-16,false,L"ink-a-horizontal-forward32.png");
				CompareInk(f,aBaseline,hforward,-16,"horizontal-forward32",-32);
				stage="ink-hwheel-reverse-16";
				InkWheel(f,channel,-WHEEL_DELTA,beforeX,beforeY,"horizontal-reverse16",true);
				const auto hreverse=ReadInkScreen(f,aRoi,aDown.y-16,false,L"ink-a-horizontal-reverse16.png");
				CompareInk(f,aBaseline,hreverse,-16,"horizontal-reverse16-final-2d",-16);
				expectedWheelCount+=2;
				stage="ink-ten-primary-heights-roundtrip";
				expectedWheelCount+=LongVerticalRoundtrip(f,channel,aDown,aRoi.right-96,beforeX,beforeY);
				stage="ink-long-returned-pixels";
				const auto returned=ReadInkScreen(f,aRoi,aDown.y-16,false,L"ink-a-long-returned.png");
				CompareInk(f,aBaseline,returned,-16,"long-returned-2d",-16);
			}
		}
		else if (!debuggerProbe)
		{
			stage = "send-input-wheel";
			if (!ScreenSourceGt(f, "before-os-input", nullptr)) throw std::runtime_error("pre-input-source-gt");
			CheckDesktop(f, stage);
			const POINT center{ f.width / 2, f.height / 2 };
			if (GetForegroundWindow() != f.source || WindowFromPoint(center) != f.source) throw std::runtime_error("wheel-target-is-not-own-source");
			const int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN), virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
			if (virtualWidth < 2 || virtualHeight < 2) throw std::runtime_error("virtual-screen");
			INPUT move{}; move.type = INPUT_MOUSE;
			move.mi.dx = static_cast<LONG>((static_cast<std::int64_t>(center.x - GetSystemMetrics(SM_XVIRTUALSCREEN)) * 65535) / (virtualWidth - 1));
			move.mi.dy = static_cast<LONG>((static_cast<std::int64_t>(center.y - GetSystemMetrics(SM_YVIRTUALSCREEN)) * 65535) / (virtualHeight - 1));
			move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK; move.mi.dwExtraInfo = inputTag;
			if (SendInput(1, &move, sizeof(INPUT)) != 1) throw std::runtime_error("os-mouse-move");
			if (!Wait(f, [&] { POINT p{}; return GetCursorPos(&p) && std::abs(p.x - center.x) <= 2 && std::abs(p.y - center.y) <= 2; }, 1000))
				throw std::runtime_error("os-cursor-position");
			CheckDesktop(f, "before-os-wheel");
			POINT actualCursor{};
			// 系统滚轮按当前光标命中路由；不能以先前确认的固定center代替当前实际位置。
			if (!GetCursorPos(&actualCursor) || std::abs(actualCursor.x - center.x) > 2 || std::abs(actualCursor.y - center.y) > 2 ||
				GetForegroundWindow() != f.source || WindowFromPoint(actualCursor) != f.source)
				throw std::runtime_error("wheel-target-changed");
			const auto acceptedBefore = LiveCapture::GetSnapshot().statistics.accepted;
			INPUT wheel{}; wheel.type = INPUT_MOUSE; wheel.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA);
			wheel.mi.dwFlags = MOUSEEVENTF_WHEEL; wheel.mi.dwExtraInfo = inputTag;
			if (SendInput(1, &wheel, sizeof(INPUT)) != 1) throw std::runtime_error("os-wheel-send");
			if (!Wait(f, [&] { return f.taggedWheels == 1 && f.paintedY == beforeY + wheelStep; }, 3000))
				throw std::runtime_error("source-did-not-receive-os-wheel");
			if (!ScreenSourceGt(f, "after-os-wheel", L"source-after-wheel.png")) throw std::runtime_error("wheel-screen-gt");
			unsigned settled = 0; std::uint64_t lastSequence = 0;
			if (!Wait(f, [&]
			{
				if (Business::GetSnapshot().state != Business::State::Tracking) throw std::runtime_error("paused-or-ended-during-wheel");
				const auto value = channel->Read();
				if (value.latest.sequence == lastSequence) return false;
				lastSequence = value.latest.sequence;
				const bool right = std::abs(value.latest.x + f.x - beforeX) <= 2.0 && std::abs(value.latest.y + f.y - beforeY) <= 2.0 &&
					std::abs(value.viewportX - base.baseX - (f.x - beforeX)) <= 2.0 && std::abs(value.viewportY - base.baseY - (f.y - beforeY)) <= 2.0;
				settled = right && value.applied >= value.latest.sequence && !value.clamped ? settled + 1 : 0;
				return settled >= 2 && LiveCapture::GetSnapshot().statistics.accepted >= acceptedBefore + 3;
			}, 8000)) throw std::runtime_error("real-tracker-owner-motion");
		}
		Facts(f, "wheel-settled");
		stage = "business-off";
		Business::RequestStop();
		if (!Wait(f, [&]
		{
			return Business::GetSnapshot().state == Business::State::Off && !channel->Blocked() &&
				ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()) &&
				IsWindowVisible(service.Handle(Role::Drawpad)) && !IsWindowVisible(service.Handle(Role::DrawpadPresentation)) &&
				!LiveCapture::GetSnapshot().occupied;
		}, 10000)) throw std::runtime_error("real-end-and-reverse-window-ack");
		const auto final = channel->Read();
		const double expectedX=externalEmpty ? f.external.dx : (longScroll ? wheelStep : 0), expectedY=externalEmpty ? f.external.dy : wheelStep;
		if (final.phase != Draw::Follow::Phase::Complete || !final.handoff || final.applied != final.final.sequence || final.clamped ||
			(!externalEmpty && (f.x != beforeX + (longScroll ? wheelStep : 0) || f.y != beforeY + wheelStep)) ||
			std::abs(final.viewportX - base.baseX - expectedX) > 2.0 ||
			std::abs(final.viewportY - base.baseY - expectedY) > 2.0 ||
			GetStateModeVersionedSnapshot().revision != realTool.revision || GetStateModeSnapshot().StateModeSelect != StateModeSelectEnum::IdtPen ||
			Draw::ProductRuntimeSnapshot().contentRevision != initialContent || (!externalEmpty && (f.foreignWheels || f.taggedWheels != expectedWheelCount)))
			throw std::runtime_error("final-session-tool-content-invariants");
		Facts(f, "off-real-reverse-ack");
		if (externalEmpty)
		{
			WriteExternal(f,L"inkeys-off-ready.json",4,base.session,base.baseX,base.baseY);
			Json::Value ended;
			if (!Wait(f,[&]{return ReadExternal(f,L"browser-final.json",ended);},10000)) throw std::runtime_error("external-final-timeout");
			CheckExternalIdentity(f,ended,5,base.session); CheckExternalObservation(f,ended["after"]);
			if (!ended["screenVerified"].isBool() || !ended["screenVerified"].asBool()) throw std::runtime_error("external-final-marker");
			for (const char* key:{"scrollX","scrollY","wheelCount","scrollCount"})
				if (ExternalNumber(ended["after"],key)!=ExternalNumber(f.external.identity["after"],key)) throw std::runtime_error("external-page-moved-after-off");
		}
		else if (!ScreenSourceGt(f, "after-off", L"source-after-off.png")) throw std::runtime_error("off-source-screen-gt");
		if (debuggerProbe)
		{
			stage = "debug-no-reopen-off-on";
			// 新会话的独立 GT 使用当前真实文档位置，不能继续比较上一会话的起点。
			f.debugNumbers->InitialPose(f.x, f.y);
			Business::RequestToggle();
			if (!Wait(f, [&] { return Business::GetSnapshot().state == Business::State::Tracking &&
				ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()) && LiveCapture::GetSnapshot().statistics.accepted >= 3; }, 8000))
				throw std::runtime_error("debug-closed-restart-real-tracking");
			if (DebugWindow::Handle() || service.Handle(Role::ScrollFollowDebug) || Business::Debug::Enabled()) throw std::runtime_error("debug-unexpected-auto-reopen");
			Business::RequestStop();
			if (!Wait(f, [&] { return Business::GetSnapshot().state == Business::State::Off && !channel->Blocked() &&
				ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch()) && IsWindowVisible(service.Handle(Role::Drawpad)) &&
				!IsWindowVisible(service.Handle(Role::DrawpadPresentation)) && !LiveCapture::GetSnapshot().occupied; }, 10000))
				throw std::runtime_error("debug-closed-restart-real-off-ack");
			if (DebugWindow::Handle() || Business::Debug::Enabled()) throw std::runtime_error("debug-reopened-after-off");
			Log(f, "[ScrollFollowDebugProduct] off_on_off real_business=1 actual_window_ack=1 capture_drained=1 automatic_reopen=0 final_source_delta=(%d,%d)\n", f.x-beforeX, f.y-beforeY);
		}
		if (ink)
		{
			stage="ink-off-pixels";
			const auto off=ReadInkScreen(f,aRoi,aDown.y-16,false,L"ink-a-off-primary.png");
			CompareInk(f,aBaseline,off,-16,"off-primary",longScroll ? -16 : 0);
			stage="real-os-ink-b";
			const bool colorAccepted = SetPenColor(RGBA(0,48,255,255),false);
			InkToolFacts(f,"B-after-setter",false,false,true,colorAccepted,false,false);
			const bool toolReady = colorAccepted && Wait(f,[&]{return PrimaryReady() && ScrollFollowPresentationAcknowledged(ScrollFollowProjectionEpoch());},3000);
			InkToolFacts(f,"B-after-wait",false,false,true,colorAccepted,colorAccepted,toolReady);
			if (!toolReady) throw std::runtime_error("ink-b-real-tool-ready");
			const int margin = longScroll ? 96 : 48;
			bDown=DrawOsInk(f,{bRoi.left+margin,bRoi.bottom-32},{bRoi.right-margin,bRoi.bottom-32},"B");
			(void)ReadInkScreen(f,bRoi,bDown.y,true,L"ink-b-primary.png");
			stage="ink-original-exit-read";
			Draw::StopProduct(); productStarted=false;
			ReadInkExit(f,final,aDown,bDown,initialDate);
			Log(f,"[ProductLiveInk] bounded_case=PASS source_total=(%d,%d) final_viewport=(%.4f,%.4f) real_os_strokes=2 synthetic_sent=0 content_pair=(0,1) history_matrix=not_run long_scroll=%s hardware_pen=not_run\n",f.x-beforeX,f.y-beforeY,final.viewportX,final.viewportY,longScroll ? "ten-primary-heights-2d-roundtrip-runtime-passed-initial-survivor-log-check-required" : "not_run");
		}
		else if (externalEmpty)
			Log(f,"[ProductLiveExternal] bounded_case=PASS delta=(%.6f,%.6f) final_viewport=(%.6f,%.6f) visible_ink=not_run persistence=not_run browser_mode=private-app\n",f.external.dx,f.external.dy,final.viewportX,final.viewportY);
		else
		Log(f, "[ProductLive] bounded_case=PASS source_delta=(%d,%d) final_viewport=(%.4f,%.4f) end_frames=%llu recovery_frames=%llu visible_ink_case=not_run real_pen_contact_case=not_run persistence_readback_case=not_run\n",
			f.x - beforeX, f.y - beforeY, final.viewportX, final.viewportY, final.endFrames, final.recoveryFrames);
		result = 0;
	}
	catch (const std::exception& e)
	{
		Log(f, "[ProductLive] FAIL stage=%s reason=%s error=%lu security=%u\n", stage, e.what(), GetLastError(), f.security);
		if (productStarted) Facts(f, stage);
		if (debuggerProbe) DebugFailureFacts(f, stage);
	}
	catch (...) { Log(f, "[ProductLive] FAIL stage=%s reason=unknown-exception\n", stage); if (debuggerProbe) DebugFailureFacts(f, stage); }
	if (!ReleaseOwnLeft(f)) result = 1;
	if (f.security) result = 4;
	// 正常先真实Off；失败/安全层只单调Shutdown，绝不手写ACK或让StateMonitoring再显示窗口。
	if (businessBegun && !f.security && result != 0)
	{
		try { Business::RequestStop(); (void)Wait(f, [&] { return Business::GetSnapshot().state == Business::State::Off; }, 5000); }
		catch (...) {}
	}
	// 清理等待中也可能出现安全层；正常Off路径仍保留上面的真实反向ACK。
	if (f.security) { result = 4; Inkeys::Window::GetService().BeginShutdown(); }
	Business::RequestShutdown();
	offSignal = 1;
	if (stateThread.joinable()) stateThread.join();
	if (productStarted) Draw::StopProduct();
	StopMagnifierCoordinator();
	if (magThread.joinable()) magThread.join();
	auto& service = Inkeys::Window::GetService();
	if (serviceStarted || service.Running()) { service.BeginShutdown(); service.StopAndJoin(); }
	DebugWindow::ShutdownExports();
	if (f.debugNumbers) { Business::LiveTrace::Install(nullptr); f.debugNumbers.reset(); }
	if (f.source && !externalEmpty) DestroyWindow(f.source);
	if (classRegistered) UnregisterClassW(sourceClass, GetModuleHandleW(nullptr));
	f.texture.reset();
	if (f.trace)
	{
		// 原始回调线程退出后再卸载/落盘；观察器自身拥有句柄，迟到引用不依赖Fixture。
		Business::LiveTrace::Install(nullptr);
		const bool drained=f.stateReturned.load() && f.magReturned.load() && !f.stateFailed.load() && !f.magFailed.load() &&
			!Draw::ProductRunning() && !service.Running() && !magnificationCreateReady.load() && !LiveCapture::GetSnapshot().occupied && !LiveCapture::GetSnapshot().inFlight;
		try
		{
			const bool complete=f.trace->Save(drained,result);
			Log(f,"[MotionTrace] saved=%u original_threads_drained=%u manifest=%ls/motion-trace.json failure=%s\n",complete,drained,f.root.c_str(),f.trace->Failure() ? f.trace->Failure() : "none");
			if (!complete && result!=4) result=1;
		}
		catch (const std::exception& e) { Log(f,"[MotionTrace] save_failed=%s\n",e.what()); if(result!=4)result=1; }
		catch (...) { Log(f,"[MotionTrace] save_failed=unknown-exception\n"); if(result!=4)result=1; }
	}
	if (displayStarted) Inkeys::Display::Shutdown();
	if (comStarted) CoUninitialize();
	if (result == 0 && (!f.stateReturned.load() || !f.magReturned.load() || f.stateFailed.load() || f.magFailed.load() ||
		Draw::ProductRunning() || service.Running() || magnificationCreateReady || LiveCapture::GetSnapshot().occupied || f.ownLeftHeld || f.logFailed.load())) result = 1;
	Log(f, "[ProductLive] cleanup state_tid=%lu state_returned=%u state_failed=%u mag_tid=%lu mag_returned=%u mag_failed=%u product_running=%u service_running=%u mag_ready=%u capture_occupied=%u own_left_held=%u log_failed=%u result=%d\n",
		f.stateTid.load(), f.stateReturned.load(), f.stateFailed.load(), f.magTid.load(), f.magReturned.load(), f.magFailed.load(),
		Draw::ProductRunning(), service.Running(), magnificationCreateReady.load(), LiveCapture::GetSnapshot().occupied, f.ownLeftHeld, f.logFailed.load(), result);
	IDTLogger = oldLogger; globalPath = oldGlobalPath;
	if (f.log != INVALID_HANDLE_VALUE) { FlushFileBuffers(f.log); CloseHandle(f.log); }
	if (cleanupDone) SetEvent(cleanupDone);
	if (watchdog.joinable()) watchdog.join();
	if (cleanupDone) CloseHandle(cleanupDone);
	return result;
}
int RunScrollFollowDebugProductTest() { return RunScrollFollowProductLiveCase(false,false,false,true); }
int RunScrollFollowProductLiveTest() { return RunScrollFollowProductLiveCase(false); }
int RunScrollFollowProductLiveInkTest() { return RunScrollFollowProductLiveCase(true); }
int RunScrollFollowProductLiveLongTest() { return RunScrollFollowProductLiveCase(true,true); }
int RunScrollFollowProductLiveExternalTest()
{
	if (!GetEnvironmentVariableW(L"INKEYS_SCROLL_EXTERNAL_ROOT",nullptr,0)) return 2;
	return RunScrollFollowProductLiveCase(false,false,true);
}
#else
int RunScrollFollowDebugProductTest() { return 2; }
int RunScrollFollowProductLiveTest() { return 2; }
int RunScrollFollowProductLiveInkTest() { return 2; }
int RunScrollFollowProductLiveLongTest() { return 2; }
int RunScrollFollowProductLiveExternalTest() { return 2; }
#endif
