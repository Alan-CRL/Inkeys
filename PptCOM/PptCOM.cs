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
        unsafe bool Initialization(int* TotalPage, int* CurrentPage, int* OffSignal);
        string CheckCOM();

        // 获取函数
        unsafe int PptComService();

        // 信息获取函数
        string SlideNameIndex();
        IntPtr GetPptHwnd();

        // 操控函数
        void NextSlideShow(bool check);
        void PreviousSlideShow();
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
        private unsafe int* offSignal;

        // 结束界面轮询（0正常页 1/2末页或结束放映页）
        //（2设定为运行一次不被检查的翻页，虽然我也不知道当时写这个是为了特判什么情况 hhh）
        private int polling = 0;

        private bool forcePolling = false; // 强制轮询标志
        private bool bindingEvents; // 是否已绑定事件

        private DateTime updateTime; // 更新时间点

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
            string ret = "20260102a";
            return ret;
        }

        // 过程函数
        private static void CleanUpLoopObjects(IBindCtx bindCtx, IMoniker moniker, object comObject)
        {
            if (comObject != null && Marshal.IsComObject(comObject)) Marshal.ReleaseComObject(comObject);
            if (moniker != null) Marshal.ReleaseComObject(moniker);
            if (bindCtx != null) Marshal.ReleaseComObject(bindCtx);
        }
        private void SafeRelease(object comObj)
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
        private void SafeFinalRelease(object comObj)
        {
            if (comObj == null) return;

            if (Marshal.IsComObject(comObj))
            {
                try
                {
                    Marshal.FinalReleaseComObject(comObj);
                }
                catch { }
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
                        Microsoft.Office.Interop.PowerPoint.Application app = pptApplication as Microsoft.Office.Interop.PowerPoint.Application;

                        if (app != null)
                        {
                            app.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                            app.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                            app.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                            app.PresentationBeforeClose -= new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                        }
                    }
                    catch (Exception ex)
                    {
                        // 忽略 COM 对象已分离的错误
                        Console.WriteLine($"Unbind Error: {ex.Message}");
                    }

                    bindingEvents = false;
                    forcePolling = false;
                }
            }
            catch { }

            Console.WriteLine("UnbindEventsCalled");
        }
        private unsafe void FullCleanup(bool final = false)
        {
            UnbindEvents();

            Console.WriteLine("try CLEAN");

            if (final)
            {
                SafeFinalRelease(pptSlideShowWindow);
                SafeFinalRelease(pptActivePresentation);
                SafeFinalRelease(pptApplication);
            }
            else
            {
                SafeRelease(pptSlideShowWindow);
                SafeRelease(pptActivePresentation);
                SafeRelease(pptApplication);
            }

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

            // 强制 GC，这对 WPS 的资源释放至关重要
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();

            Console.WriteLine("CLEAN!");
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
        private static bool IsValidSlideShowWindow(dynamic pptSlideShowWindow)
        {
            if (pptSlideShowWindow == null) return false;

            bool ret = false;

            try
            {
                int temp = pptSlideShowWindow.Active;
                ret = true;
            }
            catch (Exception ex)
            {
                ret = false;
                Console.WriteLine($"不是ssw {ex.Message}");
            }

            return ret;
        }

        // 事件函数
        private unsafe void SlideShowChange(object WnObj)
        {
            Console.WriteLine("Change1");

            updateTime = DateTime.Now;

            try
            {
                // 假设全局变量 pptSlideShowWindow 已经是 dynamic 类型，这里的调用会自动进行后期绑定
                *pptCurrentPage = GetCurrentSlideIndex(pptSlideShowWindow);

                if (GetCurrentSlideIndex(pptSlideShowWindow) >= GetTotalSlideIndex(pptActivePresentation)) polling = 1;
                else polling = 0;
            }
            catch
            {
                *pptCurrentPage = -1;
                polling = 1;
            }

            Console.WriteLine("Change2");
        }
        private unsafe void SlideShowBegin(object WnObj)
        {
            Console.WriteLine("Begin1");

            updateTime = DateTime.Now;

            // 【修改】直接赋值给 dynamic 类型的全局变量
            pptSlideShowWindow = WnObj;

            try
            {
                if (GetCurrentSlideIndex(pptSlideShowWindow) >= GetTotalSlideIndex(pptActivePresentation)) polling = 1;
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
                *pptTotalPage = GetTotalSlideIndex(pptActivePresentation);
                *pptCurrentPage = GetCurrentSlideIndex(pptSlideShowWindow);
            }
            catch
            {
                Console.WriteLine("Begin3");
            }
            Console.WriteLine("Begin2");
        }
        private unsafe void SlideShowShowEnd(object WnObj)
        {
            Console.WriteLine("END1");

            updateTime = DateTime.Now;

            *pptCurrentPage = -1;
            *pptTotalPage = -1;

            Console.WriteLine("END2");
        }
        private void PresentationBeforeClose(object WnObj, ref bool cancel)
        {
            Console.WriteLine("PBCalled1");

            dynamic Wn = WnObj;

            try
            {
                if (bindingEvents && pptApplication != null)
                {
                    try
                    {
                        Microsoft.Office.Interop.PowerPoint.Application app = pptApplication as Microsoft.Office.Interop.PowerPoint.Application;

                        if (app != null)
                        {
                            app.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                            app.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                            app.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                            app.PresentationBeforeClose -= new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                        }
                    }
                    catch (Exception ex)
                    {
                        // 忽略 COM 对象已分离的错误
                        Console.WriteLine($"Unbind Error: {ex.Message}");
                    }

                    bindingEvents = false;
                    forcePolling = false;
                }
            }
            catch { }

            cancel = false;

            Console.WriteLine("PBCalled2");
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

                        if (LooksLikePresentationFile(displayName) || displayName == "!{91493441-5A91-11CF-8700-00AA0060263B}")
                        {
                            rot.GetObject(moniker[0], out comObject);
                            if (comObject != null)
                            {
                                // 尝试通过 Presentation 对象获取 Application
                                try
                                {
                                    // 使用反射获取 Application 属性
                                    object appObj = comObject.GetType().InvokeMember("Application", BindingFlags.GetProperty, null, comObject, null);
                                    candidateApp = appObj;
                                }
                                catch { }
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
                                                object val = ssWindow.Active;
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

                                    SafeRelease(bestApp);

                                    // 这里不需要转换，直接赋值 object
                                    bestApp = candidateApp;
                                    candidateApp = null; // 转移所有权，candidateApp 置空防止后续被错误释放

                                    // 注意：虽然 candidateApp 变量置空了，但该对象引用仍存在于 foundAppObjects 中
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
                        // 如果 candidateApp 已经转移给 bestApp，它已经是 null，SafeRelease 安全
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

                if (enumMoniker != null) Marshal.ReleaseComObject(enumMoniker);
                if (rot != null) Marshal.ReleaseComObject(rot);
            }

            return bestApp;
        }
        public unsafe int PptComService()
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
                        object bestApp = GetAnyActivePowerPoint(pptApplication, out bestPriority, out targetPriority);
                        bool needRebind = false;

                        Console.WriteLine($"now: {targetPriority}, best: {bestPriority}");

                        if (pptApplication == null && bestApp != null) needRebind = true;
                        else if (pptApplication != null && bestApp != null && bestPriority > targetPriority)
                        {
                            // 完全不同
                            if (!AreComObjectsEqual((object)pptApplication, bestApp))
                            {
                                needRebind = true;
                            }
                        }

                        if (needRebind == true)
                        {
                            bool wait = (pptApplication != null);
                            FullCleanup();

                            if (bestApp != null)
                            {
                                if (wait) Thread.Sleep(1000);

                                pptApplication = bestApp;

                                try
                                {
                                    // dynamic 后期绑定
                                    pptActivePresentation = pptApplication.ActivePresentation;
                                    updateTime = DateTime.Now;

                                    try
                                    {
                                        pptSlideShowWindow = pptActivePresentation.SlideShowWindow;
                                        *pptTotalPage = tempTotalPage = GetTotalSlideIndex(pptActivePresentation);
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
                                            *pptCurrentPage = GetCurrentSlideIndex(pptSlideShowWindow);

                                            if (GetCurrentSlideIndex(pptSlideShowWindow) >= GetTotalSlideIndex(pptActivePresentation)) polling = 1;
                                            else polling = 0;
                                        }
                                        catch
                                        {
                                            *pptCurrentPage = -1;
                                            polling = 1;
                                        }
                                    }

                                    try
                                    {
                                        // 关键修改：这里不要直接用 pptApplication +=，而是先强转
                                        Microsoft.Office.Interop.PowerPoint.Application app = pptApplication as Microsoft.Office.Interop.PowerPoint.Application;

                                        if (app != null)
                                        {
                                            app.SlideShowNextSlide += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                                            app.SlideShowBegin += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                                            app.SlideShowEnd += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);

                                            try
                                            {
                                                app.PresentationBeforeClose += new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                                            }
                                            catch
                                            {
                                                Console.WriteLine($"无法注册事件 2!");
                                            }

                                            bindingEvents = true;
                                            forcePolling = false;

                                            Console.WriteLine($"事件注册成功!");
                                        }
                                        else
                                        {
                                            bindingEvents = false;
                                            forcePolling = true;

                                            Console.WriteLine($"转换 Application 接口失败，无法注册事件");
                                        }
                                    }
                                    catch (Exception ex)
                                    {
                                        bindingEvents = false;
                                        forcePolling = true;

                                        Console.WriteLine($"无法注册事件 1! {ex.Message}");
                                    }

                                    bindingEvents = false;
                                    forcePolling = true;

                                    Console.WriteLine($"成功绑定! {pptApplication.Name}");
                                }
                                catch
                                {
                                    FullCleanup();
                                }
                            }
                        }
                        else
                        {
                            if (bestApp != null && (pptApplication == null || !AreComObjectsEqual((object)pptApplication, bestApp)))
                            {
                                SafeRelease(bestApp);
                                bestApp = null;
                            }
                        }
                    }

                    // 状态监测与轮询
                    if (pptApplication != null && pptActivePresentation != null)
                    {
                        // 检查是否同进程切换文档 (dynamic 比较引用)
                        // 注意：如果报错 RuntimeBinderException 说明对象可能已失效
                        dynamic activePersentation = null;
                        dynamic slideShowWindow = null;

                        try
                        {
                            activePersentation = pptApplication.ActivePresentation;

                            if (!AreComObjectsEqual((object)pptActivePresentation, (object)activePersentation))
                            {
                                Console.WriteLine("End in 1");
                                break;
                            }
                        }
                        catch (COMException ex) when ((uint)ex.ErrorCode == 0x8001010A)
                        {
                            Console.WriteLine($"PowerPoint 忙，稍后重试");
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"End in 2 {ex.ToString()}");
                            break;
                        }
                        finally
                        {
                            SafeRelease(activePersentation);
                            activePersentation = null;
                        }

                        // ----------
                        // 检测是否处于放映模式
                        bool isSlideShowActive = false;
                        try
                        {
                            activePersentation = pptApplication.ActivePresentation;

                            // 检查 SlideShowWindows 集合
                            if (activePersentation != null && GetSlideShowWindowsCount(pptApplication) > 0)
                            {
                                isSlideShowActive = true;

                                slideShowWindow = activePersentation.SlideShowWindow;
                                if (pptSlideShowWindow == null || (pptSlideShowWindow != null && !IsValidSlideShowWindow(pptSlideShowWindow)))
                                {
                                    if (!AreComObjectsEqual((object)pptSlideShowWindow, (object)slideShowWindow))
                                    {
                                        SafeRelease(pptSlideShowWindow);

                                        pptSlideShowWindow = slideShowWindow;

                                        Console.WriteLine($"发现窗口，成功设置 slideshowwindow");
                                    }
                                    else
                                    {
                                        Console.WriteLine($"发现窗口，但无须设置 1");
                                    }
                                }
                            }
                        }
                        catch (COMException ex) when ((uint)ex.ErrorCode == 0x8001010A)
                        {
                            Console.WriteLine($"PowerPoint 忙，稍后重试");
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"发现窗口失败 1: {ex.ToString()}");

                            // 如果这里报错，说明 App 可能挂了，或者 WPS 上下文丢失
                            // 标记为非放映状态，后续逻辑会处理
                        }
                        finally
                        {
                            SafeRelease(activePersentation);
                            activePersentation = null;

                            if (!AreComObjectsEqual((object)pptSlideShowWindow, (object)slideShowWindow))
                            {
                                SafeRelease(slideShowWindow);
                                slideShowWindow = null;

                                Console.WriteLine($"slideShowWindow 被清理");
                            }
                        }

                        if (isSlideShowActive)
                        {
                            if ((DateTime.Now - updateTime).TotalMilliseconds > 3000 || forcePolling)
                            {
                                Console.WriteLine($"轮询");

                                try
                                {
                                    slideShowWindow = pptActivePresentation.SlideShowWindow;

                                    // 获取当前播放的PPT幻灯片窗口对象（保证当前处于放映状态）
                                    if (slideShowWindow != null) *pptTotalPage = tempTotalPage = GetTotalSlideIndex(pptActivePresentation);
                                    else *pptTotalPage = tempTotalPage = -1;
                                }
                                catch (Exception ex)
                                {
                                    *pptTotalPage = tempTotalPage = -1;

                                    Console.WriteLine($"获取总页数失败 {ex.Message}");
                                }
                                finally
                                {
                                    SafeRelease(slideShowWindow);
                                    slideShowWindow = null;
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
                                        int currentPage = GetCurrentSlideIndex(pptSlideShowWindow);
                                        *pptCurrentPage = currentPage;

                                        if (currentPage >= GetTotalSlideIndex(pptActivePresentation)) polling = 1;
                                        else polling = 0;
                                    }
                                    catch (Exception ex)
                                    {
                                        *pptCurrentPage = -1;
                                        polling = 1;

                                        Console.WriteLine($"获取当前页数失败 {ex.ToString()}");
                                    }
                                }

                                updateTime = DateTime.Now;
                            }
                            if (polling != 0)
                            {
                                try
                                {
                                    *pptCurrentPage = GetCurrentSlideIndex(pptSlideShowWindow);
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

                    // 关闭信号检测
                    if (*offSignal != 0)
                    {
                        Console.WriteLine("offSignal Close");
                        break;
                    }

                    Thread.Sleep(500);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Fail 991");
                Console.WriteLine($"异常信息: {ex.Message}");
            }
            finally
            {
                FullCleanup(true);
            }

            Console.WriteLine("PPT Monitor End");

            return 0;
        }

        // 信息获取函数
        public string SlideNameIndex()
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
            IntPtr ret = IntPtr.Zero;

            // 尝试默认方法
            ret = GetPptHwndFromSlideShowWindow(pptSlideShowWindow);

            if (ret == IntPtr.Zero)
            {
                // 尝试备用方法
                ret = GetPptHwndWin32(pptActivePresentation.FullName, pptApplication.Name);
            }

            return ret;
        }
        private IntPtr GetPptHwndFromSlideShowWindow(object pptSlideShowWindowObj)
        {
            IntPtr hwnd = IntPtr.Zero;
            if (pptSlideShowWindowObj == null) return IntPtr.Zero;

            try
            {
                Microsoft.Office.Interop.PowerPoint.SlideShowWindow slideWindow = (Microsoft.Office.Interop.PowerPoint.SlideShowWindow)pptSlideShowWindowObj;

                int hwndVal = slideWindow.HWND;

                // 成功返回
                hwnd = new IntPtr(hwndVal);
            }
            catch { }

            if (hwnd == IntPtr.Zero) Console.WriteLine("啥都没有 GetPptHwndFromSSW");
            else Console.WriteLine("Got Hwnd GetPptHwndFromSSW");

            return hwnd;
        }
        private IntPtr GetPptHwndWin32(string presFullName, string appName)
        {
            // 全局 try-catch 保证异常安全，失败一律返回 Zero
            try
            {
                // -------------------------------------------------
                // 步骤 A: 基础参数校验
                // -------------------------------------------------
                if (string.IsNullOrWhiteSpace(presFullName) || string.IsNullOrWhiteSpace(appName))
                {
                    return IntPtr.Zero;
                }

                // -------------------------------------------------
                // 步骤 B: 提取关键信息 (应用类型 & 文件名)
                // -------------------------------------------------
                string targetAppKeyword;
                // 使用 OrdinalIgnoreCase 忽略大小写进行匹配，更安全
                if (appName.IndexOf("WPS", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    targetAppKeyword = "WPS";
                }
                else if (appName.IndexOf("PowerPoint", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    targetAppKeyword = "PowerPoint";
                }
                else
                {
                    // 既不是 WPS 也不是 PowerPoint，视为不支持
                    return IntPtr.Zero;
                }

                // 从路径中安全提取文件名（包含扩展名），如 "myppt.pptx"
                string targetFileName = Path.GetFileName(presFullName);
                if (string.IsNullOrWhiteSpace(targetFileName))
                {
                    return IntPtr.Zero;
                }

                // -------------------------------------------------
                // 步骤 C: 枚举窗口并查找匹配项
                // -------------------------------------------------
                // 使用 List 暂存所有符合条件的句柄，用于后续判断是否唯一
                List<IntPtr> candidates = new List<IntPtr>();

                // 调用 EnumWindows，使用 Lambda 表达式直接嵌入回调逻辑
                EnumWindows((hWnd, lParam) =>
                {
                    try
                    {
                        // [安全过滤] 1. 忽略不可见窗口 (避免匹配到后台挂起的进程或隐藏窗口)
                        if (!IsWindowVisible(hWnd)) return true;

                        // [安全获取] 2. 获取窗口标题长度
                        int length = GetWindowTextLength(hWnd);
                        if (length == 0) return true;

                        // [安全获取] 3. 获取窗口标题文本
                        StringBuilder sb = new StringBuilder(length + 1);
                        GetWindowText(hWnd, sb, sb.Capacity);
                        string title = sb.ToString();

                        if (string.IsNullOrWhiteSpace(title)) return true;

                        // [核心匹配] 4. 判断标题是否同时包含 "文件名" 和 "应用关键字"
                        bool hasFileName = title.IndexOf(targetFileName, StringComparison.OrdinalIgnoreCase) >= 0;
                        bool hasAppKey = title.IndexOf(targetAppKeyword, StringComparison.OrdinalIgnoreCase) >= 0;

                        if (hasFileName && hasAppKey)
                        {
                            candidates.Add(hWnd);
                        }

                        // 继续枚举其他窗口
                        return true;
                    }
                    catch
                    {
                        // 回调内部容错，忽略单个窗口获取信息的错误，继续枚举
                        return true;
                    }
                }, IntPtr.Zero);

                // -------------------------------------------------
                // 步骤 D: 结果判定
                // -------------------------------------------------
                // 只有当匹配到的窗口数量 唯一 (Count == 1) 时才返回句柄
                // 0 个表示没找到，>1 个表示有歧义（无法确定是哪一个），均视为失败
                if (candidates.Count == 1)
                {
                    Console.WriteLine($"Got Hwnd GetPptHwndWin32 {candidates[0]}");
                    return candidates[0];
                }

                return IntPtr.Zero;
            }
            catch
            {
                // 发生任何不可预知的异常（如Path解析错误等），返回安全值
                return IntPtr.Zero;
            }
        }

        private int GetCurrentSlideIndex(dynamic slideShowWindow)
        {
            dynamic view = null;
            dynamic slide = null;

            try
            {
                view = slideShowWindow.View;
                slide = view.Slide;
                return (int)slide.SlideIndex;
            }
            finally
            {
                SafeRelease(slide);
                SafeRelease(view);

                slide = null;
                view = null;
            }
        }
        private int GetTotalSlideIndex(dynamic presentation)
        {
            dynamic slides = null;
            try
            {
                slides = presentation.Slides;
                return (int)slides.Count;
            }
            finally
            {
                SafeRelease(slides);
                slides = null;
            }
        }
        private int GetSlideShowWindowsCount(dynamic application)
        {
            dynamic slideShowWindows = null;

            try
            {
                slideShowWindows = application.SlideShowWindows;
                return (int)slideShowWindows.Count;
            }
            finally
            {
                SafeRelease(slideShowWindows);
                slideShowWindows = null;
            }
        }

        // 操控函数
        public void NextSlideShow(bool check)
        {
            // 如果为末页情况，则需要传入 check = true 才能继续下一页
            // TODO：还需要考虑没有结束放映页的情况
            // TODO：对于没有结束放映页的 PPT，我们还没有办法判定并拦截（其实是不好写 hhh）

            bool endPage = false;
            try
            {
                var currentPage = GetCurrentSlideIndex(pptSlideShowWindow);
            }
            catch
            {
                Console.WriteLine("enter End page");
                endPage = true;
            }

            try
            {
                if (endPage == true && check != true)
                {
                    return;
                }

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
                            currentPageTemp = GetCurrentSlideIndex(pptSlideShowWindow);
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
        public void PreviousSlideShow()
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
        private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();
        [DllImport("ole32.dll")]
        private static extern int GetRunningObjectTable(int reserved, out IRunningObjectTable prot);
        [DllImport("ole32.dll")]
        private static extern int CreateBindCtx(int reserved, out IBindCtx ppbc);
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