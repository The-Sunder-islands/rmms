<div align="center">
    <h1>
    <img src="https://raw.githubusercontent.com/LMMS/artwork/master/Icon%20%26%20Mimetypes/lmms-64x64.svg" alt="RMMS Logo"><br>RMMS
    </h1>
    <p>Rip & MultiMedia Studio</p>
    <p>
        <a href="https://lmms.io/">上游项目 LMMS 官网</a>
        ⦁︎
        <a href="https://github.com/LMMS/lmms/releases">上游 LMMS 发行版</a>
        ⦁︎
        <a href="https://github.com/LMMS/lmms/wiki">上游 LMMS 开发者 Wiki</a>
        ⦁︎
        <a href="https://lmms.io/documentation">LMMS 官方用户手册</a>
        ⦁︎
        <a href="https://lmms.io/showcase/">LMMS 作品展示厅</a>
        ⦁︎
        <a href="https://lmms.io/lsp/">LMMS 资源分享平台</a>
    </p>
    <p>
        <a href="https://github.com/LMMS/lmms/actions/workflows/build.yml"><img src="https://github.com/LMMS/lmms/actions/workflows/build.yml/badge.svg" alt="上游 LMMS 构建状态"></a>
        <a href="https://github.com/LMMS/lmms/releases"><img src="https://img.shields.io/github/release/LMMS/lmms.svg?maxAge=3600" alt="上游 LMMS 最新稳定版"></a>
        <a href="https://github.com/LMMS/lmms/releases"><img src="https://img.shields.io/github/downloads/LMMS/lmms/total.svg?maxAge=3600" alt="上游 LMMS GitHub 总下载量"></a>
        <a href="https://discord.gg/3sc5su7"><img src="https://img.shields.io/badge/chat-on%20discord-7289DA.svg" alt="加入 LMMS 官方 Discord 交流"></a>
        <a href="https://www.transifex.com/lmms/lmms/"><img src="https://img.shields.io/badge/localise-on_transifex-green.svg" alt="LMMS 本地化项目"></a>
    </p>
</div>

---

## 语言 / Language

- [简体中文](README.md)
- [English](README.en.md)（待更新）

---

## 关于 RMMS

RMMS 是一个处于早期开发阶段、以 AI 音频分轨为核心方向的开源数字音频工作站，基于 LMMS 成熟稳定的音乐制作底座构建，计划原生集成 Demucs 开源音频分离模型。

我们的核心设计思路是将 AI 推理模块与 DAW 主程序彻底解耦，通过标准化网络协议实现通信，摆脱传统闭源软件的架构绑定与版本限制。我们希望能提供一个开源免费、无功能阉割的 RipX DAW 平替方案，让更多人可以低成本获得专业级的 AI 音频处理能力。

---

## 已具备的基础能力（继承自 LMMS 上游）

- 歌曲编辑器：用于编排旋律、采样、音型和自动化参数
- 音型编辑器：用于创建节拍和音乐片段
- 易用的钢琴卷帘：用于编辑音型和旋律
- 混音器：支持无限数量的混音通道，可添加任意数量的效果器
- 开箱即用的多款强大乐器和效果插件
- 完整的用户自定义轨道自动化，以及计算机控制的自动化源
- 兼容多种行业标准：SoundFont2、VST2（乐器和效果器）、LADSPA、LV2、GUS 音色补丁，以及完整的 MIDI 支持
- 支持 MIDI 文件的导入与导出

---

## 正在开发的核心功能

- 通过特定网络协议与主程序连接的 AI 多轨分离模块，目前基于 Demucs 实现，支持后端远程部署；只要确保协议兼容，未来可随技术演进灵活替换底层实现
- 适配新增功能的软件 UI 重制与交互体验优化
- 底层架构优化，以及更多面向音频工作流的创新功能迭代

---

## 编译构建

目前项目仅支持编译上述基础 DAW 功能，AI 分轨功能尚未集成到主程序中，将在后续版本逐步合并。
当前基础功能的编译流程与上游 LMMS 完全一致，详细编译指南请见 [上游 LMMS 项目 Wiki](https://github.com/LMMS/lmms/wiki/)。

---

## 参与贡献

这是一个非常早期的开源项目，任何形式的贡献都非常欢迎：

- 代码贡献：UI 优化、功能开发、构建流程完善
- 测试反馈：基础 DAW 功能的测试与 Bug 报告
- 文档贡献：完善编译指南、使用教程与项目文档
- 建议反馈：提出你对 AI 分轨功能与整体产品的想法

在开发大型新功能前，请先提交 Issue 讨论你的构想与实现方案，避免重复劳动。

---

## 致谢

- 感谢 LMMS 社区二十余年的持续开发，为我们提供了成熟可靠的开源 DAW 底座
- 感谢 Demucs 项目，提供了业界领先的开源音频分离模型
- 感谢 Hit'n'Mix 团队开发的 RipX DAW，为我们提供了宝贵的灵感
- 感谢所有开源社区的开发者，为这个项目提供了坚实的技术基础
