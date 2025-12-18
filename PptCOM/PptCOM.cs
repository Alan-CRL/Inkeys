/*
 * @file		PptCOM.cs
 * @brief		智绘教项目 PPT 联动插件
 * @note		PPT 联动插件 相关模块
 *
 * @envir		VisualStudio 2022 | .NET Framework 3.5 | Windows 11
 * @site		https://github.com/Alan-CRL/Intelligent-Drawing-Teaching
 *
 * @author		Alan-CRL
 * @qq			2685549821
 * @email		alan-crl@foxmail.com
*/

// 首次编译需要确认 .NET Framework 版本为 4.0，如果不一致请执行 <切换 .NET Framework 指南>
/////////////////////////////////////////////////////////////////////////////
// 切换 .NET Framework 指南
// .NET 版本默认为 .NET Framework 4.0 ，最低要求 .NET Framework 3.5
//
// 修改属性页中的指定框架
//
// 确认 PptCOM.manifest 中的 runtimeVersion 是你设置的版本全称（C:\Windows\Microsoft.NET\Framework），如 4.0.30319, 3.5
// 生成 -> 清理 PptCOM，然后点击重新生成解决方案
//
// 其余疑问请咨询作者 QQ2685549821
/////////////////////////////////////////////////////////////////////////////

using Microsoft.Office.Core;
using Microsoft.Office.Interop.PowerPoint;
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Threading;
using System.Windows.Forms;
using System.Reflection;

