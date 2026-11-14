**调查范围：dev `571b49ec`，并核对全部预导出历史及三个归档分支。未修改文件、运行 git、启动子进程或 GUI。**

表内缩写：

- `A`＝`Inkeys/additional/`；`F`＝`Inkeys/src/ttf/`；`L`＝`ThirdpartyLicenses/`。
- `S`＝`VcpkgInstalled/Arm64/arm64-windows-static-v143/share/`；`H`＝`VcpkgInstalled/Arm64/arm64-windows/share/`。
- `E`＝提供的 `third-party/` 证据目录；`P`＝`E/all-paths-depth3.tsv`；`D`＝`E/deletions.txt`；`V`＝`E/vcpkg-json-history.txt`。
- 归档 `1 / 2 / M` 分别表示 `inkeys1-final / inkeys2-final / main`；“未证”不等于不存在。
- “首见”指全分支最早路径记录，不能自动解释为合入 dev 的时间。缺失许可证写“未取得”，与“许可证文本无版权行”严格区分。`NOASSERTION` 表示证据不足，不能确定 SPDX。

**1. 总表**

| 组件 | 上游 URL | 许可证（SPDX） | 准确版权声明（逐字摘自许可证文件） | dev 使用方式 | 历史 | 证据 |
|---|---|---|---|---|---|---|
| Abseil | [abseil-cpp](https://github.com/abseil/abseil-cpp) | Apache-2.0 | 许可证文本无版权行；`S/abseil/copyright` 的附录只有版权模板占位 | vcpkg（abseil） | 首见 `7936ee38 2024-05-04`；旧源码删于 `a37d1d16 2024-10-02`；主程序重新引入 `fc9a8b86 2026-08-16`；`c34d3a56 2026-10-12` 改用 vcpkg；归档 1 | P、D、V；`A/ink_stroke_modeler/ink_stroke_modeler.props` |
| Brotli | [google/brotli](https://github.com/google/brotli) | MIT | `Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors.`（`S/brotli/copyright`） | vcpkg（brotli, 传递依赖 of cpp-httplib） | 当前安装已证；首次安装提交未导出；归档未证 | `VcpkgInstalled/Arm64/vcpkg/status` 的 `cpp-httplib[brotli]` |
| concurrentqueue | [concurrentqueue](https://github.com/cameron314/concurrentqueue) | BSD-2-Clause OR BSL-1.0 | `Copyright (c) 2013-2016, Cameron Desrochers.`<br>`All rights reserved.`（`S/concurrentqueue/copyright`） | vcpkg（concurrentqueue） | 首见 `4c14ead0 2025-07-22`；`64b731e6 2026-02-27` 迁入 vcpkg；归档 2/M 未见该独立文件 | P、V；`Draw3.ContactInput.cpp:20`；HiMsg 头文件 |
| cpp-httplib | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | MIT | `Copyright (c) 2017 yhirose`（`S/cpp-httplib/copyright`） | vcpkg（cpp-httplib） | 首见 `cce5540c 2024-10-05`；`64b731e6 2026-02-27` 迁入 vcpkg；归档 2/M | P、V；`Inkeys/Inkeys/Net/Net.Update.Download.cpp:6` |
| fmt | [fmtlib/fmt](https://github.com/fmtlib/fmt) | MIT | `Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors`（`S/fmt/copyright`） | vcpkg（fmt, 传递依赖 of spdlog） | 当前安装已证；首次引入待确认，旧 spdlog 内部文件未展开；归档未证 | installed `status` 的 `spdlog[fmt]` |
| FreeType | [FreeType](https://www.freetype.org/) | FTL OR GPL-2.0-or-later | `Copyright 1996-2002, 2006 by`<br>`David Turner, Robert Wilhelm, and Werner Lemberg`（`S/freetype/copyright`，FTL 部分） | vcpkg（freetype） | 内置头首见 `8e0fb28c 2024-10-08`、删于 `859d2881 2024-10-16`；`6a8fd4ea 2026-11-02` 加入当前 manifest；归档是否实际链接未证 | P、D、V；`Inkeys.vcxproj:939`；`A/imgui/misc/freetype/UPSTREAM.md` |
| JsonCpp | [jsoncpp](https://github.com/open-source-parsers/jsoncpp) | MIT | `Copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors`（`S/jsoncpp/copyright`） | vcpkg（jsoncpp） | 首见 `9f5f8cfd 2023-10-12`；`64b731e6 2026-02-27` 主程序迁入 vcpkg；归档 1/2/M；Timeout 仍内置旧源码 | P、V；主程序 `<json/json.h>`；`Timeout/.../json/` |
| libcuckoo | [efficient/libcuckoo](https://github.com/efficient/libcuckoo) | Apache-2.0 | `Copyright (C) 2013, Carnegie Mellon University and Intel Corporation`（`S/libcuckoo/copyright`、`L/libcuckoo License`） | vcpkg（libcuckoo） | 首见 `ef2b3921 2025-05-10`；`64b731e6 2026-02-27` 迁入 vcpkg；归档 2/M | P、V；`Helper.Thread.cppm:7`；HiMsg |
| LunaSVG | [sammycage/lunasvg](https://github.com/sammycage/lunasvg) | MIT | `Copyright (c) 2020-2025 Samuel Ugochukwu <sammycageagle@gmail.com>`（`S/lunasvg/copyright`） | vcpkg（lunasvg） | 首见 `4c14ead0 2025-07-22`；`64b731e6 2026-02-27` 迁入 vcpkg；归档 2/M 未见 | P、V；`Bar.UI.cpp:17` |
| magic_enum | [Neargye/magic_enum](https://github.com/Neargye/magic_enum) | MIT | `Copyright (c) 2019 - 2024 Daniil Goncharov`（`S/magic-enum/copyright`） | vcpkg（magic-enum） | 首见 `2820dff7 2026-02-23`；`64b731e6 2026-02-27` 迁入 vcpkg；归档未见 | P、V；`Helper.Enum.cppm:5` |
| msgpack-cxx | [msgpack-c](https://github.com/msgpack/msgpack-c) | BSL-1.0 | `Copyright (C) 2008-2015 FURUHASHI Sadayuki`（`S/msgpack/copyright`） | vcpkg（msgpack） | manifest 引入 `d272f888 2026-09-01`；归档未见 | V；`inkStrokeModelerTest/draw3/uink_codec*` 被主工程编译 |
| OpenSSL | [OpenSSL](https://www.openssl.org) | Apache-2.0 | 许可证文本无版权行（`S/openssl/copyright`） | vcpkg（openssl） | 内置 Win32/Win64 首见 `cce5540c 2024-10-05`，ARM64 `fec7c7de 2024-10-28`；`94b437e8 2026-02-26` 迁入 vcpkg；归档 2/M | P、V；`Net.Update.Download.cpp:3,16` 的 TLS 宏及链接指令 |
| PlutoVG | [sammycage/plutovg](https://github.com/sammycage/plutovg) | MIT | `Copyright (c) 2020-2025 Samuel Ugochukwu <sammycageagle@gmail.com>`（`S/plutovg/copyright`） | vcpkg（plutovg, 传递依赖 of lunasvg） | 当前安装及依赖关系已证；准确首次引入提交待确认；归档未证 | installed `status`；`S/plutovg/vcpkg.spdx.json` |
| spdlog | [gabime/spdlog](https://github.com/gabime/spdlog) | MIT | `Copyright (c) 2016 - present, Gabi Melman and spdlog contributors.`（`S/spdlog/copyright`） | vcpkg（spdlog） | 首见 `3e304790 2024-04-09`；`64b731e6 2026-02-27` 迁入 vcpkg；归档 1/2/M | P、V；`IdtMain.cpp:3217` |
| unordered_dense | [unordered_dense](https://github.com/martinus/unordered_dense) | MIT | `Copyright (c) 2022 Martin Leitner-Ankerl`（`S/unordered-dense/copyright`） | vcpkg（unordered-dense） | 首见 `9b2dccdc 2025-07-21`；`64b731e6 2026-02-27` 迁入 vcpkg；归档未见 | P、V；`IdtMain.h:89` |
| hashlib++ | [aksalj/hashlibpp](https://github.com/aksalj/hashlibpp) | BSD-2-Clause；另见下列算法组件 | `Copyright (c) 2007-2011 Benjamin Gr¸delbach`（`L/hashlibpp License`，原文件确为此乱码） | 源码内置（Inkeys/additional/hashlib++） | 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M | P；`Inkeys.vcxproj:927` 起；`hl_hashwrapper.h:40` 另有 `Gr&uuml;delbach` |
| RSA MD5 实现 | [RFC 1321](https://www.rfc-editor.org/rfc/rfc1321) | RSA-MD | `Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All`<br>`rights reserved.`（`A/hashlib++/hl_md5.cpp:35` 起，源码许可块） | 源码内置（Inkeys/additional/hashlib++/hl_md5.cpp） | 随 hashlib++ 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M 含该算法文件 | P；`hl_md5.cpp`；工程编译项 |
| RFC 3174 SHA-1 实现 | [RFC 3174](https://www.rfc-editor.org/rfc/rfc3174) | NOASSERTION（源码载 Internet Society 许可） | `Copyright (C) The Internet Society (2001).  All Rights Reserved.`（`A/hashlib++/hl_sha1.cpp:35`，源码许可块） | 源码内置（Inkeys/additional/hashlib++/hl_sha1.cpp） | 随 hashlib++ 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M | P；`hl_sha1.cpp`；工程编译项 |
| Aaron D. Gifford SHA-2 实现 | 上游地址未取得；来源由源码注明 | BSD-3-Clause | `Copyright (c) 2000-2001, Aaron D. Gifford`<br>`All rights reserved.`（`A/hashlib++/hl_sha256.cpp:35` 起，源码许可块） | 源码内置（Inkeys/additional/hashlib++） | 随 hashlib++ 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M | `hl_sha256.cpp`、`hl_sha2ext.h` |
| Dear ImGui（含 Win32/DX11、FreeType 适配） | [ocornut/imgui](https://github.com/ocornut/imgui) | MIT | `Copyright (c) 2014-2026 Omar Cornut`（`A/imgui/misc/freetype/LICENSE.imgui.txt:3`） | 源码内置（Inkeys/additional/imgui） | 首见 `7e357482 2024-02-17`；归档 1/2/M；当前快照 1.92.7 | P；`Inkeys.vcxproj:937` 起；ImFluent `UPSTREAM.md` |
| imgui_toggle | 独立扩展的准确上游 URL 待核 | NOASSERTION（未取得独立许可证） | 未取得许可证文件；不能套用 ImGui 的版权 | dev 已不再使用 | 首见 `7e357482 2024-02-17`；归档 1 明列文件，2/M 目录折叠；当前文件仍在，但工程及业务引用均未找到；停用提交待确认 | P；`A/imgui/imgui_toggle*`；当前工程编译项 |
| stb（image/resize/write、ImGui 的 imstb） | [nothings/stb](https://github.com/nothings/stb) | MIT OR Unlicense | `Copyright (c) 2017 Sean Barrett`（`A/stbimage/stb_image.h:7952`；其余文件亦有许可块） | 源码内置（Inkeys/additional/stbimage） | image 首见 `9f5f8cfd 2023-10-12`；imstb 首见 `7e357482 2024-02-17`；归档 1/2/M | P；`Bar.UI.cpp:695`；`A/imgui/imstb_*` |
| WinToast | [mohabouje/WinToast](https://github.com/mohabouje/WinToast) | MIT | `Copyright (C) 2016-2023 WinToast v1.3.0 - Mohammed Boujemaoui <mohabouje@gmail.com>`（`A/wintoastlib/wintoastlib.h:4`，完整许可头） | 源码内置（Inkeys/additional/wintoastlib） | 首见 `7a7e096a 2024-04-28`；归档 1/2/M | P；`Inkeys.vcxproj:945` |
| Zip Utils（Lucian Wischik 包装） | [Zip Utils](https://www.codeproject.com/Articles/7530/Zip-Utils-Clean-Elegant-Simple-Cplusplus-Win) | 所含实现见下两行；包装层独立许可未取得 | 未找到包装作者的独立版权行；源码明确署名 Lucian Wischik，不能改写为版权声明 | 源码内置（Inkeys/additional/ziputils） | 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M | P；`zip.h:5`、`unzip.h:4`；工程 `:946–947` |
| Info-ZIP（Zip Utils 内） | [Info-ZIP](http://www.info-zip.org) | Info-ZIP（本地为 1999-10-05 文本） | `Copyright (c) 1990-1999 Info-ZIP.  All rights reserved.`（`A/ziputils/zip.cpp:20`） | 源码内置（Inkeys/additional/ziputils/zip.cpp） | 随 Zip Utils 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M | `zip.cpp:6–64` |
| zlib（Zip Utils 内） | [zlib](http://www.gzip.org/zlib/) | Zlib | `Copyright (C) 1995-1998 Jean-loup Gailly and Mark Adler`（`A/ziputils/unzip.cpp:21`） | 源码内置（Inkeys/additional/ziputils/unzip.cpp） | 随 Zip Utils 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M | `unzip.cpp:18–38` |
| MiniZip / Gilles Vollant unzip 0.15 | 独立上游 URL 待核 | NOASSERTION（该片段独立许可未取得） | 未取得独立许可证；源码字符串为 ` unzip 0.15 Copyright 1998 Gilles Vollant `（`A/ziputils/unzip.cpp:2445`） | 源码内置（Inkeys/additional/ziputils/unzip.cpp） | 随 Zip Utils 首见 `9f5f8cfd 2023-10-12`；归档 1/2/M 含文件 | `unzip.cpp:2435` 起 |
| Google Ink Stroke Modeler | [ink-stroke-modeler](https://github.com/google/ink-stroke-modeler) | Apache-2.0 | `Copyright 2022 Google LLC`（`A/ink_stroke_modeler/stroke_modeler.cc` 许可头）；`Copyright 2024 Google LLC`（`numbers.h` 许可头） | 源码内置（Inkeys/additional/ink_stroke_modeler） | 首见 `7936ee38 2024-05-04`；删于 `a37d1d16 2024-10-02`；demo 重引 `5fc6cd8c 2025-11-20`；主程序 `fc9a8b86 2026-08-16`；`c34d3a56 2026-10-12` 改为源码编译；归档 1 | P、D；目录 `README.md`、共享 `.props`；旧 `.lib` 仅基线 |
| ImFluent | [lukaasm/ImFluent](https://github.com/lukaasm/ImFluent) | MIT | `Copyright (c) 2026 lukaasm`（`A/imfluent/LICENSE:3`） | 源码内置（Inkeys/additional/imfluent） | 引入 `791058df 2026-08-14`；归档未见 | P；`UPSTREAM.md`；工程 `:944` |
| HiMsg | [Alan-CRL/HiMsg](https://github.com/Alan-CRL/HiMsg.git) | MIT | `Copyright (c) 2026 Alan-CRL`（`A/HiMsg/LICENSE:3`） | Git 子模块（Inkeys/additional/HiMsg） | 引入 `41535eaf 2026-08-12`；当前 `e7f42dc1`；归档未见 | `E/gitmodules-history.txt`、`submodules.txt`；`Message.cpp:3` |
| DesktopDrawpadBlocker | [DesktopDrawpadBlocker](https://github.com/Alan-CRL/DesktopDrawpadBlocker) | NOASSERTION（README 声明 GPLv3，未核 only/or-later） | 未取得与该 EXE 对应的许可证文件 | 源码内置（Inkeys/exe/DesktopDrawpadBlocker.exe） | 首见 `92dd0eb9 2024-05-09`；归档 1/2/M；当前仍嵌入、释放并启动 | P；`Inkeys.rc:158`；`IdtPlug-in.cpp` |
| HiEasyX | [zouhuidong/HiEasyX](https://github.com/zouhuidong/HiEasyX) | MIT（README 声明，历史原许可待核） | 未取得历史许可证；README 的 `Copyright (c) 2022 zouhuidong` 不能替代原文件核验 | dev 已不再使用 | 首见 `9f5f8cfd 2023-10-12`；主程序删于 `41535eaf 2026-08-12`；归档 1/2/M | P、D；当前 `Message.Legacy.hpp` 说明类型由 HiMsg 定义 |
| EasyX | [EasyX](https://easyx.cn/) | NOASSERTION（专有授权文本未取得） | 未取得对应版本许可证文件 | dev 已不再使用 | 外部 SDK 使用的确切首引未导出；随 HiEasyX 最早记录 `9f5f8cfd 2023-10-12`；内置头/库首见 `fec7c7de 2024-10-28`；主程序删除 `41535eaf 2026-08-12`；归档 1/2/M | README 历史；P、D 的 `EasyXa.lib/EasyXw.lib` |
| Microsoft DirectX SDK (June 2010) | 未在本地材料中取得下载 URL | NOASSERTION（SDK EULA 未随目录提供） | 未取得许可证文件；头文件声明为 `Copyright (C) Microsoft Corporation.  All Rights Reserved.`（`A/Microsoft DirectX SDK (June 2010)/Include/d3dx9.h:3`，不是 EULA） | dev 已不再使用 | 目录首见 `2fddd64d 2025-05-08`；归档 2/M；dev 仍存 81 个头/内联文件，但未找到当前编译引用；停用提交待确认 | P；旧 includes 有 `D3dx9tex.h`；当前工程、业务引用检查 |
| Microsoft.Office.Interop.PowerPoint 15.0.4420.1018 | [NuGet 包](https://www.nuget.org/packages/Microsoft.Office.Interop.PowerPoint/15.0.4420.1018) | NOASSERTION | 包内未提供许可证；nuspec 自述是 Office 程序集重新打包且无许可证，不能据此认定可自由分发 | 源码内置（Package/Microsoft.Office.Interop.PowerPoint.15.0.4420.1018） | 首见 `9f5f8cfd 2023-10-12`；旧 `packages/` 删除 `cee08eaf 2025-05-09`，当前本机 `Package/` 仍被引用；归档 1 有包，2/M 有项目声明 | `PptCOM.csproj` 的 HintPath、`EmbedInteropTypes=True`；包内 nuspec |
| MicrosoftOfficeCore 15.0.0 | [NuGet 包](https://www.nuget.org/packages/MicrosoftOfficeCore/15.0.0) | NOASSERTION | 包内未提供许可证或版权声明 | 源码内置（Package/MicrosoftOfficeCore.15.0.0） | 首见 `9f5f8cfd 2023-10-12`；旧目录删除 `cee08eaf 2025-05-09`；当前仍引用；归档同上 | `PptCOM/packages.config`、`.csproj`；包内 nuspec |
| Fody 6.8.0 | [Fody/Fody](https://github.com/Fody/Fody) | NOASSERTION（历史许可证未取得） | 已删除的 `packages/Fody.6.8.0/License.txt` 内容没有导出 | dev 已不再使用 | 首见 `9f5f8cfd 2023-10-12`；删除 `fe656ffb 2024-01-27`；归档树未见 | P、D；历史 `FodyWeavers.*`；当前 `.csproj` 无 Fody 导入 |
| Costura.Fody 5.2.0 | 准确上游 URL 待核 | NOASSERTION（历史许可证未取得） | 未取得历史许可证文件 | dev 已不再使用 | 首见 `9f5f8cfd 2023-10-12`；删除 `fe656ffb 2024-01-27`；归档树未见 | P、D；未引用的 `src/rt_rcdat.bin` 仍含 Costura 及压缩 Office 资源名 |
| Mono.Cecil（含 Pdb/Rocks） | 准确上游 URL 待核 | NOASSERTION（历史许可证未取得） | 未取得历史许可证文件 | dev 已不再使用 | 随 Fody 包首见 `9f5f8cfd 2023-10-12`；删除 `fe656ffb 2024-01-27`；归档未见 | D：`packages/Fody.6.8.0/netclassictask/Mono.Cecil*.dll` 等 |
| ICU（历史声明，实际调用未证） | [unicode-org/icu](https://github.com/unicode-org/icu) | NOASSERTION | 未取得所用版本及许可证 | dev 已不再使用 | README 声明始于 `9f5f8cfd 2023-10-12`；声明删除 `74ba458c 2024-03-09`；不能把后者当作代码移除时间；归档调用未证 | `E/readme-history.txt`；路径及归档 includes 未发现对应库 |
| OpenCV（其他历史分支） | [OpenCV](https://opencv.org/) | NOASSERTION（历史声明 Apache-2.0） | 未取得该历史安装包许可证，不能采用 README 的概括版权替代 | dev 已不再使用 | `accf202b 2026-08-20` 加入 `opencv4 4.10.0#3`；CV 路径首见 `2c31dd55 2026-08-21`；仅证明其他分支存在，未证明曾合入 dev；归档未见 | V、README/NOTICE 历史；当前 manifest、安装目录无 OpenCV |
| HarmonyOS Sans（SC/TC/Condensed，Regular/Bold） | [官方资源库线索](https://github.com/openharmony/utils_system_resources/tree/master/fonts) | NOASSERTION（本地未附授权文本） | 未取得许可证；字体 name(0) 分别为 `Copyright 2021-2023 Huawei Device Co., Ltd. All Rights Reserved.`、`Copyright 2021 Huawei Device Co., Ltd. All Rights Reserved.`，仅属元数据 | 源码内置（Inkeys/src/ttf） | 家族准确首引待确认；字体目录始于 `9f5f8cfd 2023-10-12`；归档 1/2/M 仅能证实 ttf 目录，未展开家族 | `Inkeys.rc:128–142`；六个 TTF 的 name 表 |
| Noto Sans JP | [固定来源](https://github.com/google/fonts/tree/66a36c8c94b1a5d992ee4e7f392fccfe4945767c/ofl/notosansjp) | OFL-1.1 | `Copyright 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font Name 'Source'`（`F/NotoSansJP-OFL.txt:1`） | 源码内置（Inkeys/src/ttf） | 单文件首引未导出；`e5f0574e 2026-11-02` 是新增语言功能提交线索，不能据主题独断字体首引；归档家族未证 | `F/NotoSans-README.md`；`Inkeys.rc:144–146` |
| Noto Sans KR | [固定来源](https://github.com/google/fonts/tree/b38c5c93af322c45f633e17ac440ec1e6c94d489/ofl/notosanskr) | OFL-1.1 | `Copyright 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font Name 'Source'`（`F/NotoSansKR-OFL.txt:1`） | 源码内置（Inkeys/src/ttf） | 同 JP：首引待确认；`e5f0574e 2026-11-02` 为功能线索；归档家族未证 | `F/NotoSans-README.md`；`Inkeys.rc:148–150` |
| Segoe Fluent Icons | [字体内记录的 Microsoft 地址](http://www.microsoft.com/typography/fonts/) | NOASSERTION（Microsoft 专有字体条款） | 未有独立许可文件；字体 name(0)：`© 2021 Microsoft Corporation. All Rights Reserved.`；name(13) 含使用限制 | 源码内置（Inkeys/src/ttf/Segoe Fluent Icons.ttf） | 单文件首次提交、归档家族均未由三层树展开 | TTF name(0/13/14)；`Inkeys.rc:130` |
| icomoon.ttf 自定义图标字体 | 未取得实际图标来源 URL | NOASSERTION | 未取得许可证；字体 name 表只有家族名 `icomoon`，无版权或许可字段 | 源码内置（Inkeys/src/ttf/icomoon.ttf） | 首个同名字体路径 `697eaa78 2024-10-14`；曾删根目录副本 `2fe05ebd 2024-11-07`、ttf 副本 `b0f5c53c 2024-11-08`；当前重引时间待确认；归档家族未证 | P、D；TTF name 表；`Inkeys.rc:134` |
| SmileySans-Oblique（历史字体） | 未由现有材料核实 | NOASSERTION | 历史字体及许可证内容未导出 | dev 已不再使用 | 首引未定；删除 `d683a6e5 2024-02-29`；归档家族未证 | D：`智绘教/src/ttf/SmileySans-Oblique.ttf` |
| ProggyClean（ImGui 内嵌字体） | [proggyfonts](https://github.com/bluescan/proggyfonts) | MIT（源码声明） | `Copyright (c) 2004, 2005 Tristan Grimmer`（`A/imgui/imgui_draw.cpp:6292` 的字体许可标注；完整字体许可未单列） | 源码内置（Inkeys/additional/imgui/imgui_draw.cpp） | 当前压缩字体数据及未禁用条件已证；历史独立首引待确认；不能仅凭 ImGui 首引日期认定 | `imgui_draw.cpp` 默认字体数据；`imconfig.h` |
| ProggyForever（ImGui 内嵌字体） | [proggyforever](https://github.com/ocornut/proggyforever) | MIT（源码声明） | `Copyright (c) 2026 Disco Hello, Copyright (c) 2019,2023 Tristan Grimmer`（`A/imgui/imgui_draw.cpp:6481` 的许可标注） | 源码内置（Inkeys/additional/imgui/imgui_draw.cpp） | 当前数据已证；首引提交及归档状态未展开 | `imgui_draw.cpp` 默认字体数据 |
| GitHubMark.png | 准确原图 URL 未记录 | NOASSERTION | 未取得图像许可或版权文件 | 源码内置（Inkeys/src/setting/GitHubMark.png） | 单文件首引及归档情况未展开 | `Inkeys.rc:98`；`Setting.cpp` 的 `SETTING_GITHUB` |
| fluentCheckmark12Filled.svg（上游待确认） | 待确认，不能仅凭文件名指定图标库 | NOASSERTION | 未附许可证或版权行 | 源码内置（Inkeys/src/UI/fluentCheckmark12Filled.svg） | 单文件首引未展开；归档只有 UI 目录证据 | SVG 原文；`Inkeys.rc:325` |
| ClassIsland 标识图片（历史） | 实际原图 URL 未记录 | NOASSERTION | 历史图片许可证未取得 | dev 已不再使用 | 根路径首见 `64f592c6 2025-03-25`；quick 副本删除 `66595757 2026-10-25`；归档 2/M 有根路径 | P、D；归档树 |
| IslandCaller 标识图片（历史） | 实际原图 URL 未记录 | NOASSERTION | 历史图片许可证未取得 | dev 已不再使用 | 根路径首见 `64f592c6 2025-03-25`；quick 副本删除 `66595757 2026-10-25`；归档 2/M | P、D；归档树 |
| NamePicker.png（历史图片） | 实际原图 URL 未记录 | NOASSERTION | 历史图片许可证未取得 | dev 已不再使用 | 首见 `6e80fd26 2025-08-22`；移除提交未确认；归档 2/M | P；归档树；dev 无文件 |
| SecRandom.png（历史图片） | 实际原图 URL 未记录 | NOASSERTION | 历史图片许可证未取得 | dev 已不再使用 | 首见 `6e80fd26 2025-08-22`；移除提交未确认；归档 2/M | P；归档树；dev 无文件 |
| ms_ppt.png（历史图片） | 实际原图 URL 未记录 | NOASSERTION | 历史图片许可证未取得 | dev 已不再使用 | 首见 `9f5f8cfd 2023-10-12`；dev 路径删除 `66595757 2026-10-25`；归档 1/2/M | P、D、归档树 |
| wps_wpp.png（历史图片） | 实际原图 URL 未记录 | NOASSERTION | 历史图片许可证未取得 | dev 已不再使用 | 首见 `d683a6e5 2024-02-29`；dev 路径删除 `66595757 2026-10-25`；归档 1/2/M | P、D、归档树 |
| seewo_camera.png（历史图片） | 实际原图 URL 未记录 | NOASSERTION | 历史图片许可证未取得 | dev 已不再使用 | 首见 `9f5f8cfd 2023-10-12`；dev 路径删除 `66595757 2026-10-25`；归档 1/2/M | P、D、归档树 |
| vcpkg（构建工具） | [microsoft/vcpkg](https://github.com/microsoft/vcpkg.git) | MIT | `Copyright (c) Microsoft Corporation`（`Vcpkg/LICENSE.txt:3`） | Git 子模块（Vcpkg） | 首见小写路径 `b0782dae 2026-02-26`；改名 `4ec49f85 2026-03-18`；当前固定 `99a97de2`；归档未见 | `.gitmodules` 历史；`Directory.Build.props/targets` |
| vcpkg-cmake（host 构建端口） | [vcpkg](https://github.com/microsoft/vcpkg) | MIT | `Copyright (c) Microsoft Corporation`（`H/vcpkg-cmake/copyright`） | vcpkg（vcpkg-cmake, 传递依赖 of openssl） | 当前安装已证；安装时间未导出；也被其他直接端口依赖；归档未证 | installed `status` |
| vcpkg-cmake-config（host 构建端口） | [vcpkg](https://github.com/microsoft/vcpkg) | MIT | `Copyright (c) Microsoft Corporation`<br>`All rights reserved.`（`H/vcpkg-cmake-config/copyright`） | vcpkg（vcpkg-cmake-config, 传递依赖 of openssl） | 当前安装已证；首次引入、归档状态未证；多个直接端口共享 | installed `status` |
| vcpkg-cmake-get-vars（host 构建端口） | [vcpkg](https://github.com/microsoft/vcpkg) | MIT | `Copyright (c) Microsoft Corporation`（`H/vcpkg-cmake-get-vars/copyright`） | vcpkg（vcpkg-cmake-get-vars, 传递依赖 of openssl） | 当前安装已证；首次引入及归档状态未证 | installed `status` |

**仅测试／工具使用的组件**

| 组件 | 上游 URL | 许可证（SPDX） | 准确版权声明（逐字摘自许可证文件） | dev 使用方式 | 历史 | 证据 |
|---|---|---|---|---|---|---|
| DirectInkPresenter 参考项目 | [作者链接](https://github.com/ALTaleX531)；独立上游仓库 URL 未提供 | MIT | `Copyright (c) 2025 Alan-CRL`（`ResTest/DirectInkPresenter/LICENSE:3`）；README 同时说明代码由 ALTaleX531 编写，二者不可擅自互换 | 源码内置（ResTest/DirectInkPresenter） | 引入 `7151e456 2026-05-16`；归档未见 | LICENSE、README、独立 `.vcxproj` |
| NumPy（资源生成） | 未从脚本记录取得上游 URL | NOASSERTION（本机包版本、许可证未取得） | 未取得所用安装包的许可证文件 | 外部安装 / SDK（Scripts/generate_eraser_demo.py 使用 NumPy） | 使用脚本首见 `ff6fb4e1 2026-10-25`；归档未见 | 脚本 `import numpy as np`；P |
| fontTools 4.60.1（字体制备） | 未从制备文件记录取得上游 URL | NOASSERTION（安装包许可证未取得） | 未取得所用安装包的许可证文件 | 外部安装 / SDK（Noto 静态字体制备及检查工具） | 当前资源 README、制备 manifest 已证；工具使用对应提交未单独导出；归档未证 | `F/NotoSans-README.md`；归档任务 `research/build_noto_static.py`、`noto-font-manifest.json` |

`inkStrokeModelerTest*`、`InkeysHeadlessTests` 使用的 Modeler、Abseil、msgpack、ImGui、ImFluent、JsonCpp、FreeType 等已在总表列出，并非测试独有。`PptCOM.Tests` 未发现额外测试框架依赖；Timeout 的 JsonCpp 也已合并列出。demo 的 EasyX/HiEasyX 删除于 `b91366d6 2026-08-02`。

**2. 现有 README／NOTICE 的具体错误**

- **NOTICE 把 LunaSVG 版权写成 JsonCpp 的版权**；应以 `S/lunasvg/copyright` 为准。
- **ImGui 年份过时**：现有声明止于 2025，实际随附许可证为 `2014-2026`。spdlog 的现有声明也没有逐字反映当前包的 `2016 - present` 及 contributors。
- `Gr¸delbach` 是真实存在的乱码，而且已污染 `L/hashlibpp License`；源码文档中的 `Gr&uuml;delbach` 是修复线索，不能无说明地替换许可证原文。
- concurrentqueue 并非只能写“参见文件”：当前 SPDX 明确为 **BSD-2-Clause OR BSL-1.0**；libcuckoo 明确为 **Apache-2.0**。stb 还提供 **Unlicense** 选项。
- OpenSSL 当前许可证文本没有现有 NOTICE 所列的那组版权行；该声明不是当前 `copyright` 文件的逐字摘录。
- **HiEasyX、EasyX 被写成当前使用组件，实际已移除**；imgui_toggle 虽仍留在目录，也未被当前主工程编译。
- 两份声明漏列 HiMsg、msgpack、fmt、Brotli、Office PIA、字体资源，以及 hashlib++、Zip Utils 内的其他权利人。NOTICE 还漏列 README 已列的 JsonCpp、magic_enum。
- README 未列当前 FreeType；NOTICE 虽补列 FreeType，但展示的致谢文本不是许可证中现成的一行原文，应区分“按许可生成的致谢”与“逐字版权摘录”。
- README 指向 `main` 的 NOTICE／许可证目录，**无法代表 dev 当前依赖**。未证实已有上游项目 URL 拼写错误；缺失来源 URL 与分支指向不匹配应分别处理。

**3. 事实／推测／待确认**

**事实：**当前确认安装 **15 个目标端口、3 个 host 构建端口**。`absl`、`httplib`、`magic_enum`、`unordered_dense` 等 share 名称不能重复计为新组件。Brotli 来自 cpp-httplib，fmt 来自 spdlog；FreeType 当前禁用默认可选特性。HiMsg 被实际包含；旧 Modeler `.lib` 及保留的 Abseil `.0` 快照不是当前提供者。`PptCOM.dll` 是自有产物，其 Office 类型依赖须另计。

**推测：**`fluentCheckmark12Filled.svg` 可能来自 Fluent 图标集；`icomoon` 可能是自定义图标拼集。文件名不足以确定作者、上游或许可证，因此没有据此填写 MIT。OpenCV 的全分支记录也不足以证明它曾进入 dev。

**待确认：**

- 三层树折叠了字体、图片、嵌套库；需补充这些路径的完整树及逐文件新增／删除／重命名记录，才能确定首引、重引和归档家族。
- HiEasyX、EasyX、DirectX SDK、DesktopDrawpadBlocker、历史 Fody/Costura/Mono.Cecil 需要**对应版本的原许可证或原始包**；Office 包自身没有提供足够授权材料。
- HarmonyOS、icomoon、GitHubMark、其他 PNG/SVG、`message_box/error.png`、Timeout 的 `TimerDownNotice.wav` 需要原始来源及授权记录；尚不能把所有未署名资源认定为自制或第三方。
- ImGui 内还有日本字形范围数据的 CC-BY-4.0 来源标注及其他数据来源注释；需结合实际保留版本、使用路径和原始通知补核，不能保证现有汇总覆盖其全部附属材料。
- ICU 只有历史 README 声明；需早期源码／项目配置才能证明实际使用。历史传递依赖完整性需旧包或安装清单，当前安装状态不能倒推过去。

**已排除：**Windows SDK／操作系统库、MSVC CRT/STL、.NET Framework BCL（包括 `ActionsRes/v4.0.zip` 中的 Framework 程序集）。本次为静态证据调查，未执行编译或测试。