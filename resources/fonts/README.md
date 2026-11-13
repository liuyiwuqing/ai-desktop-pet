# 消息界面字体

Noto Sans SC Regular，SIL Open Font License 1.1，完整授权见 `OFL.txt`。

- 16 px 正文、24 px 标题，2 bpp，无字距表、未压缩，覆盖 GB2312 汉字、ASCII 与常用标点，共 6882 字符。
- 原字体来自 LVGL v9.5.0 测试资源中的 `NotoSansSC-Regular.ttf`，来源 URL、原字体 SHA-256、生成器版本及输出 SHA-256 见 `manifest.json`。
- 生成：`python3 tools/generate_fonts.py /absolute/path/NotoSansSC-Regular.ttf`。构建只读二进制资源，无需 npm 或下载字体。
- S5-D 暂嵌入应用，关闭 UI 时不链接资源；后续 AI 镜像预算需评估迁入 `assets` 分区。
- 不覆盖全部 Unicode；UI 对缺字/非法 UTF-8 显示方框并记录数量，不记录正文。
