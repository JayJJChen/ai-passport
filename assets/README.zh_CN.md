<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 考拉旅行资源

| 文件 | 集成方式与来源 |
| --- | --- |
| `images/koala-sprite-source.png`、`images/koala-{wave,nod,point,walk}-source.png` | 已确认的红色无图案鸭舌帽考拉透明原画。四份动作原画均为 2 × 2 图集，由内置图像工具按用户确认的角色生成；不声称存在第三方再分发许可。 |
| `images/koala_frames.bin`、`images/koala_frames.bin.manifest.json` | 确定性生成、只读嵌入固件的 20 帧 144 × 144 RGB565A8 图集：挥手 0–3、点头 4–7、右指 8–11、镜像左指 12–15、走路 16–19。 |
| `packs/shanghai/background.png` | 内置工具生成的上海插画，转换为 240 × 320 小端 RGB565。 |
| `fonts/NotoSansSC-SemiBold.ttf`、`fonts/OFL.txt` | [Google Fonts Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)，SIL Open Font License 1.1；使用 fontTools 从原始可变字体导出字重 600 的静态字体，仅保留转换工具需要的静态源文件。 |
| `fonts/ui-24.txt`、`fonts/travel_ui_font_24.c`、`fonts/travel_ui_font_36.c` | 系统中文字符与可打印 ASCII 使用 24 px，固定家长页标题使用 36 px。半粗、2 位像素、无压缩和字距调整。 |
| `packs/shanghai/cards.json`、`cards.klp`、`cards.klp.manifest.json` | 编辑文案、完整卡包和 SHA-256 身份记录。精确的 28/36 px 字体子集包含在卡包内部，通过 LVGL 内存文件系统加载；转换中间文件自动删除。 |

重现使用 Pillow 11.3.0、fontTools 4.60.1 和 `lv_font_conv` 1.5.3：

```text
python tools/prepare_travel_assets.py --converter /path/to/lv_font_conv/lv_font_conv.js
python tools/pack_koala_frames.py verify
python tools/pack_cards.py build assets/packs/shanghai/cards.json assets/packs/shanghai/cards.klp --converter /path/to/lv_font_conv/lv_font_conv.js
```

图像生成要求保存在 `images/koala-art-prompts.txt`。
内容限制、手动 USB 更新、字形覆盖和真机检查见[应用说明](../docs/development/koala-travel.zh_CN.md)。
