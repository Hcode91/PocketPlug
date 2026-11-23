// PocketPlug - Gizmondo games run directly (no Windows CE system needed)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game is the folder from its SD card, as a .zip: one Windows CE ARM program
// (the .exe next to its data folder) plus its files. The program is loaded at its
// base address and run on DidjDroid's ARM926 interpreter with the MMU off; every
// call it makes into the system (COREDLL by ordinal, libGLES_CM, WININET, ole32)
// lands on a trap address and is answered here instead:
//   - kernel: threads (switched on blocking calls and every time slice), events,
//     mutexes, Sleep / waits, a heap, the registry (in memory)
//   - files: the zip as "\SD Card\", read-only, with writes kept in memory
//   - windows: classes, a message queue per thread, window procedures called back,
//     keys sent as WM_KEYDOWN / WM_KEYUP and GetAsyncKeyState; GDI is a stub
//   - sound: waveOut buffers mixed into the output
//   - C runtime: string / memory / printf / scanf / math functions and the
//     compiler's integer and soft-float helpers
//   - graphics: OpenGL ES 1.1 + EGL from PocketSprout (wce_gles.cpp), 320 x 240

#pragma once
#include "../../core_api.h"
#include "../gpl32/arm926.h"
#include "wce_hle.h"
#include <cstdint>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace didj { class GLES; }

