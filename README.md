# WinProcessProxy

> 为指定 Windows 进程提供轻量、透明的 SOCKS5 代理，无需开启 TUN，也无需修改应用自身的代理设置。

WinProcessProxy 是一个面向 Windows x64 的按进程流量转发工具。它通过 NetFilter SDK 捕获指定进程的 TCP、UDP 和 DNS 流量，再将其转发到 SOCKS5 代理。

它特别适合 Codex、Antigravity 等没有完整代理设置、使用系统代理不稳定，或不希望为了单个应用开启全局 TUN 的开发工具。

## 为什么使用 WinProcessProxy？

- **按进程代理**：只有配置的应用经过代理，其他程序保持原有网络路径。
- **无需 TUN**：不创建虚拟网卡，不接管整台电脑的默认路由。
- **应用无感知**：目标程序不需要支持 SOCKS5，也不需要额外设置环境变量。
- **支持 TCP、UDP 和 DNS**：减少 DNS 直连、UDP 绕过等问题。
- **配置热更新**：修改 `appsettings.json` 后自动加载，无需重启服务。
- **Windows 服务运行**：安装后可随系统启动，在后台持续工作。
- **独立发布包**：Native AOT 自包含部署，目标机器无需另装 .NET Runtime。

## 工作方式

```text
Codex / Antigravity / 指定应用
                │
                ▼
       WinProcessProxy 驱动层
                │
                ▼
      WinProcessProxy.Service
                │
                ▼
       本地或远程 SOCKS5 服务
                │
                ▼
              Internet
```

WinProcessProxy 只处理 `Processes` 中列出的程序。与全局 TUN 相比，它不会为了代理一个开发工具而改变所有应用的网络环境，也能减少局域网访问、游戏平台、虚拟机和开发服务受到影响的情况。

## 快速开始

### 1. 下载

从 GitHub Releases 下载最新的：

```text
WinProcessProxy-<version>-win-x64.zip
```

解压到一个固定目录，例如：

```text
C:\Program Files\WinProcessProxy
```

不要在安装服务后移动或删除该目录，因为 Windows 服务会直接从这里启动程序。

### 2. 配置 SOCKS5 和目标进程

编辑发布目录中的 `appsettings.json`：

```json
{
  "Socks5": {
    "Host": "127.0.0.1",
    "Port": 10809,
    "Username": "",
    "Password": ""
  },
  "Dns": {
    "Host": "1.1.1.1",
    "Port": 53
  },
  "Processes": [
    "ChatGPT.exe",
    "codex.exe",
    "Codex.exe",
    "codex-code-mode-host.exe",
    "codex-command-runner.exe",
    "codex-computer-use.exe",
    "extension-host.exe",
    "node_repl.exe",
    "Antigravity.exe",
    "Antigravity IDE.exe",
    "agy.exe",
    "language_server.exe",
    "language_server_windows"
  ]
}
```

`Processes` 使用可执行文件名进行匹配，不填写完整路径，匹配时不区分大小写。因此 `codex.exe` 与 `Codex.exe` 实际只需保留一个；示例同时列出二者，是为了完整展示不同安装版本中可能看到的名称。不同版本的进程结构可能变化，可通过任务管理器的“详细信息”页面，或下面的命令确认：

```powershell
Get-Process | Sort-Object ProcessName | Select-Object ProcessName, Id
```

### 3. 前台测试

首次运行涉及网络驱动注册，请在管理员 PowerShell 中执行：

```powershell
.\WinProcessProxy.Service.exe
```

确认目标应用可以联网后按 `Ctrl+C` 停止。

### 4. 安装为 Windows 服务

在解压目录运行：

```powershell
.\install-service.ps1
```

脚本会请求管理员权限，检查发布文件、创建自动启动服务并启动 WinProcessProxy。检查状态：

```powershell
Get-Service WinProcessProxy
```

卸载服务：

```powershell
.\uninstall-service.ps1
```

## Codex 代理示例

Windows 版 Codex 不只包含一个进程。主界面、Codex CLI、代码模式、扩展宿主和工具运行时可能分别发起网络请求。完整参考配置如下：

```json
"Processes": [
  "ChatGPT.exe",
  "codex.exe",
  "Codex.exe",
  "codex-code-mode-host.exe",
  "codex-command-runner.exe",
  "codex-computer-use.exe",
  "extension-host.exe",
  "node_repl.exe"
]
```

本机 Codex 安装包中还包含 `chrome_proxy.exe`、`chrome_pwa_launcher.exe`、`notification_helper.exe`、`elevation_service.exe`、`elevated_tracing_service.exe`、`winpty-agent.exe`、`rg.exe` 和 `tectonic.exe`。这些通常不是 Codex API 的主要网络出口，因此默认不建议加入；若日志或抓包确认某个组件需要联网，再按需补充。

进程匹配不区分大小写，所以 `codex.exe` 和 `Codex.exe` 是同一条匹配规则，实际配置保留任意一个即可。示例不匹配通用的 `node.exe`，避免系统中其他 Node.js 程序被一并代理；Codex 仅保留更明确的 `node_repl.exe`。

