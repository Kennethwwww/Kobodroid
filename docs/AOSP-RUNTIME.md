# 从 AOSP 构建 Libra Colour 运行时

当前工作针对 Kobo Libra Colour N428 和固件 4.45.23646。源码构建使用 Android 7.0 ARM，基线为 `android-7.0.0_r24`，manifest 提交为 `fda0297eb5fe125c22119e7d0f809e9b63a99ee6`。构建使用独立的 x86_64 Linux 环境与 Java 8。 [AOSP 旧版本构建环境](https://source.android.com/docs/setup/start/older-versions) · [固定版本 manifest](https://android.googlesource.com/platform/manifest/+/android-7.0.0_r24/default.xml)

## 构建基础产物

```sh
docker build --platform linux/amd64 -t kobodroid-aosp-nougat release/aosp
docker volume create kobodroid-aosp-r24
docker run --rm --platform linux/amd64 -v kobodroid-aosp-r24:/aosp \
  kobodroid-aosp-nougat chown builder:builder /aosp
docker run --rm --platform linux/amd64 --user builder -v kobodroid-aosp-r24:/aosp \
  -e GIT_AUTHOR_NAME=Kobodroid -e GIT_AUTHOR_EMAIL=build@localhost \
  kobodroid-aosp-nougat bash -c '/opt/kobodroid/aosp/build.sh sync && /opt/kobodroid/aosp/build.sh'
```

同步会写出 `resolved-manifest.xml`，记录各子项目的实际提交。构建使用 ARM `aosp_arm-userdebug` 目标和 64 位 Binder 协议。构建后检查系统镜像的架构、API 和产品名，对照 ramdisk 内外的 init 并确认源码补丁进入二进制。运行时应从这一套产物提取系统镜像和 ramdisk，并保留 AOSP 生成的通知和构建记录。

构建脚本为容器内的旧版 Jack 服务提供本地 TLS 兼容设置。使用 x86 翻译层运行宿主工具时，ART 可在系统忽略 `MAP_32BIT` 的情况下重试低地址分配。构建预编译 Android 启动镜像，应用由设备上的 Android 运行时处理。

`patch_init.py` 用源码改动替换旧的 init 二进制补丁。生成 AOSP 专用启动脚本时，`adapt_setup.py` 保留原有设备探测，仅在探测结果允许时打开属性文件标签兼容。开发期间继续保留旧的实机启动路径。

## 集成与发布验收

- 从同一次构建取得 init、ramdisk、框架和平台签名。移植 Kobo 的框架改动，重建对应 ABI 的 Binder、图形和电源相关组件。逐项确认启动包内的依赖来源。
- 记录所有实际随包文件的许可证和对应源码。Nougat 的基础目标仍引用预编译 Chromium WebView，上游 README 记录了 Chromium 版本并指出 Nougat 专用 Java 源码差异。公开包需要提供可追溯的对应源码，或使用经验证的替换构建。 [AOSP WebView 构建记录](https://android.googlesource.com/platform/external/chromium-webview/+/android-7.0.0_r24/README)
- 在电脑上验证镜像、启动包、发布清单、USB 首装和升级。然后在同一台 Libra Colour 完成空白镜像启动、应用打开、正常退出和快速重进。USB 安装包沿用已有的压缩空白数据镜像方案。

## Kobo 源码集成

基础构建通过后，在同一构建容器把项目源码放到 `/tmp/kobodroid`，再运行：

```sh
bash /tmp/kobodroid/release/aosp/integrate.sh
```

这一步会生成 Nougat AIDL 对应的网络服务，应用离线启动所需的 Java 守卫，并构建 gralloc、GLES 分发、电池服务、共享内存和时钟兼容组件。打包时把这些组件与同一份系统镜像配套使用。

## 构建和替换 WebView

Chromium `119.0.6045.199` 固定提交为 `f6fcaed63cb61e2eeff44a3268ac6fbaaa2a8f88`。该版本的配置支持 API 24，并提供 `com.android.webview` 的独立构建目标。先用匹配的 depot_tools 和该提交的 DEPS 同步 Android 构建依赖，再运行：

```sh
bash release/webview/build.sh /path/to/chromium/src
```

构建配置使用 ARM 32 位并关闭专有编解码器、官方 Google API 密钥和 Widevine。发布材料保留源码修订、GN 配置和第三方许可证通知。 [Chromium 119 WebView 构建说明](https://chromium.googlesource.com/chromium/src/+/refs/tags/119.0.6045.199/android_webview/docs/build-instructions.md)

用 `release/webview/stage_apk.py` 将编译所得 APK 放进 AOSP WebView 模块后重新生成系统镜像。工具会检查包名、最低 API 和 ARM ELF，并拒绝包含原预编译 WebView 的 APK。Kobo 上的 WebView 兼容性仍属于最终验收项。

## 安装包和电脑端检查

`release/aosp/finish-local.sh` 串接编译、签名与打包。它从 AOSP 提取实际镜像与通知，确认镜像内的 WebView 使用本次编译的 ARM 库，并校验 Android 7 能验证 APK 签名。安装包沿用 NickelMenu 入口和压缩的 3.99 GiB 空白数据镜像。

脚本在模拟 Kobo USB 卷检查首装、升级和数据保留。然后在 1 GiB 的 ARM QEMU 中检查空白镜像启动和快速重进，核对 Android 桌面帧、按键传递、会话退出与进程清理。离线测试应用在 WebView 中载入 HTML 并验证 JavaScript 返回值。QEMU 需要兼容的 ARM 实验内核，可用 `KOBO_LAB_KERNEL` 指定路径。

实机验证在一轮内覆盖首装、首次进入、应用和 WebView、正常返回 Kobo 和快速重进。USB 和墨水屏驱动行为以 Libra Colour 上的结果为准。
