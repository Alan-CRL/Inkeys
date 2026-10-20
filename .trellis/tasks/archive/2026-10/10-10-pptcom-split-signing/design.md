# 设计
jobs保持check/build/package/upload ID，插入pptcom。PptCOM输出DLL/TLB的artifact ID，native下载到原Inkeys DLL/TLB路径。
DLL与TLB独立artifact；所有上传缺失文件必须error；rerun可overwrite且消费者只用本次ID。清理仅处理当前run的精确ID。
新增PE资源验证以检查IDR_DLL1嵌入字节等于选定DLL，提取不执行EXE/不开GUI。

实施阶段用户修正：Inkeys 下 DLL/TLB 继续 Git 跟踪且不忽略，由维护者在 PptCOM 源码变化后手动同步；两个工程无自动复制或依赖。中英文编译说明同步独立构建、Harness 开关和 CI artifact 交接。
