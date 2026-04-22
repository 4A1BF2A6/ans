/*
 * ns_realtime.c — 实时 NS 噪声抑制处理（PortAudio）
 *
 * 通道布局（7 通道 EMEET 设备）：
 *   ch 0-3 : 原始麦克风采集（raw mic）
 *   ch 4   : 级联通道
 *   ch 5   : 回采通道
 *   ch 6   : DSP 芯片处理后的信号
 *
 * 输出文件：
 *   rec_raw.wav  — ch0 原始音频
 *   rec_ns.wav   — ch0 经过 WebRTC NS 算法处理后
 *   rec_dsp.wav  — ch6 DSP 芯片输出（对比参考）
 *
 * 用法：
 *   ns_rt                         列出所有音频设备
 *   ns_rt <dev_idx> [raw_ch=0] [dsp_ch=6] [total_ch=7] [aggr_mode=1]
 *
 * 按 Enter 停止录制。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#endif

#include "portaudio.h"
#include "noise_suppression.h"

/* NS blockLen = 160 for fs >= 16000 */
#define NS_BLOCK 160

/* ── 全局退出标志 ──────────────────────────────────────────────── */
static volatile int g_running = 1;
static void on_sigint(int s) { (void)s; g_running = 0; }

/* ================================================================
 * WAV 写入（32-bit float）
 * ================================================================ */
typedef struct {
    FILE     *fp;
    int       channels;
    int       sample_rate;
    long      data_offset;
    uint32_t  samples_written;
} WavWriter;

static void ww_u16(FILE *fp, uint16_t v) { fwrite(&v, 2, 1, fp); }
static void ww_u32(FILE *fp, uint32_t v) { fwrite(&v, 4, 1, fp); }

static WavWriter *wav_open(const char *path, int channels, int sample_rate) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return NULL;
    WavWriter *w = (WavWriter *)malloc(sizeof(WavWriter));
    w->fp              = fp;
    w->channels        = channels;
    w->sample_rate     = sample_rate;
    w->samples_written = 0;

    fwrite("RIFF", 1, 4, fp); ww_u32(fp, 0);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    ww_u32(fp, 18);
    ww_u16(fp, 3);                                              /* IEEE float */
    ww_u16(fp, (uint16_t)channels);
    ww_u32(fp, (uint32_t)sample_rate);
    ww_u32(fp, (uint32_t)(sample_rate * channels * 4));
    ww_u16(fp, (uint16_t)(channels * 4));
    ww_u16(fp, 32);
    ww_u16(fp, 0);
    fwrite("data", 1, 4, fp);
    w->data_offset = ftell(fp);
    ww_u32(fp, 0);
    return w;
}

static void wav_write(WavWriter *w, const float *buf, int frames) {
    fwrite(buf, sizeof(float), (size_t)(frames * w->channels), w->fp);
    w->samples_written += (uint32_t)frames;
}

static void wav_close(WavWriter *w) {
    if (!w) return;
    uint32_t data_bytes = w->samples_written * (uint32_t)w->channels * 4u;
    fseek(w->fp, w->data_offset, SEEK_SET);
    ww_u32(w->fp, data_bytes);
    fseek(w->fp, 4, SEEK_SET);
    ww_u32(w->fp, (uint32_t)(w->data_offset + 4 + data_bytes - 8));
    fclose(w->fp);
    free(w);
}

/* ================================================================
 * 设备列表
 * ================================================================ */
static void list_devices(void) {
    int n = Pa_GetDeviceCount();
    if (n < 0) { fprintf(stderr, "Pa_GetDeviceCount: %s\n", Pa_GetErrorText(n)); return; }
    int def_in  = Pa_GetDefaultInputDevice();
    int def_out = Pa_GetDefaultOutputDevice();
    printf("Audio devices (%d total):\n", n);
    printf("  %-4s  %-50s  %4s  %4s  %s\n", "Idx", "Name", "In", "Out", "Rate");
    printf("  %s\n", "-------------------------------------------------------------------------------------");
    for (int i = 0; i < n; i++) {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
        const char *tag = (i == def_in) ? "<IN> " : (i == def_out) ? "<OUT>" : "     ";
        printf("  [%2d] %s %-46s  %4d  %4d  %.0f Hz\n",
               i, tag, d->name,
               d->maxInputChannels, d->maxOutputChannels,
               d->defaultSampleRate);
    }
    printf("\n");
}

