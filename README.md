# TR2013 Assist

An unofficial, open-source Windows utility for the single-player PC version of
*Tomb Raider (2013)*. It provides version-gated diagnostics, optional infinite
ammunition, and an optional aim-and-fire helper.

[English](#english) | [简体中文](#简体中文)

> [!IMPORTANT]
> This is an independent, non-commercial fan project. It is not affiliated with,
> sponsored by, approved by, or endorsed by Crystal Dynamics, Square Enix, or any
> other rights holder. It contains no game executable, game code, artwork, audio,
> logo, DRM bypass, or extracted game asset. “Tomb Raider” is used only to identify
> compatibility. Use it only with a legitimately obtained copy, only in
> single-player, and subject to the game's EULA and applicable law.

## English

### Features

- Reads Arrow, Pistol, Rifle, and Shotgun ammunition values.
- `P`: toggle infinite ammunition.
- `O`: toggle the aim-and-fire helper (available only while `P` is on).
- `F12`: immediately disable all features and release simulated mouse buttons.
- `Shift+F12`: disable all features and exit.
- Pauses memory writes and simulated input while the game is not in the foreground.
- Validates the supported game build before enabling any write-capable mode.
- Includes single-instance protection, an invisible safety watchdog, and a strictly
  read-only diagnostic mode.

All optional features start **OFF**. The application does not patch or replace any
game file.

### Compatibility

| Item | Supported configuration |
| --- | --- |
| Game | *Tomb Raider (2013)* for Steam |
| Tested version | `1.01` |
| Operating system | 64-bit Windows 10 or Windows 11 |
| Application build | x64 |

The current pointer paths are specific to the tested executable. Other stores,
regions, modified executables, and future updates may not work. If the signature
check fails, the application reports `STATUS: NOT READY` and does not enable
write-capable features.

### Download and verification

Download compiled packages only from this repository's
[Releases](https://github.com/ruoxi3450/TR2013-Assist/releases) page. Each release
should include a ZIP and its SHA-256 checksum. Avoid third-party repack sites.

```powershell
Get-FileHash .\TR2013_Assist_v0.1.0-rc1.zip -Algorithm SHA256
```

Compare the result with the checksum published on the matching Release page.

### Use

1. Back up important save files.
2. Start the game and load a controllable single-player save.
3. Close Cheat Engine, AutoHotkey scripts, and similar tools.
4. Run `TR2013_Assist.exe`; administrator rights are not normally required.
5. Confirm that the console shows `STATUS: READY`.
6. Use `P`, `O`, `F12`, and `Shift+F12` as described above.

Command-line modes:

```powershell
.\TR2013_Assist.exe --diagnostic     # strictly read-only
.\TR2013_Assist.exe --assist         # integrated mode; also the default
.\TR2013_Assist.exe --infinite-ammo  # infinite ammunition only
.\TR2013_Assist.exe --write-once     # one confirmed 4-byte developer test write
```

`--write-once` is for controlled development testing. It requires an explicit
target, a value from `0` to `999`, and the uppercase confirmation word `WRITE`.

### Build from source

Install Visual Studio 2022 with **Desktop development with C++**, MSVC v143, and a
Windows 10 or 11 SDK. Open `TR2013_Assist.sln`, select `Release` and `x64`, then
choose **Build > Build Solution**. The output is under `x64\Release\`. Release uses
the static MSVC runtime (`/MT`).

### Safety and limitations

- Use only in single-player; never use it in multiplayer, competitive, online, or
  anti-cheat-protected environments.
- Memory modification can cause crashes, unexpected behavior, or save corruption.
  Keep backups and use at your own risk.
- Antivirus products may flag memory-access or input-simulation tools by behavior.
  Review the source and build it yourself if unsure.
- This project does not claim compliance with every game license or jurisdiction.
  A disclaimer does not replace the game EULA or permission from a rights holder.

### Repository contents

```text
TR2013_Assist.sln
TR2013_Assist/
├── main.cpp
├── TR2013_Assist.rc
├── TR2013_Assist.vcxproj
└── TR2013_Assist.vcxproj.filters
```

Build output, caches, memory dumps, Cheat Engine tables, AutoHotkey scripts, game
files, and release packages are intentionally excluded from the source repository.

### Legal notice and license

This repository contains only original project source and metadata. It does not
distribute or grant rights to *Tomb Raider*, its executable, or related copyrighted
content or trademarks. *Tomb Raider*, Lara Croft, and related marks belong to the
Crystal Dynamics group of companies and/or their respective owners.

The MIT License applies only to original TR2013 Assist code and documentation. It
does not grant a license to third-party games, brands, assets, or other intellectual
property. Users are responsible for the applicable game EULA, platform terms, and
local law. Rights holders may contact the maintainer through a repository Issue.

Official references:

- [Tomb Raider official website](https://www.tombraider.com/)
- [Square Enix West Material Usage Policy](https://www.square-enix-games.com/en_AU/documents/materialusagepolicy)

Copyright (c) 2026 Snse. Original project code and documentation are released under
the [MIT License](LICENSE).

---

## 简体中文

### 项目简介与功能

TR2013 Assist 是一款面向 Windows 的非官方开源工具，仅用于
*Tomb Raider (2013)* PC 单机模式。它能够读取四种弹药数值，并提供：

- `P`：开启或关闭无限弹药。
- `O`：开启或关闭自动瞄准射击；只有 `P` 开启时才可用。
- `F12`：立即关闭全部功能并释放程序模拟按下的鼠标键。
- `Shift+F12`：关闭全部功能并退出程序。
- 游戏不在前台时自动暂停写入和模拟输入。
- 游戏版本校验、单实例保护、隐藏安全 Watchdog 和严格只读诊断。

所有可选功能启动时均为 **OFF（关闭）**。程序不会修改或替换游戏文件。

### 兼容性

| 项目 | 已支持配置 |
| --- | --- |
| 游戏 | Steam 版 *Tomb Raider (2013)* |
| 已测试版本 | `1.01` |
| 操作系统 | 64 位 Windows 10 或 Windows 11 |
| 程序架构 | x64 |

当前指针路径只适用于已测试的可执行文件。其他商店、地区、修改版本或未来更新
可能无法使用。签名检查失败时，程序显示 `STATUS: NOT READY`，不会开启写入功能。

### 下载、校验与使用

请只从本仓库的 [Releases](https://github.com/ruoxi3450/TR2013-Assist/releases)
页面下载编译包，并用发布页提供的 SHA-256 校验值验证 ZIP：

```powershell
Get-FileHash .\TR2013_Assist_v0.1.0-rc1.zip -Algorithm SHA256
```

使用步骤：

1. 备份重要存档。
2. 启动游戏并进入可以操作角色的单机存档。
3. 关闭 Cheat Engine、AutoHotkey 脚本和类似工具。
4. 运行 `TR2013_Assist.exe`，通常不需要管理员权限。
5. 确认控制台显示 `STATUS: READY`。
6. 使用上文列出的快捷键。

命令行模式：

```powershell
.\TR2013_Assist.exe --diagnostic     # 严格只读
.\TR2013_Assist.exe --assist         # 完整模式，也是双击时的默认模式
.\TR2013_Assist.exe --infinite-ammo  # 仅无限弹药
.\TR2013_Assist.exe --write-once     # 一次经确认的 4-byte 开发测试写入
```

`--write-once` 只用于受控开发测试：必须选择明确目标、输入 `0` 到 `999` 的值，
并手动输入大写 `WRITE` 才会执行。

### 从源码编译

安装 Visual Studio 2022 的**使用 C++ 的桌面开发**工作负载、MSVC v143 和
Windows 10/11 SDK。打开 `TR2013_Assist.sln`，选择 `Release`、`x64`，然后点击
“生成”→“生成解决方案”。输出位于 `x64\Release\`，Release 使用静态运行库 `/MT`。

### 安全提示与限制

- 仅限单机；不要在多人、竞技、联网或受反作弊保护的环境中使用。
- 内存修改可能造成崩溃、异常行为或存档损坏，请先备份并自行承担风险。
- 杀毒软件可能根据内存访问或模拟输入行为作出启发式警报；如有疑虑，请审查
  源码并自行编译。
- 本项目不声称符合每一份游戏许可协议或每个地区的法律。README 免责声明不能
  代替游戏 EULA，也不能代替权利人授权。

### 法律与权利声明

这是社区独立制作的非官方、非商业项目，与 Crystal Dynamics、Square Enix 或
其他权利人不存在隶属、赞助、批准或背书关系。“Tomb Raider”仅用于描述兼容性。
本仓库不包含游戏可执行文件、游戏代码、美术、音频、Logo、DRM 绕过工具或游戏
提取资源。用户必须合法取得游戏，并遵守适用的 EULA、平台条款和当地法律。

*Tomb Raider*、Lara Croft 及相关商标属于 Crystal Dynamics 集团公司和/或其
各自权利人。MIT 许可证只适用于 TR2013 Assist 的原创代码与文档，不授予任何
第三方游戏、品牌、素材或其他知识产权许可。权利人可通过仓库 Issue 联系维护者。

官方参考：

- [Tomb Raider 官网](https://www.tombraider.com/)
- [Square Enix West 素材使用政策](https://www.square-enix-games.com/en_AU/documents/materialusagepolicy)

Copyright (c) 2026 Snse。本项目原创代码和文档使用 [MIT License](LICENSE) 发布。
