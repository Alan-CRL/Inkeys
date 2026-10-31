# VMware Win7 Hardware 测试路线（2026-10-09）

## 当前证据

用户确认 Workstation Pro 17.5.2，两个 guest 都为 VM SVGA 3D、DDI10.1、WDDM1.1、有 KB2670838。实际独立 Hardware 请求 FL11_0 均返回 0x887A0004；正常应用四轮都是 Hardware 失败后 WARP FL11_0 成功。尚无虚拟硬件兼容性、Tools/SVGA 驱动版本或宿主 GPU 的具体资料，不能唯一断定是哪一个设置。

[Microsoft DXGI_ERROR](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error) 将 0x887A0004 定义为 unsupported；运行时存在 D3D11 不代表创建出的硬件达到 FL11_0。[Microsoft feature levels](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro) 区分 11_0 的 SM5.0 与 10_1 的 SM4.x，WDDM1.x 并不单独否定 11_0。Inkeys 的 SM5.0/compute 等合同不能仅通过把创建请求加 10_1 就维持原功能。

## 官方支持与最小排查

[VMware DX11 产品说明](https://blogs.vmware.com/cloud-foundation/2020/05/18/directx-11-now-with-workstation-tp20h2/) 明确 DX11 需要升级 Tools 与 powered-off VM 的 virtual hardware compatibility v18；[Workstation16 公告](https://blogs.vmware.com/cloud-foundation/2020/08/20/announcing-workstation-16-and-fusion-12/) 说明16已提供DX11。因此17.5.2应用版本本身不局限于DX10.1；仍可能保留较旧虚拟硬件/驱动。

1. 在测试副本关机后，通过 Manage -> Change Hardware Compatibility 选16.x/17.x（virtual hardware18或以上）；升级Workstation应用不等于升级每台VM兼容性。
2. Display 启用 Accelerate 3D graphics。
3. 更新实际 guest Tools/SVGA 到支持 Win7、匹配 DX11 能力的版本并重启；核实际安装的显示驱动，而非仅查看宿主应用版本。
4. 核宿主 GPU/驱动：[Workstation17 官方 FAQ](https://knowledge.broadcom.com/external/article?legacyId=90112) 要求 Windows 宿主 GPU 支持 DirectX11.1，并将 Win7+列为3D加速guest。宿主要求不是让guest请求FL11_1。
5. 再运行已有独立 Hardware probe；验收必须是 Hardware.FL11_0 create_hr=0、driver=hardware，完整105项及真实桌面墨迹均通过。dxdiag 的系统 DirectX 版本或 DDI 字样都不能取代实际创建结果。

## SHA2 与最低补丁基线

若 guest 仍是只有 KB2670838、没有 SHA2 支持，新的驱动签名可能受阻。[Broadcom Tools 兼容表](https://knowledge.broadcom.com/external/article/313371/vmware-tools-compatibility-with-guest-op.html) 将未安装 KB4474419 的 Win7SP1 限于 Tools10.1至11.0.6；这使旧驱动成为合理待核假设，不是已证实当前版本。

可在 Hardware 测试副本添加 SHA2/servicing 支持，并装兼容 Win7 的适当 Tools，保留当前最低补丁 WARP VM。[Microsoft SHA2 支持说明](https://support.microsoft.com/en-gb/servicing/os/windows/2020/09/2019-sha-2-code-signing-support-requirement-for-windows-and-wsus) 列出 KB4474419 与 KB4490628。增加补丁的 Hardware VM 不能继续称为“仅 KB2670838”验收。无需修改应用 shader 公式或关闭效果。

VMware Hardware 是 guest SVGA 驱动经虚拟化到宿主 GPU 的路径；可验证 Win7 Hardware 后端，但不能代表真实 Intel/AMD/NVIDIA Win7 厂商驱动已经验收。若这些条件核完仍为10.1/unsupported，下一证据是实际 Tools、SVGA 驱动、虚拟硬件版本、宿主GPU与vmware.log，不能用未经验证的 vmx 强制参数承诺FL11_0。

## 当前故障解释的边界

两台 Aero 条件均修复，是旧 WARP 完整 PS 控制结构兼容问题的真实正证据；候选保留数学。WARP 本身对程序做编译/JIT（[Microsoft WARP Guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp)），但现有结果没有指令跟踪，不能唯一指定某条 discard/fwidth/JIT bug。也不从微软 Flip 支持表否定当前已经创建成功且候选能正常出图的 HWND/ULW 路径。
