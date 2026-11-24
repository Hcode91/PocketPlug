// PocketPlug - ZAPiT Game Wave (2005)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Game Wave is a DVD player whose games are Lua 5.0 scripts (compiled .zbc) driving an
// on-screen-display overlay engine over DVD video. This core runs the game's script itself
// (high-level: no console firmware) and supplies the engine's libraries: OSD textures and
// overlays with animations, fonts and text, MPEG-2 still backgrounds and movies, sounds,
// the six coloured remotes, timers and resource files.
//
// Formats were worked out with the GameWaveFans project's research (zbc, zbm, zwf) and from
// the game scripts. Uses the GameWaveFans Lua 5.0.2 fork (MIT), libmpeg2 (GPL-2+) and
// minimp3 (CC0).
#pragma once
#include "../../core_api.h"
#include "../hyperscan/cdimage.h"
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct lua_State;
struct mpeg2dec_s;

class GameWaveCore : public Core {
public:
    GameWaveCore();
    ~GameWaveCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    bool wantsRomPaths() const override { return true; }
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return OUT_W; }
    int frameHeight() const override { return OUT_H; }
    double refreshRate() const override { return 30.0; }
    int sampleRate() const override { return RATE; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // input bits: key codes 0..20 as the remotes send them (digits, Up 10, Down 11, Right 12,
    // Left 13, Select 14, DVD menu 15, A 16, B 17, C 18, D 19, Game menu 20); bits 24..26 pick
    // which remote (0 red, 1 yellow, 2 blue, 3 green, 4 purple, 5 orange) is pressing
    static constexpr int REMOTE_SHIFT = 24;

    static constexpr int W = 720, H = 480;                // DVD frame and OSD space
    static constexpr int OUT_W = 640, OUT_H = 480;        // shown square-pixel 4:3
    static constexpr int RATE = 48000;

    // ---- used by the Lua bindings ----
    struct Texture { int w = 0, h = 0; std::vector<uint32_t> px; };   // ARGB, straight alpha
    struct Step { int cmd = 0, a = 0, b = 0; };
    struct Anim {
        int kind = 0;                         // 1 position, 2 alpha, 3 texture frames
        int start = 0, dur = 0;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0, mode = 0;
        std::vector<Step> steps; int step = 0, stepStart = 0;
    };
    struct Overlay {
        std::vector<int> tex;                 // frames (texture ids)
        int frame = 0, x = 0, y = 0, z = 0;
        bool visible = false;
        float alpha = 1.0f;
        std::vector<Anim> anims;
    };
    struct Glyph { int x0, y0, x1, y1, adv, left, width, right; };
    struct Font { int tex = 0, first = 32, count = 0, cellH = 0; std::vector<Glyph> g; };
    struct Sound { std::vector<int16_t> pcm; int rate = 22050, ch = 2; };
    struct Voice { int snd = 0; double pos = 0; bool loop = false; };
    struct FileEnt { uint32_t lba = 0; uint32_t size = 0; };

    enum WaitKind { WAIT_NONE, WAIT_SLEEP, WAIT_KEY, WAIT_POLL_KEY, WAIT_POLL_MOVIE, WAIT_POLL_TIME, WAIT_POLL_ANIM, WAIT_POLL_VIS };
    int yieldFor(lua_State* L, WaitKind kind, int arg = 0);
    bool poll(lua_State* L, WaitKind kind, int arg = 0);   // true: yield now (busy loop)
    int pushKey(lua_State* L);                             // key, remote, time
    int nowMs() const { return int(m_frame * 1000 / 30); }
    bool findFile(const std::string& path, FileEnt& out) const;
    bool readFile(const FileEnt& f, std::vector<uint8_t>& out);
    std::string resolve(int res, const std::string& name) const;
    int loadTexture(const std::string& path);
    int newTexture(int w, int h);
    int loadFont(const std::string& path);
    int renderText(const std::string& s, int font, int w, int h, int align, uint32_t color);
    int loadSound(const std::string& path);
    int loadIframe(const std::string& path);
    void showIframe(int id);
    int loadMovie(const std::string& path);
    void playMovie();
    void stopMovie();
    void dbg(const char* fmt, ...);
    void clearKeys() { m_keyQ.clear(); }
    void sleepUntil(int ms) { m_wakeMs = ms; }

    std::vector<std::string> m_res;                    // resource folders (rm.OpenResource)
    std::map<int, Texture> m_tex; int m_nextTex = 1;
    std::map<int, Overlay> m_ovl; int m_nextOvl = 1;
    std::map<int, Font> m_fonts; int m_nextFont = 1; int m_builtinFont = 0;
    std::map<int, std::pair<int, int>> m_texts; int m_nextText = 1;   // text id -> texture, overlay
    std::map<int, Sound> m_sounds; int m_nextSound = 1;
    std::vector<Voice> m_voices;
    std::map<int, std::vector<uint32_t>> m_iframes; int m_nextIframe = 1;
    struct ZFile { std::vector<uint8_t> data; };
    std::map<int, ZFile> m_zfiles; int m_nextZFile = 1;
    bool m_osdShown = true;
    bool m_remotesOn = true;
    std::string m_engineVersion = "1.01";
    uint32_t m_rand = 1;

    // movie
    struct Movie { FileEnt f; };
    std::map<int, Movie> m_movies; int m_nextMovie = 1; int m_curMovie = 0;
    bool m_movieLoop = false;
    int m_movieState = 0;                              // 0 stopped / finished, 1 playing

private:
    void startScript();
    void resume();
    void updateAnims();
    void compose();
    void mixAudio();
    void movieFrame();
    bool movieDemux();                                 // false at end of file
    void yuvToVideo(const uint8_t* const* buf, int w, int h, int cw, int ch);
    bool decodeStill(const std::vector<uint8_t>& es, std::vector<uint32_t>& rgb);
    bool scanDir(uint32_t lba, uint32_t size, const std::string& path, bool joliet, int depth);

    CdImage m_cd;
    std::map<std::string, FileEnt> m_files;           // lower-case path -> extent
    std::vector<uint8_t> m_script;

    lua_State* m_L = nullptr;
    lua_State* m_co = nullptr;
    int m_coRef = 0;
    bool m_started = false, m_dead = false, m_yielded = false;
    WaitKind m_wait = WAIT_NONE; int m_waitArg = 0; int m_wakeMs = 0;
    int m_polls = 0;

    uint64_t m_frame = 0;
    uint32_t m_keys = 0, m_lastKeys = 0;
    struct KeyEv { int key, remote, time; };
    std::deque<KeyEv> m_keyQ;

    std::vector<uint32_t> m_video, m_frameBuf, m_out;

    // movie decoding
    mpeg2dec_s* m_mpeg = nullptr;
    uint32_t m_movPos = 0;                             // byte offset in the file
    std::vector<uint8_t> m_movBuf;                     // undemuxed program stream bytes
    size_t m_movBufPos = 0;
    std::vector<uint8_t> m_esChunk;                    // video bytes being fed to libmpeg2
    std::vector<uint8_t> m_mp2;                        // audio elementary stream
    void* m_mp3dec = nullptr;
    std::deque<int16_t> m_movAudio;                    // mono at 48 kHz
    bool m_movEof = false;
    int m_noPicture = 0;

    std::vector<int16_t> m_audioOut;
    bool m_debug = false;
};
