# 在 Kobo Libra Colour 安装 Kobodroid

适用设备为 Kobo Libra Colour N428，固件 **4.45.23646**。需要电脑上的 Python 3.9 或更新版本和 USB 数据线。首次安装建议在 Kobo 上预留 **6 GiB**，安装器会根据镜像大小检查实际所需空间。Kobodroid 通过 NickelMenu 启动 Android 7 会话，退出后回到 Kobo 书架，无需刷写 Android 固件。

1. 备份 Kobo 上的书籍和个人资料。按上游说明安装 NickelMenu 和 **KOReader v2025.10 Kobo 版**。安装器会核对已验证的 FBInk 文件。 [NickelMenu 安装](https://pgaskin.net/NickelMenu/) · [KOReader 安装](https://github.com/koreader/koreader/wiki/Installation-on-Kobo-devices) · [KOReader v2025.10](https://github.com/koreader/koreader/releases/tag/v2025.10)
2. 下载并解压 **Libra Colour 安装包** `Kobodroid-v0.1.0-preview.1-N428.zip`，进入解压目录。约 3.99 GiB 的数据镜像已压缩在包内，安装时会自动展开。把 Kobo 连到电脑，并在屏幕上选择连接电脑。
3. 运行与你的电脑对应的命令。Windows 将 `E:\` 换成 Kobo 的实际盘符。Linux 将路径换成实际挂载点。

   **macOS**

   ```sh
   python3 install.py /Volumes/KOBOeReader
   ```

   **Windows PowerShell**

   ```powershell
   py -3 install.py E:\
   ```

   **Linux**

   ```sh
   python3 install.py "/media/$USER/KOBOeReader"
   ```

4. 安装成功后安全弹出设备，在 NickelMenu 选择 **Kobodroid**。首次启动需要初始化 Android 数据。进入桌面后可通过控制中心的 **返回 Kobo** 退出。
5. 安装应用时，把兼容 Android 7 和 ARM 32 位的 APK 放入 Kobo USB 卷根目录下的 `Kobodroid-APK`。下次启动 Kobodroid 会自动安装。

## 更新和卸载

更新前先退出 Android，再连接 USB 并运行新版安装器。已有的 `data.img`、设置和时区会保留。备份应用数据时，在 Android 已退出的情况下复制 `.adds/kobodroid/data.img`。

要移除启动入口，在 Kobo USB 卷上删除 `.adds/nm/kobodroid`。若还要回收空间，备份所需的 Android 数据后删除 `.adds/kobodroid`。该目录包含应用和应用数据。NickelMenu、KOReader 与 Kobo 书籍可继续使用。

若启动失败，请保留 `.adds/kobodroid/reports` 中最新一份日志。公开发帖前删去设备网络地址、个人路径等信息。