/* ================================================================
 * 电平工具
 * ================================================================ */
static float rms(const float *buf, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += (double)buf[i] * buf[i];
    return (float)sqrt(s / n + 1e-20);
}
static float dbfs(float r) { return 20.0f * log10f(r + 1e-20f); }

#define METER_W 40
static void print_meter(const char *lbl, float db) {
    int bars = (int)((db + 60.0f) * METER_W / 60.0f);
    if (bars < 0)       bars = 0;
    if (bars > METER_W) bars = METER_W;
    char bar[METER_W + 1];
    for (int i = 0; i < METER_W; i++) bar[i] = (i < bars) ? '#' : ' ';
    bar[METER_W] = '\0';
    printf("  %-10s [%s] %+6.1f dBFS\n", lbl, bar, db);
}

/* ================================================================
 * main
 * ================================================================ */
int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "Pa_Initialize: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    if (argc < 2) {
        list_devices();
        printf("Usage: %s <dev_idx> [raw_ch=0] [dsp_ch=6] [total_ch=7] [aggr_mode=1]\n", argv[0]);
        printf("  aggr_mode: 0=Mild(6dB), 1=Medium(10dB), 2=Aggressive(15dB)\n\n");
        Pa_Terminate();
        return 0;
    }

    int dev       = atoi(argv[1]);
    int raw_ch    = (argc > 2) ? atoi(argv[2]) : 0;
    int dsp_ch    = (argc > 3) ? atoi(argv[3]) : 6;
    int total_ch  = (argc > 4) ? atoi(argv[4]) : 7;
    int aggr_mode = (argc > 5) ? atoi(argv[5]) : 1;

    if (dev < 0 || dev >= Pa_GetDeviceCount()) {
        fprintf(stderr, "Invalid device index: %d\n", dev);
        Pa_Terminate(); return 1;
    }
    const PaDeviceInfo *di = Pa_GetDeviceInfo(dev);
    if (di->maxInputChannels < total_ch) {
        fprintf(stderr, "Device only has %d channels, total_ch=%d\n",
                di->maxInputChannels, total_ch);
        Pa_Terminate(); return 1;
    }
    if (raw_ch >= total_ch || dsp_ch >= total_ch) {
        fprintf(stderr, "Channel index out of range (total_ch=%d)\n", total_ch);
        Pa_Terminate(); return 1;
    }

    const int sample_rate = 48000;

    /* ── NS 初始化 ── */
    NsHandle *ns = WebRtcNs_Create();
    if (!ns) { fprintf(stderr, "WebRtcNs_Create failed\n"); Pa_Terminate(); return 1; }
    if (WebRtcNs_Init(ns, (uint32_t)sample_rate) != 0) {
        fprintf(stderr, "WebRtcNs_Init failed\n"); Pa_Terminate(); return 1;
    }
    WebRtcNs_set_policy(ns, aggr_mode);

    printf("=== Real-time NS ===\n");
    printf("Device  : [%d] %s\n", dev, di->name);
    printf("Channels: total=%d  raw_ch=%d  dsp_ch=%d\n", total_ch, raw_ch, dsp_ch);
    printf("NS      : 48kHz, %d samples/block (%.2f ms/block), aggr_mode=%d\n",
           NS_BLOCK, (float)NS_BLOCK / sample_rate * 1000.0f, aggr_mode);

    /* ── 打开 PortAudio 输入流 ── */
    PaStreamParameters inp;
    memset(&inp, 0, sizeof(inp));
    inp.device                    = dev;
    inp.channelCount              = total_ch;
    inp.sampleFormat              = paFloat32;
    inp.suggestedLatency          = di->defaultLowInputLatency;
    inp.hostApiSpecificStreamInfo = NULL;

    PaStream *stream = NULL;
    err = Pa_OpenStream(&stream, &inp, NULL,
                        (double)sample_rate, NS_BLOCK, paNoFlag, NULL, NULL);
    if (err != paNoError) {
        fprintf(stderr, "Pa_OpenStream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate(); return 1;
    }

    /* ── 打开 WAV 文件 ── */
    WavWriter *wav_raw = wav_open("rec_raw.wav", 1, sample_rate);
    WavWriter *wav_ns  = wav_open("rec_ns.wav",  1, sample_rate);
    WavWriter *wav_dsp = wav_open("rec_dsp.wav", 1, sample_rate);
    if (!wav_raw || !wav_ns || !wav_dsp) {
        fprintf(stderr, "Failed to open WAV output files\n");
        Pa_Terminate(); return 1;
    }
    printf("Output  : rec_raw.wav  rec_ns.wav  rec_dsp.wav\n\n");

    /* ── 缓冲区 ── */
    float *in_buf   = (float *)malloc(sizeof(float) * NS_BLOCK * total_ch);
    float  raw_buf[NS_BLOCK];
    float  dsp_buf[NS_BLOCK];
    float  ns_out[NS_BLOCK];

    /* NS Process 需要指针数组 */
    const float *in_ptrs[1]  = { raw_buf };
    float       *out_ptrs[1] = { ns_out  };

    Pa_StartStream(stream);
    signal(SIGINT, on_sigint);

    printf("Recording... (press Enter to stop)\n\n\n\n");

    long frame_cnt    = 0;
    long overflow_cnt = 0;

    while (g_running) {
#ifdef _WIN32
        if (_kbhit()) {
            int c = _getch();
            if (c == '\r' || c == '\n' || c == ' ') { g_running = 0; break; }
        }
#endif
        /* ① 读取一帧多通道音频 */
        err = Pa_ReadStream(stream, in_buf, NS_BLOCK);
        if (err == paInputOverflowed) {
            overflow_cnt++;
        } else if (err != paNoError) {
            fprintf(stderr, "\nPa_ReadStream: %s\n", Pa_GetErrorText(err));
            break;
        }

        /* ② 解交织提取目标通道 */
        for (int i = 0; i < NS_BLOCK; i++) {
            raw_buf[i] = in_buf[i * total_ch + raw_ch];
            dsp_buf[i] = in_buf[i * total_ch + dsp_ch];
        }

        /* ③ NS 处理（Analyze 估计噪声，Process 抑制） */
        WebRtcNs_Analyze(ns, raw_buf);
        WebRtcNs_Process(ns, in_ptrs, 1, out_ptrs);

        /* ④ 写入 WAV */
        wav_write(wav_raw, raw_buf, NS_BLOCK);
        wav_write(wav_ns,  ns_out,  NS_BLOCK);
        wav_write(wav_dsp, dsp_buf, NS_BLOCK);

        /* ⑤ 每 150 帧刷新电平表（约 0.5s） */
        frame_cnt++;
        if (frame_cnt % 150 == 0) {
            float db_raw = dbfs(rms(raw_buf, NS_BLOCK));
            float db_ns  = dbfs(rms(ns_out,  NS_BLOCK));
            float db_dsp = dbfs(rms(dsp_buf, NS_BLOCK));

            printf("\033[4A");
            printf("  Frames: %-8ld  Overflows: %-4ld  (%.1f s)\n",
                   frame_cnt, overflow_cnt,
                   (float)frame_cnt * NS_BLOCK / sample_rate);
            print_meter("Raw ch0", db_raw);
            print_meter("NS out",  db_ns);
            print_meter("DSP ch6", db_dsp);
            fflush(stdout);
        }
    }

    /* ── 清理 ── */
    printf("\nStopping...\n");
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    WebRtcNs_Free(ns);
    free(in_buf);
    Pa_Terminate();

    wav_close(wav_raw);
    wav_close(wav_ns);
    wav_close(wav_dsp);

    float duration = (float)frame_cnt * NS_BLOCK / sample_rate;
    printf("Saved : rec_raw.wav  rec_ns.wav  rec_dsp.wav\n");
    printf("Total : %ld frames / %.1f seconds\n", frame_cnt, duration);
    return 0;
}
