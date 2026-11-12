/*
 * 唤醒词离线验证工具(不参与正常构建, 由 tools/snowboy_detect_test.sh 单独编译)。
 *
 * 作用: 把训练录音喂给工程里那份 snowboy 静态库, 扫描灵敏度, 告诉你
 * "哪一档能稳定命中" —— 生产代码 Idle.cc / Speaking.cc 里那两个
 * SnowboyDetectSetSensitivity() 就该填这个值。
 *
 * 用法: snowboy_detect_test.sh <model.pmdl> <wav1> [wav2 ...]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../third_party/snowboy/include/snowboy-detect-c-wrapper.h"

static const char *kResource = "third_party/snowboy/resources/common.res";

/* 解析 RIFF, 只接受 16kHz / 单声道 / 16bit —— 和板子上 AIChat_sample_rate 一致 */
static std::vector<int16_t> read_wav16k_mono(const char *path)
{
    std::vector<int16_t> pcm;
    FILE *f = fopen(path, "rb");
    if (!f) { printf("打不开 %s\n", path); return pcm; }

    char riff[4] = {0};
    fread(riff, 1, 4, f);
    if (memcmp(riff, "RIFF", 4) != 0) { printf("%s 不是 RIFF wav\n", path); fclose(f); return pcm; }
    fseek(f, 12, SEEK_SET);

    int rate = 0, channels = 0, bits = 0;
    char id[5] = {0};
    while (fread(id, 1, 4, f) == 4) {
        unsigned len = 0;
        fread(&len, 4, 1, f);
        if (memcmp(id, "fmt ", 4) == 0) {
            unsigned fmt_tag = 0;
            fread(&fmt_tag, 2, 1, f);
            unsigned ch = 0;      fread(&ch, 2, 1, f);
            unsigned r = 0;       fread(&r, 4, 1, f);
            fseek(f, 4, SEEK_CUR);
            unsigned align = 0;   fread(&align, 2, 1, f);
            unsigned b = 0;       fread(&b, 2, 1, f);
            channels = ch; rate = (int)r; bits = (int)b;
            fseek(f, (long)(len - 16), SEEK_CUR);
        } else if (memcmp(id, "data", 4) == 0) {
            pcm.resize(len / 2);
            fread(pcm.data(), 2, len / 2, f);
            break;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
    fclose(f);

    if (rate != 16000 || channels != 1 || bits != 16) {
        printf("!! %s 格式不对: %dHz/%d声道/%dbit (需要 16000Hz/单声道/16bit)\n"
               "   转换: ffmpeg -i in.wav -ar 16000 -ac 1 -sample_fmt s16 out.wav\n",
               path, rate, channels, bits);
        pcm.clear();
    }
    return pcm;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("用法: %s <model.pmdl> <wav1> [wav2 ...]\n", argv[0]);
        return 2;
    }
    const char *model = argv[1];
    std::vector<std::string> wavs;
    for (int i = 2; i < argc; i++) wavs.push_back(argv[i]);

    std::vector<std::vector<int16_t>> clips;
    for (auto &w : wavs) {
        auto pcm = read_wav16k_mono(w.c_str());
        if (pcm.empty()) return 1;
        printf("载入 %s = %.2fs\n", w.c_str(), pcm.size() / 16000.0);
        clips.push_back(pcm);
    }

    const char *sens[] = {"0.4", "0.5", "0.55", "0.6", "0.65", "0.7", "0.8", "0.9"};
    printf("\n灵敏度  命中情况\n");
    for (const char *s : sens) {
        SnowboyDetect *d = SnowboyDetectConstructor(kResource, model);
        SnowboyDetectSetSensitivity(d, s);
        SnowboyDetectSetAudioGain(d, 1);
        SnowboyDetectApplyFrontend(d, false);

        int hits = 0;
        printf("  %-5s ", s);
        for (size_t c = 0; c < clips.size(); c++) {
            SnowboyDetectReset(d);
            int at = -1;
            for (size_t i = 0; i + 512 <= clips[c].size(); i += 512) {
                if (SnowboyDetectRunDetection(d, &clips[c][i], 512, false) > 0) { at = (int)(i / 160.0); break; }
            }
            if (at >= 0) hits++;
            printf("录音%zu %s(%.2fs) ", c + 1, at >= 0 ? "命中" : "未命中", at / 100.0);
        }
        printf("| %d/%zu\n", hits, clips.size());
        SnowboyDetectDestructor(d);
    }
    printf("\n提示: 选一个能全部命中的最小值再 +0.05~0.1, 兼顾误触发。\n");
    return 0;
}
