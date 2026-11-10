# Task: Inventory every third-party component Inkeys uses now (dev) or used historically

You are a read-only investigator. Do NOT modify any file. Your sandbox cannot run git or any subprocess; all git history you need has been pre-exported as text files (listed below). Read files directly.

## Repository
- Working directory: `D:\Project\Inkeys\Repo\Inkeys` (branch `dev`, HEAD 571b49ec). Windows desktop app "Inkeys" (智绘教Inkeys), C++20 + a C# COM add-in (`PptCOM/`).
- History: 2023-10 → 2026-11. Major versions archived on branches `inkeys1-final`, `inkeys2-final`; `main` = last release; `dev` = current.

## Pre-exported evidence (folder `C:\Users\alan-\AppData\Local\Temp\inkeys-codex\third-party\`)
- `all-paths-depth3.tsv` — every path ever committed (all branches), folded to 3 directory levels, with first/last commit seen (`first=<hash date>` is the OLDEST commit touching it, `last=` the NEWEST).
- `dev-tree-depth3.txt`, `main-tree-depth3.txt`, `inkeys1-final-tree-depth3.txt`, `inkeys2-final-tree-depth3.txt` — file trees per branch (3 levels).
- `includes-HEAD.txt`, `includes-inkeys1-final.txt`, `includes-inkeys2-final.txt` — unique `#include` / `import` / `#pragma comment(lib)` lines with counts.
- `linkdeps-*.txt`, `dev-vcxproj-dirs.txt` — linker deps and include/lib dirs from vcxproj.
- `vcpkg-json-history.txt`, `notice-history.txt`, `readme-history.txt`, `gitmodules-history.txt` — full `git log -p` of those files.
- `deletions.txt` — every deleted file per commit. `all-commits.txt` — all commit subjects.
- `submodules.txt` — current submodules.

## Other sources you can read directly
- Current code: `Inkeys/` (vendored libs under `Inkeys/additional/`), `PptCOM/`, `Inkeys/src/` (resources: check for bundled fonts, icon sets, images, DLLs), `*.vcxproj`, `vcpkg.json`, `Directory.Build.props/targets`.
- vcpkg installed ports (includes transitive deps): `VcpkgInstalled/Arm64/arm64-windows-static-v143/share/<port>/copyright` and `vcpkg.spdx.json` — authoritative license/copyright text for vcpkg ports.
- `NOTICE`, `ThirdpartyLicenses/`, `README.md` section "## 项目引用" (the table we are revising).
- Submodule `Inkeys/additional/HiMsg`, vendored dirs' own LICENSE/README headers.

## What to produce
Write the report in Simplified Chinese, Markdown, ≤ 2500 Chinese characters excluding the table.

### 1. 总表（one row per component; include components used only in the past)
Columns:
| 组件 | 上游 URL | 许可证（SPDX） | 准确版权声明（逐字摘自许可证文件） | dev 使用方式 | 历史 | 证据 |
- dev 使用方式 must be exactly one of: `源码内置（<path>）`, `vcpkg（<port>[, 传递依赖 of <x>]）`, `Git 子模块（<path>）`, `外部安装 / SDK（说明）`, `dev 已不再使用`.
- 历史: when introduced and (if applicable) removed — commit hash + date from the evidence files; which archived branches contain it (inkeys1-final / inkeys2-final / main).
- 准确版权声明: copy verbatim from the actual license file (give its path). If no copyright line exists in the license, write `许可证文本无版权行` — do not invent.
- 证据: file paths (and line numbers where useful). Keep short.

Scope:
- Include: vendored source, vcpkg direct AND transitive ports actually installed, submodules, bundled binaries/DLLs, external SDK-style libs (e.g. EasyX), bundled fonts / icon sets / third-party images if any, the C# PptCOM dependencies, `Microsoft DirectX SDK (June 2010)` under `Inkeys/additional/`.
- Separate sub-table for components used ONLY by test/tool projects (`inkStrokeModelerTest*`, `InkeysHeadlessTests`, `PptCOM.Tests`, `ResTest`, `Timeout`).
- Exclude: Windows SDK / OS system libraries, MSVC CRT/STL, .NET Framework BCL — but list them in one line as "已排除" for completeness.

### 2. 现有 README / NOTICE 的错误
List concrete errors: wrong/missing copyright (e.g. mojibake `Gr¸delbach`), wrong license, components listed but not used in dev, components used but missing, wrong URLs.

### 3. 事实 / 推测 / 待确认
Clearly separate what the evidence proves from what you infer. Anything you could not verify goes to 待确认 with what evidence would settle it.

## Rules
- Evidence beats memory. If evidence contradicts an assumption, say so; do not guess.
- A component counts as "used in dev" only if dev code/projects actually compile/link it (vendored dir present AND referenced, or vcpkg port installed for the main project, or submodule referenced).
- Do not modify any file. Output only the report as your final message.
