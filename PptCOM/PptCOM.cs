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
// 确认 NuGet 包 Costura.Fody 版本为 5.2.0
//
// 删除 FodyWeavers.xsd
// 修改 PptCOM 项目目录下的 FodyWeavers.xml，修改内容为
/*

<?xml version="1.0" encoding="utf-8" ?>
<Weavers>
  <Costura />
</Weavers>

*/
//
//
// 确认 PptCOM.manifest 中的 runtimeVersion 是你设置的版本全称（C:\Windows\Microsoft.NET\Framework），如 4.0.30319, 3.5
// 生成 -> 清理 PptCOM，然后点击重新生成解决方案
//
// 其余疑问请咨询作者 QQ2685549821
/////////////////////////////////////////////////////////////////////////////

using System;
using System.Runtime.InteropServices;

using Microsoft.Office.Core;
using Microsoft.Office.Interop.PowerPoint;

namespace PptCOM
{
    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("65F6E9C1-63EC-4003-B89F-8F425A3C2FEA")]
    public interface IPptCOMServer
    {
        string LinkTest();

        string IsPptDependencyLoaded();

        int GetSlideShowViewAdvanceMode();

        int SetSlideShowViewAdvanceMode(int AdvanceMode);

        string slideNameIndex();

        int currentSlideIndex();

        int totalSlideIndex();

        //string totalSlideIndex();

        int NextSlideShow(int check);

        int PreviousSlideShow();

        IntPtr GetPptHwnd();

        void EndSlideShow();
    }

    [ComVisible(true)]
    [ClassInterface(ClassInterfaceType.None)]
    [Guid("C44270BE-9A52-400F-AD7C-ED42050A77D8")]
    public class PptCOMServer : IPptCOMServer
    {
        public static Microsoft.Office.Interop.PowerPoint.Application pptApp;
        public static Microsoft.Office.Interop.PowerPoint.Presentation pptDoc;
        public static Microsoft.Office.Interop.PowerPoint.SlideShowWindow pptWindow;

        public PptCOMServer()
        {
        }

        public string LinkTest()
        {
            return "C# COM接口 连接成功，版本 20240308.01";
        }

        public string IsPptDependencyLoaded()
        {
            try
            {
                pptApp = (Microsoft.Office.Interop.PowerPoint.Application)Marshal.GetActiveObject("PowerPoint.Application");
                return "组件正常";
            }
            catch (Exception ex)
            {
                return ex.Message;
            }
        }

        public string slideNameIndex()
        {
            string slidesName = "";

            try
            {
                // 获取正在播放的PPT应用程序对象
                pptApp = (Microsoft.Office.Interop.PowerPoint.Application)Marshal.GetActiveObject("PowerPoint.Application");
                // 获取当前播放的PPT文档对象
                pptDoc = pptApp.ActivePresentation;

                // 获取正在播放的PPT的名称
                slidesName += pptDoc.FullName + "\n";
                slidesName += pptApp.Caption;
            }
            catch
            {
                // 获取PPT信息失败
            }

            return slidesName;
        }

        public int GetSlideShowViewAdvanceMode()
        {
            int AdvanceMode = -1;

            try
            {
                if (pptDoc.SlideShowSettings.AdvanceMode == PpSlideShowAdvanceMode.ppSlideShowUseSlideTimings) AdvanceMode = 1;
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
                if (AdvanceMode == 1) pptDoc.SlideShowSettings.AdvanceMode = PpSlideShowAdvanceMode.ppSlideShowUseSlideTimings;
            }
            catch
            {
            }

            return AdvanceMode;
        }

        public int currentSlideIndex()
        {
            int currentSlides = -1;

            try
            {
                // 获取当前播放的幻灯片页索引
                currentSlides = pptWindow.View.Slide.SlideIndex;
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
            //string temp;

            try
            {
                // 获取正在播放的PPT应用程序对象
                pptApp = (Microsoft.Office.Interop.PowerPoint.Application)Marshal.GetActiveObject("PowerPoint.Application");
                // 获取当前播放的PPT文档对象
                pptDoc = pptApp.ActivePresentation;
                // 获取当前播放的PPT幻灯片窗口对象（保证当前处于放映状态）
                pptWindow = pptDoc.SlideShowWindow;

                // 获取当前播放的幻灯片总页数
                totalSlides = pptDoc.Slides.Count;
            }
            catch// (Exception ex)
            {
                //return ex.Message;
            }

            //return "yes";
            return totalSlides;
        }

        public int NextSlideShow(int check)
        {
            try
            {
                int temp_SlideIndex = pptWindow.View.Slide.SlideIndex;
                if (temp_SlideIndex != check && check != -1) return pptWindow.View.Slide.SlideIndex;

                // 下一页
                pptWindow.View.Next();
                // 获取当前播放的幻灯片页索引
                return pptWindow.View.Slide.SlideIndex;
            }
            catch
            {
            }
            return -1;
        }

        public int PreviousSlideShow()
        {
            try
            {   // 上一页
                pptWindow.View.Previous();
                // 获取当前播放的幻灯片页索引
                return pptWindow.View.Slide.SlideIndex;
            }
            catch
            {
            }
            return -1;
        }

        public IntPtr GetPptHwnd()
        {
            IntPtr hWnd = IntPtr.Zero;
            try
            {
                // 获取正在播放的PPT应用程序对象
                pptApp = (Microsoft.Office.Interop.PowerPoint.Application)Marshal.GetActiveObject("PowerPoint.Application");
                // 获取当前播放的PPT文档对象
                pptDoc = pptApp.ActivePresentation;
                // 获取当前播放的PPT幻灯片窗口对象
                pptWindow = pptDoc.SlideShowWindow;

                // 获取PPT窗口句柄
                hWnd = new IntPtr(pptWindow.HWND);
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
                // 结束放映
                pptWindow.View.Exit();
            }
            catch
            {
            }
        }
    }
}