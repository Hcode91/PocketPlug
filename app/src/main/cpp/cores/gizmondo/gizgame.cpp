// PocketPlug - Gizmondo games run directly. See gizgame.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// COREDLL is imported by ordinal; the numbers below are Windows CE's (the same in
// every CE version the Gizmondo's games were built for).

#include "gizgame.h"
#include "wce_gles.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <zlib.h>

using didj::u32;

namespace {
int dbg() { static int v = getenv("GIZDBG") ? atoi(getenv("GIZDBG")) : 0; return v; }

constexpr uint32_t HEAP_BASE = 0x00800000, HEAP_END = 0x03f00000;
constexpr uint32_t SCRATCH_BASE = 0x03f00000, SCRATCH_END = 0x04000000;
constexpr uint64_t MIPS = 100;       // emulated instructions per µs
constexpr uint32_t WM_CREATE = 0x0001, WM_DESTROY = 0x0002, WM_CLOSE = 0x0010, WM_QUIT = 0x0012,
                   WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, MM_WOM_OPEN = 0x3bb, MM_WOM_DONE = 0x3bd;

enum Fn {
    F_UNKNOWN = 0,
#define C(n, o) F_##n,
#include "wce_coredll.inc"
#undef C
    F_GL, F_EGL_SWAP, F_EGL_SWAPINTERVALNV, F_WININET, F_INTERNETOPEN, F_COINIT, F_COUNINIT,
};
struct OrdName { int ord; const char* name; int fn; };
const OrdName g_coredll[] = {
#define C(n, o) { o, #n, F_##n },
#include "wce_coredll.inc"
#undef C
};

uint32_t f2u(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float u2f(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
double u2d(uint32_t lo, uint32_t hi) { uint64_t v = uint64_t(hi) << 32 | lo; double d; std::memcpy(&d, &v, 8); return d; }
uint64_t d2u(double d) { uint64_t v; std::memcpy(&v, &d, 8); return v; }
std::string lower(std::string s) { for (auto& c : s) c = char(tolower((unsigned char)c)); return s; }
std::string narrow(const std::u16string& w) { std::string s; for (char16_t c : w) s += c < 0x100 ? char(c) : '?'; return s; }
std::u16string widen(const std::string& s) { std::u16string w; for (unsigned char c : s) w += char16_t(c); return w; }
bool wildMatch(const char* p, const char* s)
{
    if (*p == 0) return *s == 0;
    if (*p == '*') return wildMatch(p + 1, s) || (*s && wildMatch(p, s + 1));
    if (*s && (*p == '?' || *p == *s)) return wildMatch(p + 1, s + 1);
    return false;
}
}

GizGameCore::GizGameCore() : m_ram(RAM_SIZE, 0), m_fb(320 * 240, 0xff000000u)
{
    m_hle.readMem = [this](u32 a, void* d, u32 n) { return readMem(a, d, n); };
    m_hle.writeMem = [this](u32 a, const void* s, u32 n) { return writeMem(a, s, n); };
    m_hle.scratch = [this](u32 n) -> u32 {
        n = (n + 7) & ~7u;
        if (m_scratch + n > SCRATCH_END) return 0;
        u32 a = m_scratch; m_scratch += n; return a;
    };
    m_hle.log = [this](const std::string& s) { log(s); };
    m_hle.sys = &m_sys;
    m_hle.log_calls = dbg() > 2;
    m_sys.present = [this](const u32* px, int w, int h) {
        for (int y = 0; y < 240 && y < h; y++)
            for (int x = 0; x < 320 && x < w; x++) m_fb[y * 320 + x] = px[y * w + x] | 0xff000000u;
        if (dbg() > 3) {
            int nz = 0; for (int i = 0; i < w * h; i++) if (px[i] & 0xffffff) nz++;
            fprintf(stderr, "present %dx%d, %d lit pixels\n", w, h, nz);
        }
    };
}

GizGameCore::~GizGameCore() { delete m_cpu; }

void GizGameCore::log(const std::string& s)
{
    if (dbg()) fprintf(stderr, "%s\n", s.c_str());
    debugLog += s; debugLog += '\n';
    if (debugLog.size() > 200000) debugLog.erase(0, 100000);
}

void GizGameCore::fail(const std::string& why)
{
    log("STOP: " + why);
    m_failed = true;
}

// ---------------------------------------------------------------- zip

std::string GizGameCore::normPath(const std::string& p)
{
    std::string s;
    for (char c : p) {
        char d = c == '\\' ? '/' : char(tolower((unsigned char)c));
        if (d == '/' && (s.empty() || s.back() == '/')) continue;
        s += d;
    }
    while (!s.empty() && s.back() == '/') s.pop_back();
    for (const char* pre : { "sd card/", "storage card/", "sdmmc/", "sd/" })
        if (s.compare(0, strlen(pre), pre) == 0) { s.erase(0, strlen(pre)); break; }
    // "a/./b", "a/../b"
    std::vector<std::string> parts; size_t i = 0;
    while (i <= s.size()) {
        size_t e = s.find('/', i); if (e == std::string::npos) e = s.size();
        std::string seg = s.substr(i, e - i);
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        i = e + 1;
    }
    std::string out;
    for (auto& q : parts) { if (!out.empty()) out += '/'; out += q; }
    return out;
}

bool GizGameCore::indexZip(std::string& error)
{
    const auto& z = m_zip;
    auto u16 = [&](size_t o) { return uint32_t(z[o] | z[o + 1] << 8); };
    auto u32v = [&](size_t o) { return uint32_t(z[o] | z[o + 1] << 8 | z[o + 2] << 16 | uint32_t(z[o + 3]) << 24); };
    if (z.size() < 22) { error = "The game file is not a zip"; return false; }
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22; i + 1 > 0 && z.size() - i < 70000; i--) {
        if (u32v(i) == 0x06054b50) { eocd = i; break; }
        if (i == 0) break;
    }
    if (eocd == std::string::npos) { error = "The game file is not a zip"; return false; }
    uint32_t n = u16(eocd + 10), cd = u32v(eocd + 16);
    size_t p = cd;
    for (uint32_t k = 0; k < n && p + 46 <= z.size(); k++) {
        if (u32v(p) != 0x02014b50) break;
        ZipEntry e;
        e.method = uint16_t(u16(p + 10));
        e.csize = u32v(p + 20); e.usize = u32v(p + 24);
        uint32_t nl = u16(p + 28), xl = u16(p + 30), cl = u16(p + 32);
        uint32_t lho = u32v(p + 42);
        std::string name(reinterpret_cast<const char*>(&z[p + 46]), nl);
        p += 46 + nl + xl + cl;
        if (lho + 30 > z.size()) continue;
        e.off = lho + 30 + u16(lho + 26) + u16(lho + 28);
        std::string norm = normPath(name);
        if (name.empty() || name.back() == '/') { m_dirs.insert(norm); continue; }
        m_zipIndex[norm] = e;
        for (size_t s = norm.find('/'); s != std::string::npos; s = norm.find('/', s + 1)) m_dirs.insert(norm.substr(0, s));
    }
    if (m_zipIndex.empty()) { error = "The game zip is empty"; return false; }
    // A zip holding one top-level folder (the SD card copied into a folder): use that folder as the card
    std::set<std::string> tops;
    for (auto& kv : m_zipIndex) tops.insert(kv.first.substr(0, kv.first.find('/')));
    if (tops.size() == 1 && m_dirs.count(*tops.begin())) {
        std::string top = *tops.begin() + "/";
        std::map<std::string, ZipEntry> idx; std::set<std::string> dirs;
        for (auto& kv : m_zipIndex) idx[kv.first.substr(top.size())] = kv.second;
        for (auto& d : m_dirs) if (d.size() > top.size()) dirs.insert(d.substr(top.size()));
        bool hasExe = false;
        for (auto& kv : idx) if (kv.first.find('/') == std::string::npos && kv.first.size() > 4 && kv.first.compare(kv.first.size() - 4, 4, ".exe") == 0) hasExe = true;
        if (hasExe) { m_zipIndex.swap(idx); m_dirs.swap(dirs); }
    }
    return true;
}

std::shared_ptr<std::vector<uint8_t>> GizGameCore::readFile(const std::string& norm)
{
    auto w = m_written.find(norm);
    if (w != m_written.end()) return w->second;
    auto it = m_zipIndex.find(norm);
    if (it == m_zipIndex.end()) return nullptr;
    const ZipEntry& e = it->second;
    auto out = std::make_shared<std::vector<uint8_t>>(e.usize);
    if (size_t(e.off) + e.csize > m_zip.size()) return nullptr;
    if (e.method == 0) std::memcpy(out->data(), &m_zip[e.off], e.usize);
    else if (e.method == 8) {
        z_stream s{};
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return nullptr;
        s.next_in = const_cast<Bytef*>(&m_zip[e.off]); s.avail_in = e.csize;
        s.next_out = out->data(); s.avail_out = e.usize;
        inflate(&s, Z_FINISH);
        inflateEnd(&s);
    } else return nullptr;
    return out;
}

// ---------------------------------------------------------------- memory

bool GizGameCore::readMem(uint32_t a, void* d, uint32_t n) const
{
    if (uint64_t(a) + n > RAM_SIZE) { std::memset(d, 0, n); return false; }
    std::memcpy(d, &m_ram[a], n);
    return true;
}

bool GizGameCore::writeMem(uint32_t a, const void* s, uint32_t n)
{
    if (uint64_t(a) + n > RAM_SIZE) return false;
    std::memcpy(&m_ram[a], s, n);
    return true;
}

uint32_t GizGameCore::read32(uint32_t pa) { return pa < RAM_SIZE - 3 ? rd32(pa & ~3u) : 0; }
uint16_t GizGameCore::read16(uint32_t pa) { uint16_t v = 0; if (pa < RAM_SIZE - 1) std::memcpy(&v, &m_ram[pa & ~1u], 2); return v; }
uint8_t GizGameCore::read8(uint32_t pa) { return pa < RAM_SIZE ? m_ram[pa] : 0; }
void GizGameCore::write32(uint32_t pa, uint32_t v) { if (pa < RAM_SIZE - 3) std::memcpy(&m_ram[pa & ~3u], &v, 4); }
void GizGameCore::write16(uint32_t pa, uint16_t v) { if (pa < RAM_SIZE - 1) std::memcpy(&m_ram[pa & ~1u], &v, 2); }
void GizGameCore::write8(uint32_t pa, uint8_t v) { if (pa < RAM_SIZE) m_ram[pa] = v; }

std::string GizGameCore::readStr(uint32_t a, uint32_t max) const
{
    std::string s;
    while (a && a < RAM_SIZE && s.size() < max && m_ram[a]) s += char(m_ram[a++]);
    return s;
}

std::u16string GizGameCore::readWStr(uint32_t a, uint32_t max) const
{
    std::u16string s;
    while (a && a + 1 < RAM_SIZE && s.size() < max) {
        char16_t c = char16_t(m_ram[a] | m_ram[a + 1] << 8);
        if (!c) break;
        s += c; a += 2;
    }
    return s;
}

void GizGameCore::writeStr(uint32_t a, const std::string& s) { writeMem(a, s.c_str(), uint32_t(s.size() + 1)); }
void GizGameCore::writeWStr(uint32_t a, const std::u16string& s)
{
    for (size_t i = 0; i <= s.size(); i++) wr16(a + uint32_t(i * 2), i < s.size() ? uint16_t(s[i]) : 0);
}

uint32_t GizGameCore::alloc(uint32_t size)
{
    size = std::max<uint32_t>((size + 15) & ~15u, 16);
    for (auto it = m_free.begin(); it != m_free.end(); ++it) {
        if (it->second < size) continue;
        uint32_t a = it->first, left = it->second - size;
        m_free.erase(it);
        if (left) m_free[a + size] = left;
        m_used[a] = size;
        std::memset(&m_ram[a], 0, size);
        return a;
    }
    log("out of memory for " + std::to_string(size) + " bytes");
    return 0;
}

void GizGameCore::release(uint32_t addr)
{
    auto it = m_used.find(addr);
    if (it == m_used.end()) return;
    uint32_t a = addr, s = it->second;
    m_used.erase(it);
    auto nx = m_free.lower_bound(a);
    if (nx != m_free.end() && a + s == nx->first) { s += nx->second; nx = m_free.erase(nx); }
    if (nx != m_free.begin()) {
        auto pv = std::prev(nx);
        if (pv->first + pv->second == a) { pv->second += s; return; }
    }
    m_free[a] = s;
}

uint32_t GizGameCore::blockSize(uint32_t addr) const
{
    auto it = m_used.find(addr);
    return it == m_used.end() ? 0 : it->second;
}

uint32_t GizGameCore::resize(uint32_t addr, uint32_t size)
{
    if (!addr) return alloc(size);
    if (!size) { release(addr); return 0; }
    uint32_t old = blockSize(addr);
    if (old >= size) return addr;
    uint32_t n = alloc(size);
    if (!n) return 0;
    std::memmove(&m_ram[n], &m_ram[addr], old);
    release(addr);
    return n;
}

// ---------------------------------------------------------------- loading

bool GizGameCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_zip.clear();
    for (auto& r : roms) if (r.data.size() > 4 && r.data[0] == 'P' && r.data[1] == 'K') m_zip = r.data;
    if (m_zip.empty()) { error = "Pick a game: a .zip of the game's SD card files (its .exe and data folder)"; return false; }
    m_zipIndex.clear(); m_dirs.clear();
    if (!indexZip(error)) return false;
    // the game program: an .exe at the top of the card that isn't the launcher
    uint32_t best = 0;
    for (auto& kv : m_zipIndex) {
        const std::string& n = kv.first;
        if (n.find('/') != std::string::npos || n.size() < 5 || n.compare(n.size() - 4, 4, ".exe")) continue;
        uint32_t sz = kv.second.usize + (n == "autorun.exe" ? 0 : 0x10000000u);
        if (sz > best) { best = sz; m_exeName = n; }
    }
    if (m_exeName.empty()) { error = "No .exe found at the top of the game zip"; return false; }
    m_exeDir = "";
    delete m_cpu;
    m_cpu = new didj::ARM926(this);
    m_hle.cpu = m_cpu;
    m_gles.reset(new didj::GLES(&m_hle));
    reset();
    if (m_failed) { error = debugLog; return false; }
    return true;
}

