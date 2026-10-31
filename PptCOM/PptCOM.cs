/*
 * @file		PptCOM.cs
 * @brief		智绘教项目 PPT 联动插件
 * @note		PPT 联动插件 相关模块
 *
 * @envir		.NET Framework 4.0
 * @site		https://github.com/Alan-CRL/Inkeys
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// 首次编译需要确认 .NET Framework 版本为 4.0，如果不一致请执行 <切换 .NET Framework 指南>
/////////////////////////////////////////////////////////////////////////////
// 切换 .NET Framework 指南
// .NET 版本默认为 .NET Framework 4.0 ，最低要求 .NET Framework 4.0
//
// 修改属性页中的指定框架
//
// 确认 PptCOM.manifest 中的 runtimeVersion 是你设置的版本全称（C:\Windows\Microsoft.NET\Framework），如 4.0.30319
// 生成 -> 清理 PptCOM，然后点击重新生成解决方案
//
// 其余疑问请咨询作者 QQ2685549821
/////////////////////////////////////////////////////////////////////////////

using Microsoft.Office.Core;
using Microsoft.Office.Interop.PowerPoint;
using System;
using System.CodeDom;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace PptCOM
{
    internal sealed class SlideShowWindowEvidence
    {
        public IntPtr Handle;
        public uint ProcessId;
        public string ClassName;
        public string Title;

        public static IntPtr Select(IEnumerable<SlideShowWindowEvidence> candidates,
            uint processId, string fileName)
        {
            if (processId == 0 || string.IsNullOrEmpty(fileName)) return IntPtr.Zero;
            SlideShowWindowEvidence selected = null;
            foreach (var candidate in candidates)
            {
                if (candidate.Handle == IntPtr.Zero || candidate.ProcessId != processId ||
                    !string.Equals(candidate.ClassName, "screenClass", StringComparison.OrdinalIgnoreCase)) continue;
                if (selected != null) return IntPtr.Zero;
                selected = candidate;
            }
            return selected != null && selected.Title != null &&
                selected.Title.IndexOf(fileName, StringComparison.OrdinalIgnoreCase) >= 0
                ? selected.Handle : IntPtr.Zero;
        }
    }

    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("65F6E9C1-63EC-4003-B89F-8F425A3C2FEA")]
    public interface IPptCOMServer
    {
        // 初始化函数
        unsafe bool Initialization(int* TotalPage, int* CurrentPage, int* OffSignal);
        string CheckCOM();

        // 获取函数
        unsafe int PptComService();

        // 信息获取函数
        string SlideNameIndex();
        IntPtr GetPptHwnd();
        int GetSlideShowAnnotationTool();
        bool ExitSlideShowAnnotationTool();

        // 操控函数
        void NextSlideShow(bool check);
        void PreviousSlideShow();
        void EndSlideShow();
        void ViewSlideShow();
        void ActivateSildeShowWindow();

        // 追加在接口末尾，保持既有 COM 方法顺序不变。
        void SetConsoleOutputEnabled(bool enabled);
        string GetPresentationDescriptor();
    }

    // 独立查询扩展，不能向旧 IUnknown vtable 插入方法。
    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("D7A63F98-43B8-4BA1-A875-B55A5A04DC3E")]
    public interface IPptCOMSessionState
    {
        string GetSlideShowState(long afterRevision);
        int EndSlideShowIfSession(long expectedShowSessionRevision);
    }

    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    [Guid("C44270BE-9A52-400F-AD7C-ED42050A77D8")]
    public class PptCOMServer : IPptCOMServer, IPptCOMSessionState
    {
        private dynamic pptApplication;
        private dynamic pptActivePresentation;
        private dynamic pptSlideShowWindow;

        private readonly object presentationDescriptorLock = new object();
        private readonly PresentationDescriptorReader presentationDescriptorReader;
        private readonly ILateBoundComAccessor presentationAccessor;
        private readonly SlideShowSessionCache slideShowSession = new SlideShowSessionCache();
        private readonly SlideShowOwnerMailbox slideShowOwner = new SlideShowOwnerMailbox();
        private readonly object annotationToolLock = new object();
        private long annotationToolSession, annotationToolBinding, annotationToolWindow;
        private int annotationToolCache;
        private PresentationDescriptorValue presentationDescriptorCache =
            PresentationDescriptorValue.CreateStatus("Unavailable", 0);
        private long presentationBindingRevision;
        private int presentationDescriptorRefreshPending = 1;
        private DateTime nextPresentationDescriptorRefreshUtc = DateTime.MinValue;
        private DateTime presentationDescriptorRetryUtc = DateTime.MaxValue;
        private int showObservationPending = 1;
        private string slideNameCache = string.Empty;
        private long slideShowHwndCache;
        private ComEventConnection eventConnection;

        private unsafe int* pptTotalPage;
        private unsafe int* pptCurrentPage;
        private unsafe int* offSignal;

        // 结束界面轮询（0正常页 1/2末页或结束放映页）
        //（2设定为运行一次不被检查的翻页，虽然我也不知道当时写这个是为了特判什么情况 hhh）
        private int polling = 0;

        private bool forcePolling = false; // 强制轮询标志
        private bool bindingEvents; // 是否已绑定事件


        private const int SlideShowAnnotationToolNone = 0;
        private const int SlideShowAnnotationToolPen = 1;
        private const int SlideShowAnnotationToolLaser = 2;
        private const int SlideShowAnnotationToolHighlighter = 3;

        private const int SlideShowPointerNone = 0;
        private const int SlideShowPointerArrow = 1;
        private const int SlideShowPointerPen = 2;
        private const int SlideShowPointerAlwaysHidden = 3;
        private const int SlideShowPointerAutoArrow = 4;

        public PptCOMServer() : this(new MarshalLateBoundComAccessor()) { }

        internal PptCOMServer(ILateBoundComAccessor accessor)
        {
            presentationAccessor = accessor;
            presentationDescriptorReader = new PresentationDescriptorReader(
                presentationAccessor, ResolveWindowProcessId, ResolveSlideShowHwnd);
        }

        private static Type[] LegacyEventTypeMetadata()
        {
            // 仅保留旧 TLB 已导出的委托类型；不实例化、不订阅，生产事件走 raw 连接点。
            return new[] {
                typeof(EApplication_SlideShowNextSlideEventHandler),
                typeof(EApplication_SlideShowBeginEventHandler),
                typeof(EApplication_SlideShowEndEventHandler),
                typeof(EApplication_PresentationBeforeCloseEventHandler)
            };
        }

        // 初始化函数
        public unsafe bool Initialization(int* TotalPage, int* CurrentPage, int* OffSignal)
        {
            try
            {
                pptTotalPage = TotalPage;
                pptCurrentPage = CurrentPage;
                offSignal = OffSignal;

                return true;
            }
            catch
            {
            }

            return false;
        }
        public string CheckCOM()
        {
            string ret = "20261024a";
            return ret;
        }

        public void SetConsoleOutputEnabled(bool enabled)
        {
            // 托管 Console 独立开关，避免 Draw3 分配控制台后带出 PptCOM 日志。
            if (!enabled)
            {
                Console.SetOut(TextWriter.Null);
                Console.SetError(TextWriter.Null);
                return;
            }

            try
            {
                Console.SetOut(new StreamWriter(Console.OpenStandardOutput()) { AutoFlush = true });
                Console.SetError(new StreamWriter(Console.OpenStandardError()) { AutoFlush = true });
            }
            catch
            {
                Console.SetOut(TextWriter.Null);
                Console.SetError(TextWriter.Null);
            }
        }

        public string GetPresentationDescriptor()
        {
            try
            {
                PresentationDescriptorValue snapshot;
                lock (presentationDescriptorLock)
                {
                    snapshot = presentationDescriptorCache.Clone();
                }
                // COM getter 只复制纯托管缓存；序列化期间不持锁，也不访问 Office。
                return PresentationDescriptorJson.Serialize(snapshot);
            }
            catch
            {
                return PresentationDescriptorJson.SerializeUnavailable(
                    Interlocked.Read(ref presentationBindingRevision));
            }
        }

        public string GetSlideShowState(long afterRevision)
        {
            return slideShowSession.GetSince(afterRevision);
        }

        public int EndSlideShowIfSession(long expectedShowSessionRevision)
        {
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            if (stamp.Session != expectedShowSessionRevision ||
                !slideShowSession.Matches(expectedShowSessionRevision)) return 0;
            // 模态业务线程等待 owner 的实际执行结果；超时后排队请求失效。
            return RequestOwnerCommand(stamp, SlideShowOwnerOperation.ExitShow, false);
        }

        private void ProcessSlideShowExitRequests()
        {
            slideShowOwner.ProcessExits(ExecuteOwnerCommand);
        }

        private void PublishSlideShowLifecycle()
        {
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            slideShowSession.Publish(stamp,
                PresentationDescriptorValue.CreateStatus("Unavailable", stamp.Binding), "Unknown");
        }

        private static int ResolveWindowProcessId(IntPtr window)
        {
            try
            {
                uint processId;
                GetWindowThreadProcessId(window, out processId);
                return processId > int.MaxValue ? 0 : (int)processId;
            }
            catch
            {
                return 0;
            }
        }

        private void RequestPresentationDescriptorRefresh()
        {
            Interlocked.Exchange(ref presentationDescriptorRefreshPending, 1);
            slideShowOwner.Wake();
        }

        private void InvalidatePresentationDescriptor()
        {
            long revision = Interlocked.Increment(ref presentationBindingRevision);
            slideShowSession.InvalidateBinding(revision);
            PublishSlideShowLifecycle();
            lock (presentationDescriptorLock)
            {
                presentationDescriptorCache =
                    PresentationDescriptorValue.CreateStatus("Unavailable", revision);
            }
            RequestPresentationDescriptorRefresh();
            nextPresentationDescriptorRefreshUtc = DateTime.MinValue;
            presentationDescriptorRetryUtc = DateTime.MaxValue;
            Interlocked.Exchange(ref slideShowHwndCache, 0);
            Interlocked.Exchange(ref slideNameCache, string.Empty);
        }

        private void RefreshPresentationDescriptorIfNeeded(bool force)
        {
            DateTime now = DateTime.UtcNow;
            int pending = Interlocked.Exchange(
                ref presentationDescriptorRefreshPending, 0);
            if (!force && pending == 0 && now < nextPresentationDescriptorRefreshUtc &&
                now < presentationDescriptorRetryUtc) return;

            long revision = Interlocked.Read(ref presentationBindingRevision);
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            if (stamp.Lifecycle != "Active")
            {
                PublishSlideShowLifecycle();
                lock (presentationDescriptorLock)
                    presentationDescriptorCache = PresentationDescriptorValue.CreateStatus("Unavailable", revision);
                nextPresentationDescriptorRefreshUtc = now.AddSeconds(3);
                presentationDescriptorRetryUtc = DateTime.MaxValue;
                return;
            }
            string pageStatus;
            PresentationDescriptorValue candidate = presentationDescriptorReader.ReadShow(
                (object)pptApplication, (object)pptActivePresentation,
                (object)pptSlideShowWindow, revision, out pageStatus);
            if (!slideShowSession.IsCurrent(stamp))
            {
                RequestPresentationDescriptorRefresh();
                return;
            }

            if (!slideShowSession.Publish(stamp, candidate, pageStatus))
            {
                RequestPresentationDescriptorRefresh();
                return;
            }

            lock (presentationDescriptorLock)
            {
                if (revision != Interlocked.Read(ref presentationBindingRevision) ||
                    !slideShowSession.IsCurrent(stamp)) return;
                // 旧稳定画面由 native 保留；两个 getter 都发布本次证据，不能将旧页冒充 Valid。
                presentationDescriptorCache = candidate.Clone();
            }
            nextPresentationDescriptorRefreshUtc = now.AddSeconds(3);
            presentationDescriptorRetryUtc = candidate.status == "TransientBusy"
                ? now.AddMilliseconds(100) : DateTime.MaxValue;
            Interlocked.Exchange(ref slideShowHwndCache, candidate.slideShowHwnd);
            if (candidate.status != "TransientBusy")
            {
                string caption = string.Empty;
                try { caption = Convert.ToString(ReadScalar((object)pptApplication, "Caption"), CultureInfo.InvariantCulture); }
                catch { }
                string name = candidate.fullName + "\n" + caption;
                Interlocked.Exchange(ref slideNameCache,
                    Encoding.UTF8.GetByteCount(name) <= 32768 ? name : string.Empty);
            }
        }

        // 过程函数
        private static void CleanUpLoopObjects(IBindCtx bindCtx, IMoniker moniker, object comObject)
        {
            ReleaseMarshalObject(comObject);
            ReleaseMarshalObject(moniker);
            ReleaseMarshalObject(bindCtx);
        }
        private void SafeRelease(object comObj)
        {
            try { presentationAccessor.Release(comObj); }
            catch { }
        }
        private static void ReleaseMarshalObject(object comObj)
        {
            if (comObj == null) return;

            if (Marshal.IsComObject(comObj))
            {
                try
                {
                    Marshal.ReleaseComObject(comObj);
                }
                catch { }
            }
        }
        private void UnbindEvents()
        {
            ComEventConnection connection = eventConnection;
            eventConnection = null;
            if (connection != null)
            {
                connection.Deactivate();
                if (!connection.TryDisconnect()) ComEventRetirementRegistry.Retire(connection);
            }
            bindingEvents = false;
            forcePolling = true;
        }
        private void RetryRetiredEventConnections()
        {
            ComEventRetirementRegistry.Retry();
        }
        private unsafe void FullCleanup(bool final = false)
        {
            // 先让 native 无法消费旧 binding，再解绑事件和释放长期 RCW。
            InvalidatePresentationDescriptor();
            lock (annotationToolLock)
            {
                annotationToolSession = annotationToolBinding = annotationToolWindow = 0;
                annotationToolCache = SlideShowAnnotationToolNone;
            }
            UnbindEvents();

            // 诊断输出失败也不能中断 COM 根引用的确定性清理。
            try { Console.WriteLine("try CLEAN"); } catch { }

            // owner 已停止旧请求；每个 root 只配平本模块持有的一次取得，不清空共享 RCW 计数。
            SafeRelease((object)pptSlideShowWindow);
            SafeRelease((object)pptActivePresentation);
            SafeRelease((object)pptApplication);

            pptSlideShowWindow = null;
            pptActivePresentation = null;
            pptApplication = null;

            // 重置指针为 -1 (外部程序约定的结束标志)
            try
            {
                *pptTotalPage = -1;
                *pptCurrentPage = -1;
            }
            catch { }

            try { Console.WriteLine("CLEAN!"); } catch { }
        }
        private static bool IsBusyComErrorCode(int errorCode)
        {
            uint hr = unchecked((uint)errorCode);
            return hr == 0x8001010A || hr == 0x800AC472 || hr == 0x80010001;
        }
        private static bool TryGetBusyComErrorCode(Exception ex, out uint errorCode)
        {
            while (ex != null)
            {
                COMException comEx = ex as COMException;
                if (comEx != null && IsBusyComErrorCode(comEx.ErrorCode))
                {
                    errorCode = unchecked((uint)comEx.ErrorCode);
                    return true;
                }

                ex = ex.InnerException;
            }

            errorCode = 0;
            return false;
        }
        private static int CoerceToInt(object value, int fallbackValue = int.MinValue)
        {
            if (value == null) return fallbackValue;

            try
            {
                return Convert.ToInt32(value);
            }
            catch
            {
                return fallbackValue;
            }
        }
        private static string CoerceToText(object value)
        {
            if (value == null) return string.Empty;

            try
            {
                return Convert.ToString(value) ?? string.Empty;
            }
            catch
            {
                return string.Empty;
            }
        }
        private static bool HasSlideShowPointerName(object value, params string[] names)
        {
            string text = CoerceToText(value);
            if (string.IsNullOrWhiteSpace(text)) return false;

            foreach (string name in names)
            {
                if (string.Equals(text, name, StringComparison.OrdinalIgnoreCase) ||
                    text.EndsWith("." + name, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }

            return false;
        }
        // 判断函数
        private static bool AreComObjectsEqual(object o1, object o2)
        {
            if (o1 == null || o2 == null) return false;
            if (ReferenceEquals(o1, o2)) return true; // 优化：引用相等直接返回

            IntPtr pUnk1 = IntPtr.Zero;
            IntPtr pUnk2 = IntPtr.Zero;
            try
            {
                pUnk1 = Marshal.GetIUnknownForObject(o1);
                pUnk2 = Marshal.GetIUnknownForObject(o2);
                return pUnk1 == pUnk2;
            }
            catch { return false; }
            finally
            {
                if (pUnk1 != IntPtr.Zero) Marshal.Release(pUnk1);
                if (pUnk2 != IntPtr.Zero) Marshal.Release(pUnk2);
            }
        }
        private static string NormalizePresentationFullNameForCompare(string fullName)
        {
            if (string.IsNullOrWhiteSpace(fullName)) return "";
            return fullName.Trim();
        }
        private string TryGetPresentationFullNameForCompare(object presentationObj)
        {
            if (presentationObj == null) return "";

            try
            {
                object value = ReadScalar(presentationObj, "FullName");
                return NormalizePresentationFullNameForCompare(Convert.ToString(value, CultureInfo.InvariantCulture));
            }
            catch
            {
                return "";
            }
        }
        private string TryGetActivePresentationFullNameForCompare(object applicationObj)
        {
            if (applicationObj == null) return "";

            object activePresentation = null;

            try
            {
                activePresentation = presentationAccessor.GetProperty(applicationObj, "ActivePresentation");
                return TryGetPresentationFullNameForCompare(activePresentation);
            }
            catch
            {
                return "";
            }
            finally
            {
                // 相同 identity 仍是本次新取得；这里只比较，从未转移给 root。
                SafeRelease(activePresentation);
            }
        }
        private bool TryGetActiveSlideShowHwndForCompare(object applicationObj, out IntPtr hwnd)
        {
            hwnd = IntPtr.Zero;
            if (applicationObj == null) return false;

            object activePresentation = null;
            object slideShowWindow = null;

            try
            {
                activePresentation = presentationAccessor.GetProperty(applicationObj, "ActivePresentation");
                if (activePresentation == null) return false;

                slideShowWindow = presentationAccessor.GetProperty(activePresentation, "SlideShowWindow");
                hwnd = GetPptHwndFromSlideShowWindow(slideShowWindow);
                return hwnd != IntPtr.Zero;
            }
            catch
            {
                return false;
            }
            finally
            {
                SafeRelease(slideShowWindow);
                SafeRelease(activePresentation);
            }
        }
        private bool ShouldKeepCurrentBindingForSamePresentation(object bestApp)
        {
            IntPtr currentHwnd = GetPptHwndFromSlideShowWindow((object)pptSlideShowWindow);
            IntPtr bestHwnd;

            if (currentHwnd != IntPtr.Zero && TryGetActiveSlideShowHwndForCompare(bestApp, out bestHwnd))
            {
                return currentHwnd == bestHwnd;
            }

            string currentFullName = TryGetPresentationFullNameForCompare((object)pptActivePresentation);
            string bestFullName = TryGetActivePresentationFullNameForCompare(bestApp);

            if (currentFullName.Length == 0 || bestFullName.Length == 0) return false;
            return string.Equals(currentFullName, bestFullName, StringComparison.OrdinalIgnoreCase);
        }
        private bool IsSlideShowWindowActive(object sswObj)
        {
            try
            {
                dynamic ssw = sswObj;

                // 1. 获取前台窗口 (用户当前正在操作的窗口)
                IntPtr foregroundHwnd = GetForegroundWindow();
                if (foregroundHwnd == IntPtr.Zero) return false;

                // 获取前台窗口的 PID
                uint fgPid;
                GetWindowThreadProcessId(foregroundHwnd, out fgPid);

                // 2. 获取 COM App 对象的 PID
                IntPtr sswHwnd = IntPtr.Zero;
                try
                {
                    sswHwnd = GetPptHwndFromSlideShowWindow(sswObj);
                    // sswHwnd = GetSlideShowWindowHwnd(ssw);
                }
                catch { return false; }
                if (sswHwnd == IntPtr.Zero) return false;

                uint sswPid;
                GetWindowThreadProcessId(sswHwnd, out sswPid);

                // 匹配
                if (fgPid == sswPid) return true;

                // WPS 跨进程判定
                try
                {
                    using (Process fgProc = Process.GetProcessById((int)fgPid))
                    using (Process appProc = Process.GetProcessById((int)sswPid))
                    {
                        string fgName = fgProc.ProcessName.ToLower();
                        string appName = appProc.ProcessName.ToLower();

                        if (fgName.StartsWith("wps") && appName.StartsWith("wpp"))
                        {
                            return true;
                        }
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"  Process Name Check Failed: {ex.Message}");
                }

                return false;
            }
            catch
            {
                return false;
            }
        }
        private static bool LooksLikePresentationFile(string displayName)
        {
            if (string.IsNullOrEmpty(displayName))
                return false;

            string[] PptLikeExtensions = new[]
            {
            ".pptx", ".pptm", ".ppt",
            ".ppsx", ".ppsm", ".pps",
            ".potx", ".potm", ".pot",
            ".dps", ".dpt",
        };

            string lower = displayName.ToLowerInvariant();
            foreach (var ext in PptLikeExtensions)
            {
                if (lower.Contains(ext))
                    return true;
            }
            return false;
        }
        private static string[] GetApplicationMonikersFromProgIds()
        {
            string[] ApplicationProgIds = new[]
            {
                "PowerPoint.Application",
                "KWPP.Application",
                "Wpp.Application",
                "WPP.Application",
            };

            List<string> monikers = new List<string>();

            foreach (string progId in ApplicationProgIds)
            {
                Guid clsid;
                if (CLSIDFromProgID(progId, out clsid) == 0 && clsid != Guid.Empty)
                {
                    string moniker = "!" + clsid.ToString("B").ToUpperInvariant();
                    if (!ContainsMoniker(monikers, moniker))
                    {
                        monikers.Add(moniker);
                    }
                }
            }

            return monikers.ToArray();
        }
        private static bool ContainsMoniker(IEnumerable<string> monikers, string displayName)
        {
            if (monikers == null || string.IsNullOrEmpty(displayName))
                return false;

            foreach (string moniker in monikers)
            {
                if (string.Equals(displayName, moniker, StringComparison.OrdinalIgnoreCase))
                    return true;
            }

            return false;
        }
        private static bool IsFallbackApplicationMoniker(string displayName)
        {
            string[] FallbackApplicationMonikers = new[]
            {
                "!{91493441-5A91-11CF-8700-00AA0060263B}",
                "!{44720441-94BF-4940-926D-4F38FECF2A48}",
            };

            return ContainsMoniker(FallbackApplicationMonikers, displayName);
        }
        // 回调参数属于事件适配器的借用；只发提示，不跨回调持有或释放 WnObj。
        private void SlideShowChange(object WnObj)
        {
            slideShowSession.PageChanged();
            Interlocked.Exchange(ref showObservationPending, 1);
            RequestPresentationDescriptorRefresh();
        }
        private void SlideShowBegin(object WnObj)
        {
            slideShowSession.PageChanged();
            Interlocked.Exchange(ref showObservationPending, 1);
            RequestPresentationDescriptorRefresh();
        }
        private void SlideShowShowEnd(object WnObj)
        {
            // Begin/End 也可能迟到；事件只废弃在途结果，真正生命周期由 owner 的 fresh 观测认定。
            slideShowSession.PageChanged();
            Interlocked.Exchange(ref showObservationPending, 1);
            RequestPresentationDescriptorRefresh();
        }
        private void PresentationBeforeClose(object WnObj, ref bool cancel)
        {
            // 关闭通知也不能在回调内解绑/释放正在使用的 root。
            slideShowSession.PageChanged();
            Interlocked.Exchange(ref showObservationPending, 1);
            RequestPresentationDescriptorRefresh();
        }

        private void DispatchBoundEvent(long binding, int kind)
        {
            // cleanup 在解绑前推进 binding；旧连接点重入的通知不能污染新 binding。
            if (binding != Interlocked.Read(ref presentationBindingRevision)) return;
            if (kind == 2013) SlideShowChange(null);
            else if (kind == 2011) SlideShowBegin(null);
            else if (kind == 2014) SlideShowShowEnd(null);
            else if (kind == 2025)
            {
                slideShowSession.PageChanged();
                Interlocked.Exchange(ref showObservationPending, 1);
                RequestPresentationDescriptorRefresh();
            }
        }

        private void BindEvents()
        {
            UnbindEvents();
            RetryRetiredEventConnections();
            // 外部持续拒绝解除时限制待清理连接，不让反复重绑累积无界工作；基础轮询继续运行。
            if (ComEventRetirementRegistry.PendingCount >= 8) return;
            try
            {
                long binding = Interlocked.Read(ref presentationBindingRevision);
                eventConnection = ComEventConnection.Connect((object)pptApplication,
                    dispId => DispatchBoundEvent(binding, dispId));
                bindingEvents = true;
                forcePolling = false;
            }
            catch (Exception error)
            {
                Console.WriteLine("[PptCOM] events=0 polling=1 hr=0x" + ComFailure.ErrorCode(error).ToString("X8"));
            }
            finally
            {
                Console.WriteLine("[PptCOM] events=" + (bindingEvents ? 1 : 0) +
                    " polling=" + (forcePolling ? 1 : 0));
            }
        }

        // 获取函数
        private object GetAnyActivePowerPoint(object targetApp, out int bestPriority, out int targetPriority)
        {
            IRunningObjectTable rot = null;
            IEnumMoniker enumMoniker = null;

            object bestApp = null;

            bestPriority = 0;
            targetPriority = 0;
            int highestPriority = 0;

            System.Collections.Generic.List<object> foundAppObjects = new System.Collections.Generic.List<object>();

            try
            {
                int hr = GetRunningObjectTable(0, out rot);
                if (hr != 0 || rot == null) return null;

                rot.EnumRunning(out enumMoniker);
                if (enumMoniker == null) return null;

                IMoniker[] moniker = new IMoniker[1];
                IntPtr fetched = IntPtr.Zero;
                string[] applicationMonikersFromProgIds = GetApplicationMonikersFromProgIds();

                while (enumMoniker.Next(1, moniker, fetched) == 0)
                {
                    IBindCtx bindCtx = null;
                    object comObject = null;

                    dynamic candidateApp = null;
                    string displayName = "Unknown";

                    dynamic activePres = null;
                    dynamic ssWindow = null;

                    // 标记当前 candidateApp 是否需要“保活”供后续去重对比
                    bool keepAlive = false;

                    try
                    {
                        CreateBindCtx(0, out bindCtx);
                        moniker[0].GetDisplayName(bindCtx, null, out displayName);

                        bool looksLikePresentationFile = LooksLikePresentationFile(displayName);
                        bool isApplicationMoniker = ContainsMoniker(applicationMonikersFromProgIds, displayName);
                        if (!isApplicationMoniker)
                        {
                            isApplicationMoniker = IsFallbackApplicationMoniker(displayName);
                        }

                        if (looksLikePresentationFile || isApplicationMoniker)
                        {
                            rot.GetObject(moniker[0], out comObject);
                            if (comObject != null)
                            {
                                if (isApplicationMoniker)
                                {
                                    candidateApp = comObject;
                                    comObject = null;
                                }
                                else
                                {
                                    // 尝试通过 Presentation 对象获取 Application
                                    try
                                    {
                                        object appObj = presentationAccessor.GetProperty(comObject, "Application");
                                        candidateApp = appObj;
                                    }
                                    catch { }
                                }
                            }
                            else { }
                        }

                        // COM 对象去重检查
                        bool isDuplicate = false;
                        if (candidateApp != null)
                        {
                            foreach (var processedApp in foundAppObjects)
                            {
                                if (AreComObjectsEqual((object)candidateApp, processedApp))
                                {
                                    isDuplicate = true;

                                    Console.WriteLine("  -> [Deduplication] Skip: This COM instance was already scanned.");
                                    break;
                                }
                            }

                            if (!isDuplicate)
                            {
                                // 如果不是重复项，加入列表并标记保活
                                // 注意：必须保持此引用有效，后续循环才能进行对比
                                foundAppObjects.Add(candidateApp);
                                keepAlive = true;
                            }
                        }

                        if (candidateApp != null && !isDuplicate)
                        {
                            int currentPriority = 0;
                            bool isTarget = false;

                            // 1. 检查是否是 Target
                            if (targetApp != null && AreComObjectsEqual((object)candidateApp, targetApp))
                            {
                                isTarget = true;
                            }

                            // 2. 计算优先级
                            try
                            {
                                // 尝试获取 ActivePresentation
                                try
                                {
                                    activePres = candidateApp.ActivePresentation;
                                }
                                catch { }

                                if (activePres != null)
                                {
                                    currentPriority = 1;

                                    // 检查 SlideShowWindows
                                    try
                                    {
                                        ssWindow = activePres.SlideShowWindow;
                                    }
                                    catch
                                    {
                                    }

                                    if (ssWindow != null)
                                    {
                                        currentPriority = 2;

                                        try
                                        {
                                            // 判定 Active
                                            // MS PPT 返回 int (-1), WPS 可能返回 bool (true)
                                            bool isActive = false;
                                            try
                                            {
                                                object val = ReadScalar((object)ssWindow, "Active");
                                                if (val is int && (int)val == -1) isActive = true; // MsoTriState.msoTrue
                                                else if (val is bool && (bool)val == true) isActive = true;
                                            }
                                            catch { }

                                            if (isActive)
                                            {
                                                currentPriority = 3;
                                            }
                                            else
                                            {
                                                // 针对 WPP 的 Active 在非全屏播放下不一定生效的情况
                                                if (IsSlideShowWindowActive(ssWindow))
                                                {
                                                    Console.WriteLine("  [Fix] App process has focus via PID check. Upgrading priority to 3.");

                                                    currentPriority = 3;
                                                }
                                            }
                                        }
                                        catch { }
                                    }
                                }
                            }
                            catch (Exception ex)
                            {
                                Console.WriteLine($"Check Priority Error: {ex.Message}");
                            }

                            // 3. 更新 Target Priority
                            if (isTarget)
                            {
                                targetPriority = currentPriority;
                            }

                            // 4. 更新 Best App
                            if (currentPriority > 0)
                            {
                                if (currentPriority > highestPriority)
                                {
                                    highestPriority = currentPriority;

                                    // 列表持有取得；best 只是选择，替换 best 不能提前释放列表项。
                                    bestApp = candidateApp;
                                }
                            }

                            Console.WriteLine($"{displayName}: {currentPriority}");
                        }
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Loop Error: {ex.Message}");
                    }
                    finally
                    {
                        SafeRelease(ssWindow);
                        SafeRelease(activePres);

                        // 如果是为了去重而暂存在列表中的新对象，不要在这里释放
                        // 如果是重复对象(keepAlive=false)，或者出现异常没加入列表，则正常释放
                        // bestApp 仍只是列表别名，最终只转移获胜项的一笔取得。
                        if (!keepAlive)
                        {
                            SafeRelease(candidateApp);
                        }

                        CleanUpLoopObjects(bindCtx, moniker[0], comObject);
                    }
                }

                bestPriority = highestPriority;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ROT Scan Critical Error: {ex.Message}");
            }
            finally
            {
                // 清理去重列表中的 COM 对象
                if (foundAppObjects != null)
                {
                    foreach (var cachedApp in foundAppObjects)
                    {
                        // 关键：如果这个对象最终成为了 bestApp，千万不能释放，因为我们要返回它
                        if (bestApp != null && ReferenceEquals(cachedApp, bestApp))
                            continue;

                        SafeRelease(cachedApp);
                    }
                    foundAppObjects.Clear();
                }

                ReleaseMarshalObject(enumMoniker);
                ReleaseMarshalObject(rot);
            }

            return bestApp;
        }
        private void MaintainBinding()
        {
            int bestPriority, targetPriority;
            object bestApp = GetAnyActivePowerPoint((object)pptApplication,
                out bestPriority, out targetPriority);
            try
            {
                bool rebind = bestApp != null && (pptApplication == null || pptActivePresentation == null);
                if (!rebind && bestApp != null && bestPriority > targetPriority &&
                    !AreComObjectsEqual((object)pptApplication, bestApp))
                    rebind = !ShouldKeepCurrentBindingForSamePresentation(bestApp);
                if (!rebind) return;
                FullCleanup();
                pptApplication = bestApp;
                bestApp = null; // 唯一一次所有权转移，其余 ROT 返回即使同 identity 仍由 finally 配平。
                pptActivePresentation = presentationAccessor.GetProperty((object)pptApplication, "ActivePresentation");
                BindEvents();
                Interlocked.Exchange(ref showObservationPending, 1);
            }
            finally { SafeRelease(bestApp); }
        }

        private static bool IsMissingComMember(Exception error)
        {
            while (error != null)
            {
                var com = error as COMException;
                if (error is MissingMemberException ||
                    error is Microsoft.CSharp.RuntimeBinder.RuntimeBinderException ||
                    (com != null && (com.ErrorCode == unchecked((int)0x80020003) ||
                        com.ErrorCode == unchecked((int)0x80020006)))) return true;
                error = error.InnerException;
            }
            return false;
        }

        private static bool IsDisconnectedComObject(Exception error)
        {
            while (error != null)
            {
                if (error is InvalidComObjectException) return true;
                var com = error as COMException;
                if (com != null && (com.ErrorCode == unchecked((int)0x80010108) ||
                    com.ErrorCode == unchecked((int)0x800401FD) ||
                    com.ErrorCode == unchecked((int)0x80010007) ||
                    com.ErrorCode == unchecked((int)0x80010012))) return true;
                error = error.InnerException;
            }
            return false;
        }

        private unsafe void MarkBindingDisconnected(SlideShowObservationStamp stamp)
        {
            if (!slideShowSession.ObserveWindow(stamp,
                Interlocked.Read(ref presentationBindingRevision), false, false, 0)) return;
            *pptCurrentPage = *pptTotalPage = -1;
            PublishSlideShowLifecycle();
        }

        private object ReadScalar(object target, string name)
        {
            object value = presentationAccessor.GetProperty(target, name);
            if (!presentationAccessor.IsComObject(value)) return value;
            SafeRelease(value);
            throw new InvalidCastException(name);
        }

        private void ObserveUnknown(SlideShowObservationStamp stamp)
        {
            slideShowSession.ObserveIfCurrent(stamp,
                Interlocked.Read(ref presentationBindingRevision), null, 0);
            RequestPresentationDescriptorRefresh();
        }

        private unsafe bool ObserveBoundShow()
        {
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            long binding = Interlocked.Read(ref presentationBindingRevision);
            object activePresentation = null;
            object freshWindow = null;
            object showPresentation = null;
            try
            {
                try { activePresentation = presentationAccessor.GetProperty((object)pptApplication, "ActivePresentation"); }
                catch (Exception error)
                {
                    uint code;
                    if (IsDisconnectedComObject(error) || TryGetBusyComErrorCode(error, out code)) throw;
                    // 关闭最后文稿时 ActivePresentation 可先失效；独立文稿 Count=0 才确认关闭。
                    try
                    {
                        if (GetPresentationsCount((object)pptApplication) == 0)
                        { MarkBindingDisconnected(stamp); return false; }
                    }
                    catch (Exception countError) { if (IsDisconnectedComObject(countError)) throw; }
                    throw;
                }
                if (activePresentation == null)
                {
                    try
                    {
                        if (GetPresentationsCount((object)pptApplication) == 0) MarkBindingDisconnected(stamp);
                    }
                    catch { }
                    return false;
                }
                if (!AreComObjectsEqual(activePresentation, (object)pptActivePresentation)) return false;
                int showCount = -1;
                try { showCount = GetSlideShowWindowsCount((object)pptApplication); }
                catch (Exception error) { if (!IsMissingComMember(error)) throw; }
                if (showCount == 0)
                {
                    if (!slideShowSession.ObserveWindow(stamp, binding, false, false, 0)) return true;
                    // Count=0 才是明确结束；先废弃场次，再释放旧窗口，不凭旧 Active 可读推断仍放映。
                    object oldWindow = (object)pptSlideShowWindow;
                    pptSlideShowWindow = null;
                    SafeRelease(oldWindow);
                    *pptCurrentPage = *pptTotalPage = -1;
                    Interlocked.Exchange(ref slideShowHwndCache, 0);
                    PublishSlideShowLifecycle();
                    return true;
                }

                freshWindow = presentationAccessor.GetProperty(activePresentation, "SlideShowWindow");
                if (freshWindow == null) { ObserveUnknown(stamp); return true; }
                if (showCount < 0)
                {
                    // 旧提供方可能没有 Count；必须用本次 show 的文稿和 fresh View 证据保底。
                    showPresentation = presentationAccessor.GetProperty(freshWindow, "Presentation");
                    if (!AreComObjectsEqual(showPresentation, activePresentation))
                    { ObserveUnknown(stamp); return true; }
                }
                int total = GetTotalSlideIndex(activePresentation);
                int current = -1;
                bool endScreen = false;
                object view = null;
                try
                {
                    view = presentationAccessor.GetProperty(freshWindow, "View");
                    try { endScreen = Convert.ToInt32(ReadScalar(view, "State"), CultureInfo.InvariantCulture) == 5; }
                    catch (Exception error) { if (!IsMissingComMember(error)) throw; }
                    if (!endScreen) current = GetCurrentSlideIndexFromView(view);
                }
                finally { SafeRelease(view); }
                if (total <= 0 || total > 10000 || (!endScreen && (current <= 0 || current > total)))
                { ObserveUnknown(stamp); return true; }
                long hwnd = ResolveSlideShowHwnd((object)pptApplication, activePresentation, freshWindow);
                if (!slideShowSession.IsCurrent(stamp))
                {
                    Interlocked.Exchange(ref showObservationPending, 1);
                    return true;
                }
                bool replaced = !AreComObjectsEqual((object)pptSlideShowWindow, freshWindow);
                if (!slideShowSession.ObserveWindow(stamp, binding, true, replaced, hwnd)) return true;
                if (replaced)
                {
                    object oldWindow = (object)pptSlideShowWindow;
                    pptSlideShowWindow = freshWindow;
                    freshWindow = null; // 只在实际安装时转移；同 identity 的临时返回仍在 finally 释放。
                    SafeRelease(oldWindow);
                }
                bool changed = *pptCurrentPage != current || *pptTotalPage != total || replaced;
                *pptCurrentPage = current;
                *pptTotalPage = total;
                if (endScreen) { if (polling == 0) polling = 1; }
                else polling = current >= total ? 2 : 0;
                Interlocked.Exchange(ref slideShowHwndCache, hwnd);
                if (changed || stamp.Lifecycle != "Active") RequestPresentationDescriptorRefresh();
                return true;
            }
            catch (Exception error)
            {
                if (IsDisconnectedComObject(error))
                {
                    MarkBindingDisconnected(stamp);
                    return false; // 让 service owner 清理失效 roots，后续重新发现，不能永久保留旧 RCW。
                }
                uint code;
                bool busy = TryGetBusyComErrorCode(error, out code);
                ObserveUnknown(stamp);
                if (busy) presentationDescriptorRetryUtc = DateTime.UtcNow.AddMilliseconds(100);
                Console.WriteLine("[PptCOM] show-observation=0 busy=" + (busy ? 1 : 0) +
                    " hr=0x" + (busy ? code : unchecked((uint)ComFailure.ErrorCode(error))).ToString("X8"));
                return true; // 失败只表明 Unknown，不能据此断言用户结束或丢弃可恢复 binding。
            }
            finally
            {
                SafeRelease(showPresentation);
                SafeRelease(freshWindow);
                SafeRelease(activePresentation);
            }
        }

        public unsafe int PptComService()
        {
            Console.WriteLine("PPT Monitor ReStarted");
            slideShowOwner.Start();
            forcePolling = true;
            *pptCurrentPage = *pptTotalPage = -1;
            polling = 0;
            try
            {
                while (Thread.VolatileRead(ref *offSignal) == 0)
                {
                    // 快速事件只重读绑定对象；ROT 仍保持约 500ms 慢维护，所有 root 都由本 owner 使用。
                    if (Interlocked.Exchange(ref showObservationPending, 0) != 0 &&
                        pptApplication != null && !ObserveBoundShow()) break;
                    RefreshPresentationDescriptorIfNeeded(false);
                    ProcessSlideShowExitRequests();
                    int wait = slideShowOwner.MaintenanceWaitMilliseconds;
                    if (wait > 0)
                    {
                        if (presentationDescriptorRetryUtc != DateTime.MaxValue)
                            wait = Math.Min(wait, Math.Max(1, (int)(presentationDescriptorRetryUtc - DateTime.UtcNow).TotalMilliseconds));
                        slideShowOwner.Wait(wait);
                        continue;
                    }
                    try
                    {
                        RetryRetiredEventConnections();
                        MaintainBinding();
                        if (pptApplication == null || pptActivePresentation == null) break;
                        Interlocked.Exchange(ref showObservationPending, 0);
                        if (!ObserveBoundShow()) break;
                        RefreshPresentationDescriptorIfNeeded(false);
                        RefreshSlideShowAnnotationTool();
                        ProcessSlideShowExitRequests();
                    }
                    catch (Exception error)
                    {
                        uint code;
                        if (!TryGetBusyComErrorCode(error, out code)) throw;
                        ObserveUnknown(slideShowSession.Capture());
                    }
                    slideShowOwner.MaintenanceCompleted();
                }
            }
            catch (Exception error)
            {
                Console.WriteLine("[PptCOM] owner-failed hr=0x" + ComFailure.ErrorCode(error).ToString("X8"));
            }
            finally
            {
                slideShowOwner.Stop();
                FullCleanup(true);
                // 停止只做有界解除重试；外部 COM 持续拒绝时保留 CP/cookie，不假称已释放。
                for (int attempt = 0; attempt < 3 && ComEventRetirementRegistry.PendingCount != 0; ++attempt)
                {
                    RetryRetiredEventConnections();
                    if (ComEventRetirementRegistry.PendingCount != 0 && attempt < 2) slideShowOwner.Wait(100);
                }
            }
            return ComEventRetirementRegistry.PendingCount == 0 ? 0 : -1;
        }

        // 信息获取函数
        public string SlideNameIndex()
        {
            return Interlocked.CompareExchange(ref slideNameCache, null, null);
        }
        public IntPtr GetPptHwnd()
        {
            return new IntPtr(Interlocked.Read(ref slideShowHwndCache));
        }
        private int ReadSlideShowAnnotationTool(object view)
        {
            if (view == null) return SlideShowAnnotationToolNone;
            try
            {
                if (Convert.ToBoolean(ReadScalar(view, "LaserPointerEnabled"), CultureInfo.InvariantCulture))
                    return SlideShowAnnotationToolLaser;
            }
            catch { }
            object pointerTypeValue;
            try { pointerTypeValue = ReadScalar(view, "PointerType"); }
            catch { return SlideShowAnnotationToolNone; }
            if (HasSlideShowPointerName(pointerTypeValue, "ppSlideShowPointerHighlighter", "Highlighter"))
                return SlideShowAnnotationToolHighlighter;
            if (HasSlideShowPointerName(pointerTypeValue, "ppSlideShowPointerPen") ||
                CoerceToInt(pointerTypeValue, SlideShowPointerNone) == SlideShowPointerPen)
                return SlideShowAnnotationToolPen;
            return SlideShowAnnotationToolNone;
        }
        private void RefreshSlideShowAnnotationTool()
        {
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            int tool = SlideShowAnnotationToolNone;
            object view = null;
            try
            {
                if (stamp.Lifecycle == "Active" && stamp.Session > 0 && pptSlideShowWindow != null)
                {
                    view = presentationAccessor.GetProperty((object)pptSlideShowWindow, "View");
                    tool = ReadSlideShowAnnotationTool(view);
                }
            }
            catch { }
            finally { SafeRelease(view); }
            SlideShowObservationStamp current = slideShowSession.Capture();
            if (current.Session != stamp.Session || current.Binding != stamp.Binding ||
                current.Window != stamp.Window || current.Lifecycle != stamp.Lifecycle) return;
            lock (annotationToolLock)
            {
                annotationToolSession = stamp.Session;
                annotationToolBinding = stamp.Binding;
                annotationToolWindow = stamp.Window;
                annotationToolCache = tool;
            }
        }
        public int GetSlideShowAnnotationTool()
        {
            // native 轮询只读 owner 发布的纯值，不跨线程访问 Office RCW。
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            lock (annotationToolLock)
                return stamp.Lifecycle == "Active" && stamp.Session > 0 &&
                    stamp.Session == annotationToolSession && stamp.Binding == annotationToolBinding &&
                    stamp.Window == annotationToolWindow ? annotationToolCache : SlideShowAnnotationToolNone;
        }
        public bool ExitSlideShowAnnotationTool()
        {
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            return stamp.Lifecycle == "Active" && stamp.Session > 0 &&
                RequestOwnerCommand(stamp, SlideShowOwnerOperation.ExitAnnotation, false) == 1;
        }
        private bool ResetSlideShowPointer(object view)
        {
            // 枚举是写入值，不是类型接口依赖；dynamic 对 Office 枚举和 WPS 整数均按 IDispatch 传递。
            try { presentationAccessor.SetProperty(view, "PointerType", PpSlideShowPointerType.ppSlideShowPointerAutoArrow); return true; }
            catch { return false; }
        }
        private bool DisableSlideShowLaser(object view)
        {
            try { presentationAccessor.SetProperty(view, "LaserPointerEnabled", false); return true; }
            catch { return false; }
        }
        private int ExitSlideShowAnnotationToolOnOwner(SlideShowExitRequest request)
        {
            if (request.IsCancelled || !slideShowSession.Matches(request.Session) ||
                pptSlideShowWindow == null) return 0;
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            object view = null;
            try
            {
                view = presentationAccessor.GetProperty((object)pptSlideShowWindow, "View");
                if (request.IsCancelled || !slideShowSession.IsCurrent(stamp) ||
                    !slideShowSession.Matches(request.Session)) return 0;
                int tool = ReadSlideShowAnnotationTool(view);
                if (tool == SlideShowAnnotationToolNone) return 0;
                if (tool == SlideShowAnnotationToolLaser && !DisableSlideShowLaser(view)) return -1;
                bool pointerReset = ResetSlideShowPointer(view);
                if (!pointerReset && tool != SlideShowAnnotationToolLaser) return -1;
                if (ReadSlideShowAnnotationTool(view) != SlideShowAnnotationToolNone) return -1;
                lock (annotationToolLock)
                {
                    if (annotationToolSession == stamp.Session && annotationToolWindow == stamp.Window)
                        annotationToolCache = SlideShowAnnotationToolNone;
                }
                return 1;
            }
            catch { return -1; }
            finally { SafeRelease(view); }
        }
        private IntPtr GetPptHwndFromSlideShowWindow(object window)
        {
            try
            {
                object value = ReadScalar(window, "HWND");
                return value is IntPtr ? (IntPtr)value : new IntPtr(Convert.ToInt64(value, CultureInfo.InvariantCulture));
            }
            catch { return IntPtr.Zero; }
        }

        private long ResolveSlideShowHwnd(object application, object presentation, object window)
        {
            long direct = 0;
            try
            {
                object value = ReadScalar(window, "HWND");
                direct = value is IntPtr ? ((IntPtr)value).ToInt64() : Convert.ToInt64(value, CultureInfo.InvariantCulture);
            }
            catch (Exception error)
            {
                uint code;
                if (TryGetBusyComErrorCode(error, out code)) throw;
            }
            if (direct != 0)
                return !Marshal.IsComObject(window) || IsWindow(new IntPtr(direct)) ? direct : 0;
            if (!Marshal.IsComObject(application)) return 0;

            object showPresentation = null;
            try
            {
                // HWND 是已验证的受限标量例外；Application.HWND 只给出进程锚，绝不直接当放映句柄。
                var typedApplication = application as Microsoft.Office.Interop.PowerPoint.Application;
                if (typedApplication == null) return 0;
                IntPtr applicationWindow = new IntPtr(typedApplication.HWND);
                if (!IsWindow(applicationWindow)) return 0;
                uint processId;
                GetWindowThreadProcessId(applicationWindow, out processId);
                if (processId == 0 || GetSlideShowWindowsCount(application) != 1) return 0;
                showPresentation = presentationAccessor.GetProperty(window, "Presentation");
                if (!AreComObjectsEqual(showPresentation, presentation)) return 0;
                string fullName = Convert.ToString(ReadScalar(presentation, "FullName"), CultureInfo.InvariantCulture);
                string appName = Convert.ToString(ReadScalar(application, "Name"), CultureInfo.InvariantCulture);
                return GetPptHwndWin32(fullName, appName, processId).ToInt64();
            }
            catch (Exception error)
            {
                uint code;
                if (TryGetBusyComErrorCode(error, out code)) throw;
                return 0;
            }
            finally { SafeRelease(showPresentation); }
        }

        private IntPtr GetPptHwndWin32(string presFullName, string appName, uint processId)
        {
            try
            {
                string fileName = Path.GetFileName(presFullName);
                if (processId == 0 || string.IsNullOrWhiteSpace(fileName) ||
                    string.IsNullOrEmpty(appName) ||
                    appName.IndexOf("PowerPoint", StringComparison.OrdinalIgnoreCase) < 0) return IntPtr.Zero;
                // screenClass 是 PowerPoint 放映窗口；未知 WPP 窗口类没有证据时保留基础模式。
                List<SlideShowWindowEvidence> candidates = new List<SlideShowWindowEvidence>();
                EnumWindows((hwnd, parameter) =>
                {
                    uint actualProcessId;
                    GetWindowThreadProcessId(hwnd, out actualProcessId);
                    if (actualProcessId != processId || !IsWindowVisible(hwnd)) return true;
                    StringBuilder className = new StringBuilder(256);
                    GetClassName(hwnd, className, className.Capacity);
                    StringBuilder title = new StringBuilder(GetWindowTextLength(hwnd) + 1);
                    GetWindowText(hwnd, title, title.Capacity);
                    candidates.Add(new SlideShowWindowEvidence { Handle = hwnd, ProcessId = actualProcessId,
                        ClassName = className.ToString(), Title = title.ToString() });
                    return true;
                }, IntPtr.Zero);
                IntPtr result = SlideShowWindowEvidence.Select(candidates, processId, fileName);
                uint finalProcessId;
                if (result == IntPtr.Zero || !IsWindow(result) ||
                    GetWindowThreadProcessId(result, out finalProcessId) == 0 || finalProcessId != processId)
                    return IntPtr.Zero;
                StringBuilder finalClass = new StringBuilder(256);
                GetClassName(result, finalClass, finalClass.Capacity);
                StringBuilder finalTitle = new StringBuilder(GetWindowTextLength(result) + 1);
                GetWindowText(result, finalTitle, finalTitle.Capacity);
                if (!string.Equals(finalClass.ToString(), "screenClass", StringComparison.OrdinalIgnoreCase) ||
                    finalTitle.ToString().IndexOf(fileName, StringComparison.OrdinalIgnoreCase) < 0)
                    return IntPtr.Zero;
                return result;
            }
            catch { return IntPtr.Zero; }
        }

        private int GetCurrentSlideIndexFromView(object view)
        {
            object slide = null;
            try
            {
                slide = presentationAccessor.GetProperty(view, "Slide");
                return Convert.ToInt32(ReadScalar(slide, "SlideIndex"), CultureInfo.InvariantCulture);
            }
            finally { SafeRelease(slide); }
        }
        private int GetCurrentSlideIndex(object window)
        {
            object view = null;
            try
            {
                view = presentationAccessor.GetProperty(window, "View");
                return GetCurrentSlideIndexFromView(view);
            }
            finally { SafeRelease(view); }
        }
        private int GetTotalSlideIndex(object presentation)
        {
            object slides = null;
            try
            {
                slides = presentationAccessor.GetProperty(presentation, "Slides");
                return Convert.ToInt32(ReadScalar(slides, "Count"), CultureInfo.InvariantCulture);
            }
            finally { SafeRelease(slides); }
        }
        private int GetSlideShowWindowsCount(object application)
        {
            object windows = null;
            try
            {
                windows = presentationAccessor.GetProperty(application, "SlideShowWindows");
                return Convert.ToInt32(ReadScalar(windows, "Count"), CultureInfo.InvariantCulture);
            }
            finally { SafeRelease(windows); }
        }

        private int GetPresentationsCount(object application)
        {
            object presentations = null;
            try
            {
                presentations = presentationAccessor.GetProperty(application, "Presentations");
                return Convert.ToInt32(ReadScalar(presentations, "Count"), CultureInfo.InvariantCulture);
            }
            finally { SafeRelease(presentations); }
        }

        private int RequestOwnerCommand(SlideShowObservationStamp stamp,
            SlideShowOwnerOperation operation, bool check)
        {
            if (!slideShowSession.Matches(stamp.Session)) return 0;
            var request = new SlideShowExitRequest(stamp, operation, check);
            if (slideShowOwner.IsOwner)
                return request.TryBeginExecution() ? ExecuteOwnerCommand(request) : 0;
            return slideShowOwner.Request(request, 2000);
        }

        private int ExecuteOwnerCommand(SlideShowExitRequest request)
        {
            if (request.IsCancelled || !slideShowSession.Matches(request.Session) ||
                (request.HasStamp && !slideShowSession.IsCurrent(request.Stamp))) return 0;
            object window = (object)pptSlideShowWindow;
            SlideShowObservationStamp stamp = slideShowSession.Capture();
            if (window == null) return 0;
            if (request.Operation == SlideShowOwnerOperation.ExitAnnotation)
                return ExitSlideShowAnnotationToolOnOwner(request);
            if (request.Operation == SlideShowOwnerOperation.ExitShow)
                return SlideShowSessionExit.Execute(request, slideShowSession, window, presentationAccessor,
                    captured => ResolveSlideShowHwnd((object)pptApplication, (object)pptActivePresentation, captured));
            object temporary = null;
            try
            {
                if (request.Operation == SlideShowOwnerOperation.Next)
                {
                    bool endPage = false;
                    try { GetCurrentSlideIndex(window); }
                    catch (Exception error)
                    {
                        uint code;
                        if (TryGetBusyComErrorCode(error, out code)) throw;
                        endPage = true;
                    }
                    // 保留旧 Next(check) 末页保护：结束页必须明确 check，polling=1 还须能读到当前页。
                    if (endPage && !request.Check) return 0;
                    if (polling == 1)
                    {
                        try { GetCurrentSlideIndex(window); }
                        catch { return 0; }
                    }
                }
                if (request.Operation == SlideShowOwnerOperation.Activate)
                {
                    if (!slideShowSession.IsCurrent(stamp)) return 0;
                    presentationAccessor.InvokeMethod(window, "Activate");
                    return 1;
                }
                temporary = presentationAccessor.GetProperty(window,
                    request.Operation == SlideShowOwnerOperation.View ? "SlideNavigation" : "View");
                if (!slideShowSession.IsCurrent(stamp) || !slideShowSession.Matches(request.Session)) return 0;
                if (request.Operation == SlideShowOwnerOperation.View)
                    presentationAccessor.SetProperty(temporary, "Visible", true);
                else
                {
                    presentationAccessor.InvokeMethod(temporary,
                        request.Operation == SlideShowOwnerOperation.Next ? "Next" : "Previous");
                    if (request.Operation == SlideShowOwnerOperation.Next && polling != 0) polling = 1;
                }
                Interlocked.Exchange(ref showObservationPending, 1);
                RequestPresentationDescriptorRefresh();
                return 1;
            }
            catch { return -1; }
            finally { SafeRelease(temporary); }
        }

        public void NextSlideShow(bool check)
        { RequestOwnerCommand(slideShowSession.Capture(), SlideShowOwnerOperation.Next, check); }
        public void PreviousSlideShow()
        { RequestOwnerCommand(slideShowSession.Capture(), SlideShowOwnerOperation.Previous, false); }
        public void EndSlideShow()
        { RequestOwnerCommand(slideShowSession.Capture(), SlideShowOwnerOperation.ExitShow, false); }
        public void ViewSlideShow()
        { RequestOwnerCommand(slideShowSession.Capture(), SlideShowOwnerOperation.View, false); }
        public void ActivateSildeShowWindow()
        { RequestOwnerCommand(slideShowSession.Capture(), SlideShowOwnerOperation.Activate, false); }

        // 外部引用函数
        private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();
        [DllImport("ole32.dll")]
        private static extern int GetRunningObjectTable(int reserved, out IRunningObjectTable prot);
        [DllImport("ole32.dll")]
        private static extern int CreateBindCtx(int reserved, out IBindCtx ppbc);
        [DllImport("ole32.dll", CharSet = CharSet.Unicode)]
        private static extern int CLSIDFromProgID(string lpszProgID, out Guid pclsid);
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetWindowTextLength(IntPtr hWnd);
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsWindowVisible(IntPtr hWnd);
        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool IsWindow(IntPtr hWnd);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetClassName(IntPtr hWnd, StringBuilder className, int maximum);

        // Test
        private void DiagnoseComStatus()
        {
            Console.WriteLine("============== COM 对象存活诊断开始 ==============");

            // 1. 检查 Application
            try
            {
                if (pptApplication == null)
                {
                    Console.WriteLine("❌ pptApplication: NULL (空引用)");
                }
                else
                {
                    // 尝试访问一个简单属性，如 Caption 或 Version
                    string temp = pptApplication.Caption;
                    Console.WriteLine("✅ pptApplication: ALIVE (存活)");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"❌ pptApplication: DEAD (已失效) -> {ex.GetType().Name}");
            }

            // 2. 检查 ActivePresentation
            try
            {
                if (pptActivePresentation == null)
                {
                    Console.WriteLine("❌ pptActivePresentation: NULL (空引用)");
                }
                else
                {
                    // 尝试访问 Name
                    string temp = pptActivePresentation.Name;
                    Console.WriteLine("✅ pptActivePresentation: ALIVE (存活)");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"❌ pptActivePresentation: DEAD (已失效) -> {ex.GetType().Name}");
            }

            // 3. 检查 SlideShowWindow
            try
            {
                if (pptSlideShowWindow == null)
                {
                    Console.WriteLine("⚠️ pptSlideShowWindow: NULL (当前无窗口)");
                }
                else
                {
                    // 尝试访问 Height
                    int temp = pptSlideShowWindow.Active;
                    Console.WriteLine("✅ pptSlideShowWindow: ALIVE (存活)");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"❌ pptSlideShowWindow: DEAD (已失效) -> {ex.GetType().Name}");
            }

            Console.WriteLine("==================================================");
        }
    }
}

// 未完善列表
/*
public int GetSlideShowViewAdvanceMode()
{
    int AdvanceMode = -1;

    try
    {
        if (pptActivePresentation.SlideShowSettings.AdvanceMode == PpSlideShowAdvanceMode.ppSlideShowUseSlideTimings) AdvanceMode = 1;
        else AdvanceMode = 0;
    }
    catch
    {
    }

    return AdvanceMode;
}

public int SetSlideShowViewAdvanceMode(int AdvanceMode)
{
    try
    {
        if (AdvanceMode == 1) pptActivePresentation.SlideShowSettings.AdvanceMode = PpSlideShowAdvanceMode.ppSlideShowUseSlideTimings;
    }
    catch
    {
    }

    return AdvanceMode;
}
*/
