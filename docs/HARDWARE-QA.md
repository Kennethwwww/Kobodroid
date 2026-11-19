# Libra Colour 空白镜像验收

选用 Kobo Libra Colour N428，固件 4.45.23646，准备已安装的 NickelMenu 和验证过的 KOReader FBInk。使用同一轮测试完成首次安装、启动、退出和重启。

1. 从 Android 正常返回 Kobo 书架，接 USB 并进入连接电脑模式。有原有安装时，将 `.adds/kobodroid` 和 `.adds/nm/kobodroid` 分别改名保存到现有目录之外，并备份 Kobo 书籍和数据。为首装预留 6 GiB。记录安装包 SHA-256。
2. 按安装指南运行安装器。确认新生成的 `.adds/kobodroid/data.img` 约为 3.99 GiB，再安全弹出。
3. 从 NickelMenu 进入 **Kobodroid**，等到 Android 桌面。打开设置验证触控，有准备兼容阅读应用时打开应用内网页检查 WebView。随后用控制中心的 **返回 Kobo** 返回原生书架。
4. 再启动一次。确认应用数据镜像可复用，返回书架后重新接 USB，读取新生成的两份报告。核对 `ANDROID_DEFAULTS_INITIALIZED=1`、第二次启动的 `ANDROID_DEFAULTS_REUSED=1`、两轮的 `LAUNCHER_BRIDGE=READY`、`SESSION_INPUT_CONNECTED=1`、`NICKEL_RESUME=PASS`、`SESSION_CLEANUP=PASS remaining=0` 和 `ENTRY_COMPLETE=1`。
5. 需恢复原安装时，将本次验证目录移出 `.adds/kobodroid` 后把原目录和原菜单配置放回。保留测试报告用于诊断。

验收通过后，再使用正式发布资产进行一次校验核对。

## V74-AOSP 实机记录

2026 年 11 月 19 日在 N428、固件 4.45.23646 上完成。电脑端安装器先在空白卷上校验，真机测试目录使用同一发布包的运行时和数据镜像，并逐一核对 SHA-256。

| 项目 | 结果 |
| --- | --- |
| 用包内空白镜像首次启动 | 约 114 秒到桌面，Android 默认设置完成初始化 |
| 返回 Kobo | 通过 SSH 模拟触控，在控制中心确认退出，原生界面恢复且 Android 会话进程清理完毕 |
| 使用同一镜像再次启动 | 约 18 秒到桌面，沿用首次启动生成的设置 |
| 再次返回 Kobo | 控制中心退出成功，原生界面恢复且 Android 会话进程清理完毕 |

两次都通过 SSH 执行 NickelMenu 使用的同一启动命令。本轮通过电脑上的独立测试卷检查 USB 安装器，真机测试使用 SSH。