bool GizGameCore::loadExe(std::string& error)
{
    auto img = readFile(m_exeName);
    if (!img || img->size() < 0x200) { error = "Can't read " + m_exeName; return false; }
    const auto& d = *img;
    auto u16 = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8); };
    auto u32v = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24); };
    uint32_t pe = u32v(0x3c);
    if (pe + 0xf8 > d.size() || u32v(pe) != 0x4550) { error = m_exeName + " isn't a Windows program"; return false; }
    uint32_t machine = u16(pe + 4);
    if (machine != 0x1c0 && machine != 0x1c2) { error = m_exeName + " isn't an ARM program"; return false; }
    uint32_t nsec = u16(pe + 6), optSize = u16(pe + 20);
    uint32_t opt = pe + 24;
    m_imageBase = u32v(opt + 28);
    m_entry = m_imageBase + u32v(opt + 16);
    uint32_t imageSize = u32v(opt + 56), hdrSize = u32v(opt + 60);
    if (m_imageBase < 0x10000 || m_imageBase + imageSize > HEAP_BASE) { error = "The program's load address isn't supported"; return false; }
    std::memcpy(&m_ram[m_imageBase], d.data(), std::min<size_t>(hdrSize, d.size()));
    uint32_t sec = opt + optSize;
    for (uint32_t i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsize = u32v(sec + 8), va = u32v(sec + 12), rsize = u32v(sec + 16), raw = u32v(sec + 20);
        uint32_t n = std::min(rsize, vsize ? vsize : rsize);
        if (raw + n > d.size()) n = raw < d.size() ? uint32_t(d.size() - raw) : 0;
        if (m_imageBase + va + std::max(vsize, n) > HEAP_BASE) { error = "Section too large"; return false; }
        std::memcpy(&m_ram[m_imageBase + va], &d[raw], n);
    }
    // imports: every one points at its own trap address
    uint32_t impRva = u32v(opt + 104);
    m_traps.clear(); m_trapFn.clear();
    std::map<std::string, int> glFns;
    for (uint32_t desc = m_imageBase + impRva; impRva && rd32(desc + 12); desc += 20) {
        std::string dll = lower(readStr(m_imageBase + rd32(desc + 12)));
        uint32_t ilt = rd32(desc) ? rd32(desc) : rd32(desc + 16);
        uint32_t iat = rd32(desc + 16);
        for (uint32_t k = 0;; k++) {
            uint32_t ent = rd32(m_imageBase + ilt + k * 4);
            if (!ent) break;
            Import im{ dll, "", -1 };
            if (ent & 0x80000000u) im.ordinal = int(ent & 0xffff);
            else im.name = readStr(m_imageBase + (ent & 0x7fffffff) + 2);
            int fn = F_UNKNOWN;
            if (dll == "coredll.dll") {
                for (auto& o : g_coredll)
                    if ((im.ordinal >= 0 && o.ord == im.ordinal) || (im.ordinal < 0 && im.name == o.name)) { fn = o.fn; if (im.name.empty()) im.name = o.name; break; }
            } else if (dll == "libgles_cm.dll" || dll == "libgles_cl.dll") {
                if (im.name == "eglSwapBuffers") fn = F_EGL_SWAP;
                else if (im.name == "eglSwapIntervalNV") fn = F_EGL_SWAPINTERVALNV;
                else fn = F_GL;
            } else if (dll == "wininet.dll") fn = im.name == "InternetOpenW" ? F_INTERNETOPEN : F_WININET;
            else if (dll == "ole32.dll") fn = im.name == "CoInitializeEx" ? F_COINIT : F_COUNINIT;
            if (im.name.empty()) im.name = "#" + std::to_string(im.ordinal);
            uint32_t idx = uint32_t(m_traps.size());
            if (idx * 4 >= TRAP_SIZE - 0x20) { error = "Too many imports"; return false; }
            m_traps.push_back(im);
            m_trapFn.push_back(fn);
            wr32(m_imageBase + iat + k * 4, TRAP_BASE + idx * 4);
            if (fn == F_UNKNOWN) log("import not handled: " + dll + " " + im.name);
        }
    }
    if (dbg()) fprintf(stderr, "loaded %s at %08x, entry %08x, %zu imports\n", m_exeName.c_str(), m_imageBase, m_entry, m_traps.size());
    return true;
}

