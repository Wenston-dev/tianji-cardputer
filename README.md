# 天机 TianJi

**M5Stack Cardputer ADV 掌上固件：余生倒计时 · 小六壬 / 求签 / 掷筊 · 3D 骰子与硬币 · SD 卡长录音 · 番茄钟 · Kindle 翻页器**

[English](README.en.md) · [技术笔记](docs/NOTES.md) · [第三方声明](NOTICE.md)

![界面一览](docs/gallery.png)

<sub>上图由 `tools/preview` 用**同一份界面代码**在电脑上渲染而成（不是实机照片）。屏幕 240×135。</sub>

## 功能

**首页 · 余生**
开机即是一个剩余天数的大数字，其余全靠动效表达：一条随一天流逝而缩短的线、以「年」为格的方阵（当前这一年会随时间慢慢填满、跟着秒针呼吸）、底部的人生进度条——每过一秒，燃烧的一端会飘起一粒火星。专注时，番茄钟的进度也会显示在首页。

**占卜与随机**
- **小六壬**：默认「时辰起课」，自动取当前农历月、日和时辰；按 Tab 切到「报数起课」，自己报三个数。罗盘上逐宫跳动，结果页有竖排诗诀。农历覆盖 1900–2100。
- **求签**：3D 竹签筒，按住空格摇或直接甩动设备，一支签跳出落地；共 64 支，签诗选自古诗名句，附解签和事业、财运、感情、健康、出行五项运势。
- **掷筊**：两块月牙筊的刚体物理，圣杯 / 笑杯 / 阴杯，连得三圣杯有特效。
- **骰子**：1–6 颗倒角骰，一点四点为红，宫格摆放、各自抛出，结果自动识别豹子 / 顺子 / 对子。
- **硬币**：一枚开元通宝，字面有浮雕光照，落地后像真硬币一样越转越快再停下。
- **蓄力投掷**：按住空格蓄力、松手抛出（力度随蓄力变化），或直接甩动设备。

**录音**
- 任何界面都能一键开始 / 停止：**G0 键**或 **Ctrl+R**；**Fn+1 / 2 / 3** 直接选预设开始（随手录 / 长时录音 / 高保真 44.1 kHz）。
- WAV 直接流式写入 SD 卡，**可长时间录制**：文件头每 5 秒刷新一次，中途断电也能保住已录的部分；单文件接近 4 GB 时自动续写下一个文件。
- 滚动的镜像波形；录音时屏幕自动调暗、再熄屏，CPU 降到 80 MHz 省电。可打标记（M）。
- **录音库**：整段波形随播放点亮起，快进快退、音量、跳到标记；删除要按两次确认。
- **U 盘模式**：插上电脑会弹出 3 秒倒计时，把 SD 卡当 U 盘用，方便直接拷走录音；在电脑上推出后设备自动回到正常模式。

**番茄钟**
预设（工作 / 阅读 / 学习 / Deep Work，可自己增删改，名称可用英文）、专注 / 短休 / 长休 / 每几轮长休都能设；专注时自动调暗、熄屏；每个专注块写入 SD 卡 `/POMO/focus.csv`，统计页显示近 7 天柱状图和连续天数。

**Kindle 翻页器**
把 Cardputer 伪装成 Kindle Click 蓝牙翻页器（BLE HID）：翻页、调亮度、让 Kindle 休眠。蓝牙只在翻页器页面里开启，设置里可整体关闭以省电。

**其它**：NTP 联网校时（同步后自动关 WiFi，可设为每天一次）；时区；亮度 / 音量。

## 快速上手

### A. 直接刷预编译固件

到 [Releases](../../releases) 下载 `tianji-*-merged.bin`，用任意 ESP32-S3 刷写工具写到地址 **0x0**：

```bash
pip install esptool
esptool --chip esp32s3 --port <串口> write_flash 0x0 tianji-merged.bin
```

