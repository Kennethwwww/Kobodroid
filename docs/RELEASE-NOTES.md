# Kobodroid v0.1.0-preview.1

Kobo Libra Colour N428 的首个预览版支持固件 **4.45.23646**，运行时为 V74-AOSP。

- 通过 NickelMenu 启动 Android 7，从控制中心返回 Kobo。
- 支持触控、实体翻页键、彩色墨水屏及 Wi-Fi。应用可通过 USB 拷贝 APK 安装。
- 安装包已包含压缩的空白 Android 数据镜像，首次安装会自动展开到约 3.99 GiB。升级保留已有应用数据。
- 长时间阅读和部分应用仍可能触发内存保护，Kobodroid 会结束 Android 会话并回到 Kobo。后台通知、蓝牙音频和其他固件暂未验证。

V74-AOSP 使用固定版本的 AOSP 构建系统、框架和图形兼容模块，并使用独立编译的 Chromium ARM WebView。

修复了 Kobo 原生界面遗留的文件描述符导致 Android 卡在 88% 的问题。若关键进程反复崩溃，启动器会保存日志并自动回到书架。

Libra Colour 实机用空白镜像首次进入桌面约需 114 秒，复用同一镜像再次进入约需 18 秒。两次均通过控制中心正常返回书架。相同运行时另已验证 WebView 的网页渲染、JavaScript 和 HTTPS 联网。

下载 `Kobodroid-v0.1.0-preview.1-N428.zip` 并按包内的 `INSTALL.md` 安装。其他 ZIP 提供项目及第三方源码。
