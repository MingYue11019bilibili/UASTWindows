# UASTWindows

> 一个 Windows 平台的图形化提权工具。启动目标程序时不弹 UAC，支持管理员 / SYSTEM / TrustedInstaller 三种运行身份。

**作者**: [明明月明月11019](https://space.bilibili.com/3707056078982013)（哔哩哔哩）

**注意**: 使用本工具需要一定的计算机技术，本程序默认您已有一定的计算机基础，且您知道您在干什么。

---

## 功能特性

| 功能 | 说明 |
|------|------|
| 三级降级 UAC 绕过 | fodhelper → computerdefaults → 标准 UAC |
| 三种运行身份 | 管理员 / SYSTEM / TrustedInstaller |
| 令牌特权控制 | 默认 / 启用全部特权 / 禁用全部特权 |
| 完整性级别 | 默认 / System / High / Medium / Low |
| 进程优先级 | 默认 / 实时 / 高 / 高于正常 / 正常 / 低于正常 / 低 |
| 窗口显示模式 | 显示 / 隐藏 / 最大化 / 最小化 |
| 自定义工作目录 | 指定子进程的起始目录 |
| 等待模式 | 等待子进程结束后再退出 |
| 防重复提权 | 降级前检测已有管理员实例，避免产生多个进程 |
| 命令行接口 | 参考 NSudoL 的参数格式 |
| 独立引擎 | GUI 与引擎分离，引擎可被脚本直接调用 |
| 多语言支持 | 通过 language.json 切换界面和日志语言 |

---

## 项目结构

```
UASTWindows/
├── UASTWindows.slnx          解决方案文件
├── LICENSE                  MIT + 署名条款
├── README.md                本文件
├── .gitignore               Git 忽略规则
├── Common/
│   ├── json.h                   JSON 解析器接口
│   ├── json.c                   JSON 解析器实现
│   ├── translation.h            多语言模块接口
│   └── translation.c            多语言模块实现
├── UASTCmdup/               提权引擎（控制台）
│   ├── UASTCmdup.vcxproj
│   ├── UASTCmdup.vcxproj.filters
│   └── TCU.c                引擎源码
└── UASTWindows/             图形前端
    ├── UASTWindows.vcxproj
    ├── UASTWindows.vcxproj.filters
    └── UASTWin.c            GUI 源码
```

- **UASTCmdup.exe** — 纯命令行提权引擎，可独立使用
- **UASTWindows.exe** — 图形界面，通过调用 UASTCmdup 完成提权

**两个 exe 编译后必须位于同一目录。**

---

## 编译

### 环境要求

| 项目 | 要求 |
|------|------|
| Visual Studio | 2019 或更高版本 |
| Windows SDK | 10.0 或更高 |
| 平台 | x64（推荐） |

### 注意

如果直接下载本仓库的源码，项目的编译配置已预设完成，
打开 `.slnx` 后点击上方菜单栏的`Debug`下拉框，将其改为`Release`即可直接按 F7 生成解决方案。

### UASTCmdup（控制台引擎）

右键项目 → 属性 → 配置为 **Release | x64**：

| 项目属性 | 值 |
|---------|-----|
| 配置类型 | 应用程序 (.exe) |
| 字符集 | 使用 Unicode 字符集 |
| C/C++ → 语言 → C 语言标准 | ISO C11 或更高 |
| C/C++ → 代码生成 → 运行时库 | 多线程 (/MT) |
| C/C++ → 命令行 → 其他选项 | /utf-8 |
| 链接器 → 系统 → 子系统 | 控制台 (/SUBSYSTEM:CONSOLE) |

### UASTWindows（GUI）

同样配置为 **Release | x64**：

| 项目属性 | 值 |
|---------|-----|
| 配置类型 | 应用程序 (.exe) |
| 字符集 | 使用 Unicode 字符集 |
| C/C++ → 语言 → C 语言标准 | ISO C11 或更高 |
| C/C++ → 代码生成 → 运行时库 | 多线程 (/MT) |
| C/C++ → 命令行 → 其他选项 | /utf-8 |
| 链接器 → 系统 → 子系统 | 窗口 (/SUBSYSTEM:WINDOWS) |

### 编译步骤

1. 用 Visual Studio 打开 `UASTWindows.slnx`
2. 顶部工具栏：配置选 **Release**，平台选 **x64**
3. 菜单 **生成 → 生成解决方案**（或按 F7）
4. 无特殊配置时，两个 exe 会输出到解决方案目录的 `x64\Release\` 下

### 添加公共模块

两个项目都需要引用 `Common/` 下的文件：

1. 右键项目 → **添加 → 现有项** → 选 `Common/json.c` 和 `Common/translation.c`
2. 项目属性 → **C/C++ → 常规 → 附加包含目录** → 填 `$(SolutionDir)Common`

---

## 使用

### 图形界面

双击 `UASTWindows.exe`，在界面中填写选项，点击 **执行提权**。

引擎会自动弹出独立控制台窗口显示日志，然后以指定身份启动目标程序。

### 命令行

```
UASTCmdup.exe [选项] 命令行
```

#### 选项列表

| 选项 | 值 | 说明 |
|------|-----|------|
| -U: | T | TrustedInstaller |
| | S | SYSTEM |
| | C | 当前用户（保持当前权限） |
| | E | 当前用户（提权为管理员） |
| -P: | E | 启用全部特权 |
| | D | 禁用全部特权 |
| -M: | S | System 完整性 |
| | H | High 完整性 |
| | M | Medium 完整性 |
| | L | Low 完整性 |
| -Priority: | RealTime | 实时 |
| | High | 高 |
| | AboveNormal | 高于正常 |
| | Normal | 正常 |
| | BelowNormal | 低于正常 |
| | Idle | 低 |
| -ShowWindowMode: | Show | 显示 |
| | Hide | 隐藏 |
| | Maximize | 最大化 |
| | Minimize | 最小化 |
| -CurrentDirectory: | 路径 | 子进程工作目录 |
| -Wait | — | 等待子进程结束 |
| -Version | — | 显示版本 |
| -? / -H / -Help | — | 显示帮助 |

**参数规则**：

- 选项不区分大小写
- 前缀 `-` / `--` / `/` 等价
- 分隔符 `:` / `=` 等价
- 选项必须位于目标命令行之前

#### 示例

```
:: 以 SYSTEM 身份运行命令提示符
UASTCmdup.exe -U:S cmd.exe

:: 以 TrustedInstaller 身份, 启用全部特权, System 完整性, 实时优先级
:: 运行命令并等待其结束
UASTCmdup.exe -U:T -P:E -M:S -Priority:RealTime -Wait cmd.exe /c whoami

:: 以管理员身份, High 完整性, 最大化窗口运行记事本
UASTCmdup.exe -U:E -M:H -ShowWindowMode:Maximize notepad.exe
```

---

## 常见问题

### Q1: 会被杀毒软件报毒吗？

会。

Windows Defender 及大多数杀软会对 UAC 绕过行为报警，通常报为：

- Behavior:Win32/UACBypassExp
- HackTool:Win32/Elevate
- Trojan:Win32/Wacatac.B!ml（机器学习误报）

这是预期行为。所有做 UAC 绕过的工具都会被这样拦——因为它的行为特征与恶意软件的提权手法完全相同。

**处理方式（任选其一）**：

1. 将程序所在目录添加到 Defender 的排除项
2. 在虚拟机中运行

### Q2: 第一次运行时被 Windows 拦截怎么办？

Windows 11 的智能应用控制（Smart App Control）会拦截未签名且不弹 UAC 的提权行为。

**处理方式（任选其一）**：

1. 手动确认一次：双击 `UASTCmdup.exe`，在弹出的对话框中点击"运行"或"仍要运行"。Windows 会记住这个决定，之后不再拦截。

2. 关闭智能应用控制：
   设置 → 隐私和安全性 → Windows 安全中心 → 应用和浏览器控制 → 智能应用控制 → 关闭

3. 开启开发者模式：
   设置 → 系统 → 面向开发人员 → 开发人员模式 → 开

4. 在虚拟机中运行。

5. 如果对话框中没有"运行"或"仍要运行"选项，可尝试用管理员权限运行几次，让 Windows 记住该程序并建立本地信誉。

6. 如果上述方法均不行，可尝试为 exe 制作自签名证书。步骤参考 [sign.txt](sign.txt)。**注意**：虽已证明自签名证书对智能应用控制**无效**，仅对部分杀软误报有缓解作用，不过作者测试也有一定的局限性，仅供参考。

### Q3: 为什么会有多次提权？

引擎内置了三级降级策略。如果第一级 fodhelper 因各种原因（Defender 拦截、系统补丁、时序问题）失败，会自动尝试第二级、第三级。

同时引擎内置了防重复机制：每次降级前会检查是否已存在同路径的管理员实例。若存在，则视为前一次实际已成功，不再降级。

### Q4: 支持哪些 Windows 版本？

Windows 7 及以上。

Windows 11 24H2 之后，fodhelper 绕过可能失效，届时程序会自动降级到 computerdefaults 或标准 UAC。

### Q5: 会不会修改我的系统？

不会持久化。程序只在提权时临时写入注册表：

```
HKCU\Software\Classes\ms-settings\Shell\Open\command
```

无论提权成功与否，都会在返回前清理。

程序不会：

- 修改系统目录
- 修改启动项
- 安装系统服务
- 创建计划任务
- 留下任何持久化痕迹

### Q6: 与其他提权工具有什么区别（如 NSudo）？

| 维度 | 其他提权工具 | UASTWindows |
|------|-------|-------------|
| UAC 弹窗 | 每次都弹 | 不弹 |
| SAC/杀软 | 放行 | 拦截（可手动确认后放行，具体见常见问题的Q2部分） |
| 界面 | 命令行 | 命令行 + 图形界面 |

其他提权工具 弹 UAC = 用户明确授权 = 安全机制放行。

UASTWindows 不弹 UAC = 绕过用户确认 = 安全机制拦截。

这是两种不同路径的取舍，没有绝对的好坏。

### Q7: 编译后两个 exe 不在同一目录？

默认情况下，两个项目的输出目录是：

```
UASTWindows\x64\Release\UASTWindows.exe
UASTWindows\x64\Release\UASTCmdup.exe
```

若您更改了配置，您需要手动把 `UASTCmdup.exe` 复制到 `UASTWindows.exe` 所在目录。

---

## 技术说明

### 提权策略

程序采用三级降级 UAC 绕过策略。

**第 1 级：fodhelper.exe**

`fodhelper.exe` 是 Windows 内置的 `autoElevate` 程序。启动时系统会自动给予管理员令牌，不弹 UAC 确认框。

它会读取 `ms-settings` 协议的处理程序，而该协议的注册表位置在 `HKCU` 下（普通用户可写），因此可以通过劫持该注册表项间接执行任意命令。

**第 2 级：computerdefaults.exe**

同样是 `autoElevate` 程序，同样读取 `ms-settings` 协议。作为 fodhelper 的备选。

**第 3 级：标准 UAC**

使用 `ShellExecuteEx` 的 `runas` 动词，弹出标准 UAC 确认框。作为最后手段。

### 新进程同步

为了避免轮询检测的时序竞态，程序使用命名事件同步：

1. 父进程创建命名事件 `Local\UASTEvt_<PID>_<Tick>`
2. 把事件名通过 `--notify` 参数写入注册表命令行
3. 启动 fodhelper/computerdefaults
4. 父进程用 `WaitForSingleObject` 等待事件被 SetEvent
5. 新进程启动后立即 `OpenEvent` + `SetEvent` 通知父进程

这样父进程能精确知道提权是否成功，不依赖进程扫描的采样时机。

### 令牌操作

| 目标身份 | 令牌来源 |
|---------|---------|
| 管理员 | 复制当前进程令牌 |
| SYSTEM | 复制 winlogon.exe 主令牌 |
| TrustedInstaller | 模拟 SYSTEM → 启动 TI 服务 → 复制 TrustedInstaller.exe 主令牌 |

---

## 代码来源与致谢

- 本项目代码为原创实现，未复制任何第三方开源项目的源码。
- 提权技术思路基于安全社区多年公开的研究（fodhelper 绕过、令牌复制等），API 调用遵循微软官方文档。
- 参考了作者自己之前开发的控制台项目 bd_all。
- 感谢 [UACME](https://github.com/hfiref0x/UACME) 项目所整理的公开技术资料。
- 感谢 [NSudo](https://nsudo.m2team.org) 为本程序提供命令行语法灵感。

不同开发者基于相同公开技术独立实现相似功能，属于正常现象。

---

## 免责声明

本工具仅供合法的系统管理与安全研究使用。

- 使用本工具需要一定的计算机技术，本程序默认您已有一定的计算机基础，且您知道您在干什么。
- 使用者需自行承担使用本工具产生的一切后果
- 禁止用于未授权的系统访问
- 禁止用于恶意软件的开发与传播
- 作者不对使用本工具造成的任何损失负责

下载或使用本工具即表示你已阅读并同意以上条款。

---

## 许可证

本项目使用 MIT with Attribution Requirement 许可证。

你可以自由使用、修改、分发本软件，包括商业用途。唯一的额外要求是：

在分发时须注明本项目基于 [明明月明月11019](https://space.bilibili.com/3707056078982013)（哔哩哔哩）的 UASTWindows 开发。

详见 [LICENSE](LICENSE) 文件。

---

## 反馈

- B 站: [明明月明月11019](https://space.bilibili.com/3707056078982013)
- 问题反馈请附上：
  - Windows 版本（`winver` 命令查看）
  - 复现步骤
  - 控制台日志（如果存在）

---

## 更新日志

### v1.0.0（初始发布）

- 三级降级 UAC 绕过
- 三种运行身份支持（管理员 / SYSTEM / TrustedInstaller）
- 令牌特权控制
- 完整性级别设置
- 进程优先级设置
- 窗口模式设置
- 命令行 + 图形界面双模式
- 命名事件新进程同步
- 防重复提权机制

### v1.0.1

- 修复：移除项目配置中残余的签名命令，解决部分用户无法编译的问题

### v1.1.0

- 优化了 [README文件](README.md) 的表述

### v2.0.0

- 添加了 [README文件](README.md) 的关于语言选项的表述
- 新增多语言支持，通过 `language.json` 切换界面和日志语言
- 新增 `-language` 命令行参数
- 新增 GUI 语言下拉框
- 首次运行自动生成 `language.json`（含中英双语）

**当前支持语言**：

| 语言 | 代号 |
|------|------|
| 简体中文 | `zh_cn` |
| English | `en_us` |