# NS — WebRTC 噪声抑制实时测试

基于 WebRTC 的浮点噪声抑制算法（`NoiseSuppressionC`），用于对 EMEET 麦克风设备进行实时录音并输出三路音频对比文件。

## 通道布局

| 通道 | 内容 |
|------|------|
| ch 0–3 | 原始麦克风采集（raw mic） |
| ch 4 | 级联通道 |
| ch 5 | 回采通道（far-end reference） |
| ch 6 | DSP 芯片处理后的信号 |

## 依赖

| 工具 | 说明 |
|------|------|
| Visual Studio 2022 BuildTools | MSVC 编译器（`cl.exe`） |
| [vcpkg](https://github.com/microsoft/vcpkg) | 包管理，路径 `C:\vcpkg` |
| PortAudio（vcpkg） | 跨平台音频 I/O |

安装 PortAudio：
```
vcpkg install portaudio:x64-windows
```

## 构建

```bat
compile.bat
```

生成 `ns_rt.exe` 并自动拷贝 `portaudio.dll`。

编译器路径、vcpkg 路径均在 `compile.bat` 顶部，按需修改。

## 使用

**列出音频设备：**
```
ns_rt.exe
```

**开始录制：**
```
ns_rt.exe <dev_idx> [raw_ch=0] [dsp_ch=6] [total_ch=7] [aggr_mode=1]
```

| 参数 | 说明 | 默认 |
|------|------|------|
| `dev_idx` | PortAudio 设备索引 | — |
| `raw_ch` | 原始麦克风通道号 | 0 |
| `dsp_ch` | DSP 芯片输出通道号 | 6 |
| `total_ch` | 设备总通道数 | 7 |
| `aggr_mode` | NS 强度：0=温和(6dB) 1=中等(10dB) 2=激进(15dB) | 1 |

**示例（EMEET OfficeCore M0 Plus，设备索引 1）：**
```
ns_rt.exe 1
ns_rt.exe 1 0 6 7 2
```

按 **Enter** 停止录制。

## 输出文件

| 文件 | 内容 |
|------|------|
| `rec_raw.wav` | ch0 原始麦克风（32-bit float，48kHz，单声道） |
| `rec_ns.wav` | ch0 经 WebRTC NS 处理后 |
| `rec_dsp.wav` | ch6 DSP 芯片处理后（对比基准） |

用 Audacity 打开三个文件对齐播放，可直观对比算法与 DSP 芯片的降噪效果。

## 算法参数

- 采样率：48000 Hz
- 帧长：160 samples（≈ 3.33 ms）
- 分析窗：256 点 FFT，Hann 窗
- 噪声估计：分位数估计（Quantile）
- 增益计算：Wiener 滤波 + 语音概率先验模型（LRT / 谱平坦度 / 谱差异）

## 文件说明

| 文件 | 说明 |
|------|------|
| `ns_core.c / .h` | NS 算法核心 |
| `noise_suppression.c / .h` | 公共 API 封装 |
| `fft4g.c / .h` | Ooura FFT |
| `windows_private.h` | 分析/合成窗系数 |
| `defines.h` | 算法常量 |
| `ns_realtime.c` | 实时录音主程序 |
| `compile.bat` | MSVC 一键编译脚本 |
