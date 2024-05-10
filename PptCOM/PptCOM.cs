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
using System.Threading;
using System.Runtime.InteropServices;

using Microsoft.Office.Core;
using Microsoft.Office.Interop.PowerPoint;
using System.Runtime.CompilerServices;

namespace PptCOM
{
    [ComVisible(true)]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("65F6E9C1-63EC-4003-B89F-8F425A3C2FEA")]
    public interface IPptCOMServer
    {
        string LinkTest();

        unsafe bool Initialization(int* TotalPage, int* CurrentPage);

        unsafe int IsPptOpen();

        //
        string slideNameIndex();

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
        private Microsoft.Office.Interop.PowerPoint.Application pptApp;

        private Microsoft.Office.Interop.PowerPoint.Presentation pptActDoc;
        private Microsoft.Office.Interop.PowerPoint.SlideShowWindow pptActWindow;

        private unsafe int* pptTotalPage;
        private unsafe int* pptCurrentPage;

        public string LinkTest()
        {
            return "C# COM接口 连接成功，版本 20240508.01";
        }

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

        // 事件查询函数
        private unsafe void SlideShowChange(Microsoft.Office.Interop.PowerPoint.SlideShowWindow Wn)
        {
            *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;
        }

        private unsafe void SlideShowBegin(Microsoft.Office.Interop.PowerPoint.SlideShowWindow Wn)
        {
            pptActWindow = Wn;

            // 获取页数
            *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;
            *pptTotalPage = pptActDoc.Slides.Count;
        }

        private unsafe void SlideShowShowEnd(Microsoft.Office.Interop.PowerPoint.Presentation Wn)
        {
            *pptCurrentPage = -1;
            *pptTotalPage = -1;
        }

        // 判断是否有 Ppt 文件被打开（并注册事件）
        public unsafe int IsPptOpen()
        {
            int ret = 0;

            try
            {
                // 获取幻灯片放映文档集合
                pptApp = (Microsoft.Office.Interop.PowerPoint.Application)Marshal.GetActiveObject("PowerPoint.Application");

                Microsoft.Office.Interop.PowerPoint.Presentations presentations = pptApp.Presentations;
                ret = presentations.Count;

                if (ret > 0)
                {
                    pptActDoc = pptApp.ActivePresentation;

                    try
                    {
                        pptActWindow = pptActDoc.SlideShowWindow;

                        *pptCurrentPage = pptActWindow.View.Slide.SlideIndex;
                        *pptTotalPage = pptActDoc.Slides.Count;
                    }
                    catch
                    {
                        *pptCurrentPage = -1;
                        *pptTotalPage = -1;
                    }

                    // 绑定事件
                    pptApp.SlideShowNextSlide += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                    pptApp.SlideShowBegin += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                    pptApp.SlideShowEnd += new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);

                    while (pptActDoc == pptApp.ActivePresentation) Thread.Sleep(500);

                    // 解绑事件
                    pptApp.SlideShowNextSlide -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowNextSlideEventHandler(SlideShowChange);
                    pptApp.SlideShowBegin -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowBeginEventHandler(SlideShowBegin);
                    pptApp.SlideShowEnd -= new Microsoft.Office.Interop.PowerPoint.EApplication_SlideShowEndEventHandler(SlideShowShowEnd);
                }
            }
            catch
            {
            }

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
        public int NextSlideShow(int check)
        {
            try
            {
                int temp_SlideIndex = pptActWindow.View.Slide.SlideIndex;
                if (temp_SlideIndex != check && check != -1) return pptActWindow.View.Slide.SlideIndex;

                // 下一页
                pptActWindow.View.Next();
                // 获取当前播放的幻灯片页索引
                return pptActWindow.View.Slide.SlideIndex;
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
                pptActWindow.View.Previous();
                // 获取当前播放的幻灯片页索引
                return pptActWindow.View.Slide.SlideIndex;
            }
            catch
            {
            }
            return -1;
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
    }
}