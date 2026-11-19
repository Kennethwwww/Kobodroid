# 首个 GitHub Release 的分发状态

发行标签为 `v0.1.0-preview.1`，安装包运行时为 V74-AOSP。Kobodroid 原创代码采用 Apache-2.0。第三方组件各自的许可证、署名和源码记录随对应资产分发。

| 组件 | 来源与发布处理 |
| --- | --- |
| Kobodroid 原创源码 | 原创代码采用 Apache-2.0。仓库根目录提供 LICENSE 和 NOTICE。第三方文件沿用各自的许可与署名。 |
| BusyBox | 二进制已与 Ubuntu Noble ARMHF 包逐字节比对。对应源码 ZIP 已备好，含上游源码、Ubuntu 补丁、配置、构建规则和 GPLv2 文本。公开分发二进制时一起上传。 |
| KOReader FBInk 与 NickelMenu | 由用户从各自上游安装。候选包仅记录 FBInk 的校验值。 |
| Kobo 拼音与面板字形 | Rime 的 Apache-2.0 许可证、字典来源说明和 Noto Sans CJK 的 OFL 通知已补入。合并前的 KOReader 字典已匹配到 v2025.10，该文件标注来源为 Rime pinyin-simp，适用 Apache-2.0。 |
| SwiftShader 和 compiler-rt | 记录了实际构建的提交，保留构建脚本、兼容头和 SwiftShader 作者名单。通知包含 Subzero、LLVM 子集和 compiler-rt 的许可文本。 |
| V74-AOSP 系统与启动 | 从固定 AOSP 提交构建 init、Binder、框架、本地图形组件和系统镜像。打包时保留解析后的源码清单及编译产物生成的通知。 |
| V74-AOSP WebView | 从固定的 Chromium 源码构建 ARM 版本，随包放入构建参数、源码依赖修订及来自目标依赖图的第三方通知。打包验证镜像内的 ARM 库与该编译产物一致。 |
| AOSP 和 WebView 的附带源码 | ARM 运行时源码包包含 AOSP GNU 许可组件、上游 ARM gdbserver 所用的源码与构建记录、WebView 对应的 FFmpeg 源码。完整 AOSP 与 Chromium 构建输入由固定版本清单记录。 |

## GitHub Release 资产

- `Kobodroid-v0.1.0-preview.1-N428.zip` 提供安装器、安装说明、运行时、压缩数据镜像和第三方通知。
- `Kobodroid-v0.1.0-preview.1-source.zip` 提供 Kobodroid 源码、构建工具和依赖记录。
- `Kobodroid-v0.1.0-preview.1-busybox-source.zip` 与 `Kobodroid-v0.1.0-preview.1-arm-runtime-source.zip` 随二进制一起上传。
- `SHA256SUMS` 核对全部 ZIP。`RELEASE-NOTES.md` 用作发布说明。

安装者下载对应机型的安装 ZIP 即可。构建与验收记录见 [AOSP 运行时](AOSP-RUNTIME.md) 和 [首装验证](HARDWARE-QA.md)。