没有命令行的话，可用浏览器版 [esptool-js](https://espressif.github.io/esptool-js/)。刷不进去时，按住 **G0** 再插 USB 进入下载模式。

### B. 从源码构建

需要 [arduino-cli](https://arduino.github.io/arduino-cli/)：

```bash
arduino-cli config add board_manager.additional_urls https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32@3.3.7
arduino-cli lib install M5Cardputer@1.1.1 M5Unified@0.2.17 M5GFX@0.2.22

./build.sh                       # 编译到 build/
./build.sh upload <串口>          # 编译并烧录
```

字库 `tianji/fontdata.cpp` 已随仓库提交，不装字体也能直接编译。要改界面文字的话，需要重新生成它：装好 Pillow，下载 [思源黑体](https://github.com/adobe-fonts/source-han-sans) 的可变字体，设置 `SOURCE_HAN_SANS=/路径/字体.otf`，之后每次 `./build.sh` 会自动重新生成（见 `tools/mkfont.py`）。

### 第一次使用

1. 首页按 **T** 设置日期时间，按 **S** 进设置填**生日**。
2. Cardputer 没有断电保持的时钟：**不用时请长按 G0 进入待机**，而不是拨开关断电——待机时时钟继续走，按任意键或 G0 唤醒。真断电后，首页会出现「时间待校准」提示，按 **T** 校准；填了 WiFi 的话，断电后开机会自动联网校时，校完就关 WiFi（待机唤醒不会联网，录音不受影响）。
3. 录音和番茄钟统计需要一张 **FAT32** 的 SD 卡。

## 按键

| 位置 | 按键 |
|---|---|
| 首页 | 方向键 / Enter 打开应用栏；字母直达：**L** 小六壬　**Q** 求签　**J** 掷筊　**D** 骰子　**C** 硬币　**R** 录音　**F** 录音库　**P** 番茄钟　**K** 翻页器　**S** 设置　**U** U 盘　**T** 校准时间 |
| 全局 | **G0** 短按或 **Ctrl+R** 一键录音（再按停止）　**长按 G0** 1.5 秒进入待机（按任意键唤醒）　**Fn+1/2/3** 选预设录音　**Esc** 回首页 |
| 校准时间 | ←→ 选年 / 月 / 日 / 时 / 分，↑↓ 调整（Shift 一次 10），也可直接输入数字；Enter 确认，**W** 联网校时，Esc 取消 |
| 小六壬 | 空格 起课　Tab 换法　报数模式下输入数字后按空格　Enter 看诗诀 |
| 求签 / 掷筊 / 骰子 / 硬币 | 按住空格蓄力、松手投出，或甩动设备；骰子 **1–6** 选个数；求签落地后 Enter 看详解、**J** 接着掷筊 |
| 录音 | ↑↓ / 1–3 选预设，Enter 开始；录音中 空格 暂停　**M** 打标记　Enter 停止 |
| 录音库 | ↑↓ 选，Enter 播放，**D** 两次删除；播放中 ←→ 快进快退（Shift 一次 30 秒）　**-/=** 音量　**N/P** 跳标记 |
| 番茄钟 | Enter 开始　**E** 编辑　**N** 新建　**D** 两次删除　Tab 统计；计时中 空格 暂停　**S** 跳过　**X** 两次结束；设置里打开「番茄钟自动开始」，休息结束后自动开始下一轮 |
| 翻页器 | →↓ 空格 下一页　←↑ 上一页　**-/=** 亮度　**Z** Kindle 休眠　**F** 两次清除配对 |

## 状态

| | |
|---|---|
| 已在 Cardputer ADV 实机上验证 | 编译 / 烧录；录音写入 SD 卡并得到真实音频；SD 卡作为 U 盘被电脑识别；NTP 校时并自动关 WiFi；蓝牙协议栈启动 / 关闭与内存回收；各 3D 场景的帧耗时（见 [技术笔记](docs/NOTES.md)） |
| **尚未验证** | 与**真实 Kindle** 的配对和翻页——协议逐字节按[已在真机上跑通的参考实现](https://github.com/zuyan9/Open-Kindle-Click-ESP32-C6)移植，但还没有拿真 Kindle 测过 |
| 已知限制 | 蓝牙与 WiFi / 录音 / 扬声器的 RAM 放不下同时运行，所以蓝牙只在翻页器页面里开；录音时不可进入翻页器 |

欢迎试用、反馈和提 PR。

## 目录

```
tianji/            固件（Arduino 工程；入口 tianji.ino）
  app_*.cpp          各功能页面（home / liuren / qian / jiao / dice / coin / recorder / library / pomo / kindle / settings / usb）
  r3d.*              软件 3D 渲染器（四元数、透视、平面着色、画家算法）
  gfx.* sfx.*        画布与特效、程序合成的音效
  rec.* store.*      长时录音引擎、录音库与播放
  kclick.*           Kindle Click 蓝牙翻页器（NimBLE）
  usbdrive.*         U 盘模式（USB PHY 在 JTAG 与 TinyUSB 之间切换）
  lunar.* pomo.*     农历换算、番茄钟引擎
  fontdata.cpp       生成文件：思源黑体字形子集（OFL，见 NOTICE.md）
tools/
  preview/           电脑端预览：把界面代码渲染成图片，不用烧录就能看效果
  mictest/           ES8311 麦克风探针（排查录音无声时用）
  perf.sh            实机帧耗时测试（编译时加 -DPERFTEST）
  mkfont.py          字库生成　gen_lunar.js / verify_lunar.js 农历表生成与校验
docs/                总览图与技术笔记
```

## 致谢与参考

- [**Wenston-dev/omarchy-memento**](https://github.com/Wenston-dev/omarchy-memento) — 「余生」倒计时的设计来源：剩余天数、以年 / 周为格的方阵（本项目没有使用它的代码）
- [**KyleBing/m5stack-cardputer-sparks**](https://github.com/KyleBing/m5stack-cardputer-sparks) — Cardputer ADV 自制固件的先行者：参考了它「一个字母键直达一个应用」的交互，以及单个 USB PHY 在 JTAG 与 TinyUSB 之间切换的做法（该仓库未声明许可证，本项目没有复制它的源文件，详见 [NOTICE.md](NOTICE.md)）
- [**zuyan9/Open-Kindle-Click-ESP32-C6**](https://github.com/zuyan9/Open-Kindle-Click-ESP32-C6)（MIT）— Kindle 翻页器的 NimBLE 实现来源，`tianji/kclick.cpp` 由它改写而来
- [**zuyan9/Open-Kindle-Click-nRF52840**](https://github.com/zuyan9/Open-Kindle-Click-nRF52840) 的 `PROTOCOL.md` — Kindle 如何识别并驱动翻页器的详细协议分析
- [**zuyan9/Flipper-Zero-Kindle-Click**](https://github.com/zuyan9/Flipper-Zero-Kindle-Click) — 同一协议在 Flipper Zero 上的实现，用作对照
- [**Cardenza-Community/cardputer-studio**](https://github.com/Cardenza-Community/cardputer-studio)（MIT）— ES8311 麦克风的寄存器配置参考
- [**Tombotronic/audio-spy**](https://github.com/Tombotronic/audio-spy)（MIT）、[esp-idf #18621](https://github.com/espressif/esp-idf/issues/18621)、[cardputer-claude-os #8](https://github.com/dakshaymehta/cardputer-claude-os/issues/8) — 排查「新版核心下 M5 麦克风只读到常数」时的线索
- [M5Cardputer](https://github.com/m5stack/M5Cardputer) / [M5Unified](https://github.com/m5stack/M5Unified) / [M5GFX](https://github.com/m5stack/M5GFX) — 显示、键盘、扬声器等底层库
- [思源黑体 Source Han Sans](https://github.com/adobe-fonts/source-han-sans)（SIL OFL 1.1）— 界面字体
- 签诗出自李白、杜甫、苏轼、陆游等人的古诗名句；解签文字为原创

## 许可证

[MIT](LICENSE)。第三方内容见 [NOTICE.md](NOTICE.md) 和 [licenses/](licenses/)。