void GizGameCore::reset()
{
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_used.clear(); m_free.clear();
    m_free[HEAP_BASE] = HEAP_END - HEAP_BASE;
    m_scratch = SCRATCH_BASE;
    m_threads.clear(); m_objs.clear(); m_windows.clear(); m_classes.clear(); m_waves.clear();
    m_written.clear(); m_reg.clear();
    m_nextHandle = 0x100; m_nextHwnd = 0x10000; m_mainWnd = 0;
    m_quit = m_failed = false;
    m_us = 0; m_frame = 0; m_nested = 0;
    std::memset(m_vkDown, 0, sizeof m_vkDown); std::memset(m_vkPressed, 0, sizeof m_vkPressed);
    m_audio.clear();
    debugLog.clear();
    std::fill(m_fb.begin(), m_fb.end(), 0xff000000u);
    if (m_gles) m_gles->reset();
    std::string err;
    if (!loadExe(err)) { fail(err); return; }

    m_cpu->reset();
    m_cpu->hle_base = TRAP_BASE; m_cpu->hle_size = TRAP_SIZE;
    m_cpu->hle_user = this;
    m_cpu->hle_hook = [](didj::ARM926*, uint32_t pc, void* u) { return static_cast<GizGameCore*>(u)->onTrap(pc); };
    m_insAtSync = m_cpu->insn_count;

    // the main thread: WinMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd)
    Thread t;
    t.id = 0x1000; t.handle = newObj(Kind::Thread); m_objs[t.handle].thread = 0;
    t.stack = alloc(0x100000);
    uint32_t cmd = alloc(4);
    t.r[0] = m_imageBase; t.r[1] = 0; t.r[2] = cmd; t.r[3] = 5;
    t.r[13] = t.stack + 0x100000 - 16;
    t.r[14] = TRAP_THREAD_EXIT;
    t.r[15] = m_entry & ~1u;
    t.cpsr = 0x1f | 0xc0 | ((m_entry & 1) ? 0x20 : 0);
    m_threads.push_back(t);
    m_cur = 0;
    for (int i = 0; i < 16; i++) m_cpu->r[i] = t.r[i];
    m_cpu->cpsr = t.cpsr;
}