如果使用 Clash/Mihomo，建议给 WinProcessProxy 创建一个专用 SOCKS5 Listener，并固定到代理策略组，避免流量进入 SOCKS5 后再次因域名嗅探失败而落入 `DIRECT`：

```yaml
listeners:
  - name: codex-fixed
    type: socks
    listen: 127.0.0.1
    port: 10809
    udp: true
    users: []
    proxy: 🚀 节点选择
```

然后将 WinProcessProxy 的 SOCKS5 地址设为 `127.0.0.1:10809`。`proxy` 的值必须与 Clash/Mihomo 中已有的节点或策略组名称完全一致。

## Antigravity 代理示例

Antigravity 可能由桌面主进程、IDE、CLI、语言服务和 Node.js 扩展宿主共同组成。完整参考配置如下：

```json
"Processes": [
  "agy.exe",
  "language_server.exe",
  "language_server_windows",
  "Antigravity.exe",
  "Antigravity IDE.exe"
]
```

本机 Windows 安装目录已确认包含 `Antigravity.exe` 和 `language_server.exe`；其他名称可能来自不同版本、Antigravity IDE 或 CLI。示例不包含通用的 `node.exe`，以免其他 Node.js 应用被一并代理。

如果网络请求由独立的辅助进程发起，也需要把该辅助进程加入数组。可以先运行 Antigravity，再使用以下命令观察相关进程：

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.Name -match 'antigravity' } |
  Select-Object Name, ProcessId, ParentProcessId, ExecutablePath
```

Codex 与 Antigravity 也可以同时代理：

```json
"Processes": [
  "ChatGPT.exe",
  "codex.exe",
  "Codex.exe",
  "codex-code-mode-host.exe",
  "codex-command-runner.exe",
  "codex-computer-use.exe",
  "extension-host.exe",
  "node_repl.exe",
  "agy.exe",
  "language_server.exe",
  "language_server_windows",
  "Antigravity.exe",
  "Antigravity IDE.exe"
]
```

## 配置说明

| 配置项 | 说明 |
|---|---|
| `Socks5.Host` | SOCKS5 服务器地址，通常是 `127.0.0.1` |
| `Socks5.Port` | SOCKS5 端口 |
| `Socks5.Username` | 可选用户名 |
| `Socks5.Password` | 可选密码 |
| `Dns.Host` | 通过代理访问的 DNS 服务器 |
| `Dns.Port` | DNS 服务器端口，通常是 `53` |
| `Processes` | 需要代理的 Windows 可执行文件名 |

配置文件会被持续监视。有效修改将自动应用；无效修改会记录 `[CONFIG:ERROR]`，并恢复最后一次可用配置。详细说明见 [配置文档](docs/configuration.md)。

## 日志

前台运行时可以看到带有明确标签的流量日志：

```text
[APP]
[TCP:OPEN]
[TCP:CLOSE]
[UDP:OPEN]
[UDP:CLOSE]
[DNS]
[CONFIG]
```

这些日志可用于确认某个进程是否被识别、连接是否进入 SOCKS5，以及配置热更新是否成功。

## 从源码构建

构建环境：

- Windows 10/11 x64
- .NET 10 SDK
- Visual Studio 或 Build Tools，包含 **Desktop development with C++** 工作负载
- PowerShell 7 或 Windows PowerShell

执行：

```powershell
.\scripts\build.ps1 -Configuration Release
```

构建脚本会编译原生组件、运行测试、发布 Native AOT 程序、复制驱动和运行库，并清理发布目录中的 PDB。默认输出目录为 `output`。

指定版本和输出目录：

```powershell
.\scripts\build.ps1 `
  -Configuration Release `
  -Version 1.0.0 `
  -OutputDirectory .\output
```

最终发布包包含：

```text
appsettings.json
install-service.ps1
nfapi.dll
nfdriver.sys
uninstall-service.ps1
WinProcessProxy.Native.dll
WinProcessProxy.Service.exe
```

## 注意事项

- 本项目仅支持 Windows x64。
- 安装服务和注册网络驱动需要管理员权限。
- 请仅代理自己有权控制的设备、程序和网络流量。
- SOCKS5 服务必须支持项目所需的 TCP/UDP 转发能力。
- 安装新版本前建议先停止服务，并保留自己的 `appsettings.json`。

## License

项目自有代码使用 [MIT License](LICENSE)。第三方组件不自动适用本项目的 MIT License，详细信息见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。

## 致谢与第三方声明

本项目在按进程代理、Windows 流量转发和工程组织方面参考了 [netchx/netch](https://github.com/netchx/netch) 项目。WinProcessProxy 是独立实现，与 Netch 项目及其维护者不存在隶属或官方关联；相关权利归各自权利人所有。

本项目使用了 Vitaly Sidorov 的 **NetFilter SDK**，包括相关头文件、导入库、`nfapi.dll` 和 `nfdriver.sys`。NetFilter SDK 及其二进制文件不属于本项目的 MIT 授权范围，其版权和许可条款归原作者或权利人所有。分发源码或二进制发布包之前，请确认你拥有符合用途的 NetFilter SDK 使用与再分发许可，并保留适用的版权和许可声明。
