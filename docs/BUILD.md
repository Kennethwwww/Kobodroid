# 构建 Kobodroid

从源码生成公开运行时的构建流程见 [AOSP 运行时](AOSP-RUNTIME.md)。

| 目录 | 内容 |
| --- | --- |
| `kobo-prototype/session` | 墨水屏显示、触控与按键、电源和联网、会话脚本、Android 服务补丁 |
| `kobo-prototype/batch/src` | Android 会话运行器、兼容层和打包脚本 |
| `kobo-prototype/launcher` 与 `overlay-framework` | 启动器、输入法、Android 框架资源覆盖 |
| `kobo-prototype/gles` | GLES 转发与 Android 信号兼容 |
| `android7-prototype` | Kobo 运行时依赖的共享协议、HAL、启动配置和兼容代码 |
| `release` | USB 安装器、部分原生组件构建和完整安装包组装脚本 |

## 构建项目组件

安装 Zig 后运行 `sh release/build-native.sh`，产物在 `build/native`。它构建 runner、面板、截帧程序和信号代理。原生组件以 32 位 ARM 为目标。

启动器和资源覆盖的入口是 `kobo-prototype/launcher/build.sh`。它使用 Android SDK Build Tools 35.0.1、API 31 的 `android.jar`，以及 Android 7 对应的 `framework-res.apk` 和系统镜像使用的 AOSP 平台测试签名。将本地输入放入 `kobo-prototype/build/aosp-platform-key`，其中框架资源命名为 `fwres.apk`。会话输入组件可用 `kobo-prototype/session/build-input.sh` 构建。现有 rootfs 和 services 构建脚本还依赖原开发容器和中间产物。

## 组装历史 V73 本地安装候选

用现成 V73 运行时、经校验的最小化 Android 系统镜像及其通知文件组装。脚本会创建全新的 ext4 数据种子，计算 SHA-256 并验证压缩包。

```sh
python3 release/build-release.py \
  --runtime /path/to/v73-runtime \
  --system /path/to/system.img \
  --android-notice /path/to/NOTICE.txt \
  --output Kobodroid-v0.1.0-preview.1-N428-local-RC.zip
```

电脑需安装 Python 3.9 或更新版本和 e2fsprogs，可通过 `--mke2fs` 指定其路径。公开安装包采用 V74-AOSP 构建流程。 [分发说明](DISTRIBUTION.md)

## 从本地 SDK 压缩包提取构建输入

使用已单独取得的 Android SDK ARM API 24 r07 ZIP。脚本会验证原始压缩包和 system.img 的 SHA-256，并提取 system.img、ramdisk.img 和原始通知。

```sh
python3 release/extract_sdk_input.py /path/to/armeabi-v7a-24_r07.zip /path/to/sdk-input
```

提取的是原始系统镜像。上面的 V73 本地打包脚本固定使用已在 Kobo 验证过的缩容镜像，后续仍要准备对应的启动包和服务覆盖。 [Android SDK 条款](https://developer.android.com/studio/terms)

## 从本地 SDK 镜像重建 framework 服务覆盖

安装 Java、e2fsprogs 和 smali/baksmali 及其运行依赖。现有验证使用 Ubuntu/Debian 的 smali 打包和 Java 17。将包含 smali、baksmali、dexlib2、util、Guava、JCommander、ANTLR runtime 的 JAR 目录传给脚本。用新文件名接收输出。

```sh
python3 release/build-sdk-services.py /path/to/sdk-input/system.img \
  /path/to/services-offline.jar --classpath '/path/to/smali-jars/*'
```

脚本也接受已校验的 V73 缩容镜像。它从本地镜像解包并反优化服务 DEX，应用 Kobo 的框架、内存通知和网络补丁，再组装服务覆盖。在工作区验证中，重新反汇编生成的 DEX 后，与 V73 实机候选的所有 smali 文件逐字节一致。DEX 和 ZIP 文件的校验值可能因汇编排序或打包元数据而不同。