// ---------------------------------------------------------------- threads

void GizGameCore::syncTime()
{
    uint64_t n = m_cpu->insn_count;
    uint64_t d = (n - m_insAtSync) / MIPS;
    m_us += d;
    m_insAtSync += d * MIPS;
}

bool GizGameCore::runnable(const Thread& t) const
{
    if (t.done || t.suspended) return false;
    if (t.wakeAt) return m_us >= t.wakeAt;
    if (t.waiting) return true;    // re-runs its wait call, which checks the object and the deadline
    return true;
}

int GizGameCore::pickNext(int after)
{
    int n = int(m_threads.size());
    for (int k = 1; k <= n; k++) {
        int i = (after + k) % n;
        const Thread& t = m_threads[i];
        if (!runnable(t)) continue;
        if (t.waiting) {
            // only worth switching to when its wait can finish
            bool ready = m_us >= t.waitUntil;
            if (!ready) {
                // peek at the object (the call itself consumes it)
                uint32_t h = t.r[0];
                Obj* o = const_cast<GizGameCore*>(this)->obj(h);
                if (o) {
                    if (o->kind == Kind::Event) ready = o->signaled;
                    else if (o->kind == Kind::Mutex) ready = o->count == 0 || o->owner == t.id;
                    else if (o->kind == Kind::Thread) ready = o->thread >= 0 && m_threads[size_t(o->thread)].done;
                }
                if (!t.queue.empty()) ready = true;
            }
            if (!ready) continue;
        }
        return i;
    }
    return -1;
}

