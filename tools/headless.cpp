// PocketPlug headless runner (desktop testing only, not part of the app)
// Usage: headless <machine> <romfile> <frames> <outdir> [script]
// script: comma list of frame:mask pairs, e.g. "300:8,310:0" (mask held from that frame)
#include "../app/src/main/cpp/core_api.h"
#include "../app/src/main/cpp/cores/vt1682/vt1682.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <chrono>
#include <array>
#include <dirent.h>
#include <sys/stat.h>

static void writePPM(const char* path, const uint32_t* px, int w, int h)
{
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) { uint8_t c[3] = { (uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i] }; fwrite(c, 1, 3, f); }
    fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage\n"); return 1; }
    // a directory = a whole ROM set (every file in it)
    std::vector<RomFile> roms;
    struct stat stt;
    if (stat(argv[2], &stt) == 0 && S_ISDIR(stt.st_mode)) {
        DIR* d = opendir(argv[2]);
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            RomFile rf; rf.name = e->d_name; rf.path = std::string(argv[2]) + "/" + e->d_name;
            std::ifstream in(rf.path, std::ios::binary);
            rf.data.assign(std::istreambuf_iterator<char>(in), {});
            roms.push_back(rf);
        }
        closedir(d);
    } else {
        std::ifstream in(argv[2], std::ios::binary);
        RomFile rf; rf.name = strrchr(argv[2], '/') ? strrchr(argv[2], '/') + 1 : argv[2];
        rf.path = argv[2];
        rf.data.assign(std::istreambuf_iterator<char>(in), {});
        roms.push_back(rf);
    }
    Core* core = createCore(argv[1]);
    std::string err;
    if (!core || !core->load(roms, err)) { fprintf(stderr, "load failed: %s\n", err.c_str()); return 1; }
    // MEDIA="drive:path" inserted before the first frame
    if (const char* m = getenv("MEDIA")) {
        int drv = atoi(m); const char* p = strchr(m, ':');
        if (p && !core->mediaInsert(drv, p + 1, err)) fprintf(stderr, "media: %s\n", err.c_str());
    }
    int frames = atoi(argv[3]);
    std::map<int, uint32_t> script, tapScript;
    // "frame:j<stick>:<x>:<y>" moves an analog stick (-1..1)
    std::map<int, std::vector<std::array<float, 3>>> stickScript;
    // "frame:k<code>:<1|0>" presses / releases a computer key
    std::map<int, std::vector<std::pair<int, bool>>> keyScript;
    // "frame:o<id>:<value>" sets a core option
    std::map<int, std::vector<std::pair<int, int>>> optScript;
    if (argc > 5) {
        std::stringstream ss(argv[5]); std::string tok;
        while (std::getline(ss, tok, ',')) {
            int f; unsigned m;
            float jx, jy; int st;
            if (sscanf(tok.c_str(), "%d:j%d:%f:%f", &f, &st, &jx, &jy) == 4) { stickScript[f].push_back({ (float)st, jx, jy }); continue; }
            int oi, ov;
            if (sscanf(tok.c_str(), "%d:o%d:%d", &f, &oi, &ov) == 3) { optScript[f].push_back({ oi, ov }); continue; }
            int kc, kd;
            if (sscanf(tok.c_str(), "%d:k%d:%d", &f, &kc, &kd) == 3) { keyScript[f].push_back({ kc, kd != 0 }); continue; }
            if (sscanf(tok.c_str(), "%d:t%x", &f, &m) == 2) tapScript[f] = m;
            else { sscanf(tok.c_str(), "%d:%x", &f, &m); script[f] = m; }
        }
    }
    std::vector<int16_t> audio;
    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; f++) {
        if (script.count(f)) core->setInputs(script[f]);
        if (tapScript.count(f)) core->tapInputs(tapScript[f]);
        if (optScript.count(f)) for (auto& o : optScript[f]) core->setOption(o.first, o.second);
        if (keyScript.count(f)) for (auto& k : keyScript[f]) core->keyEvent(k.first, k.second);
        if (stickScript.count(f)) for (auto& j : stickScript[f]) core->setAnalog((int)j[0], j[1], j[2]);
        core->runFrame();
        int16_t buf[4096]; size_t n;
        while ((n = core->readAudio(buf, 4096)) > 0) audio.insert(audio.end(), buf, buf + n);
        if (getenv("DBG") && std::string(argv[1]) == "110dance" && (f + 1) % atoi(getenv("DBG")) == 0) printf("%d %s\n", f + 1, ((Vt1682Core*)core)->debugInfo().c_str());
        if ((f + 1) % (getenv("EVERY") ? atoi(getenv("EVERY")) : 50) == 0) {
            char p[512]; snprintf(p, sizeof p, "%s/f%05d.ppm", argv[4], f + 1);
            writePPM(p, core->frameBuffer(), core->frameWidth(), core->frameHeight());
        }
    }
    if (getenv("DUMP") && std::string(argv[1]) == "110dance") { unsigned a0 = strtoul(getenv("DUMP"), 0, 16); FILE* d = fopen("/home/claude/out/dump.bin", "wb"); for (unsigned a = a0; a < a0 + (getenv("DUMPLEN") ? strtoul(getenv("DUMPLEN"),0,16) : 0x100); a++) { uint8_t b = ((Vt1682Core*)core)->mainRead(a); fwrite(&b, 1, 1, d); } fclose(d); }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%d frames in %.2fs (%.1f fps)\n", frames, secs, frames / secs);
    // WAV
    char p[512]; snprintf(p, sizeof p, "%s/audio.wav", argv[4]);
    FILE* w = fopen(p, "wb");
    uint32_t dsz = audio.size() * 2, rate = core->sampleRate();
    fwrite("RIFF", 1, 4, w); uint32_t v = 36 + dsz; fwrite(&v, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
    v = 16; fwrite(&v, 4, 1, w); uint16_t s = 1; fwrite(&s, 2, 1, w); fwrite(&s, 2, 1, w);
    fwrite(&rate, 4, 1, w); v = rate * 2; fwrite(&v, 4, 1, w); s = 2; fwrite(&s, 2, 1, w); s = 16; fwrite(&s, 2, 1, w);
    fwrite("data", 1, 4, w); fwrite(&dsz, 4, 1, w); fwrite(audio.data(), 2, audio.size(), w); fclose(w);
    printf("audio samples: %zu\n", audio.size());
    return 0;
}
