# Shape 数据兼容与启动修复

## Goal

修复 feature/shape 审查确认的三个与 UI 整改无关的缺陷：高 DPI 旧矩形导入识别、白板历史孤儿笔迹导致整页丢失、SelectionOverlay 启动失败后继续启动。

## Requirements

1. 旧矩形高 DPI 兼容
   - 导出端旧矩形圆角为 `min(4*dpiScale, 半短边)`，文件不保存 dpiScale；导入端必须识别 100%~500% 标准缩放档位下保存的旧矩形（无 `inkeysShape` 元数据、无 extra、无旋转）。
   - 圆角不属于任何标准档位的无元数据矩形仍按通用标准矩形导入，保留参数；带 `inkeysShape` 元数据的矩形不经过旧路径。
2. 白板历史孤儿笔迹兼容
   - 生产路径可能出现「已追加画布但 history 追加失败」的孤儿笔迹，保存后 `strokes` 多于 `items`。
   - 读取时允许未被任何 item 引用的 strokes（孤儿），整页仍能恢复；孤儿不可见、不进入历史。
   - 仍拒绝：items 多于 strokes、索引越界、重复引用、格式错误等真正损坏。
3. SelectionOverlay 启动失败
   - `SelectionOverlay::Acquire()` 失败按既有 fatal 退场顺序清理并 `return 1`。

## Acceptance Criteria

- [x] 共享测试 `--uink-file-only`、`--shape-core-only` 新增 150%/200% 旧矩形识别与非档位圆角负例并通过。
- [x] `Inkeys.exe --draw3-shape-core-test`（Shape.EditHistory 冷页历史用例）新增孤儿笔迹恢复与损坏拒绝用例并通过；`--draw3-whiteboard-storage-test` 通过。
- [x] Debug|ARM64 完整构建成功。
- [x] 保留原文件编码/BOM/CRLF；提交在 feature/shape 子分支，不 push。