void GizGameCore::switchTo(int idx)
{
    if (idx == m_cur) return;
    Thread& a = m_threads[size_t(m_cur)];
    for (int i = 0; i < 16; i++) a.r[i] = m_cpu->r[i];
    a.cpsr = m_cpu->cpsr;
    m_cur = idx;
    Thread& b = m_threads[size_t(idx)];
    for (int i = 0; i < 16; i++) m_cpu->r[i] = b.r[i];
    m_cpu->cpsr = b.cpsr;
}

// The current thread can't continue (its PC stays on the call, which runs again when
// it is picked): carry on with another thread, or stop this run if none can go.
void GizGameCore::block()
{
    int n = pickNext(m_cur);
    if (n >= 0 && n != m_cur) { switchTo(n); return; }
    m_idle = true;
}

bool GizGameCore::signaled(uint32_t h, bool consume)
{
    Obj* o = obj(h);
    if (!o) return true;
    Thread& t = m_threads[size_t(m_cur)];
    switch (o->kind) {
    case Kind::Event:
        if (!o->signaled) return false;
        if (consume && !o->manual) o->signaled = false;
        return true;
    case Kind::Mutex:
        if (o->count && o->owner != t.id) return false;
        if (consume) { o->owner = t.id; o->count++; }
        return true;
    case Kind::Thread:
        return o->thread < 0 || m_threads[size_t(o->thread)].done;
    default:
        return true;
    }
}

