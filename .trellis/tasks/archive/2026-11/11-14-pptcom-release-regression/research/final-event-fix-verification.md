# 最终关闭通知候选版验证

## 原因与修改

第二次实机日志证明 get:Presentations 自身耗时 28.1419972 秒，返回 0x800706BE；之后原绑定最终配平。当前改为 2025/2004 不查询对象模型，2031 PresentationCloseFinal 直接让 owner 释放本模块绑定，再发现其他仍打开的文稿。没有调用 Office Quit/Close，其他文稿可能短暂重连。

补齐 ActivePresentation 查询过程中重入最终关闭并抛异常的分支，防止它再落入 Presentations fallback。保留无关闭提示的旧版/无事件轮询、取消关闭恢复、原 GC、逐笔释放和公共 ABI。Unadvise 新增排除输出的实际调用耗时。

## 验证结果

- 新最终关闭回归在上一候选上 RED：编译 exit0、测试 exit1，四项预期失败。
- 修改后最终托管 suite 与原生 COM fixture：exit0，47 cases、0 failures。还覆盖异常重入关闭时不再读取集合。
- 完整 InkeysRepo.sln Debug|ARM64：ARM64 host MSBuild、PATH workaround、既有 PDB 可用环境，exit0。
- DLL 同步曾短暂遇到 mapped section 占用；重试成功后以哈希作为构建前置条件，重新构建并校验了最终产物。中间那次成功但仍包含早一份 DLL 的构建不作为交付。
- 最终 DLL 与 Inkeys/PptCOM.dll、EXE 内嵌资源一致，SHA256：9D81481451A7CB66E50D7392969C7CBDB6673EFAF3A750CBAB748EBC972FCB8D。
- 模块 MVID：230cb63b-1639-4077-bf63-257114a882e5。
- 原 GC 三步仍在；TLB 逐字节不变；编码/BOM/CRLF 保持，git diff --check 通过。

日志：validation/final-close-red-build.log、final-close-red-tests.log、final-close-green-build.log、final-close-green-tests.log、final-close-solution-build.log。

未启动 Office/GUI 或 Computer Use；未 commit/push。实机效果仍由用户验证。
