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
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Threading;
using System.Windows.Forms;

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
        private dynamic pptApplication;
        private dynamic pptActivePresentation;
        private dynamic pptSlideShowWindow;

        private unsafe int* pptTotalPage;
        private unsafe int* pptCurrentPage;

        // 结束界面轮询（0正常页 1/2末页或结束放映页）
        //（2设定为运行一次不被检查的翻页，虽然我也不知道当时写这个是为了特判什么情况 hhh）
        private int polling = 0;

        private bool forcePolling = false; // 强制轮询标志
        private bool bindingEvents; // 是否已绑定事件

        private DateTime updateTime; // 更新时间点

        // 初始化函数
        public unsafe bool Initialization(int* TotalPage, int* CurrentPage)
        {
            try
            {
                pptTotalPage = TotalPage;
                pptCurrentPage = CurrentPage;

                return true;
            }
            catch
            {
            }

            return false;
        }
        public string CheckCOM()
        {
            string ret = "20251220a";
            return ret;
        }

        // 过程函数
        private static void CleanUpLoopObjects(IBindCtx bindCtx, IMoniker moniker, object comObject)
        {
            if (comObject != null && Marshal.IsComObject(comObject)) Marshal.ReleaseComObject(comObject);
            if (moniker != null) Marshal.ReleaseComObject(moniker);
            if (bindCtx != null) Marshal.ReleaseComObject(bindCtx);
        }
        private static void SafeRelease(object obj)
        {
            if (obj != null && Marshal.IsComObject(obj))
            {
                try { Marshal.ReleaseComObject(obj); } catch { }
            }
        }
        private void UnbindEvents()
        {
            try
            {
                if (bindingEvents && pptApplication != null)
                {
                    try
                    {
                        // 【重要修改】同上，移除 new ...EventHandler(...) 包装
                        // 改为直接 -= 方法名，适配 dynamic
                        pptApplication.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                        pptApplication.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                        pptApplication.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                        pptApplication.PresentationBeforeClose -= new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                    }
                    catch { }

                    bindingEvents = false;
                }
            }
            catch { }
        }
        private unsafe void FullCleanup()
        {
            Console.WriteLine("CLEAN!");

            UnbindEvents();

            try
            {
                if (pptSlideShowWindow != null) { Marshal.ReleaseComObject((object)pptSlideShowWindow); pptSlideShowWindow = null; }
            }
            catch (Exception bindEx)
            {
                Console.WriteLine($"CLEAN1 设置失败 {bindEx.Message}");
            }
            finally
            {
                pptSlideShowWindow = null;
            }

            try
            {
                if (pptActivePresentation != null) { Marshal.ReleaseComObject((object)pptActivePresentation); pptActivePresentation = null; }
            }
            catch (Exception bindEx)
            {
                Console.WriteLine($"CLEAN2 设置失败 {bindEx.Message}");
            }
            finally
            {
                pptActivePresentation = null;
            }

            try
            {
                if (pptApplication != null) { Marshal.ReleaseComObject((object)pptApplication); pptApplication = null; }
            }
            catch (Exception bindEx)
            {
                Console.WriteLine($"CLEAN3 设置失败 {bindEx.Message}");
            }
            finally
            {
                pptApplication = null;
            }

            // 重置指针为 -1 (外部程序约定的结束标志)
            try
            {
                *pptTotalPage = -1;
                *pptCurrentPage = -1;
            }
            catch { }

            // 强制 GC，这对 WPS 的资源释放至关重要
            GC.Collect();
            GC.WaitForPendingFinalizers();
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
        private static bool IsSlideShowWindowActive(object sswObj)
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
                    sswHwnd = GetSlideShowWindowHwnd(ssw);
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
        private static bool IsValidSlideShowWindow(object pptSlideShowWindowObj)
        {
            dynamic pptSlideShowWindow;
            try
            {
                pptSlideShowWindow = pptSlideShowWindowObj;
            }
            catch
            {
                return false;
            }

            if (pptSlideShowWindow == null)
                return false;
            if (pptSlideShowWindow is IntPtr ptr && ptr == IntPtr.Zero)
                return false;

            bool ret = false;

            try
            {
                var tmp = pptSlideShowWindow.Active;
                ret = true;
            }
            catch
            {
                ret = false;
            }

            return ret;
        }

        // 事件函数
        private unsafe void SlideShowChange(object WnObj)
        {
            updateTime = DateTime.Now;

            try
            {
                // 假设全局变量 pptSlideShowWindow 已经是 dynamic 类型，这里的调用会自动进行后期绑定
                *pptCurrentPage = pptSlideShowWindow.View.Slide.SlideIndex;

                if (pptSlideShowWindow.View.Slide.SlideIndex >= pptActivePresentation.Slides.Count) polling = 1;
                else polling = 0;
            }
            catch
            {
                *pptCurrentPage = -1;
                polling = 1;
            }
        }
        private unsafe void SlideShowBegin(object WnObj)
        {
            Console.WriteLine("Begin1");

            updateTime = DateTime.Now;

            // 【修改】直接赋值给 dynamic 类型的全局变量
            pptSlideShowWindow = WnObj;

            try
            {
                if (pptSlideShowWindow.View.Slide.SlideIndex >= pptActivePresentation.Slides.Count) polling = 1;
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
                *pptTotalPage = pptActivePresentation.Slides.Count;
                *pptCurrentPage = pptSlideShowWindow.View.Slide.SlideIndex;
            }
            catch
            {
                Console.WriteLine("Begin3");
            }
            Console.WriteLine("Begin2");
        }
        private unsafe void SlideShowShowEnd(object WnObj)
        {
            updateTime = DateTime.Now;

            *pptCurrentPage = -1;
            *pptTotalPage = -1;

            Console.WriteLine("END2");
        }
        private void PresentationBeforeClose(object WnObj, ref bool cancel)
        {
            dynamic Wn = WnObj;

            if (bindingEvents && Wn == pptActivePresentation)
            {
                try
                {
                    // 这里不知道怎么改
                    pptApplication.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                    pptApplication.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                    pptApplication.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                    pptApplication.PresentationBeforeClose -= new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                }
                catch { }

                bindingEvents = false;
            }

            cancel = false;
        }

        // 获取函数
        private static object GetAnyActivePowerPoint(object targetApp, out int bestPriority, out int targetPriority)
        {
            IRunningObjectTable rot = null;
            IEnumMoniker enumMoniker = null;

            // bestApp 改为 object
            object bestApp = null;

            bestPriority = 0;
            targetPriority = 0;
            int highestPriority = 0;

            try
            {
                int hr = GetRunningObjectTable(0, out rot);
                if (hr != 0 || rot == null) return null;

                rot.EnumRunning(out enumMoniker);
                if (enumMoniker == null) return null;

                IMoniker[] moniker = new IMoniker[1];
                IntPtr fetched = IntPtr.Zero;

                while (enumMoniker.Next(1, moniker, fetched) == 0)
                {
                    IBindCtx bindCtx = null;
                    object comObject = null;

                    // 【修改点1】改为 dynamic
                    dynamic candidateApp = null;

                    string displayName = "Unknown";

                    // 【修改点2】改为 dynamic，用于释放
                    dynamic activePres = null;
                    dynamic ssWindows = null;

                    try
                    {
                        CreateBindCtx(0, out bindCtx);
                        moniker[0].GetDisplayName(bindCtx, null, out displayName);

                        if (LooksLikePresentationFile(displayName)) // 保持你的逻辑
                        {
                            Console.WriteLine($"{displayName}");

                            rot.GetObject(moniker[0], out comObject);
                            if (comObject != null)
                            {
                                // 尝试通过 Presentation 对象获取 Application
                                try
                                {
                                    // 使用反射获取 Application 属性 (这对 WPS 和 PPT 都通用)
                                    object appObj = comObject.GetType().InvokeMember("Application",
                                        BindingFlags.GetProperty, null, comObject, null);

                                    // 【修改点3】直接赋值，不要使用 'as' 强转
                                    candidateApp = appObj;
                                }
                                catch (Exception ex)
                                {
                                    Console.WriteLine($"  -> 获取 Application 属性失败: {ex.Message}");
                                }
                            }
                            else
                            {
                                Console.WriteLine($"not com object");
                            }
                        }

                        if (candidateApp != null)
                        {
                            Console.WriteLine($"Yes999!");

                            int currentPriority = 0;
                            bool isTarget = false;

                            // 1. 检查是否是 Target (使用 object 比较)
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
                                    // dynamic 调用
                                    activePres = candidateApp.ActivePresentation;
                                }
                                catch (Exception ex)
                                {
                                    // 保持你的日志逻辑
                                    Console.WriteLine($"");
                                    Console.WriteLine($"Fail 267");
                                    Console.WriteLine($"异常类型: {ex.GetType().FullName}");
                                    Console.WriteLine($"异常信息: {ex.Message}");
                                    Console.WriteLine($"堆栈: {ex.StackTrace}");
                                    Console.WriteLine($"");
                                }

                                if (activePres != null)
                                {
                                    currentPriority = 1;

                                    // 检查 SlideShowWindows
                                    try { ssWindows = candidateApp.SlideShowWindows; } catch { }

                                    // dynamic 集合也能访问 Count
                                    if (ssWindows != null && ssWindows.Count > 0)
                                    {
                                        currentPriority = 2;

                                        int count = ssWindows.Count;
                                        for (int i = 1; i <= count; i++)
                                        {
                                            dynamic ssWin = null;
                                            try
                                            {
                                                ssWin = ssWindows[i];

                                                // 【修改点4】兼容性判定 Active
                                                // MS PPT 返回 int (-1), WPS 可能返回 bool (true)
                                                bool isActive = false;
                                                try
                                                {
                                                    object val = ssWin.Active;
                                                    if (val is int && (int)val == -1) isActive = true; // MsoTriState.msoTrue
                                                    else if (val is bool && (bool)val == true) isActive = true;
                                                }
                                                catch { }

                                                if (isActive)
                                                {
                                                    currentPriority = 3;
                                                    SafeRelease((object)ssWin);
                                                    break;
                                                }
                                                else
                                                {
                                                    // 针对 WPP 的 Active 在非全屏播放下不一定生效的情况
                                                    if (IsSlideShowWindowActive((object)ssWin))
                                                    {
                                                        Console.WriteLine("  [Fix] App process has focus via PID check. Upgrading priority to 3.");

                                                        currentPriority = 3;
                                                        SafeRelease((object)ssWin);
                                                        break;
                                                    }
                                                }
                                            }
                                            catch { }
                                            finally
                                            {
                                                SafeRelease((object)ssWin);
                                            }
                                        }
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

                                    SafeRelease(bestApp);

                                    // 这里不需要转换，直接赋值 object
                                    bestApp = candidateApp;
                                    candidateApp = null; // 转移所有权
                                }
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Loop Error: {ex.Message}");
                    }
                    finally
                    {
                        // 显式释放所有中间产生的 COM 对象
                        SafeRelease((object)activePres);
                        SafeRelease((object)ssWindows);

                        SafeRelease((object)candidateApp);

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
                if (enumMoniker != null) Marshal.ReleaseComObject(enumMoniker);
                if (rot != null) Marshal.ReleaseComObject(rot);
            }

            return bestApp;
        }
        public unsafe int IsPptOpen()
        {
            Console.WriteLine("PPT Monitor ReStarted");

            // 初始化
            bindingEvents = false;
            *pptCurrentPage = -1;
            *pptTotalPage = -1;
            polling = 0;

            int tempTotalPage = -1;

            int bestPriority = 0;
            int targetPriority = 0;

            try
            {
                while (true)
                {
                    // 动态绑定/切换逻辑
                    {
                        // 【修改】类型改为 object，因为 GetAnyActivePowerPoint 现在返回 object
                        object bestApp = GetAnyActivePowerPoint(pptApplication, out bestPriority, out targetPriority);

                        bool needRebind = false;

                        if (pptApplication != null && bestApp != null)
                        {
                            Console.WriteLine($"bestP: {bestPriority}; targetP {targetPriority}");
                        }

                        // 之前没绑，现在找到了
                        if (pptApplication == null && bestApp != null)
                        {
                            needRebind = true;
                            Console.WriteLine("first band");
                        }
                        // 之前绑了，但现在找到了不一样的
                        else if (pptApplication != null && bestApp != null && bestPriority > targetPriority)
                        {
                            // 【说明】dynamic 下访问 HWND 属性是安全的
                            Console.WriteLine($"find new {pptApplication.HWND}");

                            Console.WriteLine($"check1 {bestPriority}");
                            Console.WriteLine($"check2 {targetPriority}");

                            // 发现了完全不同的 Application 实例，必须切换
                            if (!AreComObjectsEqual((object)pptApplication, bestApp))
                            {
                                needRebind = true;

                                Console.WriteLine("Detected Application Switch");
                            }
                        }

                        if (needRebind)
                        {
                            bool wait = (pptApplication != null);

                            Console.WriteLine("Try Rebind !!!!!!!!!!!!!!!!!!!!!!!!!!!");
                            FullCleanup();

                            if (bestApp != null)
                            {
                                if (wait) Thread.Sleep(1000);

                                pptApplication = bestApp; // dynamic = object

                                if (pptApplication != null) Console.WriteLine($"Enter Part 2 {pptApplication.Name} = 2");

                                try
                                {
                                    // dynamic 后期绑定
                                    pptActivePresentation = pptApplication.ActivePresentation;
                                    updateTime = DateTime.Now;

                                    try
                                    {
                                        pptSlideShowWindow = pptActivePresentation.SlideShowWindow;
                                        *pptTotalPage = tempTotalPage = pptActivePresentation.Slides.Count;
                                    }
                                    catch (Exception bindEx)
                                    {
                                        *pptTotalPage = tempTotalPage = -1;

                                        Console.WriteLine($"pptSlideShowWindow 设置失败 {bindEx.Message}");
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
                                            *pptCurrentPage = pptSlideShowWindow.View.Slide.SlideIndex;

                                            if (pptSlideShowWindow.View.Slide.SlideIndex >= pptActivePresentation.Slides.Count) polling = 1;
                                            else polling = 0;
                                        }
                                        catch
                                        {
                                            *pptCurrentPage = -1;
                                            polling = 1;
                                        }
                                    }

                                    // 【修改】尝试绑定事件 (包含错误输出)
                                    try
                                    {
                                        pptApplication.SlideShowNextSlide += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                                        pptApplication.SlideShowBegin += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                                        pptApplication.SlideShowEnd += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);

                                        // PresentationBeforeClose 带有 ref 参数，可能比较敏感，建议单独包裹 try-catch 或者保留在最后
                                        try
                                        {
                                            pptApplication.PresentationBeforeClose += new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                                        }
                                        catch { /* 忽略这个特殊的 Ref 参数事件失败 */ }

                                        // Console.WriteLine($"Bind Success to {pptApplication.HWND}");

                                        bindingEvents = true;
                                        forcePolling = false;
                                    }
                                    catch (Exception bindEx)
                                    {
                                        // 【新增】专门捕获事件绑定失败
                                        Console.WriteLine("--------------------------------------------------");
                                        Console.WriteLine($"[警告] 事件绑定失败: {bindEx.Message}");
                                        Console.WriteLine("如果是 'Type library' 错误，说明不支持事件，将自动使用轮询。");
                                        Console.WriteLine("--------------------------------------------------");

                                        bindingEvents = false;
                                        forcePolling = false;
                                        // 不抛出异常，继续执行，允许进入下方的轮询逻辑
                                    }
                                }
                                catch
                                {
                                    FullCleanup();
                                }
                            }
                        }
                    }

                    // 状态监测与轮询
                    if (pptApplication != null && pptActivePresentation != null)
                    {
                        //if (pptApplication != null) Console.WriteLine($"Enter Part 2 {pptApplication.HWND} 1");

                        // 检查是否同进程切换文档 (dynamic 比较引用)
                        // 注意：如果报错 RuntimeBinderException 说明对象可能已失效
                        try
                        {
                            if (!AreComObjectsEqual((object)pptActivePresentation, (object)pptApplication.ActivePresentation)) break;
                        }
                        catch { break; }

                        // 检测是否处于放映模式
                        bool isSlideShowActive = false;
                        try
                        {
                            // 检查 SlideShowWindows 集合
                            if (pptApplication.SlideShowWindows != null && pptApplication.SlideShowWindows.Count > 0)
                            {
                                isSlideShowActive = true;

                                if (pptActivePresentation.SlideShowWindow != null && !IsValidSlideShowWindow(pptSlideShowWindow))
                                {
                                    pptSlideShowWindow = pptActivePresentation.SlideShowWindow;

                                    Console.WriteLine($"发现窗口，成功设置 slideshowwindow");
                                }
                                else if (pptActivePresentation.SlideShowWindow != null)
                                {
                                    Console.WriteLine($"发现窗口，但无须设置");
                                }
                            }
                        }
                        catch
                        {
                            // 如果这里报错，说明 App 可能挂了，或者 WPS 上下文丢失
                            // 标记为非放映状态，后续逻辑会处理
                        }

                        if (isSlideShowActive)
                        {
                            if ((DateTime.Now - updateTime).TotalMilliseconds > 3000 || forcePolling)
                            {
                                Console.WriteLine($"轮询");

                                try
                                {
                                    // 获取当前播放的PPT幻灯片窗口对象（保证当前处于放映状态）
                                    if (pptActivePresentation.SlideShowWindow != null) *pptTotalPage = tempTotalPage = pptActivePresentation.Slides.Count;
                                    else *pptTotalPage = tempTotalPage = -1;
                                }
                                catch
                                {
                                    *pptTotalPage = tempTotalPage = -1;
                                    Console.WriteLine($"error 1");
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
                                        // *pptCurrentPage = pptSlideShowWindow.View.Slide.SlideIndex;

                                        //if (pptSlideShowWindow.View.Slide.SlideIndex >= pptActivePresentation.Slides.Count) polling = 1;
                                        //else polling = 0;

                                        try
                                        {
                                            // 2. 检查 .View 属性
                                            dynamic tempView = null;
                                            try
                                            {
                                                tempView = pptSlideShowWindow.View;
                                                // Console.WriteLine("  Debug: View OK");
                                            }
                                            catch (Exception ex)
                                            {
                                                Console.WriteLine($"[DEBUG定位] .View 属性获取失败! (可能是窗口正处于黑屏/切换状态) 错误: {ex.Message}");
                                                throw;
                                            }

                                            // 3. 检查 .Slide 对象
                                            dynamic tempSlide = null;
                                            try
                                            {
                                                tempSlide = tempView.Slide;
                                                // Console.WriteLine("  Debug: Slide OK");
                                            }
                                            catch (Exception ex)
                                            {
                                                Console.WriteLine($"[DEBUG定位] .View.Slide 获取失败! (可能处于放映结束页或过渡动画中) 错误: {ex.Message}");
                                                throw;
                                            }

                                            // 4. 最后获取 Index
                                            try
                                            {
                                                int finalIndex = tempSlide.SlideIndex;
                                                *pptCurrentPage = finalIndex;

                                                // 判定逻辑
                                                if (finalIndex >= pptActivePresentation.Slides.Count) polling = 1;
                                                else polling = 0;
                                            }
                                            catch (Exception ex)
                                            {
                                                Console.WriteLine($"[DEBUG定位] .SlideIndex 读取失败! 错误: {ex.Message}");
                                                throw;
                                            }
                                        }
                                        catch { }
                                    }
                                    catch (Exception ex)
                                    {
                                        *pptCurrentPage = -1;
                                        polling = 1;

                                        Console.WriteLine($"error 2");
                                        Console.WriteLine($"异常类型: {ex.GetType().FullName}");
                                        Console.WriteLine($"异常信息: {ex.Message}");
                                        Console.WriteLine($"堆栈: {ex.StackTrace}");
                                        Console.WriteLine($"");
                                    }
                                }

                                updateTime = DateTime.Now;
                            }
                            if (polling != 0)
                            {
                                try
                                {
                                    *pptCurrentPage = pptSlideShowWindow.View.Slide.SlideIndex;
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
                Console.WriteLine($"Fail 960");
                Console.WriteLine($"异常信息: {ex.Message}");
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
                slidesName += pptActivePresentation.FullName + "\n";
                slidesName += pptApplication.Caption;
            }
            catch
            {
                // 获取PPT信息失败
                slidesName = "";
            }

            return slidesName;
        }
        public IntPtr GetPptHwnd()
        {
            IntPtr hWnd = IntPtr.Zero;
            try
            {
                // 获取PPT窗口句柄
                if (pptSlideShowWindow != null)
                {
                    hWnd = GetSlideShowWindowHwnd(pptSlideShowWindow);
                    Console.WriteLine("get pptSlideShowWindow is 983");
                }
                else Console.WriteLine("pptSlideShowWindow is null 983");
            }
            catch
            {
            }

            if (hWnd == IntPtr.Zero) Console.WriteLine("啥都没有 983");

            return hWnd;
        }

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
                        pptSlideShowWindow.View.Next();
                    }
                    else if (polling == 1)
                    {
                        int currentPageTemp = -1;
                        try
                        {
                            currentPageTemp = pptSlideShowWindow.View.Slide.SlideIndex;
                        }
                        catch
                        {
                            currentPageTemp = -1;
                        }
                        if (currentPageTemp != -1)
                        {
                            pptSlideShowWindow.View.Next();
                        }
                    }
                    polling = 1;
                }
                else
                {
                    pptSlideShowWindow.View.Next();
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
                pptSlideShowWindow.View.Previous();
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
                pptSlideShowWindow.View.Exit();
            }
            catch
            {
            }
        }
        public void ViewSlideShow()
        {
            try
            {   // 打开 ppt 浏览视图
                pptSlideShowWindow.SlideNavigation.Visible = true;
            }
            catch
            {
            }
            return;
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

        // IDispatch
        [ComImport, Guid("00020400-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IDispatch
        {
            int GetTypeInfoCount();
            [PreserveSig]
            int GetTypeInfo(int iTInfo, int lcid, out ITypeInfo typeInfo);
            [PreserveSig]
            int GetIDsOfNames(ref Guid iid,
                [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPWStr)] string[] names,
                int cNames, int lcid, [Out] int[] rgDispId);
            [PreserveSig]
            int Invoke(int dispIdMember, ref Guid riid, int lcid, short wFlags,
                ref System.Runtime.InteropServices.ComTypes.DISPPARAMS pDispParams,
                out object pVarResult,
                out System.Runtime.InteropServices.ComTypes.EXCEPINFO pExcepInfo,
                out uint puArgErr);
        }

        [Flags]
        private enum InvokeFlags : short
        {
            DISPATCH_METHOD = 0x1,
            DISPATCH_PROPERTYGET = 0x2,
            DISPATCH_PROPERTYPUT = 0x4,
            DISPATCH_PROPERTYPUTREF = 0x8
        }

        // IDispatch 方法
        public static IntPtr GetSlideShowWindowHwnd(object slideShowWindowComObj)
        {
            try
            {
                if (slideShowWindowComObj == null)
                {
                    Console.WriteLine("[PptInteropHelper] slideShowWindowComObj 为 null。");
                    return IntPtr.Zero;
                }

                IntPtr pDisp = IntPtr.Zero;

                try
                {
                    try
                    {
                        pDisp = Marshal.GetIDispatchForObject(slideShowWindowComObj);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine("[PptInteropHelper] GetIDispatchForObject 失败: " + ex);
                        return IntPtr.Zero;
                    }

                    IDispatch dispatch;
                    try
                    {
                        dispatch = (IDispatch)Marshal.GetObjectForIUnknown(pDisp);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine("[PptInteropHelper] GetObjectForIUnknown / IDispatch 转换失败: " + ex);
                        return IntPtr.Zero;
                    }

                    Guid riid = Guid.Empty;
                    string[] names = new[] { "HWND" };
                    int[] dispIds = new int[1];

                    int hr;
                    try
                    {
                        hr = dispatch.GetIDsOfNames(ref riid, names, 1, 0, dispIds);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine("[PptInteropHelper] GetIDsOfNames(HWND) 调用异常: " + ex);
                        return IntPtr.Zero;
                    }

                    if (hr != 0)
                    {
                        Console.WriteLine("[PptInteropHelper] GetIDsOfNames(HWND) 失败，hr = 0x" + hr.ToString("X8"));
                        return IntPtr.Zero;
                    }

                    int dispId = dispIds[0]; // 正常情况下这就是 2010

                    System.Runtime.InteropServices.ComTypes.DISPPARAMS dispParams =
                        new System.Runtime.InteropServices.ComTypes.DISPPARAMS();

                    object result;
                    System.Runtime.InteropServices.ComTypes.EXCEPINFO excepInfo;
                    uint argErr;

                    try
                    {
                        hr = dispatch.Invoke(
                            dispId,
                            ref riid,
                            0,
                            (short)InvokeFlags.DISPATCH_PROPERTYGET,
                            ref dispParams,
                            out result,
                            out excepInfo,
                            out argErr);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine("[PptInteropHelper] Invoke(HWND) 调用异常: " + ex);
                        return IntPtr.Zero;
                    }

                    if (hr != 0)
                    {
                        Console.WriteLine("[PptInteropHelper] Invoke(HWND) 失败，hr = 0x" + hr.ToString("X8"));
                        return IntPtr.Zero;
                    }

                    // 转成 IntPtr
                    try
                    {
                        if (result is int)
                            return new IntPtr((int)result);
                        if (result is long)
                            return new IntPtr((long)result);

                        Console.WriteLine("[PptInteropHelper] HWND 返回值不是 int/long，实际类型：" +
                                          (result == null ? "null" : result.GetType().FullName));
                        return IntPtr.Zero;
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine("[PptInteropHelper] 转换 HWND 返回值为 IntPtr 时异常: " + ex);
                        return IntPtr.Zero;
                    }
                }
                finally
                {
                    if (pDisp != IntPtr.Zero)
                    {
                        try
                        {
                            Marshal.Release(pDisp);
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine("[PptInteropHelper] 释放 IDispatch 指针时异常: " + ex);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                // 兜底：即便内部哪里没想到的地方抛异常，也不让上层崩溃
                Console.WriteLine("[PptInteropHelper] GetSlideShowWindowHwnd 未处理异常: " + ex);
                return IntPtr.Zero;
            }
        }

        // Test
        public static void DumpComMembers(object comObj)
        {
            if (comObj == null)
            {
                Console.WriteLine("Object is null.");
                return;
            }

            IntPtr pDisp = IntPtr.Zero;
            ITypeInfo typeInfo = null;
            IntPtr pTypeAttr = IntPtr.Zero;

            try
            {
                // 获取 IDispatch
                pDisp = Marshal.GetIDispatchForObject(comObj);
                var dispatch = (IDispatch)Marshal.GetObjectForIUnknown(pDisp);

                int hr = dispatch.GetTypeInfo(0, 0, out typeInfo);
                if (hr != 0 || typeInfo == null)
                {
                    Console.WriteLine(string.Format("GetTypeInfo 失败，hr = 0x{0:X8}", hr));
                    return;
                }

                typeInfo.GetTypeAttr(out pTypeAttr);
                var attr = (System.Runtime.InteropServices.ComTypes.TYPEATTR)Marshal.PtrToStructure(pTypeAttr, typeof(System.Runtime.InteropServices.ComTypes.TYPEATTR));

                Console.WriteLine("=== COM members (from ITypeInfo) ===");
                Console.WriteLine("cFuncs = " + attr.cFuncs + ", cVars = " + attr.cVars);
                Console.WriteLine();

                // 枚举函数（方法 + 属性 getter/setter）
                for (int i = 0; i < attr.cFuncs; i++)
                {
                    IntPtr pFuncDesc = IntPtr.Zero;
                    typeInfo.GetFuncDesc(i, out pFuncDesc);
                    try
                    {
                        var funcDesc = (System.Runtime.InteropServices.ComTypes.FUNCDESC)Marshal.PtrToStructure(pFuncDesc, typeof(System.Runtime.InteropServices.ComTypes.FUNCDESC));

                        string[] names = new string[1];
                        int cNames;
                        typeInfo.GetNames(funcDesc.memid, names, 1, out cNames);
                        string name = cNames > 0 ? names[0] : ("MEMBER_" + funcDesc.memid);

                        Console.WriteLine(string.Format(
                            "FUNC[{0}] DISPID={1}, Name={2}, invkind={3}",
                            i, funcDesc.memid, name, funcDesc.invkind));
                    }
                    finally
                    {
                        if (pFuncDesc != IntPtr.Zero)
                            typeInfo.ReleaseFuncDesc(pFuncDesc);
                    }
                }

                Console.WriteLine();

                // 枚举变量（字段/常量）
                for (int i = 0; i < attr.cVars; i++)
                {
                    IntPtr pVarDesc = IntPtr.Zero;
                    typeInfo.GetVarDesc(i, out pVarDesc);
                    try
                    {
                        var varDesc = (System.Runtime.InteropServices.ComTypes.VARDESC)Marshal.PtrToStructure(pVarDesc, typeof(System.Runtime.InteropServices.ComTypes.VARDESC));
                        string[] names = new string[1];
                        int cNames;
                        typeInfo.GetNames(varDesc.memid, names, 1, out cNames);
                        string name = cNames > 0 ? names[0] : ("VAR_" + varDesc.memid);

                        Console.WriteLine(string.Format(
                            "VAR[{0}] DISPID={1}, Name={2}",
                            i, varDesc.memid, name));
                    }
                    finally
                    {
                        if (pVarDesc != IntPtr.Zero)
                            typeInfo.ReleaseVarDesc(pVarDesc);
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("DumpComMembers 异常: " + ex);
            }
            finally
            {
                if (typeInfo != null && pTypeAttr != IntPtr.Zero)
                    typeInfo.ReleaseTypeAttr(pTypeAttr);

                if (pDisp != IntPtr.Zero)
                    Marshal.Release(pDisp);
            }
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