// ---------------------------------------------------------------- calls

uint32_t GizGameCore::arg(int i) const
{
    return i < 4 ? m_cpu->r[i] : rd32(m_cpu->r[13] + uint32_t(i - 4) * 4);
}

double GizGameCore::argD(int i) const { return u2d(arg(i), arg(i + 1)); }

void GizGameCore::retD(double d)
{
    uint64_t v = d2u(d);
    m_cpu->r[1] = uint32_t(v >> 32);
    m_cpu->hle_return(uint32_t(v));
}

uint32_t GizGameCore::callGuest(uint32_t fn, std::initializer_list<uint32_t> args)
{
    uint32_t save[16]; std::memcpy(save, m_cpu->r, sizeof save);
    uint32_t saveCpsr = m_cpu->cpsr;
    std::vector<uint32_t> a(args);
    uint32_t sp = (m_cpu->r[13] - 64) & ~7u;
    if (a.size() > 4) {
        sp -= uint32_t((a.size() - 4) * 4);
        for (size_t i = 4; i < a.size(); i++) wr32(sp + uint32_t((i - 4) * 4), a[i]);
    }
    for (size_t i = 0; i < 4; i++) m_cpu->r[i] = i < a.size() ? a[i] : 0;
    m_cpu->r[13] = sp;
    m_cpu->r[14] = TRAP_RETURN;
    m_cpu->r[15] = fn & ~1u;
    m_cpu->cpsr = (m_cpu->cpsr & ~0x20u) | ((fn & 1) ? 0x20u : 0);
    m_nested++;
    bool done = m_callbackDone; m_callbackDone = false;
    bool idle = m_idle; m_idle = false;
    for (int guard = 0; !m_callbackDone && !m_failed; guard++) {
        if (guard > 200000) { fail("a callback never returned"); break; }
        m_cpu->run(20000);
        syncTime();
    }
    uint32_t r = m_cpu->r[0];
    m_callbackDone = done;
    m_idle = idle;
    m_nested--;
    std::memcpy(m_cpu->r, save, sizeof save);
    m_cpu->cpsr = saveCpsr;
    return r;
}

