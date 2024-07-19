<div align="center">

新版 Readme 尚未完善 访问[旧版 Readme 页面](https://github.com/Alan-CRL/IDT/blob/1d63b4ba18e01f7ac45abb0e470d2748380b4407/README.md)  

您当前看到的是为了临时停更前构建的稳定版本独立分支，如果需要访问历史 commits 请转到 [dev分支](https://github.com/Alan-CRL/IDT/tree/dev)。  
**非常抱歉的通知您，由于开发者本人原因，即日起至 2024.11 智绘教将临时停止更新**

[![LOGO](GithubRes/logo.png?raw=true "LOGO")](# "LOGO")

# 智绘教Inkeys
**简体中文** | [English](README_EN.md)  

[官网下载](https://www.inkeys.top/col.jsp?id=106) | **[官方网站](https://www.inkeys.top)** | 常见问题

[![交流群](https://img.shields.io/badge/-%E4%BA%A4%E6%B5%81%E7%BE%A4%20618720802-blue?style=flat&logo=TencentQQ)](https://qm.qq.com/cgi-bin/qm/qr?k=9V2l83dc0yP4UYeDF-NkTX0o7_TcYqlh&jump_from=webapi&authKey=LsLLUhb1KSzHYbc8k5nCQDqTtRcRUCEE3j+DdR9IgHaF/7JF7LLpY191hsiYEBz6)  ![GitHub issues](https://img.shields.io/github/issues/Alan-CRL/IDT?logo=github&color=green)  ![GitHub stars](https://img.shields.io/github/stars/Alan-CRL/IDT)

Windows 屏幕批注工具，拥有高效批注和丰富功能，  
让屏幕演示变得简单，让教学授课变得高效，适用于触摸设备和PC端。

原名 `Intelligent-Drawing-Teaching`（简称 IDT）

![](GithubRes/cover1.png?raw=true#gh-dark-mode-only)
![](GithubRes/cover2.png?raw=true#gh-light-mode-only)

</div>

## 集锦
**[Bilibili 上的 24H2 版本介绍视频](https://www.bilibili.com/video/BV1Tz421z72e/)**  
[Bilibili 上的 24H1 版本介绍视频（较旧）](https://www.bilibili.com/video/BV1vJ4m147rN/)  
[Codebus 上的介绍推文（较旧）](https://codebus.cn/alancrl/intelligent-painting-teaching)  

## 好消息 !!!
智绘教 UI3 已经开始制作，这将是一个史诗级更新。  
针对 大屏触摸环境 和 PC鼠标环境 等，推出多种 UI 样式。  
让智绘教在各个设备上，都更易用，更好用！

## 为什么要使用智绘教？
正在编写中，敬请期待……

## 下载
[官网下载页](https://www.inkeys.top/col.jsp?id=106) | [免登录云盘](https://www.123pan.com/s/duk9-n4dAd.html) | [Github Release 附件](https://github.com/Alan-CRL/IDT/releases)  

#### 要求
最低支持 Windows 7 Service Pack 1，支持 32 / 64 位系统。  
对于 win7 用户，需要 `d3dcompiler_47.dll`，可以通过 KB2670838 更新获取或使用 DXR 修复工具。

#### 提示
获取 公测版本 及 开发版本 请加 [官方用户QQ群](https://qm.qq.com/cgi-bin/qm/qr?k=9V2l83dc0yP4UYeDF-NkTX0o7_TcYqlh&jump_from=webapi&authKey=LsLLUhb1KSzHYbc8k5nCQDqTtRcRUCEE3j+DdR9IgHaF/7JF7LLpY191hsiYEBz6) 下载。

## 展望
`全新外观` `笔锋压感` `墨迹识别` `书写平滑` `丰富插件` ……  
你能猜中哪一个会率先发布吗？

## 反馈
问题报告与功能建议：[点击此处](https://www.wjx.cn/vm/mqNTTRL.aspx#)  
[官方用户QQ群](https://qm.qq.com/cgi-bin/qm/qr?k=9V2l83dc0yP4UYeDF-NkTX0o7_TcYqlh&jump_from=webapi&authKey=LsLLUhb1KSzHYbc8k5nCQDqTtRcRUCEE3j+DdR9IgHaF/7JF7LLpY191hsiYEBz6)：`618720802`  
作者QQ：`2685549821`  
作者邮箱：`alan-crl@foxmail.com`

---

## 编译说明

### 编译环境
- Visual Studio 2022 (MSVC v143 编译器)
> 勾选 `.NET 桌面开发` `使用 C++ 的桌面开发` `Windows 应用程序开发` 等工作负荷
- C++ 20
- Windows 11 SDK 10.26100
- [EasyX](https://easyx.cn/download/EasyX_2023%E5%A4%A7%E6%9A%91%E7%89%88.exe) (EasyX_2023大暑版)
- .NET Framework 4.0 SDK

### 分支说明
- `main`：主仓库，存储近期较为稳定的可构建的程序源码
- `dev`：分支仓库，每日及时更新，存储自动保存的源码，可能无法构建

### 编译步骤
智绘教采用完全开源方式，所有源码和资源全部开源  
1. 下载 `main` 仓库  
2. 使用 Visual Studio 2022 打开 `智绘教.sln`  
3. 选择 `智绘教` 项目
4. 切换为 `Release | Win32` 构建配置
5. 点击构建即可生成程序

## 项目引用
[Dear Imgui](https://github.com/ocornut/imgui)  
[DesktopDrawpadBlocker](https://github.com/Alan-CRL/DesktopDrawpadBlocker)  
[Hashlib++](https://github.com/aksalj/hashlibpp)  
[HiEasyX](https://github.com/zouhuidong/HiEasyX)  
[JsonCpp](https://github.com/open-source-parsers/jsoncpp)  
[Stb](https://github.com/nothings/stb)  
[WinToast](https://github.com/mohabouje/WinToast)  
[Zip Utils](https://www.codeproject.com/Articles/7530/Zip-Utils-Clean-Elegant-Simple-Cplusplus-Win)  