namespace PptCOM
{
    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("65F6E9C1-63EC-4003-B89F-8F425A3C2FEA")]
    public interface IPptCOMServer
    {
        unsafe bool Initialization(int* TotalPage, int* CurrentPage/*, bool autoCloseWPSTarget*/);
        string CheckCOM();

        unsafe int IsPptOpen();

        //
        string slideNameIndex();

        unsafe void NextSlideShow(int check);

        unsafe void PreviousSlideShow();

        IntPtr GetPptHwnd();

        void EndSlideShow();
        void ViewSlideShow();
    }

    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    [Guid("C44270BE-9A52-400F-AD7C-ED42050A77D8")]
    public class PptCOMServer : IPptCOMServer
    {
        private Microsoft.Office.Interop.PowerPoint.Application pptApp;

        private Microsoft.Office.Interop.PowerPoint.Presentation pptActDoc;
        private Microsoft.Office.Interop.PowerPoint.SlideShowWindow pptActWindow;

        private unsafe int* pptTotalPage;
        private unsafe int* pptCurrentPage;

        private int polling = 0; // 结束界面轮询（0正常页 1/2末页或结束放映页）（2设定为运行一次不被检查的翻页，虽然我也不知道当时写这个是为了特判什么情况Hhh）
        private DateTime updateTime; // 更新时间点
        private bool bindingEvents;

        //private bool autoCloseWPS = false;
        //private bool hasWpsProcessID;
        //private Process wpsProcess;

        // 初始化函数
        public unsafe bool Initialization(int* TotalPage, int* CurrentPage/*, bool autoCloseWPSTarget*/)
        {
            try
            {
                pptTotalPage = TotalPage;
                pptCurrentPage = CurrentPage;
                //autoCloseWPS = autoCloseWPSTarget;

                return true;
            }
            catch
            {
            }

            return false;
        }
        public string CheckCOM()
        {
            string ret = "20250830a";

            try
            {
                Microsoft.Office.Interop.PowerPoint.Application pptTest = new Microsoft.Office.Interop.PowerPoint.Application();
                Marshal.ReleaseComObject(pptTest);

                // TODO 需要测试对于没有安装 Powerpoint 设备，或是只有 WPS 的设备是否工作正常
            }
            catch (Exception ex)
            {
                ret += "\n" + ex.Message;
            }

            return ret;
        }

        // 外部引用函数

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();
        [DllImport("ole32.dll")]
        private static extern int GetRunningObjectTable(int reserved, out IRunningObjectTable prot);
        [DllImport("ole32.dll")]
        private static extern int CreateBindCtx(int reserved, out IBindCtx ppbc);

        // --- 辅助检测函数 ---
        public static bool LooksLikePresentationFile(string displayName)
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

        private static void CleanUpLoopObjects(IBindCtx bindCtx, IMoniker moniker, object comObject)
        {
            if (comObject != null && Marshal.IsComObject(comObject)) Marshal.ReleaseComObject(comObject);
            if (moniker != null) Marshal.ReleaseComObject(moniker);
            if (bindCtx != null) Marshal.ReleaseComObject(bindCtx);
        }

        // --- 核心获取函数：获取最佳 PPT 实例 ---
        public static Microsoft.Office.Interop.PowerPoint.Application GetAnyActivePowerPoint()
        {
            IRunningObjectTable rot = null;
            IEnumMoniker enumMoniker = null;

            Microsoft.Office.Interop.PowerPoint.Application bestApp = null;
            int highestPriority = 0;

            try
            {
                int hr = GetRunningObjectTable(0, out rot);
                if (hr != 0 || rot == null) return null;

                rot.EnumRunning(out enumMoniker);
                if (enumMoniker == null) return null;

                IMoniker[] moniker = new IMoniker[1];
                IntPtr fetched = IntPtr.Zero;

                // [调试] 获取当前前台窗口
                IntPtr foregroundHwnd = GetForegroundWindow();
                Console.WriteLine($"\n--- [ROT SCAN START] Foreground Window: {foregroundHwnd} ---");

                while (enumMoniker.Next(1, moniker, fetched) == 0)
                {
                    IBindCtx bindCtx = null;
                    object comObject = null;
                    Microsoft.Office.Interop.PowerPoint.Application candidateApp = null;
                    string displayName = "Unknown";

                    try
                    {
                        CreateBindCtx(0, out bindCtx);
                        moniker[0].GetDisplayName(bindCtx, null, out displayName);

                        if (LooksLikePresentationFile(displayName))
                        {
                            // [调试] 发现疑似 PPT 文件
                            Console.WriteLine($"Found Candidate: {displayName}");

                            rot.GetObject(moniker[0], out comObject);
                            if (comObject != null)
                            {
                                try
                                {
                                    object appObj = comObject.GetType().InvokeMember("Application",
                                        BindingFlags.GetProperty, null, comObject, null);
                                    candidateApp = appObj as Microsoft.Office.Interop.PowerPoint.Application;
                                }
                                catch (Exception ex)
                                {
                                    Console.WriteLine($"  Get Application Failed: {ex.Message}");
                                }
                            }
                        }

                        if (candidateApp != null)
                        {
                            int currentPriority = 0;
                            int appHwnd = 0;

                            try
                            {
                                // [调试] 获取 App 主窗口句柄
                                try { appHwnd = candidateApp.HWND; } catch { }
                                Console.WriteLine($"  App HWND: {appHwnd}");

                                if (candidateApp.ActivePresentation != null)
                                {
                                    currentPriority = 1;

                                    bool hasSlideShows = false;
                                    try
                                    {
                                        if (candidateApp.SlideShowWindows != null && candidateApp.SlideShowWindows.Count > 0)
                                        {
                                            hasSlideShows = true;
                                            currentPriority = 2;
                                            Console.WriteLine($"  Has SlideShowWindows (Count: {candidateApp.SlideShowWindows.Count})");
                                        }
                                    }
                                    catch { }

                                    if (hasSlideShows)
                                    {
                                        try
                                        {
                                            foreach (Microsoft.Office.Interop.PowerPoint.SlideShowWindow window in candidateApp.SlideShowWindows)
                                            {
                                                IntPtr slideHwnd = (IntPtr)window.HWND;

                                                // [调试] 打印放映窗口句柄对比
                                                bool match = (slideHwnd == foregroundHwnd);
                                                Console.WriteLine($"    > SlideWindow HWND: {slideHwnd} | Match Foreground? {match}");

                                                if (match)
                                                {
                                                    currentPriority = 3;

                                                    Console.WriteLine($"ret: match's HWND is: {match}");

                                                    break;
                                                }
                                            }
                                        }
                                        catch (Exception ex)
                                        {
                                            Console.WriteLine($"    Check SlideWindow Error: {ex.Message}");
                                        }
                                    }
                                }
                            }
                            catch (Exception ex)
                            {
                                Console.WriteLine($"  Check ActivePresentation Error: {ex.Message}");
                                currentPriority = 0;
                            }

                            Console.WriteLine($"  -> Calculated Priority: {currentPriority}");

                            if (currentPriority > 0)
                            {
                                // [疑似问题点]：这里如果直接返回，且这个 App 是死掉的那个（但它的 SlideWindow 属性因为 WPS 共享内存机制返回了活的句柄），就会导致错误绑定
                                if (currentPriority == 3)
                                {
                                    Console.WriteLine($"  !!! PERFECT MATCH FOUND (Return Immediately) !!! App HWND: {appHwnd}");

                                    if (bestApp != null) Marshal.ReleaseComObject(bestApp);
                                    bestApp = candidateApp;
                                    candidateApp = null; // 移交所有权

                                    return bestApp;
                                }

                                if (currentPriority > highestPriority)
                                {
                                    Console.WriteLine($"  New Best Candidate (Priority {currentPriority})");
                                    if (bestApp != null) Marshal.ReleaseComObject(bestApp);
                                    bestApp = candidateApp;
                                    highestPriority = currentPriority;
                                    candidateApp = null; // 移交所有权
                                }
                            }
                        }
                    }
                    catch { }
                    finally
                    {
                        if (candidateApp != null && Marshal.IsComObject(candidateApp))
                            Marshal.ReleaseComObject(candidateApp);

                        CleanUpLoopObjects(bindCtx, moniker[0], comObject);
                    }
                }
            }
            finally
            {
                Console.WriteLine("--- [ROT SCAN END] ---\n");
                if (enumMoniker != null) Marshal.ReleaseComObject(enumMoniker);
                if (rot != null) Marshal.ReleaseComObject(rot);
            }

            return bestApp;
        }

        // 比较
        private static bool AreComObjectsEqual(object o1, object o2)
        {
            if (o1 == null || o2 == null) return false;
            if (o1 == o2) return true;

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
        private static bool IsSlideShowInconsistent(Microsoft.Office.Interop.PowerPoint.Application app1, Microsoft.Office.Interop.PowerPoint.Application app2)
        {
            int hwnd1 = GetSlideShowHwndSafe(app1);
            if (hwnd1 == 0) return false;

            int hwnd2 = GetSlideShowHwndSafe(app2);
            if (hwnd2 == 0) return false;

            return hwnd1 != hwnd2;
        }
        private static int GetSlideShowHwndSafe(Microsoft.Office.Interop.PowerPoint.Application app)
        {
            if (app == null) return 0;

            try
            {
                if (app.ActivePresentation != null)
                {
                    var window = app.ActivePresentation.SlideShowWindow;
                    return window.HWND;
                }
            }
            catch { }

            return 0;
        }

        // 事件查询函数
        private unsafe void SlideShowChange(Microsoft.Office.Interop.PowerPoint.SlideShowWindow Wn)
        {
            updateTime = DateTime.Now;

            try
            {
                *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;

                if (pptActWindow.View.Slide.SlideIndex >= pptActDoc.Slides.Count) polling = 1;
                else polling = 0;
            }
            catch
            {
                *pptCurrentPage = -1;
                polling = 1;
            }
        }

        private unsafe void SlideShowBegin(Microsoft.Office.Interop.PowerPoint.SlideShowWindow Wn)
        {
            Console.WriteLine("Begin1");

            updateTime = DateTime.Now;
            pptActWindow = Wn;

            try
            {
                if (pptActWindow.View.Slide.SlideIndex >= pptActDoc.Slides.Count) polling = 1;
                else polling = 0;
            }
            catch
            {
                // Begin 事件定在结束放映页的小丑情况（虽然不可能有这种情况）
                polling = 1;
                Console.WriteLine("Begin4");
            }

            // 获取页数
            try
            {
                *pptTotalPage = pptActDoc.Slides.Count;
                *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;
            }
            catch
            {
                Console.WriteLine("Begin3");
            }
            Console.WriteLine("Begin2");
        }

        private unsafe void SlideShowShowEnd(Microsoft.Office.Interop.PowerPoint.Presentation Wn)
        {
            updateTime = DateTime.Now;

            *pptCurrentPage = -1;
            *pptTotalPage = -1;

            Console.WriteLine("END2");
        }

        private void PresentationBeforeClose(Microsoft.Office.Interop.PowerPoint.Presentation Wn, ref bool cancel)
        {
            if (bindingEvents && Wn == pptActDoc)
            {
                pptApp.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                pptApp.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                pptApp.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                pptApp.PresentationBeforeClose -= new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                bindingEvents = false;
            }
            // 对于延迟未关闭的 WPP，先记录进程 ID，待所有结束事件处理完毕后强制关闭
            //if (autoCloseWPS && Wn.Application.Path.Contains("Kingsoft\\WPS Office\\") && Wn.Application.Presentations.Count <= 1)
            //{
            //    uint processId;
            //    GetWindowThreadProcessId((IntPtr)Wn.Application.HWND, out processId);
            //    wpsProcess = Process.GetProcessById((int)processId);
            //    hasWpsProcessID = true;
            //}
            cancel = false;
        }

        // --- 清理与解绑 ---
        private void UnbindEvents()
        {
            try
            {
                if (bindingEvents && pptApp != null)
                {
                    try
                    {
                        pptApp.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                        pptApp.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                        pptApp.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                        pptApp.PresentationBeforeClose -= new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                    }
                    catch { }
                    bindingEvents = false;
                }
            }
            catch { }
        }

        // 彻底清理当前绑定的对象
        private unsafe void FullCleanup()
        {
            Console.WriteLine("CLEAN!");

            UnbindEvents();

            if (pptActWindow != null) { Marshal.ReleaseComObject(pptActWindow); pptActWindow = null; }
            if (pptActDoc != null) { Marshal.ReleaseComObject(pptActDoc); pptActDoc = null; }
            if (pptApp != null) { Marshal.ReleaseComObject(pptApp); pptApp = null; }

            // 重置指针为 -1 (外部程序约定的结束标志)
            *pptCurrentPage = -1;
            *pptTotalPage = -1;

            // 强制 GC，这对 WPS 的资源释放至关重要
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }

        // --- 主循环 ---
        public unsafe int IsPptOpen()
        {
            Console.WriteLine("PPT Monitor ReStarted");

            // 初始化
            bindingEvents = false;
            *pptCurrentPage = -1;
            *pptTotalPage = -1;
            polling = 0;

            int tempTotalPage = -1;
            DateTime lastCheckRotTime = DateTime.MinValue;

            try
            {
                while (true)
                {
                    // ============================================================
                    // 1. 动态绑定/切换逻辑 (每 1000ms 检查一次)
                    // ============================================================
                    if ((DateTime.Now - lastCheckRotTime).TotalMilliseconds > 3000)
                    {
                        lastCheckRotTime = DateTime.Now;

                        Microsoft.Office.Interop.PowerPoint.Application bestApp = GetAnyActivePowerPoint();

                        bool needRebind = false;

                        // 情况 A: 之前没绑，现在找到了 -> 绑
                        if (pptApp == null && bestApp != null)
                        {
                            needRebind = true;
                            Console.WriteLine("first band");
                        }
                        // 情况 B: 之前绑了，但现在找到了不一样的 (例如 PPT <-> WPS) -> 换
                        else if (pptApp != null && bestApp != null)
                        {
                            Console.WriteLine($"find new {bestApp.HWND}");

                            // 发现了完全不同的 Application 实例，必须切换
                            if (!AreComObjectsEqual(pptApp, bestApp) && IsSlideShowInconsistent(pptApp, bestApp))
                            {
                                needRebind = true;
                                Console.WriteLine("Detected Application Switch");
                            }
                        }
                        // 情况 D: 之前绑了，现在找不到任何 PPT 了 -> 不急着在这里 FullCleanup
                        // 让 Watchdog 去发现死活，避免 ROT 瞬时列表为空造成的闪烁

                        // --- 执行绑定 ---
                        if (needRebind)
                        {
                            Console.WriteLine("Try Rebind");
                            FullCleanup(); // 包含 GC 和 指针重置

                            // 如果 bestApp 在上面被释放了(情况C)，或者我们需要重新绑定
                            // 为了逻辑简单和安全，我们重新获取一次当前最佳（确保它是活的）
                            if (bestApp == null) bestApp = GetAnyActivePowerPoint();

                            if (bestApp != null)
                            {
                                pptApp = bestApp;
                                try
                                {
                                    pptActDoc = pptApp.ActivePresentation;
                                    updateTime = DateTime.Now;

                                    try
                                    {
                                        pptActWindow = pptActDoc.SlideShowWindow;
                                        *pptTotalPage = tempTotalPage = pptActDoc.Slides.Count;
                                    }
                                    catch
                                    {
                                        *pptTotalPage = tempTotalPage = -1;
                                    }

                                    if (tempTotalPage == -1)
                                    {
                                        *pptCurrentPage = -1;
                                        polling = 0;
                                    }
                                    else
                                    {
                                        try
                                        {
                                            *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;

                                            if (pptActWindow.View.Slide.SlideIndex >= pptActDoc.Slides.Count) polling = 1;
                                            else polling = 0;
                                        }
                                        catch
                                        {
                                            *pptCurrentPage = -1;
                                            polling = 1;
                                        }
                                    }

                                    // 绑定事件
                                    bindingEvents = true;
                                    pptApp.SlideShowNextSlide += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                                    pptApp.SlideShowBegin += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                                    pptApp.SlideShowEnd += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                                    pptApp.PresentationBeforeClose += new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);

                                    Console.WriteLine($"Bind Success to {pptApp.HWND}");
                                }
                                catch
                                {
                                    FullCleanup();
                                }
                            }
                        }
                    }

                    // ============================================================
                    // 2. 状态监测与 3000ms 心跳轮询 (Watchdog)
                    // ============================================================
                    if (pptApp != null && pptActDoc != null)
                    {
                        Console.WriteLine($"Enter Part 2 {pptApp.HWND}");
                        // 检查是否同进程切换文档
                        if (pptActDoc != pptApp.ActivePresentation) break;

                        // 检测是否处于放映模式
                        bool isSlideShowActive = false;
                        try
                        {
                            // 检查 SlideShowWindows 集合
                            if (pptApp.SlideShowWindows != null && pptApp.SlideShowWindows.Count > 0)
                            {
                                isSlideShowActive = true;
                            }
                        }
                        catch
                        {
                            // 如果这里报错，说明 App 可能挂了，或者 WPS 上下文丢失
                            // 标记为非放映状态，后续逻辑会处理
                        }

                        if (isSlideShowActive)
                        {
                            if ((DateTime.Now - updateTime).TotalMilliseconds > 3000)
                            {
                                try
                                {
                                    // 获取当前播放的PPT幻灯片窗口对象（保证当前处于放映状态）
                                    if (pptActDoc.SlideShowWindow != null) *pptTotalPage = tempTotalPage = pptActDoc.Slides.Count;
                                    else *pptTotalPage = tempTotalPage = -1;
                                }
                                catch
                                {
                                    *pptTotalPage = tempTotalPage = -1;
                                }

                                if (tempTotalPage == -1)
                                {
                                    *pptCurrentPage = -1;
                                    polling = 0;
                                }
                                else
                                {
                                    try
                                    {
                                        *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;

                                        if (pptActWindow.View.Slide.SlideIndex >= pptActDoc.Slides.Count) polling = 1;
                                        else polling = 0;
                                    }
                                    catch
                                    {
                                        *pptCurrentPage = -1;
                                        polling = 1;
                                    }
                                }

                                updateTime = DateTime.Now;
                            }
                            if (polling != 0)
                            {
                                try
                                {
                                    *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;
                                    polling = 2;
                                }
                                catch
                                {
                                    *pptCurrentPage = -1;
                                }
                            }
                        }
                        else
                        {
                            *pptCurrentPage = -1;
                            *pptTotalPage = -1;

                            // 等待用户再次点击“开始放映”时能立即响应 SlideShowBegin 事件
                        }
                    }
                    else
                    {
                        // 没有绑定对象
                        *pptCurrentPage = -1;
                        *pptTotalPage = -1;

                        break;
                    }

                    Thread.Sleep(500);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"");
                Console.WriteLine($"Fail 710");
                Console.WriteLine($"异常类型: {ex.GetType().FullName}");
                Console.WriteLine($"异常信息: {ex.Message}");
                Console.WriteLine($"堆栈: {ex.StackTrace}");
                Console.WriteLine($"");
            }
            finally
            {
                FullCleanup();
            }

            return 0;
        }

        // 信息获取函数
        public string slideNameIndex()
        {
            string slidesName = "";

            try
            {
                // 获取正在播放的PPT的名称
                slidesName += pptActDoc.FullName + "\n";
                slidesName += pptApp.Caption;
            }
            catch
            {
                // 获取PPT信息失败
            }

            return slidesName;
        }

        public IntPtr GetPptHwnd()
        {
            IntPtr hWnd = IntPtr.Zero;
            try
            {
                // 获取正在播放的PPT应用程序对象
                pptApp = (Microsoft.Office.Interop.PowerPoint.Application)Marshal.GetActiveObject("PowerPoint.Application");
                // 获取当前播放的PPT文档对象
                pptActDoc = pptApp.ActivePresentation;
                // 获取当前播放的PPT幻灯片窗口对象
                pptActWindow = pptActDoc.SlideShowWindow;

                // 获取PPT窗口句柄
                hWnd = new IntPtr(pptActWindow.HWND);
            }
            catch
            {
            }

            return hWnd;
        }

        // 未完善列表
        /*
        public int GetSlideShowViewAdvanceMode()
        {
            int AdvanceMode = -1;

            try
            {
                if (pptActDoc.SlideShowSettings.AdvanceMode == PpSlideShowAdvanceMode.ppSlideShowUseSlideTimings) AdvanceMode = 1;
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
                if (AdvanceMode == 1) pptActDoc.SlideShowSettings.AdvanceMode = PpSlideShowAdvanceMode.ppSlideShowUseSlideTimings;
            }
            catch
            {
            }

            return AdvanceMode;
        }
        */

        // 操控函数

        public unsafe void NextSlideShow(int check)
        {
            try
            {
                int temp_SlideIndex = *pptCurrentPage;
                if (temp_SlideIndex != check && check != -1) return;

                // 下一页
                if (polling != 0)
                {
                    if (polling == 2)
                    {
                        pptActWindow.View.Next();
                    }
                    else if (polling == 1)
                    {
                        int currentPageTemp = -1;
                        try
                        {
                            currentPageTemp = pptActWindow.View.Slide.SlideIndex;
                        }
                        catch
                        {
                            currentPageTemp = -1;
                        }
                        if (currentPageTemp != -1)
                        {
                            pptActWindow.View.Next();
                        }
                    }
                    polling = 1;
                }
                else
                {
                    pptActWindow.View.Next();
                }
            }
            catch
            {
            }
        }

        public unsafe void PreviousSlideShow()
        {
            try
            {   // 上一页
                pptActWindow.View.Previous();
            }
            catch
            {
            }
            return;
        }

        public void EndSlideShow()
        {
            try
            {
                // 结束放映
                pptActWindow.View.Exit();
            }
            catch
            {
            }
        }
        public void ViewSlideShow()
        {
            try
            {   // 打开 ppt 浏览视图
                pptActWindow.SlideNavigation.Visible = true;
            }
            catch
            {
            }
            return;
        }
    }
}