class GizGameCore : public Core, public didj::Bus {
public:
    GizGameCore();
    ~GizGameCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb.data(); }
    int frameWidth() const override { return 320; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // didj::Bus: flat memory, the MMU stays off
    uint8_t* page_ptr(uint32_t pa) override { return pa < RAM_SIZE ? m_ram.data() + (pa & ~0xfffu) : nullptr; }
    uint32_t read32(uint32_t pa) override;
    uint16_t read16(uint32_t pa) override;
    uint8_t read8(uint32_t pa) override;
    void write32(uint32_t pa, uint32_t v) override;
    void write16(uint32_t pa, uint16_t v) override;
    void write8(uint32_t pa, uint8_t v) override;

    std::string debugLog;              // headless testing
    const std::vector<uint8_t>& debugRam() const { return m_ram; }

    static constexpr uint32_t RAM_SIZE = 0x4000000;          // 64 MB at 0
    static constexpr uint32_t TRAP_BASE = 0x7f000000, TRAP_SIZE = 0x10000;
    static constexpr uint32_t TRAP_RETURN = TRAP_BASE + TRAP_SIZE - 0x10;   // a callback finished
    static constexpr uint32_t TRAP_THREAD_EXIT = TRAP_BASE + TRAP_SIZE - 0x0c;

private:
    // ---- files in the game zip ----
    struct ZipEntry { uint32_t off = 0, csize = 0, usize = 0; uint16_t method = 0; };
    std::vector<uint8_t> m_zip;
    std::map<std::string, ZipEntry> m_zipIndex;          // normalised path -> entry
    std::set<std::string> m_dirs;                        // normalised directory paths
    std::map<std::string, std::shared_ptr<std::vector<uint8_t>>> m_written;   // files the game wrote
    bool indexZip(std::string& error);
    std::shared_ptr<std::vector<uint8_t>> readFile(const std::string& norm);
    static std::string normPath(const std::string& p);

    // ---- loading ----
    std::string m_exeName;               // name in the zip
    std::string m_exeDir;                // normalised directory of the exe ("" = root)
    struct Import { std::string dll; std::string name; int ordinal; };
    std::vector<Import> m_traps;         // trap index -> what it stands for
    std::vector<int> m_trapFn;           // trap index -> handler id
    uint32_t m_entry = 0, m_imageBase = 0;
    bool loadExe(std::string& error);

    // ---- memory ----
    std::vector<uint8_t> m_ram;
    std::map<uint32_t, uint32_t> m_used, m_free;        // heap blocks: address -> size
    uint32_t alloc(uint32_t size);
    void release(uint32_t addr);
    uint32_t resize(uint32_t addr, uint32_t size);
    uint32_t blockSize(uint32_t addr) const;
    uint32_t m_scratch = 0;
    bool readMem(uint32_t a, void* d, uint32_t n) const;
    bool writeMem(uint32_t a, const void* s, uint32_t n);
    uint32_t rd32(uint32_t a) const { uint32_t v = 0; readMem(a, &v, 4); return v; }
    void wr32(uint32_t a, uint32_t v) { writeMem(a, &v, 4); }
    void wr16(uint32_t a, uint16_t v) { writeMem(a, &v, 2); }
    std::string readStr(uint32_t a, uint32_t max = 4096) const;
    std::u16string readWStr(uint32_t a, uint32_t max = 4096) const;
    void writeStr(uint32_t a, const std::string& s);
    void writeWStr(uint32_t a, const std::u16string& s);

    // ---- CPU and calls ----
    didj::ARM926* m_cpu = nullptr;
    uint32_t arg(int i) const;
    double argD(int i) const;            // a double in arguments i, i+1
    void ret(uint32_t v) { m_cpu->hle_return(v); }
    void ret64(uint64_t v) { m_cpu->r[1] = uint32_t(v >> 32); m_cpu->hle_return(uint32_t(v)); }
    void retD(double d);
    bool onTrap(uint32_t pc);
    void dispatch(int fn, uint32_t trap);
    int m_nested = 0;                    // > 0 while a callback runs inside a call
    bool m_callbackDone = false;
    uint32_t callGuest(uint32_t fn, std::initializer_list<uint32_t> args);

    // ---- threads and kernel objects ----
    struct Thread {
        uint32_t id = 0, handle = 0;
        uint32_t r[16] = {}, cpsr = 0;
        bool done = false, suspended = false;
        uint64_t wakeAt = 0;             // sleeping until (µs); 0 = not sleeping
        uint64_t waitUntil = 0;          // wait deadline (µs, ~0 = forever) while blocked in a wait
        bool waiting = false;
        uint32_t stack = 0;
        uint32_t exitCode = 0;
        std::deque<std::array<uint32_t, 4>> queue;   // posted messages: hwnd, msg, wParam, lParam
    };
    std::vector<Thread> m_threads;
    int m_cur = 0;
    uint64_t m_us = 0;                   // emulated time in µs
    uint64_t m_insAtSync = 0;
    void syncTime();
    bool runnable(const Thread& t) const;
    int pickNext(int after);
    void switchTo(int idx);
    void block();                        // current thread can't go on: run another (or idle)
    bool m_idle = false;

    enum class Kind { None, Event, Mutex, Thread, File, Find, Wave, RegKey, Gdi };
    struct Obj {
        Kind kind = Kind::None;
        bool manual = false, signaled = false;
        uint32_t owner = 0, count = 0;   // mutex
        int thread = -1;                 // thread object
        std::string name;
        // files
        std::shared_ptr<std::vector<uint8_t>> data;
        std::string path;
        uint32_t pos = 0;
        bool writable = false;
        std::vector<std::string> findList; size_t findPos = 0;
    };
    std::map<uint32_t, Obj> m_objs;
    uint32_t m_nextHandle = 0x100;
    uint32_t newObj(Kind k) { uint32_t h = m_nextHandle; m_nextHandle += 4; m_objs[h].kind = k; return h; }
    Obj* obj(uint32_t h) { auto it = m_objs.find(h); return it == m_objs.end() ? nullptr : &it->second; }
    bool signaled(uint32_t h, bool consume);
    uint32_t m_lastError = 0;

    // ---- windows ----
    struct Window { uint32_t proc = 0; int thread = 0; std::u16string cls; };
    std::map<uint32_t, Window> m_windows;
    std::map<std::u16string, uint32_t> m_classes;       // class name -> window procedure
    uint32_t m_nextHwnd = 0x10000;
    uint32_t m_mainWnd = 0;
    bool m_quit = false;
    void postMessage(int thread, uint32_t hwnd, uint32_t msg, uint32_t w, uint32_t l);
    uint32_t sendMessage(uint32_t hwnd, uint32_t msg, uint32_t w, uint32_t l);
    int windowThread(uint32_t hwnd) const;

    // ---- input ----
    uint32_t m_keys = 0;
    uint8_t m_vkDown[256] = {};
    uint8_t m_vkPressed[256] = {};       // went down since the last GetAsyncKeyState
    void keyEvent(uint8_t vk, bool down);

    // ---- sound (waveOut) ----
    struct Wave {
        uint32_t handle = 0, rate = 22050, channels = 1, bits = 16;
        uint32_t cbType = 0, callback = 0, instance = 0;
        std::deque<uint32_t> queue;      // WAVEHDR addresses
        size_t pos = 0;                  // bytes into the front buffer
        uint64_t played = 0;             // bytes played
        double frac = 0;
        std::vector<uint32_t> done;      // finished headers waiting to be reported
    };
    std::map<uint32_t, Wave> m_waves;
    std::vector<int16_t> m_audio;
    void mixAudio(size_t samples);
    void waveDone();

    // ---- graphics ----
    didj::Hle m_hle;
    didj::GizSys m_sys;
    std::unique_ptr<didj::GLES> m_gles;
    bool m_swapped = false;
    std::vector<uint32_t> m_fb;

    // ---- registry (in memory) ----
    std::map<std::string, std::map<std::u16string, std::vector<uint8_t>>> m_reg;

    // ---- printf / scanf ----
    std::string format(const std::string& fmt, int firstArg, uint32_t vaList, bool wide);
    int scan(const std::string& in, const std::string& fmt, int firstArg);

    uint32_t m_rand = 1;
    uint64_t m_frame = 0;
    bool m_failed = false;
    void fail(const std::string& why);
    void log(const std::string& s);
};
