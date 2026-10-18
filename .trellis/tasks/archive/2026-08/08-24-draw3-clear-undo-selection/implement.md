# Implement: Draw3 清空撤回与选择语义

- [x] 扩展 standalone/product `ink_history`：item kind、Clear append、内容判定、可见 Tile/redo 分支和 CPU 测试。
- [x] 扩展 standalone/product `ink_history_gpu`：Clear 透明 operator 的 raster、cache rebuild 与 ordered replay。
- [x] 改造产品 `DrawingController` Clear/Undo/Redo 事务、内容发布、GPU/瞬态清理和诊断；更新隐藏 HWND 集成断言。
- [x] 增加主栏 Clear 点击纯决策 helper，接入普通点击与 double-click continuation，补齐 `--no-window` 测试。
- [x] 更新 native runtime/history 与 native-desktop Draw3 集成规范，保持 UTF-8 BOM/CRLF 及最小修改范围。
- [x] 静态检查双份 Draw3 核心同步、项目登记、编码换行和 `git diff --check`。
- [x] 使用 ARM64 host MSBuild 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，运行 `InkeysHeadlessTests.exe --no-window` 与 `Inkeys.exe --draw3-hidden-test`。
- [x] 构建 `inkStrokeModelerTest.sln` 的 `Debug|ARM64` 并运行 `inkStrokeModelerTestTests.exe`；只运行无窗口测试，不提交 commit。

## Validation Result

- `InkeysRepo.sln Debug|ARM64`：PASS，最终重建 0 error；warning 为仓库既有类型转换/第三方诊断。
- `InkeysHeadlessTests.exe --no-window`：PASS。
- `Inkeys.exe --draw3-hidden-test`：PASS，覆盖 DComp、DWM2、DWM、ULW 及 Clear/Undo/Redo/page/resize。
- `inkStrokeModelerTest.sln Debug|ARM64`：PASS，0 error。
- `inkStrokeModelerTestTests.exe`：PASS，全部 history/geometry/contact 测试通过。
- `git diff --check`：PASS；修改源码为 UTF-8 BOM+CRLF，任务/规范 Markdown 保持 UTF-8 无 BOM+CRLF。

## Risk And Rollback Points

- Clear operator 的 Tile 覆盖和 range tree invalidation 是主要正确性风险；先以 CPU history/composition 测试锁定，再接 controller。
- Clear/Undo/Redo 是 GPU 与 visibility 的事务；任何失败必须保持 CPU history 与当前画面可恢复，不可先提交 visibility。
- 双击状态不得通过通用 toggle coalescer；只在 Clear 按钮 continuation 范围内保存一次性资格。