bool GizGameCore::onTrap(uint32_t pc)
{
    if (pc == TRAP_RETURN) { m_callbackDone = true; return true; }
    if (m_failed || (m_idle && !m_nested)) return true;   // spin out the rest of this run
    if (m_callbackDone && m_nested) return true;
    if (pc == TRAP_THREAD_EXIT) {
        Thread& t = m_threads[size_t(m_cur)];
        t.done = true; t.exitCode = m_cpu->r[0];
        log("thread " + std::to_string(t.id) + " ended");
        if (m_cur == 0) { m_quit = true; m_idle = true; log("the game exited"); return true; }
        block();
        return true;
    }
    uint32_t idx = (pc - TRAP_BASE) / 4;
    if (idx >= m_traps.size()) { fail("jump into an unknown system address"); return true; }
    if (dbg() > 1) {
        char b[160];
        snprintf(b, sizeof b, "call %s %s(%08x, %08x, %08x, %08x) lr %08x t%u", m_traps[idx].dll.c_str(), m_traps[idx].name.c_str(),
                 m_cpu->r[0], m_cpu->r[1], m_cpu->r[2], m_cpu->r[3], m_cpu->r[14], m_threads[size_t(m_cur)].id);
        log(b);
    }
    dispatch(m_trapFn[idx], idx);
    return true;
}

// ---------------------------------------------------------------- windows / input / sound helpers

int GizGameCore::windowThread(uint32_t hwnd) const
{
    auto it = m_windows.find(hwnd);
    return it == m_windows.end() ? 0 : it->second.thread;
}

void GizGameCore::postMessage(int thread, uint32_t hwnd, uint32_t msg, uint32_t w, uint32_t l)
{
    if (thread < 0 || size_t(thread) >= m_threads.size()) return;
    m_threads[size_t(thread)].queue.push_back({ hwnd, msg, w, l });
}

uint32_t GizGameCore::sendMessage(uint32_t hwnd, uint32_t msg, uint32_t w, uint32_t l)
{
    auto it = m_windows.find(hwnd);
    if (it == m_windows.end() || !it->second.proc) return 0;
    return callGuest(it->second.proc, { hwnd, msg, w, l });
}

void GizGameCore::keyEvent(uint8_t vk, bool down)
{
    if (down && !m_vkDown[vk]) m_vkPressed[vk] = 1;
    m_vkDown[vk] = down;
    if (m_mainWnd)
        postMessage(windowThread(m_mainWnd), m_mainWnd, down ? WM_KEYDOWN : WM_KEYUP, vk, down ? 1u : 0xc0000001u);
}

void GizGameCore::setInputs(uint32_t pressed)
{
    // the Gizmondo's keys as the games see them
    static const struct { uint32_t bit; uint8_t vk; } map[] = {
        { 1u << 0, 0x26 }, { 1u << 1, 0x28 }, { 1u << 2, 0x25 }, { 1u << 3, 0x27 },     // D-pad: arrows
        { 1u << 4, 0x0d }, { 1u << 5, 0x22 }, { 1u << 6, 0x21 }, { 1u << 7, 0x1b },     // play, forward, rewind, stop
        { 1u << 8, 0x51 }, { 1u << 9, 0x45 },                                           // L, R
        { 1u << 10, 0x70 }, { 1u << 11, 0x71 }, { 1u << 12, 0x72 }, { 1u << 13, 0x73 }, { 1u << 14, 0x74 },   // F1-F5
    };
    uint32_t changed = pressed ^ m_keys;
    m_keys = pressed;
    for (auto& m : map) if (changed & m.bit) keyEvent(m.vk, (pressed & m.bit) != 0);
}

void GizGameCore::mixAudio(size_t samples)
{
    std::vector<int32_t> acc(samples, 0);
    for (auto& kv : m_waves) {
        Wave& w = kv.second;
        if (!w.rate) continue;
        double step = double(w.rate) / 44100.0;
        uint32_t frameBytes = w.channels * (w.bits / 8);
        for (size_t i = 0; i < samples && !w.queue.empty(); i++) {
            uint32_t hdr = w.queue.front();
            uint32_t data = rd32(hdr), len = rd32(hdr + 4);
            if (w.pos + frameBytes > len) {
                w.done.push_back(hdr); w.queue.pop_front(); w.pos = 0; w.frac = 0;
                i--; continue;
            }
            int32_t s = 0;
            for (uint32_t c = 0; c < w.channels; c++) {
                uint32_t a = data + uint32_t(w.pos) + c * (w.bits / 8);
                if (w.bits == 16) s += int16_t(read16(a));
                else s += (int32_t(read8(a)) - 128) << 8;
            }
            acc[i] += s / int32_t(w.channels);
            w.frac += step;
            uint32_t adv = uint32_t(w.frac);
            w.frac -= adv;
            w.pos += adv * frameBytes;
            w.played += adv * frameBytes;
        }
    }
    for (size_t i = 0; i < samples; i++) m_audio.push_back(int16_t(std::max(-32768, std::min(32767, acc[i]))));
}

