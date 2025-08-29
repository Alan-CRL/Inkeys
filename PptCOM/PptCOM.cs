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

using System;
using System.Threading;
using System.Runtime.InteropServices;
using System.Diagnostics;

using System.Windows.Forms;

using Microsoft.Office.Core;
using Microsoft.Office.Interop.PowerPoint;

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
            string ret = "20250729a";

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
            }

            // 获取页数
            try
            {
                *pptTotalPage = pptActDoc.Slides.Count;
                *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;
            }
            catch { }
        }

        private unsafe void SlideShowShowEnd(Microsoft.Office.Interop.PowerPoint.Presentation Wn)
        {
            updateTime = DateTime.Now;

            *pptCurrentPage = -1;
            *pptTotalPage = -1;
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

        // 判断是否有 Ppt 文件被打开（并注册事件）
        public unsafe int IsPptOpen()
        {
            int ret = 0;
            bindingEvents = false;
            //hasWpsProcessID = false;

            // 通用尝试，获取 Active 的 Application 并检测是否正确
            try
            {
                // 获取活动的 Application 示例
                pptApp = (Microsoft.Office.Interop.PowerPoint.Application)Marshal.GetActiveObject("PowerPoint.Application");

                // 获取 PPT 文档实例个数（如果为 0 则是没有打开文件的 Application 实例，或是游离状态的 WPP）
                ret = pptApp.Presentations.Count;
            }
            catch { }

            // 锁定 Application 并执行后续操作
            if (ret > 0)
            {
                try
                {
                    pptActDoc = pptApp.ActivePresentation;
                    updateTime = DateTime.Now;

                    int tempTotalPage;
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

                    try
                    {
                        while (true)
                        {
                            if (pptActDoc != pptApp.ActivePresentation) break;
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

                            // 计时轮询（超过3秒不刷新就轮询一次）
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

                            Thread.Sleep(500);
                        }
                    }
                    catch { }

                    // 解绑事件
                    if (bindingEvents)
                    {
                        pptApp.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                        pptApp.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                        pptApp.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                        pptApp.PresentationBeforeClose -= new Microsoft.Office.Interop.PowerPoint.EApplication_PresentationBeforeCloseEventHandler(PresentationBeforeClose);
                        bindingEvents = false;
                    }
                    // 关闭未正确关闭的 WPP 进程
                    //if (hasWpsProcessID == true && !wpsProcess.HasExited)
                    //{
                    //    wpsProcess.Kill();
                    //    hasWpsProcessID = false;
                    //}

                    // 释放 COM
                    if (pptActWindow != null)
                    {
                        Marshal.ReleaseComObject(pptActWindow);
                        pptActWindow = null;
                    }
                    if (pptActDoc != null)
                    {
                        Marshal.ReleaseComObject(pptActDoc);
                        pptActDoc = null;
                    }
                    if (pptApp != null)
                    {
                        Marshal.ReleaseComObject(pptApp);
                        pptApp = null;
                    }
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                }
                catch { }
            }
            // else：则找不到 Application 示例，或 Application 示例均不符合条件。

            *pptCurrentPage = -1; *pptTotalPage = -1;
            return ret;
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
                int temp_SlideIndex = pptActWindow.View.Slide.SlideIndex;
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