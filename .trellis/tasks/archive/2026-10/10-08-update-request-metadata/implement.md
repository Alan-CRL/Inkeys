# 实施计划

## 实施前

1. 用户已确认最新计划及通道同时写文件，执行 task.py start；实现/检查 manifests 使用最新 h-binding 研究。
2. 子代理读取 PRD、design、implement 和所列 specs；核对实时来源声明和锁区，记录受影响文件原编码/EOL。

## 实现

1. 在 Other.Config.cppm 为 H 增加强类型只读固定/地址/加锁地址来源，独立的文件和 Referer 开关；保持 X 接口。
2. 适配 schema walker 与读/default/write/upload handlers：H 不回读不回写来源，输出按需取得值副本，writeFile=false 不遗留已知键。
3. 修正 Config 内部复制，H 描述符独立于文件输出策略；回归 X 和 configOnce 复制。
4. 按 PRD 矩阵绑定现有六字段，新增加锁 UpdateChannel，文件与 Referer 输出均开启；id 仅发送副本去括号。
5. 补版本检查固定 UA；保持 GetRefererInfo 的单一 Config::GetUploadInfo 入口。
6. 添加关键中文注释和 H 声明维护示例。

## 验证

- 扩展现有 Config headless 覆盖：无重新读取的实时变更、固定来源、文件/Referer 开关组合、ev 仍落盘、GUID 输出区别、通道编码与切换、Read/Reset/Copy 不写外部值、X 不回归。
- 使用安装发现获得 ARM64 原生 MSBuild，完整构建 InkeysRepo.sln Debug | ARM64，超时至少五分钟。MSBuild 同一 PowerShell invocation 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue，并设置 MSBUILDDISABLENODEREUSE=1。
- 构建/运行现有 headless 目标，仅 --no-window，不启动产品、设置、浏览器或交互 GUI，不访问真实更新服务。
- 检查退出代码、第一个有意义错误、UA 请求头和文件字节格式。区分源码与环境错误，不修改依赖或构建设置绕过失败。
- check 子代理审查全任务，不扩大测试或修改范围；质量记录写入本任务 research。

## 收尾

主代理审查 diff，更新与 H 合同相关的最小 spec 内容，报告行为和验证。未获 commit/push 授权，不创建提交、不推送；遵循仓库当前任务收尾要求记录状态。