void GizGameCore::waveDone()
{
    for (auto& kv : m_waves) {
        Wave& w = kv.second;
        std::vector<uint32_t> list; list.swap(w.done);
        for (uint32_t hdr : list) {
            wr32(hdr + 16, (rd32(hdr + 16) | 1u) & ~0x10u);      // WHDR_DONE, no longer queued
            switch (w.cbType) {
            case 0x30000: if (w.callback) callGuest(w.callback, { w.handle, MM_WOM_DONE, w.instance, hdr, 0 }); break;
            case 0x50000: if (Obj* o = obj(w.callback)) o->signaled = true; break;
            case 0x10000: postMessage(windowThread(w.callback), w.callback, MM_WOM_DONE, w.handle, hdr); break;
            case 0x20000:
                for (size_t i = 0; i < m_threads.size(); i++)
                    if (m_threads[i].id == w.callback) postMessage(int(i), 0, MM_WOM_DONE, w.handle, hdr);
                break;
            }
        }
    }
}

size_t GizGameCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

// ---------------------------------------------------------------- frame

void GizGameCore::runFrame()
{
    m_frame++;
    if (!m_failed && !m_quit) {
        uint64_t frameEnd = m_us + 16667;
        m_swapped = false;
        for (int guard = 0; !m_swapped && m_us < frameEnd && !m_failed && !m_quit && guard < 100000; guard++) {
            if (!runnable(m_threads[size_t(m_cur)]) || m_threads[size_t(m_cur)].waiting) {
                int n = pickNext(m_cur);
                if (n < 0 && runnable(m_threads[size_t(m_cur)]) && m_threads[size_t(m_cur)].waiting
                    && m_us >= m_threads[size_t(m_cur)].waitUntil) n = m_cur;
                if (n < 0) {
                    // everyone is waiting: jump to the next wake-up
                    uint64_t next = frameEnd;
                    for (auto& t : m_threads) {
                        if (t.done || t.suspended) continue;
                        if (t.wakeAt) next = std::min(next, t.wakeAt);
                        if (t.waiting) next = std::min(next, t.waitUntil);
                    }
                    m_us = std::max(m_us + 1, std::min(next, frameEnd));
                    // keep waits fair: let the current thread re-check its call
                    continue;
                }
                switchTo(n);
            }
            m_idle = false;
            m_cpu->run(30000);
            syncTime();
            m_idle = false;
            int n = pickNext(m_cur);
            if (n >= 0 && n != m_cur && !m_swapped) switchTo(n);
        }
        // a buffer swap waits for the display's next refresh
        if (m_swapped && m_us < frameEnd) { m_us = frameEnd; m_insAtSync = m_cpu->insn_count; }
    }
    mixAudio(735);
    waveDone();
    if (dbg() && m_frame % 60 == 0) {
        char b[200];
        snprintf(b, sizeof b, "[frame %llu, %.2f s, %llu instructions]", (unsigned long long)m_frame, m_us / 1e6, (unsigned long long)m_cpu->insn_count);
        log(b);
        for (auto& t : m_threads) {
            uint32_t pc = (&t == &m_threads[size_t(m_cur)]) ? m_cpu->r[15] : t.r[15];
            std::string at = pc >= TRAP_BASE && (pc - TRAP_BASE) / 4 < m_traps.size() ? m_traps[(pc - TRAP_BASE) / 4].name : "";
            snprintf(b, sizeof b, "  thread %x pc %08x %s%s%s%s", t.id, pc, at.c_str(), t.done ? " done" : "", t.waiting ? " waiting" : "", t.wakeAt ? " sleeping" : "");
            log(b);
        }
    }
    if (m_failed || m_quit) {
        // dim the last picture so it's clear the game stopped
        for (auto& p : m_fb) p = 0xff000000u | ((p >> 1) & 0x7f7f7f);
    }
}
