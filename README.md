# Intelligent-Drawing-Teaching
智绘教，适用于 Windows 桌面的悬浮窗画笔程序，高效绘制和丰富功能，只为尽可能地提供课堂教学效率。适用于触摸屏设备和PC端。

[CodeBus 上的智绘教介绍](https://codebus.cn/alancrl/intelligent-painting-teaching)

## 项目介绍
智绘教项目创立于 2023 年 2 月，由于班上一体机自带的画板程序并不好用，于是智绘教应运而生。智绘教通过长期教师的使用反馈，反复打磨，并高速发展。是一款运行于 Windows 平台的软件，无需安装即可使用。其使用 HiEasyX(EasyX 拓展库) 和 GDI+ 作为图形库，高质量抗锯齿与超低延迟，且支持多指绘制……

![程序主界面展示](https://codebus.cn/f/a/0/0/677/1.png)  
[程序主界面展示](https://codebus.cn/f/a/0/0/677/1.png)  

### 未来功能（按照实现顺序排序）
- 实时手抖修正
- UI 3.0（包括全新操作逻辑，全新界面以及更多的自定义功能，界面缩放与自定义按键）
- 皮肤模块
- 全屏白、黑板
- 激光笔
- 历史画板恢复
- 图层
- “贴图镜”

---

除了书写等基础功能外，程序还包含许多特色功能。（动图如果加载失败，可以点击图片下方链接）  

![动态画板背景、窗口定格与穿透](https://codebus.cn/f/a/0/0/677/2.gif)  
[动态画板背景、窗口定格与穿透](https://codebus.cn/f/a/0/0/677/2.gif)  

![智能绘图模块（智能直线绘制/直线吸附/矩形吸附/平滑笔迹/智能粗细橡皮擦）](https://codebus.cn/f/a/0/0/677/3.gif)  
[智能绘图模块（智能直线绘制/直线吸附/矩形吸附/平滑笔迹/智能粗细橡皮擦）](https://codebus.cn/f/a/0/0/677/3.gif)  

![炫彩全 RGBA 绘图，1-500 粗细调节](https://codebus.cn/f/a/0/0/677/4.gif)  
[炫彩全 RGBA 绘图，1-500 粗细调节](https://codebus.cn/f/a/0/0/677/4.gif)  

![全新 UI 与可打断动画（0.3 倍速）](https://codebus.cn/f/a/0/0/677/5.gif)  
[全新 UI 与可打断动画（0.3 倍速）](https://codebus.cn/f/a/0/0/677/5.gif)  

![PPT 联动（翻页/笔迹保留）](https://codebus.cn/f/a/0/0/677/6.gif)  
[PPT 联动（翻页/笔迹保留）](https://codebus.cn/f/a/0/0/677/6.gif)  

还有许多功能比如标准笔迹/荧光笔迹，撤回和历史画板恢复，画板绘制内容自动保存本地，PPT 插件/随机点名插件……

程序支持多指绘制以及模拟笔锋(均未完善)，可根据电脑环境自动选择 RTS 触控库 或 鼠标位置 作为绘制输入。

---

智绘教，基于 C++ 和 C# 开发打造的开源项目，其适用于 Windows7 及以上平台。

## 使用方法

点击悬浮窗图标展开主界面，再点击画笔即可开始书写。再次点击画笔即可展开画笔选项。点击图标则会收回悬浮窗。

点击选择则消除笔迹且可操控桌面，点击橡皮即可擦除笔迹（不会破坏背景）。

程序开始书写后，其背景就是你的电脑桌面（实时动态变化）。窗口定格：使背景静止。窗口穿透：保持笔迹展示，同时可以操控桌面。

不小心消除笔迹？点击图标下方恢复即可恢复笔迹。不小心写错了？点击撤回即可撤回。

## 下载体验

夸克网盘分流：[https://pan.quark.cn/s/e6adc1b881dc](https://pan.quark.cn/s/e6adc1b881dc)  
转到 [GitHub](https://github.com/Alan-CRL/Intelligent-Drawing-Teaching/releases) 发布页面下载最新版本按照提示运行即可或可以联系作者获取。软件经过反复测试打磨，可以直接安装在班级一体机上使用。

运行要求：Windows 7 及以上 + .NET framework 4.0 及以上，支持 PC 与 触摸一体机 使用。  
编译环境：VS2022 + MSVC143 + C++17 + EasyX20230723 + .NET framework 4.0 开发者工具包。  
.NET 功能只用于 PPT 联动，后续 PPT联动模块将使用 C++ COM 接口替代 C# COM 接口

## 技术分享

查看我的开源代码或咨询本人，你可以了解并学习以下许多的难题的解决方案。

- 了解学习使用 HiEasyX(EasyX 拓展库) 实现多窗口绘制与多窗口消息处理。
- 了解学习 UI 动画的运作方式，以及打断动画的实现。
- 了解学习使用 GDI+/D2D(未完善) 实现抗锯齿绘图并根据书写速度模拟笔锋、以及显卡加速运用。
- 了解学习使用 RTS 触控库，并支持多指绘制。
- 了解学习使用窗口样式，实现窗口穿透。使用 MagnificationAPI 实现穿透窗口截图，并运用于窗口定格。
- 了解学习如何将程序所需的图片字体资源等，都打包到程序资源中，使程序可以单文件运行。
- 了解学习使用 JsonCpp、ZipUtils、ICU、相关 API 实现软件自动更新以及崩溃反馈/JSON 文件读取/ZIP 文件解压/用正确编码读取文件。
- 了解学习使用 RegOpenKeyEx 调整注册表，实现开机自动启动。
- 了解学习我自制的直线拟合函数，运用数学知识判断该模拟是否符合直线特征。
- 了解学习在 C++ 中通过免注册 COM 组件调用 C# 类库，并获取 PPT 放映状态。
- 了解学习如何扫描窗口类名和获取大小，并实现识别其他窗口。
- 了解学习使用 StbImage 将 IMAGE 图像无损地保存到本地 png 中（带有透明通道）。
- 了解学习字符转换并使用 ICU 和相关 API 实现 utf8、utf16/unicode、gbk、urlencode 的互相转换。
- 了解学习 STL 线程锁的实现，完成多线程下的读锁与写锁。

……

还许多功能和内容我就不一一阐述了。有疑问欢迎联系我~ QQ2685549821

## 项目引用
[HiEasyX](https://github.com/zouhuidong/HiEasyX)  
[JsonCPP](https://github.com/open-source-parsers/jsoncpp)  
[Stb_image](https://github.com/nothings/stb)  
[Hashlib++](https://github.com/aksalj/hashlibpp)  
[Zip Utils](https://www.codeproject.com/Articles/7530/Zip-Utils-Clean-Elegant-Simple-Cplusplus-Win)   
[International Components for Unicode](https://github.com/unicode-org/icu)  
