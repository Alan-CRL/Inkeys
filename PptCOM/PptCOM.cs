using System;
using System.Runtime.InteropServices;

using Microsoft.Office.Core;
using PPT = Microsoft.Office.Interop.PowerPoint;

namespace PptCOM
{
    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("65F6E9C1-63EC-4003-B89F-8F425A3C2FEA")]
    public interface IPptCOMServer
    {
        string LinkTest();

        string IsPptDependencyLoaded();

        int currentSlideIndex();

        int totalSlideIndex();

        IntPtr GetPptHwnd();

        void EndSlideShow();
    }

    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    [Guid("C44270BE-9A52-400F-AD7C-ED42050A77D8")]
    public class PptCOMServer : IPptCOMServer
    {
        public PptCOMServer()
        {
        }

        public string LinkTest()
        {
            return "C# COM接口 连接成功，版本 20231012.01";
        }

        public string IsPptDependencyLoaded()
        {
            try
            {
                PPT.Application pptApp = new PPT.Application();
                return "组件正常";
            }
            catch (Exception ex)
            {
                return ex.Message;
            }
        }

        public int currentSlideIndex()
        {
            int currentSlides = -1;

            try
            {
                // 获取正在播放的PPT应用程序对象
                PPT.Application pptApp = new PPT.Application();
                // 获取当前播放的PPT文档对象
                PPT.Presentation pptDoc = pptApp.ActivePresentation;
                // 获取当前播放的PPT幻灯片窗口对象
                PPT.SlideShowWindow pptWindow = pptDoc.SlideShowWindow;
                // 获取当前播放的幻灯片页对象
                PPT.Slide pptSlide = pptWindow.View.Slide;
                // 获取当前播放的幻灯片页索引
                currentSlides = pptSlide.SlideIndex;

                // 释放资源
                pptSlide = null;
                pptWindow = null;
                pptDoc = null;
                pptApp = null;
            }
            catch
            {
                // 获取PPT信息失败
            }

            return currentSlides;
        }

        public int totalSlideIndex()
        {
            int totalSlides = -1;

            try
            {
                // 获取正在播放的PPT应用程序对象
                PPT.Application pptApp = new PPT.Application();
                // 获取当前播放的PPT文档对象
                PPT.Presentation pptDoc = pptApp.ActivePresentation;
                // 获取当前播放的PPT幻灯片窗口对象
                PPT.SlideShowWindow pptWindow = pptDoc.SlideShowWindow;
                // 获取当前播放的幻灯片总页数
                totalSlides = pptDoc.Slides.Count;

                // 释放资源
                pptWindow = null;
                pptDoc = null;
                pptApp = null;
            }
            catch
            {
                // 获取PPT信息失败
            }

            return totalSlides;
        }

        public IntPtr GetPptHwnd()
        {
            IntPtr hWnd = IntPtr.Zero;
            try
            {
                // 获取正在播放的PPT应用程序对象
                PPT.Application pptApp = new PPT.Application();
                // 获取当前播放的PPT文档对象
                PPT.Presentation pptDoc = pptApp.ActivePresentation;
                // 获取当前播放的PPT幻灯片窗口对象
                PPT.SlideShowWindow pptWindow = pptDoc.SlideShowWindow;

                // 获取PPT窗口句柄
                hWnd = new IntPtr(pptWindow.HWND);

                // 释放资源
                pptWindow = null;
                pptDoc = null;
                pptApp = null;
            }
            catch
            {
            }

            return hWnd;
        }

        public void EndSlideShow()
        {
            try
            {
                // 获取正在播放的PPT应用程序对象
                PPT.Application pptApp = new PPT.Application();
                // 获取当前播放的PPT文档对象
                PPT.Presentation pptDoc = pptApp.ActivePresentation;
                // 获取当前播放的PPT幻灯片窗口对象
                PPT.SlideShowWindow pptWindow = pptDoc.SlideShowWindow;

                // 结束放映
                pptWindow.View.Exit();

                // 释放资源
                pptWindow = null;
                pptDoc = null;
                pptApp = null;
            }
            catch
            {
            }
        }
    }
}