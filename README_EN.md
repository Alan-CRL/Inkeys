<div align="center">

We are very sorry to inform you that the program itself did not produce an international language.  
**We expect to complete this task within 2 months.**  
  
  
[![LOGO](GithubRes/logo.png?raw=true "LOGO")](# "LOGO")

# 智绘教Inkeys
[简体中文](README.md) | **English**  

[Download from Github Release Assets](https://github.com/Alan-CRL/IDT/releases) | **[Official Website(Chinese)](https://www.inkeys.top)** | FAQ

[![Communication Group](https://img.shields.io/badge/-QQ%20Group%20618720802-blue?style=flat&logo=TencentQQ)](https://qm.qq.com/cgi-bin/qm/qr?k=9V2l83dc0yP4UYeDF-NkTX0o7_TcYqlh&jump_from=webapi&authKey=LsLLUhb1KSzHYbc8k5nCQDqTtRcRUCEE3j+DdR9IgHaF/7JF7LLpY191hsiYEBz6)  ![GitHub issues](https://img.shields.io/github/issues/Alan-CRL/IDT?logo=github&color=green)  ![GitHub stars](https://img.shields.io/github/stars/Alan-CRL/IDT)

Windows screen annotation tool with efficient annotation and rich features,  
Make screen demonstrations simpler, teaching more efficient,  
suitable for touch devices and computers.

Old name `Intelligent-Drawing-Teaching`（referred to as `IDT`）

![](GithubRes/cover1.png?raw=true#gh-dark-mode-only)
![](GithubRes/cover2.png?raw=true#gh-light-mode-only)

</div>

## Collection
**[Introduction video of 24H2 version on _bilibili.com_ (Chinese)](https://www.bilibili.com/video/BV1Tz421z72e/)**  
[Introduction video of 24H1 version on _bilibili.com_ (Chinese)(older)](https://www.bilibili.com/video/BV1vJ4m147rN/)  
[Introduction blog on _codebus.cn_ (Chinese)(VPN required)(older)](https://codebus.cn/alancrl/intelligent-painting-teaching)  

## News !!!
We will create new UI interface for Inkeys, which will be an epic change.  
We have launched various UI style for large screen touch devices and laptops.  
Make Inkeys easier and more user-friendly on various devices!  

## Why use Inkeys?
Under development, please stay tuned ~

## Download
[Github Release Assets](https://github.com/Alan-CRL/IDT/releases) | [Official Website Download(Chinese)](https://www.inkeys.top/col.jsp?id=106)   

#### System Requirements
The minimum support is `Windows 7 Service Pack 1` and supports `32/64 bit` windows.  
For Windows 7 users, `d3dcompiler_47.dll` is required, which can be obtained through `KB2670838` update.  

## Future Features
`New style` `Pen tip and pressure sensitivity` `Ink recognition` `Smooth writing ink` `Various plugins` and so on.  
Can you guess which one will be released first?  

## Feedback
Author email: `alan-crl@foxmail.com`  
Author QQ: `2685549821`  

---

## Compile Instructions

### Compile Preparation
- Visual Studio 2022 (MSVC v143 Compiler)
> Check `.NET Desktop Development` `C++ Desktop Development` `Windows Application Development` Workload.
- C++ 20
- Windows 11 SDK 10.26100
- [EasyX](https://easyx.cn/download/EasyX_2023%E5%A4%A7%E6%9A%91%E7%89%88.exe) (EasyX_2023 Summer Release Version)
- .NET Framework 4.0 SDK
- Microsoft DirectX SDK (June 2010)

### Branch Description
- `main`: Store recently stable and buildable program source code.
- `dev`: Daily timely updates, storing automatically saved source code, may not be able to build.

### Compile Step
Inkeys adopts a completely open source approach, with all source code and resources being open source.  
1. Download `main` branch.  
2. Use Visual Studio 2022 open `智绘教.sln`.  
3. Choose `智绘教` project.
4. Choose `Release | Win32` build configuration.
5. Click on build to generate the program.

## Project Reference
[Dear Imgui](https://github.com/ocornut/imgui)  
[DesktopDrawpadBlocker](https://github.com/Alan-CRL/DesktopDrawpadBlocker)  
[Hashlib++](https://github.com/aksalj/hashlibpp)  
[HiEasyX](https://github.com/zouhuidong/HiEasyX)  
[JsonCpp](https://github.com/open-source-parsers/jsoncpp)  
[Stb](https://github.com/nothings/stb)  
[WinToast](https://github.com/mohabouje/WinToast)  
[Zip Utils](https://www.codeproject.com/Articles/7530/Zip-Utils-Clean-Elegant-Simple-Cplusplus-Win)  