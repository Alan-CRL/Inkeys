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
using System.CodeDom;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
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
    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("65F6E9C1-63EC-4003-B89F-8F425A3C2FEA")]
    public interface IPptCOMServer
    {
        // 初始化函数
        unsafe bool Initialization(int* TotalPage, int* CurrentPage);
        string CheckCOM();

        // 获取函数
        unsafe int IsPptOpen();

        // 信息获取函数
        string slideNameIndex();
        IntPtr GetPptHwnd();

        // 操控函数
        unsafe void NextSlideShow(int check);
        unsafe void PreviousSlideShow();
        void EndSlideShow();
        void ViewSlideShow();
        void ActivateSildeShowWindow();
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
                // TODO 通过宽高判断有效性

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
                            Console.WriteLine($"找到 application!");

                            /*
                            if (candidateApp is Microsoft.Office.Interop.PowerPoint.Application)
                            {
                                try
                                {
                                    Microsoft.Office.Interop.PowerPoint.Application app = (Microsoft.Office.Interop.PowerPoint.Application)candidateApp;

                                    candidateApp = app;

                                    Console.WriteLine($"找到正版 application!");
                                }
                                catch
                                {
                                    Console.WriteLine($"转换为正版 application 失败!");
                                }
                            }*/

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
                                                    if (false && IsSlideShowWindowActive((object)ssWin))
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
                            // Console.WriteLine($"find new {pptApplication.HWND}");

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

                                        Console.WriteLine($"事件绑定成功 !!!");

                                        try
                                        {
                                            var tmp = pptApplication.HWND;
                                            Console.WriteLine($"获取 HWND 绑定成功 !!!");
                                        }
                                        catch { }
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

                                if (pptActivePresentation.SlideShowWindow == null || (pptActivePresentation.SlideShowWindow != null && !IsValidSlideShowWindow(pptSlideShowWindow)))
                                {
                                    if (pptSlideShowWindow != pptActivePresentation.SlideShowWindow)
                                    {
                                        pptSlideShowWindow = pptActivePresentation.SlideShowWindow;

                                        Console.WriteLine($"发现窗口，成功设置 slideshowwindow");
                                    }
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
                                        *pptCurrentPage = pptSlideShowWindow.View.Slide.SlideIndex;

                                        if (pptSlideShowWindow.View.Slide.SlideIndex >= pptActivePresentation.Slides.Count) polling = 1;
                                        else polling = 0;
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
            IntPtr hwnd = IntPtr.Zero;
            if (pptSlideShowWindow == null) return IntPtr.Zero;

            object pptSlideShowWindowObj = pptSlideShowWindow;
            // object pptSlideShowWindowObj = pptApplication;

            /*
            IDispatch disp;
            try
            {
                disp = (IDispatch)pptSlideShowWindowObj;
            }
            catch (Exception ex)
            {
                Console.WriteLine("[HWND-Check] 无法转换为 IDispatch: " + ex);
                return IntPtr.Zero;
            }

            int hr;
            uint typeInfoCount;
            hr = disp.GetTypeInfoCount(out typeInfoCount);
            Console.WriteLine("[HWND-Check] GetTypeInfoCount hr=0x{0:X}, count={1}", hr, typeInfoCount);
            if (hr != 0 || typeInfoCount == 0)
                return IntPtr.Zero;

            ITypeInfo typeInfo;
            disp.GetTypeInfo(0, 0, out typeInfo);
            if (typeInfo == null)
            {
                Console.WriteLine("[HWND-Check] GetTypeInfo 返回 null");
                return IntPtr.Zero;
            }

            IntPtr typeAttrPtr = IntPtr.Zero;
            try
            {
                typeInfo.GetTypeAttr(out typeAttrPtr);
                var attr = (System.Runtime.InteropServices.ComTypes.TYPEATTR)Marshal.PtrToStructure(typeAttrPtr, typeof(System.Runtime.InteropServices.ComTypes.TYPEATTR));

                Console.WriteLine("[HWND-Check] TYPEATTR: cFuncs={0}, guid={1}", attr.cFuncs, attr.guid);

                int indexFound = -1;
                IntPtr funcDescPtr = IntPtr.Zero;

                for (int i = 0; i < attr.cFuncs; i++)
                {
                    typeInfo.GetFuncDesc(i, out funcDescPtr);
                    var funcDesc = (System.Runtime.InteropServices.ComTypes.FUNCDESC)Marshal.PtrToStructure(funcDescPtr, typeof(System.Runtime.InteropServices.ComTypes.FUNCDESC));

                    // 打印一下每个函数的 memid 和名字，方便你自己再对一下
                    string name;
                    int cNames;
                    {
                        var names = new string[1];
                        typeInfo.GetNames(funcDesc.memid, names, 1, out cNames);
                        name = cNames > 0 ? names[0] : "<no name>";
                    }

                    Console.WriteLine("[HWND-Check] Func index={0}, memid={1}, name={2}, invkind={3}",
                        i, funcDesc.memid, name, funcDesc.invkind);

                    typeInfo.ReleaseFuncDesc(funcDescPtr);
                    funcDescPtr = IntPtr.Zero;
                }

                if (indexFound == -1)
                    Console.WriteLine("[HWND-Check] 没找到 memid=2010 的 FUNCDESC（和之前 dump 不一致？）");
            }
            catch (Exception ex)
            {
                Console.WriteLine("[HWND-Check] 枚举 FUNCDESC 失败: " + ex);
            }
            finally
            {
                if (typeAttrPtr != IntPtr.Zero)
                    typeInfo.ReleaseTypeAttr(typeAttrPtr);
            }

            hwnd = TryGetHwndViaDispId2010(pptSlideShowWindowObj);*/

            hwnd = GetSlideShowWindowHwnd(pptSlideShowWindow);

            if (hwnd == IntPtr.Zero) Console.WriteLine("啥都没有 983");

            return hwnd;
        }

        /*

        public static IntPtr TryGetHwndViaDispId2010(object pptSlideShowWindow)
        {
            if (pptSlideShowWindow == null)
            {
                Console.WriteLine("[HWND-2010] pptSlideShowWindow == null");
                return IntPtr.Zero;
            }

            Console.WriteLine("[HWND-2010] 尝试通过 DispId=2010 的 IDispatch.Invoke 读取 SlideShowWindow.HWND ...");

            IDispatch disp;
            try
            {
                disp = (IDispatch)pptSlideShowWindow;
            }
            catch (Exception ex)
            {
                Console.WriteLine("[HWND-2010] 无法转换为 IDispatch: " + ex);
                return IntPtr.Zero;
            }

            Guid iidNull = Guid.Empty;

            var dp = new DISPPARAMS
            {
                rgvarg = IntPtr.Zero,
                rgdispidNamedArgs = IntPtr.Zero,
                cArgs = 0,
                cNamedArgs = 0
            };

            object result;
            var exInfo = new EXCEPINFO();

            try
            {
                const ushort DISPATCH_PROPERTYGET = 0x2;
                const int DISP_ID_HWND = 2010;

                int hr = disp.Invoke(
                    DISP_ID_HWND,
                    ref iidNull,
                    0,
                    DISPATCH_PROPERTYGET,
                    ref dp,
                    out result,
                    ref exInfo,
                    null
                );

                Console.WriteLine("[HWND-2010] Invoke(hr=0x{0:X}), DispId={1}", hr, DISP_ID_HWND);

                if (hr != 0)
                {
                    Console.WriteLine("[HWND-2010] Invoke 返回错误 hr=0x{0:X}", hr);
                    if (!string.IsNullOrEmpty(exInfo.bstrSource))
                        Console.WriteLine("[HWND-2010] Source: " + exInfo.bstrSource);
                    if (!string.IsNullOrEmpty(exInfo.bstrDescription))
                        Console.WriteLine("[HWND-2010] Description: " + exInfo.bstrDescription);
                    return IntPtr.Zero;
                }

                if (result == null)
                {
                    Console.WriteLine("[HWND-2010] Invoke 成功，但 result == null");
                    return IntPtr.Zero;
                }

                Console.WriteLine("[HWND-2010] Invoke 返回类型: {0}, 值: {1}",
                    result.GetType().FullName, result);

                IntPtr hwnd;

                if (result is int i32)
                    hwnd = new IntPtr(i32);
                else if (result is long i64)
                    hwnd = new IntPtr(i64);
                else if (result is short i16)
                    hwnd = new IntPtr(i16);
                else if (result is uint ui32)
                    hwnd = new IntPtr(unchecked((int)ui32));
                else if (result is ulong ui64)
                    hwnd = new IntPtr(unchecked((long)ui64));
                else
                    throw new InvalidCastException("HWND 返回了无法识别的类型: " + result.GetType().FullName);

                Console.WriteLine("[HWND-2010] 成功获取 HWND: " + hwnd);
                return hwnd;
            }
            catch (Exception ex)
            {
                Console.WriteLine("[HWND-2010] 调用 IDispatch.Invoke 失败: " + ex);
                return IntPtr.Zero;
            }
        }

        [ComImport]
        [Guid("91493453-5A91-11CF-8700-00AA0060263B")]   // _SlideShowWindow
        [InterfaceType(ComInterfaceType.InterfaceIsIDispatch)]
        private interface ISlideShowWindow
        {
            [DispId(2010)]            // HWND
            int HWND { get; }
        }

        [ComImport]
        [Guid("00020400-0000-0000-C000-000000000046")]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface IDispatch
        {
            int GetTypeInfoCount(out uint pctinfo);

            void GetTypeInfo(
                uint iTInfo,
                uint lcid,
                out ITypeInfo ppTInfo
            );

            int GetIDsOfNames(
                ref Guid riid,
                [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPWStr)] string[] rgszNames,
                uint cNames,
                uint lcid,
                [MarshalAs(UnmanagedType.LPArray)] int[] rgDispId
            );

            int Invoke(
                int dispIdMember,
                ref Guid riid,
                uint lcid,
                ushort wFlags,
                ref DISPPARAMS pDispParams,
                out object pVarResult,
                ref EXCEPINFO pExcepInfo,
                IntPtr[] pArgErr
            );
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct DISPPARAMS
        {
            public IntPtr rgvarg;
            public IntPtr rgdispidNamedArgs;
            public uint cArgs;
            public uint cNamedArgs;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct EXCEPINFO
        {
            public ushort wCode;
            public ushort wReserved;
            public string bstrSource;
            public string bstrDescription;
            public string bstrHelpFile;
            public uint dwHelpContext;
            public IntPtr pvReserved;
            public IntPtr pfnDeferredFillIn;
            public int scode;
        }

        */

        private static readonly ConcurrentDictionary<Guid, Type> _ifaceCache =
        new ConcurrentDictionary<Guid, Type>();

        private static void Log(string msg) => Console.WriteLine(msg);

        public static IntPtr GetSlideShowWindowHwnd(object slideShowWindowObj)
        {
            if (slideShowWindowObj == null)
                return IntPtr.Zero;

            Log($"[PPT] .NET 类型 = {slideShowWindowObj.GetType().FullName}");

            /*=================== 路线 A：v-table / 早期绑定 ===================*/
            try
            {
                IntPtr pUnk = Marshal.GetIUnknownForObject(slideShowWindowObj);
                Log($"[PPT] IUnknown* = 0x{pUnk.ToInt64():X}");

                Guid iid = GetRealIid(slideShowWindowObj);
                Log($"[PPT] 真实 IID = {iid}");

                Type ifaceType = _ifaceCache.GetOrAdd(iid, BuildRuntimeInterface);   // ← 生成/缓存接口
                if (ifaceType == null)
                    throw new Exception("动态接口生成失败");

                object strong = Marshal.GetTypedObjectForIUnknown(pUnk, ifaceType);

                object raw = ifaceType.InvokeMember("get_HWND",
                                                    BindingFlags.InvokeMethod,
                                                    null,
                                                    strong,
                                                    null);

                long hwndVal = Convert.ToInt64(raw);
                Log($"[PPT] 早期绑定获得 HWND = 0x{hwndVal:X}");
                return new IntPtr(hwndVal);
            }
            catch (Exception ex)
            {
                Log($"[PPT] 早期绑定失败：{ex.Message}");
            }

            /*=================== 路线 B：IDispatch.Invoke ===================*/
            Log("[PPT] 尝试改用 IDispatch.Invoke…");
            IntPtr h = TryInvokeDispGet(slideShowWindowObj);
            if (h != IntPtr.Zero)
            {
                Log($"[PPT] IDispatch.Invoke 获得 HWND = 0x{h.ToInt64():X}");
                return h;
            }

            Log("[PPT] 仍然失败，返回 0。");
            return IntPtr.Zero;
        }

        /*------------------- BuildRuntimeInterface (方案 A) ------------------*/
        private static Type BuildRuntimeInterface(Guid iid)
        {
            try
            {
                string asmName = "DynPptIfaceAsm_" + iid.ToString("N");
                string typeName = "SlideShowWindow_" + iid.ToString("N");

                var an = new AssemblyName(asmName);
                AssemblyBuilder ab = AppDomain.CurrentDomain
                                              .DefineDynamicAssembly(an, AssemblyBuilderAccess.Run);
                ModuleBuilder mb = ab.DefineDynamicModule(asmName);

                TypeBuilder tb = mb.DefineType(typeName,
                                               TypeAttributes.Public |
                                               TypeAttributes.Interface |
                                               TypeAttributes.Abstract |
                                               TypeAttributes.Import,
                                               typeof(object));

                tb.SetCustomAttribute(new CustomAttributeBuilder(
                    typeof(ComImportAttribute).GetConstructor(Type.EmptyTypes),
                    new object[0]));
                tb.SetCustomAttribute(new CustomAttributeBuilder(
                    typeof(InterfaceTypeAttribute).GetConstructor(new[] { typeof(ComInterfaceType) }),
                    new object[] { ComInterfaceType.InterfaceIsIUnknown }));
                tb.SetCustomAttribute(new CustomAttributeBuilder(
                    typeof(GuidAttribute).GetConstructor(new[] { typeof(string) }),
                    new object[] { iid.ToString() }));

                // ---- 只声明一个 PreserveSig 方法 get_HWND() ----
                MethodBuilder mbGet = tb.DefineMethod("get_HWND",
                                                      MethodAttributes.Public |
                                                      MethodAttributes.Abstract |
                                                      MethodAttributes.Virtual |
                                                      MethodAttributes.NewSlot,
                                                      typeof(IntPtr),            // 直接 IntPtr
                                                      Type.EmptyTypes);
                mbGet.SetCustomAttribute(new CustomAttributeBuilder(
                    typeof(PreserveSigAttribute).GetConstructor(Type.EmptyTypes),
                    new object[0]));

                // 如果你乐意，也可以再加 DispId，但 v-table 调用其实用不到
                // mbGet.SetCustomAttribute(new CustomAttributeBuilder(
                //     typeof(DispIdAttribute).GetConstructor(new[] { typeof(int) }),
                //     new object[] { 2010 }));

                return tb.CreateType();          // 不再报 TypeLoadException
            }
            catch (Exception ex)
            {
                Log($"[PPT] BuildRuntimeInterface() 异常：{ex}");
                return null;
            }
        }

        /*------------------- 其余辅助函数保持不变 ------------------*/

        private static Guid GetRealIid(object comObj)
        {
            try
            {
                var disp = (IDispatch)comObj;
                disp.GetTypeInfo(0, 0, out ITypeInfo ti);
                ti.GetTypeAttr(out IntPtr pAttr);

                var attr = (System.Runtime.InteropServices.ComTypes.TYPEATTR)
                            Marshal.PtrToStructure(pAttr,
                                typeof(System.Runtime.InteropServices.ComTypes.TYPEATTR));

                Guid iid = attr.guid;

                ti.ReleaseTypeAttr(pAttr);
                Marshal.ReleaseComObject(ti);
                return iid;
            }
            catch
            {
                return Guid.Empty;
            }
        }

        private static IntPtr TryInvokeDispGet(object comObj)
        {
            try
            {
                var disp = (IDispatch)comObj;
                Guid riid = Guid.Empty;

                int[] dispIds = new int[1];
                disp.GetIDsOfNames(ref riid, new[] { "HWND" }, 1, 0, dispIds);
                int dispidHwnd = dispIds[0];
                Log($"[PPT] GetIDsOfNames 得到 dispid = {dispidHwnd}");

                const ushort DISPATCH_PROPERTYGET = 0x0002;
                var dp = new System.Runtime.InteropServices.ComTypes.DISPPARAMS();

                object result;
                var exInfo = new System.Runtime.InteropServices.ComTypes.EXCEPINFO();

                int hr = disp.Invoke(dispidHwnd, ref riid, 0, DISPATCH_PROPERTYGET,
                                     ref dp, out result, ref exInfo, null);

                if (hr != 0 || result == null)
                    return IntPtr.Zero;

                return new IntPtr(Convert.ToInt64(result));
            }
            catch (Exception ex)
            {
                Log($"[PPT] TryInvokeDispGet() 异常：{ex.Message}");
                return IntPtr.Zero;
            }
        }

        [ComImport]
        [Guid("00020400-0000-0000-C000-000000000046")]
        [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IDispatch
        {
            int GetTypeInfoCount(out uint pctinfo);
            void GetTypeInfo(uint iTInfo, uint lcid, out ITypeInfo ppTInfo);
            void GetIDsOfNames(
                ref Guid riid,
                [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPWStr)]
        string[] rgszNames,
                uint cNames,
                uint lcid,
                [Out, MarshalAs(UnmanagedType.LPArray)] int[] rgDispId);
            int Invoke(int dispIdMember, ref Guid riid, uint lcid, ushort wFlags,
                       ref System.Runtime.InteropServices.ComTypes.DISPPARAMS pDispParams, out object pVarResult,
                       ref System.Runtime.InteropServices.ComTypes.EXCEPINFO pExcepInfo, IntPtr[] pArgErr);
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
        public void ActivateSildeShowWindow()
        {
            if (pptSlideShowWindow == null) return;

            try
            {
                pptSlideShowWindow.Activate();

                Console.WriteLine("ActivateSildeShowWindow called");
            }
            catch
            {
            }
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

        // Test

        private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool EnumChildWindows(IntPtr hWndParent, EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

        [DllImport("user32.dll")]
        private static extern bool GetWindowRect(IntPtr hWnd, out pptRECT lpRect);

        [DllImport("gdi32.dll")]
        private static extern int GetDeviceCaps(IntPtr hdc, int nIndex);

        [DllImport("user32.dll")]
        private static extern IntPtr GetDC(IntPtr hWnd);

        [DllImport("user32.dll")]
        private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

        [StructLayout(LayoutKind.Sequential)]
        public struct pptRECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
            public int Width => Right - Left;
            public int Height => Bottom - Top;
        }

        private const int LOGPIXELSX = 88;
        private const int LOGPIXELSY = 90;

        // =========================================================
        // 2. 候选窗口结构
        // =========================================================
        private class WindowCandidate
        {
            public IntPtr Hwnd { get; set; }
            public string Title { get; set; }
            public int Priority { get; set; }
            public string DebugInfo { get; set; }
        }

        private static void GetSystemDpi(out float dpiX, out float dpiY)
        {
            IntPtr hdc = GetDC(IntPtr.Zero);
            try
            {
                // 获取屏幕逻辑 DPI
                dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
                dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
            }
            finally
            {
                ReleaseDC(IntPtr.Zero, hdc);
            }
        }

        private static int PointsToPixels(float points, float dpi)
        {
            // 公式: Pixels = Points * (DPI / 72)
            return (int)Math.Round(points * (dpi / 72.0f));
        }

        // =========================================================
        // 3. 核心逻辑
        // =========================================================

        /// <summary>
        /// 利用 COM 对象的几何数据和元数据，暴力扫描系统窗口获取 HWND。
        /// 支持枚举子窗口以解决 WPS 嵌入放映的问题。
        /// </summary>

        public static IntPtr FindWindowByDetailedMatch(string pptFullName, string comAppName, dynamic ssw)
        {
            Console.WriteLine("========== [PptPreciseFinder] 开始扫描 (单位修正版) ==========");

            // -----------------------------------------------------
            // A. 参数与环境预检
            // -----------------------------------------------------
            if (ssw == null || string.IsNullOrEmpty(pptFullName) || string.IsNullOrEmpty(comAppName))
            {
                Console.WriteLine("[Error] 参数无效，返回 Zero。");
                return IntPtr.Zero;
            }

            string requiredAppKeyword = "";
            if (comAppName.IndexOf("PowerPoint", StringComparison.OrdinalIgnoreCase) >= 0) requiredAppKeyword = "PowerPoint";
            else if (comAppName.IndexOf("WPS", StringComparison.OrdinalIgnoreCase) >= 0) requiredAppKeyword = "WPS";

            if (string.IsNullOrEmpty(requiredAppKeyword))
            {
                Console.WriteLine($"[Error] 未知 AppName: '{comAppName}'");
                return IntPtr.Zero;
            }

            string requiredFileName = "";
            try { requiredFileName = Path.GetFileNameWithoutExtension(pptFullName); } catch { }
            if (string.IsNullOrEmpty(requiredFileName)) return IntPtr.Zero;

            // -----------------------------------------------------
            // B. 几何数据提取与转换 (Points -> Pixels)
            // -----------------------------------------------------
            float ptLeft = 0, ptTop = 0, ptWidth = 0, ptHeight = 0;
            try
            {
                // 先拿 float 类型的 Points
                ptLeft = (float)ssw.Left;
                ptTop = (float)ssw.Top;
                ptWidth = (float)ssw.Width;
                ptHeight = (float)ssw.Height;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Error] 读取 COM 失败: {ex.Message}");
                return IntPtr.Zero;
            }

            // 获取 DPI
            float dpiX, dpiY;
            GetSystemDpi(out dpiX, out dpiY);
            Console.WriteLine($"[DPI 检测] System DPI: X={dpiX}, Y={dpiY}");

            // 执行转换
            int pxLeft = PointsToPixels(ptLeft, dpiX);
            int pxTop = PointsToPixels(ptTop, dpiY);
            int pxWidth = PointsToPixels(ptWidth, dpiX);
            int pxHeight = PointsToPixels(ptHeight, dpiY);

            Console.WriteLine("[几何变换] COM(Points) -> Win32(Pixels):");
            Console.WriteLine($"  Left:   {ptLeft,8:F2} -> {pxLeft}");
            Console.WriteLine($"  Top:    {ptTop,8:F2} -> {pxTop}");
            Console.WriteLine($"  Width:  {ptWidth,8:F2} -> {pxWidth}");
            Console.WriteLine($"  Height: {ptHeight,8:F2} -> {pxHeight}");

            if (pxWidth <= 0 || pxHeight <= 0)
            {
                Console.WriteLine("[Error] 转换后宽高无效，停止。");
                return IntPtr.Zero;
            }

            // -----------------------------------------------------
            // C. 准备扫描
            // -----------------------------------------------------
            IntPtr fgHwnd = GetForegroundWindow();
            uint fgPid;
            GetWindowThreadProcessId(fgHwnd, out fgPid);

            List<WindowCandidate> candidates = new List<WindowCandidate>();
            int scannedCount = 0;

            bool ScanCallback(IntPtr hWnd)
            {
                scannedCount++;
                try
                {
                    // [第一道关卡] 标题匹配
                    StringBuilder sbTitle = new StringBuilder(512);
                    GetWindowText(hWnd, sbTitle, sbTitle.Capacity);
                    string title = sbTitle.ToString();

                    if (string.IsNullOrEmpty(title)) return true;

                    bool matchApp = title.IndexOf(requiredAppKeyword, StringComparison.OrdinalIgnoreCase) >= 0;
                    bool matchFile = title.IndexOf(requiredFileName, StringComparison.OrdinalIgnoreCase) >= 0;

                    if (matchApp && matchFile)
                    {
                        Console.WriteLine("--------------------------------------------------");
                        Console.WriteLine($"[发现潜在目标] HWND: {hWnd}");
                        Console.WriteLine($"  -> 标题: '{title}'");
                    }
                    else
                    {
                        return true;
                    }

                    // [第二道关卡] 几何匹配 (使用转换后的 Pixels)
                    pptRECT winRect;
                    if (!GetWindowRect(hWnd, out winRect)) return true;

                    // 容差设定: 20px (覆盖 Win10 阴影和四舍五入误差)
                    int tolerance = 20;

                    int dLeft = Math.Abs(winRect.Left - pxLeft);
                    int dTop = Math.Abs(winRect.Top - pxTop);
                    int dWidth = Math.Abs(winRect.Width - pxWidth);
                    int dHeight = Math.Abs(winRect.Height - pxHeight);

                    Console.WriteLine($"  -> 几何对比 (目标 Pixels vs 实际 Pixels):");
                    Console.WriteLine($"     目标: L={pxLeft}, T={pxTop}, W={pxWidth}, H={pxHeight}");
                    Console.WriteLine($"     实际: L={winRect.Left}, T={winRect.Top}, W={winRect.Width}, H={winRect.Height}");
                    Console.WriteLine($"     偏差: dL={dLeft}, dT={dTop}, dW={dWidth}, dH={dHeight}");

                    bool matchGeo =
                        dLeft <= tolerance &&
                        dTop <= tolerance &&
                        dWidth <= tolerance &&
                        dHeight <= tolerance;

                    if (!matchGeo)
                    {
                        Console.WriteLine("  -> [结果] 偏差过大，不匹配。");
                        return true;
                    }

                    Console.WriteLine("  -> [结果] 几何匹配成功！");

                    // [第三道关卡] 优先级计算
                    int priority = 100;

                    bool isFocusRelated = false;
                    if (hWnd == fgHwnd)
                    {
                        isFocusRelated = true;
                    }
                    else
                    {
                        uint targetPid;
                        GetWindowThreadProcessId(hWnd, out targetPid);
                        if (fgPid > 0 && targetPid > 0)
                        {
                            try
                            {
                                using (Process fgProc = Process.GetProcessById((int)fgPid))
                                using (Process appProc = Process.GetProcessById((int)targetPid))
                                {
                                    string fgName = fgProc.ProcessName.ToLower();
                                    string appName = appProc.ProcessName.ToLower();
                                    if (fgName.StartsWith("wps") && appName.StartsWith("wpp")) isFocusRelated = true;
                                }
                            }
                            catch { }
                        }
                    }

                    if (isFocusRelated)
                    {
                        priority += 100;
                        Console.WriteLine("  -> 焦点/进程判定: +100");
                    }

                    candidates.Add(new WindowCandidate
                    {
                        Hwnd = hWnd,
                        Title = title,
                        Priority = priority,
                        DebugInfo = $"Matched Px[{winRect.Width}x{winRect.Height}]"
                    });
                }
                catch { }
                return true;
            }

            // -----------------------------------------------------
            // D. 执行枚举
            // -----------------------------------------------------
            EnumWindows((h, p) =>
            {
                ScanCallback(h);
                EnumChildWindows(h, (hChild, pChild) =>
                {
                    ScanCallback(hChild);
                    return true;
                }, IntPtr.Zero);
                return true;
            }, IntPtr.Zero);

            // -----------------------------------------------------
            // E. 结果返回
            // -----------------------------------------------------
            if (candidates.Count == 0)
            {
                Console.WriteLine("[结果] 未找到匹配窗口。");
                return IntPtr.Zero;
            }

            int maxPriority = candidates.Max(c => c.Priority);
            var bestMatches = candidates.Where(c => c.Priority == maxPriority).ToList();

            if (bestMatches.Count > 1)
            {
                Console.WriteLine($"[警告] 发现 {bestMatches.Count} 个相同优先级的窗口，返回第一个。");
            }

            var finalMatch = bestMatches.First();
            Console.WriteLine($"========== [PptPreciseFinder] 锁定目标: {finalMatch.Hwnd} ==========");
            return finalMatch.Hwnd;
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