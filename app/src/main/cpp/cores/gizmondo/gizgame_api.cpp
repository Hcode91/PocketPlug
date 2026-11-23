// PocketPlug - the Windows CE calls the Gizmondo games make. See gizgame.h.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gizgame.h"
#include "wce_gles.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <numeric>

using didj::u32;

namespace {
int dbg() { static int v = getenv("GIZDBG") ? atoi(getenv("GIZDBG")) : 0; return v; }
enum Fn {
    F_UNKNOWN = 0,
#define C(n, o) F_##n,
#include "wce_coredll.inc"
#undef C
    F_GL, F_EGL_SWAP, F_EGL_SWAPINTERVALNV, F_WININET, F_INTERNETOPEN, F_COINIT, F_COUNINIT,
};
constexpr uint32_t INVALID_HANDLE = 0xffffffffu, WAIT_TIMEOUT = 0x102, INFINITE_MS = 0xffffffffu;
constexpr uint32_t WM_CREATE = 0x0001, WM_DESTROY = 0x0002, WM_CLOSE = 0x0010, WM_QUIT = 0x0012, MM_WOM_OPEN = 0x3bb;

uint32_t f2u(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float u2f(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
double u2d(uint32_t lo, uint32_t hi) { uint64_t v = uint64_t(hi) << 32 | lo; double d; std::memcpy(&d, &v, 8); return d; }
std::string narrow(const std::u16string& w) { std::string s; for (char16_t c : w) s += c < 0x100 ? char(c) : '?'; return s; }
std::u16string widen(const std::string& s) { std::u16string w; for (unsigned char c : s) w += char16_t(c); return w; }
std::u16string wlower(std::u16string s) { for (auto& c : s) if (c < 128) c = char16_t(tolower(c)); return s; }
bool wildMatch(const char* p, const char* s)
{
    if (*p == 0) return *s == 0;
    if (*p == '*') return wildMatch(p + 1, s) || (*s && wildMatch(p, s + 1));
    if (*s && (*p == '?' || *p == *s)) return wildMatch(p + 1, s + 1);
    return false;
}
int ctypeOf(int c)
{
    int r = 0;
    if (c < 0 || c > 255) return 0;
    if (isupper(c)) r |= 1;
    if (islower(c)) r |= 2;
    if (isdigit(c)) r |= 4;
    if (isspace(c)) r |= 8;
    if (ispunct(c)) r |= 0x10;
    if (iscntrl(c)) r |= 0x20;
    if (c == ' ' || c == '\t') r |= 0x40;
    if (isxdigit(c)) r |= 0x80;
    if (isalpha(c)) r |= 0x100;
    return r;
}
}

// ---------------------------------------------------------------- printf / scanf

// Arguments are read as 32-bit words from argument number firstArg on (or from a
// va_list in memory); 64-bit values (doubles, %I64 / %ll) take two words.
std::string GizGameCore::format(const std::string& f, int firstArg, uint32_t vaList, bool wide)
{
    int ai = firstArg;
    uint32_t vp = vaList;
    auto next = [&]() -> uint32_t {
        if (vaList) { uint32_t v = rd32(vp); vp += 4; return v; }
        return arg(ai++);
    };
    std::string out;
    for (size_t i = 0; i < f.size(); i++) {
        char c = f[i];
        if (c != '%') { out += c; continue; }
        std::string spec = "%";
        i++;
        while (i < f.size() && strchr("-+ #0", f[i])) spec += f[i++];
        if (i < f.size() && f[i] == '*') { spec += std::to_string(int32_t(next())); i++; }
        else while (i < f.size() && isdigit((unsigned char)f[i])) spec += f[i++];
        if (i < f.size() && f[i] == '.') {
            spec += f[i++];
            if (i < f.size() && f[i] == '*') { spec += std::to_string(int32_t(next())); i++; }
            else while (i < f.size() && isdigit((unsigned char)f[i])) spec += f[i++];
        }
        bool l64 = false, shortArg = false, longArg = false;
        while (i < f.size()) {
            if (f.compare(i, 3, "I64") == 0) { l64 = true; i += 3; }
            else if (f.compare(i, 2, "ll") == 0) { l64 = true; i += 2; }
            else if (f[i] == 'l') { longArg = true; i++; }
            else if (f[i] == 'h') { shortArg = true; i++; }
            else if (f[i] == 'L' || f[i] == 'w') { longArg = true; i++; }
            else break;
        }
        if (i >= f.size()) break;
        char t = f[i];
        char buf[512];
        switch (t) {
        case '%': out += '%'; break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            if (l64) {
                uint32_t lo = next(), hi = next();
                long long v = (long long)(uint64_t(hi) << 32 | lo);
                std::string s = spec + "ll" + t;
                snprintf(buf, sizeof buf, s.c_str(), v);
            } else {
                uint32_t v = next();
                if (shortArg) v = (t == 'd' || t == 'i') ? uint32_t(int32_t(int16_t(v))) : (v & 0xffff);
                std::string s = spec + t;
                if (t == 'd' || t == 'i') snprintf(buf, sizeof buf, s.c_str(), int(int32_t(v)));
                else snprintf(buf, sizeof buf, s.c_str(), unsigned(v));
            }
            out += buf;
            break;
        }
        case 'p': snprintf(buf, sizeof buf, "%08X", next()); out += buf; break;
        case 'c': case 'C': {
            uint32_t v = next();
            out += char(v & 0xff);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            uint32_t lo = next(), hi = next();
            std::string s = spec + t;
            snprintf(buf, sizeof buf, s.c_str(), u2d(lo, hi));
            out += buf;
            break;
        }
        case 's': case 'S': {
            uint32_t p = next();
            bool w = (t == 's') ? (wide && !shortArg) || longArg : !wide || longArg;
            if (t == 'S' && shortArg) w = false;
            std::string s = !p ? "(null)" : w ? narrow(readWStr(p)) : readStr(p);
            std::string sp = spec + "s";
            snprintf(buf, sizeof buf, sp.c_str(), s.c_str());
            out += buf;
            break;
        }
        default: out += spec + t; break;
        }
    }
    return out;
}

int GizGameCore::scan(const std::string& in, const std::string& f, int firstArg)
{
    size_t p = 0; int ai = firstArg, n = 0;
    for (size_t i = 0; i < f.size(); i++) {
        char c = f[i];
        if (isspace((unsigned char)c)) { while (p < in.size() && isspace((unsigned char)in[p])) p++; continue; }
        if (c != '%') { if (p < in.size() && in[p] == c) { p++; continue; } return n; }
        i++;
        bool skip = false; if (i < f.size() && f[i] == '*') { skip = true; i++; }
        int width = 0; while (i < f.size() && isdigit((unsigned char)f[i])) width = width * 10 + (f[i++] - '0');
        bool lng = false; while (i < f.size() && (f[i] == 'l' || f[i] == 'h')) { lng = f[i] == 'l'; i++; }
        char t = i < f.size() ? f[i] : 0;
        if (t != 'c') while (p < in.size() && isspace((unsigned char)in[p])) p++;
        if (p >= in.size()) return n ? n : -1;
        std::string tok;
        size_t lim = width ? size_t(width) : std::string::npos;
        if (t == 'c') { tok = in.substr(p, width ? width : 1); p += tok.size(); }
        else { while (p < in.size() && !isspace((unsigned char)in[p]) && tok.size() < lim) {
                   char ch = in[p];
                   if (t != 's' && !(isxdigit((unsigned char)ch) || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E' || ch == 'x' || ch == 'X')) break;
                   tok += ch; p++; } }
        if (tok.empty()) return n;
        if (skip) continue;
        uint32_t dst = arg(ai++);
        switch (t) {
        case 'd': case 'i': wr32(dst, uint32_t(strtol(tok.c_str(), nullptr, t == 'i' ? 0 : 10))); break;
        case 'u': wr32(dst, uint32_t(strtoul(tok.c_str(), nullptr, 10))); break;
        case 'x': case 'X': wr32(dst, uint32_t(strtoul(tok.c_str(), nullptr, 16))); break;
        case 'f': case 'e': case 'g': {
            double d = strtod(tok.c_str(), nullptr);
            if (lng) { uint64_t v; std::memcpy(&v, &d, 8); wr32(dst, uint32_t(v)); wr32(dst + 4, uint32_t(v >> 32)); }
            else wr32(dst, f2u(float(d)));
            break;
        }
        case 's': writeStr(dst, tok); break;
        case 'c': writeMem(dst, tok.data(), uint32_t(tok.size())); break;
        default: return n;
        }
        n++;
    }
    return n;
}

// ---------------------------------------------------------------- the calls

void GizGameCore::dispatch(int fn, uint32_t trap)
{
    Thread& th = m_threads[size_t(m_cur)];
    switch (fn) {
    // ---- graphics ----
    case F_GL: {
        int id = m_gles->find(m_traps[trap].name.c_str());
        if (id < 0 || !m_gles->call(id)) { log("GL call not handled: " + m_traps[trap].name); ret(0); }
        return;
    }
    case F_EGL_SWAP: {
        // show the picture and end this frame; the call returns when the next frame starts
        if (!m_swapped) {
            m_sys.frame_count++;
            int id = m_gles->find("eglSwapBuffers");
            m_gles->call(id);          // presents and returns EGL_TRUE
            m_swapped = true;
            m_idle = true;             // stop this run; the thread is already past the call
            return;
        }
        return;
    }
    case F_EGL_SWAPINTERVALNV: ret(1); return;
    case F_WININET: ret(0); return;
    case F_INTERNETOPEN: m_lastError = 12029; ret(0); return;     // no network
    case F_COINIT: ret(0); return;
    case F_COUNINIT: ret(0); return;

    // ---- memory ----
    case F_malloc: case F_opnew: case F_opnewarr: ret(alloc(arg(0))); return;
    case F_free: case F_opdelete: case F_opdeletearr: release(arg(0)); ret(0); return;
    case F_realloc: ret(resize(arg(0), arg(1))); return;
    case F_LocalAlloc: ret(alloc(arg(1))); return;
    case F_LocalReAlloc: ret(resize(arg(0), arg(1))); return;
    case F_LocalSize: ret(blockSize(arg(0))); return;
    case F_LocalFree: release(arg(0)); ret(0); return;
    case F_GlobalMemoryStatus: {
        uint32_t p = arg(0);
        uint32_t free_ = 0; for (auto& kv : m_free) free_ += kv.second;
        const uint32_t v[8] = { 32, 50, 64u << 20, free_, 0, 0, 32u << 20, free_ };
        for (int i = 0; i < 8; i++) wr32(p + uint32_t(i * 4), v[i]);
        ret(0); return;
    }
    case F_GetSystemMemoryDivision:
        if (arg(0)) wr32(arg(0), 2048);
        if (arg(1)) wr32(arg(1), 8192);
        if (arg(2)) wr32(arg(2), 4096);
        ret(1); return;
    case F_SetSystemMemoryDivision: ret(0); return;

    // ---- C runtime: memory and strings ----
    case F_memcpy: case F_memmove: {
        uint32_t d = arg(0), s = arg(1), n = arg(2);
        if (uint64_t(d) + n <= RAM_SIZE && uint64_t(s) + n <= RAM_SIZE) std::memmove(&m_ram[d], &m_ram[s], n);
        ret(d); return;
    }
    case F_memset: { uint32_t d = arg(0), n = arg(2); if (uint64_t(d) + n <= RAM_SIZE) std::memset(&m_ram[d], int(arg(1) & 0xff), n); ret(d); return; }
    case F_memcmp: {
        uint32_t a = arg(0), b = arg(1), n = arg(2); int r = 0;
        if (uint64_t(a) + n <= RAM_SIZE && uint64_t(b) + n <= RAM_SIZE) r = std::memcmp(&m_ram[a], &m_ram[b], n);
        ret(uint32_t(r < 0 ? -1 : r > 0 ? 1 : 0)); return;
    }
    case F_strlen: ret(uint32_t(readStr(arg(0), 1u << 24).size())); return;
    case F_strcpy: writeStr(arg(0), readStr(arg(1), 1u << 24)); ret(arg(0)); return;
    case F_strncpy: {
        std::string s = readStr(arg(1), arg(2)); uint32_t n = arg(2), d = arg(0);
        for (uint32_t i = 0; i < n; i++) write8(d + i, i < s.size() ? uint8_t(s[i]) : 0);
        ret(d); return;
    }
    case F_strcmp: { int r = readStr(arg(0), 1u << 24).compare(readStr(arg(1), 1u << 24)); ret(uint32_t(r < 0 ? -1 : r > 0)); return; }
    case F_strncmp: { int r = readStr(arg(0), arg(2)).compare(readStr(arg(1), arg(2))); ret(uint32_t(r < 0 ? -1 : r > 0)); return; }
    case F__stricmp: case F__strnicmp: {
        uint32_t n = fn == F__strnicmp ? arg(2) : (1u << 24);
        std::string a = readStr(arg(0), n), b = readStr(arg(1), n);
        for (auto& c : a) c = char(tolower((unsigned char)c));
        for (auto& c : b) c = char(tolower((unsigned char)c));
        int r = a.compare(b); ret(uint32_t(r < 0 ? -1 : r > 0)); return;
    }
    case F_strstr: {
        std::string h = readStr(arg(0), 1u << 24), n = readStr(arg(1));
        size_t p = h.find(n); ret(p == std::string::npos ? 0 : arg(0) + uint32_t(p)); return;
    }
    case F_strcspn: { std::string s = readStr(arg(0), 1u << 24), r = readStr(arg(1)); ret(uint32_t(std::min(s.size(), s.find_first_of(r)))); return; }
    case F__strlwr: case F__strupr: {
        uint32_t a = arg(0);
        for (uint32_t p = a; p < RAM_SIZE && m_ram[p]; p++) m_ram[p] = uint8_t(fn == F__strlwr ? tolower(m_ram[p]) : toupper(m_ram[p]));
        ret(a); return;
    }
    case F__isctype: ret(uint32_t(ctypeOf(int(arg(0))) & int(arg(1)))); return;
    case F_wcsncpy: {
        std::u16string s = readWStr(arg(1), arg(2)); uint32_t n = arg(2), d = arg(0);
        for (uint32_t i = 0; i < n; i++) wr16(d + i * 2, i < s.size() ? uint16_t(s[i]) : 0);
        ret(d); return;
    }
    case F_wcsrchr: {
        std::u16string s = readWStr(arg(0), 1u << 20);
        size_t p = s.rfind(char16_t(arg(1)));
        ret(p == std::u16string::npos ? 0 : arg(0) + uint32_t(p * 2)); return;
    }
    case F__wcsicmp: { int r = wlower(readWStr(arg(0))).compare(wlower(readWStr(arg(1)))); ret(uint32_t(r < 0 ? -1 : r > 0)); return; }
    case F__wtol: ret(uint32_t(strtol(narrow(readWStr(arg(0))).c_str(), nullptr, 10))); return;
    case F_atof: retD(strtod(readStr(arg(0)).c_str(), nullptr)); return;
    case F_MultiByteToWideChar: {
        uint32_t src = arg(2), dst = arg(4), cch = arg(5); int32_t cb = int32_t(arg(3));
        std::string s = cb < 0 ? readStr(src, 1u << 20) : std::string(reinterpret_cast<const char*>(&m_ram[std::min(src, RAM_SIZE - 1)]), size_t(std::max(cb, 0)));
        uint32_t need = uint32_t(s.size()) + (cb < 0 ? 1 : 0);
        if (!cch) { ret(need); return; }
        uint32_t n = std::min(need, cch);
        for (uint32_t i = 0; i < n; i++) wr16(dst + i * 2, i < s.size() ? uint8_t(s[i]) : 0);
        ret(n); return;
    }
    case F_WideCharToMultiByte: {
        uint32_t src = arg(2), dst = arg(4), cb = arg(5); int32_t cch = int32_t(arg(3));
        std::u16string w;
        if (cch < 0) w = readWStr(src, 1u << 20); else for (int32_t i = 0; i < cch; i++) w += char16_t(read16(src + uint32_t(i * 2)));
        std::string s = narrow(w);
        uint32_t need = uint32_t(s.size()) + (cch < 0 ? 1 : 0);
        if (!cb) { ret(need); return; }
        uint32_t n = std::min(need, cb);
        for (uint32_t i = 0; i < n; i++) write8(dst + i, i < s.size() ? uint8_t(s[i]) : 0);
        ret(n); return;
    }
    case F_sprintf: { std::string s = format(readStr(arg(1)), 2, 0, false); writeStr(arg(0), s); ret(uint32_t(s.size())); return; }
    case F_vsprintf: { std::string s = format(readStr(arg(1)), 0, arg(2), false); writeStr(arg(0), s); ret(uint32_t(s.size())); return; }
    case F_wsprintfW: case F_swprintf: {
        std::string s = format(narrow(readWStr(arg(1))), 2, 0, true);
        writeWStr(arg(0), widen(s)); ret(uint32_t(s.size())); return;
    }
    case F_sscanf: ret(uint32_t(scan(readStr(arg(0)), readStr(arg(1)), 2))); return;
    case F_rand: m_rand = m_rand * 214013u + 2531011u; ret((m_rand >> 16) & 0x7fff); return;
    case F_qsort: {
        uint32_t base = arg(0), num = arg(1), width = arg(2), cmp = arg(3);
        if (num > 1 && width && uint64_t(base) + uint64_t(num) * width <= RAM_SIZE) {
            std::vector<uint32_t> idx(num); std::iota(idx.begin(), idx.end(), 0u);
            uint32_t retTo = m_cpu->r[14], save[16]; std::memcpy(save, m_cpu->r, sizeof save);
            std::stable_sort(idx.begin(), idx.end(), [&](uint32_t a, uint32_t b) {
                return int32_t(callGuest(cmp, { base + a * width, base + b * width })) < 0;
            });
            std::vector<uint8_t> tmp(size_t(num) * width);
            for (uint32_t i = 0; i < num; i++) std::memcpy(&tmp[size_t(i) * width], &m_ram[base + idx[i] * width], width);
            std::memcpy(&m_ram[base], tmp.data(), tmp.size());
            std::memcpy(m_cpu->r, save, sizeof save); (void)retTo;
        }
        ret(0); return;
    }
    case F__purecall: fail("pure virtual function called"); return;
    case F___C_specific_handler: ret(1); return;
    case F__XcptFilter: ret(0); return;

    // ---- C runtime: maths and compiler helpers ----
    case F_sin: retD(std::sin(argD(0))); return;
    case F_cos: retD(std::cos(argD(0))); return;
    case F_tan: retD(std::tan(argD(0))); return;
    case F_sqrtf: ret(f2u(std::sqrt(u2f(arg(0))))); return;
    case F___rt_sdiv: {
        int32_t d = int32_t(arg(0)), n = int32_t(arg(1));
        if (!d) { m_cpu->r[1] = 0; ret(0); return; }
        if (n == INT32_MIN && d == -1) { m_cpu->r[1] = 0; ret(uint32_t(n)); return; }
        m_cpu->r[1] = uint32_t(n % d); ret(uint32_t(n / d)); return;
    }
    case F___rt_udiv: {
        uint32_t d = arg(0), n = arg(1);
        if (!d) { m_cpu->r[1] = 0; ret(0); return; }
        m_cpu->r[1] = n % d; ret(n / d); return;
    }
    case F___rt_sdiv64by64: {
        // Unlike __rt_sdiv, the 64-bit helper takes the dividend in r0:r1
        // and the divisor in r2:r3 (confirmed from Agaju's fixed-point maths).
        int64_t n = int64_t(uint64_t(arg(1)) << 32 | arg(0)), d = int64_t(uint64_t(arg(3)) << 32 | arg(2));
        int64_t q = d && !(n == INT64_MIN && d == -1) ? n / d : 0, r = d && d != -1 ? n % d : 0;
        m_cpu->r[2] = uint32_t(r); m_cpu->r[3] = uint32_t(uint64_t(r) >> 32);
        ret64(uint64_t(q)); return;
    }
    case F___rt_srsh: {
        int64_t v = int64_t(uint64_t(arg(1)) << 32 | arg(0)); uint32_t s = arg(2);
        ret64(uint64_t(s >= 64 ? (v < 0 ? -1 : 0) : v >> s)); return;
    }
    case F___adds: ret(f2u(u2f(arg(0)) + u2f(arg(1)))); return;
    case F___subs: ret(f2u(u2f(arg(0)) - u2f(arg(1)))); return;
    case F___muls: ret(f2u(u2f(arg(0)) * u2f(arg(1)))); return;
    case F___divs: ret(f2u(u2f(arg(0)) / u2f(arg(1)))); return;
    case F___negs: ret(arg(0) ^ 0x80000000u); return;
    case F___lts: ret(u2f(arg(0)) < u2f(arg(1))); return;
    case F___les: ret(u2f(arg(0)) <= u2f(arg(1))); return;
    case F___gts: ret(u2f(arg(0)) > u2f(arg(1))); return;
    case F___ges: ret(u2f(arg(0)) >= u2f(arg(1))); return;
    case F___nes: ret(u2f(arg(0)) != u2f(arg(1))); return;
    case F___itos: ret(f2u(float(int32_t(arg(0))))); return;
    case F___utos: ret(f2u(float(arg(0)))); return;
    case F___stoi: { float f = u2f(arg(0)); ret(std::isnan(f) ? 0 : uint32_t(int32_t(std::max(-2147483648.f, std::min(2147483520.f, f))))); return; }
    case F___stou: { float f = u2f(arg(0)); ret(f <= 0 || std::isnan(f) ? 0 : uint32_t(std::min(4294967040.f, f))); return; }
    case F___stod: retD(double(u2f(arg(0)))); return;
    case F___dtos: ret(f2u(float(argD(0)))); return;
    case F___dtoi: { double d = argD(0); ret(std::isnan(d) ? 0 : uint32_t(int32_t(std::max(-2147483648.0, std::min(2147483647.0, d))))); return; }
    case F___dtoi64: { double d = argD(0); ret64(uint64_t(int64_t(std::isnan(d) ? 0 : std::max(-9.2e18, std::min(9.2e18, d))))); return; }
    case F___itod: retD(double(int32_t(arg(0)))); return;
    case F___utod: retD(double(arg(0))); return;
    case F___i64tos: ret(f2u(float(int64_t(uint64_t(arg(1)) << 32 | arg(0))))); return;
    case F___i64tod: retD(double(int64_t(uint64_t(arg(1)) << 32 | arg(0)))); return;
    case F___addd: retD(argD(0) + argD(2)); return;
    case F___muld: retD(argD(0) * argD(2)); return;
    case F___divd: retD(argD(0) / argD(2)); return;

    // ---- time ----
    case F_GetLocalTime: case F_GetSystemTime: {
        time_t t = time(nullptr); struct tm tmv;
        if (fn == F_GetLocalTime) localtime_r(&t, &tmv); else gmtime_r(&t, &tmv);
        uint32_t p = arg(0);
        const uint16_t v[8] = { uint16_t(tmv.tm_year + 1900), uint16_t(tmv.tm_mon + 1), uint16_t(tmv.tm_wday), uint16_t(tmv.tm_mday),
                                uint16_t(tmv.tm_hour), uint16_t(tmv.tm_min), uint16_t(tmv.tm_sec), uint16_t((m_us / 1000) % 1000) };
        for (int i = 0; i < 8; i++) wr16(p + uint32_t(i * 2), v[i]);
        ret(0); return;
    }
    case F_QueryPerformanceFrequency: wr32(arg(0), 1000000); wr32(arg(0) + 4, 0); ret(1); return;
    case F_QueryPerformanceCounter: wr32(arg(0), uint32_t(m_us)); wr32(arg(0) + 4, uint32_t(m_us >> 32)); ret(1); return;
    case F_GetTimeFormatW: case F_GetDateFormatW: {
        time_t t = time(nullptr); struct tm tmv; localtime_r(&t, &tmv);
        char b[32];
        if (fn == F_GetTimeFormatW) snprintf(b, sizeof b, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        else snprintf(b, sizeof b, "%02d/%02d/%04d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
        uint32_t buf = arg(4), cch = arg(5), n = uint32_t(strlen(b)) + 1;
        if (buf && cch >= n) writeWStr(buf, widen(b));
        ret(n); return;
    }
    case F_Sleep: {
        uint32_t ms = arg(0);
        if (m_nested) { m_us += uint64_t(ms) * 1000; ret(0); return; }
        if (ms == 0) {
            ret(0);
            int n = pickNext(m_cur);
            if (n >= 0 && n != m_cur) switchTo(n);
            return;
        }
        if (th.wakeAt) {
            if (m_us >= th.wakeAt) { th.wakeAt = 0; ret(0); return; }
            block(); return;
        }
        th.wakeAt = m_us + uint64_t(ms) * 1000;
        block(); return;
    }

    // ---- threads and synchronisation ----
    case F_CreateThread: {
        uint32_t stackSize = std::max<uint32_t>(arg(1), 0x40000), start = arg(2), param = arg(3), flags = arg(4), pid = arg(5);
        Thread t;
        t.id = 0x1000 + uint32_t(m_threads.size()) * 4;
        t.handle = newObj(Kind::Thread);
        m_objs[t.handle].thread = int(m_threads.size());
        t.stack = alloc(stackSize);
        t.r[0] = param; t.r[13] = t.stack + stackSize - 16; t.r[14] = TRAP_THREAD_EXIT;
        t.r[15] = start & ~1u; t.cpsr = 0x1f | 0xc0 | ((start & 1) ? 0x20 : 0);
        t.suspended = (flags & 4) != 0;
        if (pid) wr32(pid, t.id);
        uint32_t h = t.handle;
        m_threads.push_back(t);
        ret(h); return;
    }
    case F_TerminateThread: {
        Obj* o = obj(arg(0));
        if (o && o->kind == Kind::Thread && o->thread >= 0) {
            m_threads[size_t(o->thread)].done = true;
            if (o->thread == m_cur) { ret(1); block(); return; }
        }
        ret(1); return;
    }
    case F_SuspendThread: ret(0); return;
    case F_SetThreadPriority: ret(1); return;
    case F_GetLastError: ret(m_lastError); return;
    case F_CreateEventW: {
        std::string name = arg(3) ? narrow(readWStr(arg(3))) : "";
        if (!name.empty()) for (auto& kv : m_objs) if (kv.second.kind == Kind::Event && kv.second.name == name) { ret(kv.first); return; }
        uint32_t h = newObj(Kind::Event);
        m_objs[h].manual = arg(1) != 0; m_objs[h].signaled = arg(2) != 0; m_objs[h].name = name;
        ret(h); return;
    }
    case F_OpenEventW: {
        std::string name = narrow(readWStr(arg(2)));
        for (auto& kv : m_objs) if (kv.second.kind == Kind::Event && kv.second.name == name) { ret(kv.first); return; }
        ret(0); return;
    }
    case F_EventModify: {
        Obj* o = obj(arg(0));
        if (o) { uint32_t f = arg(1); if (f == 2) o->signaled = false; else o->signaled = true; }
        ret(1); return;
    }
    case F_CreateMutexW: {
        uint32_t h = newObj(Kind::Mutex);
        if (arg(1)) { m_objs[h].owner = th.id; m_objs[h].count = 1; }
        ret(h); return;
    }
    case F_ReleaseMutex: {
        Obj* o = obj(arg(0));
        if (o && o->count && o->owner == th.id) o->count--;
        ret(1); return;
    }
    case F_WaitForSingleObject: {
        uint32_t h = arg(0), ms = arg(1);
        if (signaled(h, true)) { th.waiting = false; ret(0); return; }
        if (ms == 0 || m_nested) { th.waiting = false; ret(WAIT_TIMEOUT); return; }
        if (th.waiting && m_us >= th.waitUntil) { th.waiting = false; ret(WAIT_TIMEOUT); return; }
        if (!th.waiting) { th.waiting = true; th.waitUntil = ms == INFINITE_MS ? ~0ull : m_us + uint64_t(ms) * 1000; }
        block(); return;
    }
    case F_CloseHandle: {
        Obj* o = obj(arg(0));
        if (o && o->kind != Kind::Thread) m_objs.erase(arg(0));
        ret(1); return;
    }
    case F_DeviceIoControl: m_lastError = 50; ret(0); return;

    // ---- module ----
    case F_GetModuleHandleW: ret(m_imageBase); return;
    case F_GetModuleFileNameW: {
        std::u16string p = widen("\\SD Card\\" + m_exeName);
        uint32_t buf = arg(1), n = arg(2);
        if (n) { if (p.size() >= n) p.resize(n - 1); writeWStr(buf, p); }
        ret(uint32_t(p.size())); return;
    }

    // ---- files ----
    case F_CreateFileW: {
        std::string norm = normPath(narrow(readWStr(arg(0))));
        uint32_t access = arg(1), disp = arg(4);
        auto data = readFile(norm);
        bool write = (access & 0x40000000u) != 0;
        if (!data && !(write && disp != 3 && disp != 5)) { m_lastError = 2; ret(INVALID_HANDLE); return; }
        if (write) {
            if (!data || disp == 2 || disp == 5) data = std::make_shared<std::vector<uint8_t>>();
            m_written[norm] = data;
        }
        uint32_t h = newObj(Kind::File);
        Obj& o = m_objs[h]; o.data = data; o.path = norm; o.writable = write;
        if (dbg()) log("open " + norm);
        ret(h); return;
    }
    case F_ReadFile: {
        Obj* o = obj(arg(0));
        if (!o || o->kind != Kind::File) { ret(0); return; }
        uint32_t n = std::min<uint32_t>(arg(2), o->pos < o->data->size() ? uint32_t(o->data->size() - o->pos) : 0);
        writeMem(arg(1), o->data->data() + o->pos, n);
        o->pos += n;
        if (arg(3)) wr32(arg(3), n);
        ret(1); return;
    }
    case F_SetFilePointer: {
        Obj* o = obj(arg(0));
        if (!o || o->kind != Kind::File) { ret(INVALID_HANDLE); return; }
        int64_t d = int32_t(arg(1)), base = arg(3) == 1 ? o->pos : arg(3) == 2 ? int64_t(o->data->size()) : 0;
        o->pos = uint32_t(std::max<int64_t>(0, base + d));
        if (arg(2)) wr32(arg(2), 0);
        ret(o->pos); return;
    }
    case F_FindFirstFileW: {
        std::string pat = normPath(narrow(readWStr(arg(0))));
        size_t sl = pat.rfind('/');
        std::string dir = sl == std::string::npos ? "" : pat.substr(0, sl), mask = sl == std::string::npos ? pat : pat.substr(sl + 1);
        std::vector<std::string> found;
        auto consider = [&](const std::string& path, bool isDir) {
            size_t s = path.rfind('/');
            std::string pd = s == std::string::npos ? "" : path.substr(0, s), name = s == std::string::npos ? path : path.substr(s + 1);
            if (pd == dir && wildMatch(mask.c_str(), name.c_str())) found.push_back((isDir ? "D" : "F") + path);
        };
        for (auto& kv : m_zipIndex) consider(kv.first, false);
        for (auto& d : m_dirs) consider(d, true);
        for (auto& kv : m_written) if (!m_zipIndex.count(kv.first)) consider(kv.first, false);
        if (found.empty()) { m_lastError = 2; ret(INVALID_HANDLE); return; }
        uint32_t h = newObj(Kind::Find);
        m_objs[h].findList = found; m_objs[h].findPos = 0;
        // fall through to fill the first entry
        Obj& o = m_objs[h];
        const std::string& e = o.findList[o.findPos++];
        uint32_t p = arg(1);
        for (uint32_t i = 0; i < 40 + 520; i += 4) wr32(p + i, 0);
        bool isDir = e[0] == 'D'; std::string path = e.substr(1);
        wr32(p, isDir ? 0x10 : 0x80);
        auto f = isDir ? nullptr : readFile(path);
        wr32(p + 32, f ? uint32_t(f->size()) : 0);
        std::string name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
        writeWStr(p + 40, widen(name));
        ret(h); return;
    }
    case F_FindNextFileW: {
        Obj* o = obj(arg(0));
        if (!o || o->kind != Kind::Find || o->findPos >= o->findList.size()) { m_lastError = 18; ret(0); return; }
        const std::string e = o->findList[o->findPos++];
        uint32_t p = arg(1);
        for (uint32_t i = 0; i < 40 + 520; i += 4) wr32(p + i, 0);
        bool isDir = e[0] == 'D'; std::string path = e.substr(1);
        wr32(p, isDir ? 0x10 : 0x80);
        auto f = isDir ? nullptr : readFile(path);
        wr32(p + 32, f ? uint32_t(f->size()) : 0);
        writeWStr(p + 40, widen(path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1)));
        ret(1); return;
    }
    case F_FindClose: m_objs.erase(arg(0)); ret(1); return;
    case F_fopen: {
        std::string name = readStr(arg(0)), mode = readStr(arg(1));
        std::string norm = normPath(name);
        bool write = mode.find('w') != std::string::npos || mode.find('a') != std::string::npos || mode.find('+') != std::string::npos;
        auto data = readFile(norm);
        if (!data && !write) {
            if (dbg()) log("fopen failed: " + name);
            ret(0); return;
        }
        if (write) {
            if (!data || mode.find('w') != std::string::npos) data = std::make_shared<std::vector<uint8_t>>();
            m_written[norm] = data;
        }
        uint32_t h = newObj(Kind::File);
        Obj& o = m_objs[h]; o.data = data; o.path = norm; o.writable = write;
        if (mode.find('a') != std::string::npos) o.pos = uint32_t(data->size());
        if (dbg()) log("fopen " + norm);
        ret(h); return;
    }
    case F_fclose: m_objs.erase(arg(0)); ret(0); return;
    case F_fread: {
        Obj* o = obj(arg(3));
        uint32_t size = arg(1), count = arg(2);
        if (!o || o->kind != Kind::File || !size) { ret(0); return; }
        uint32_t avail = o->pos < o->data->size() ? uint32_t(o->data->size() - o->pos) : 0;
        uint32_t items = std::min(count, avail / size);
        writeMem(arg(0), o->data->data() + o->pos, items * size);
        o->pos += items * size;
        ret(items); return;
    }
    case F_fwrite: {
        Obj* o = obj(arg(3));
        uint32_t size = arg(1), count = arg(2), n = size * count;
        if (!o || o->kind != Kind::File || !o->writable) { ret(0); return; }
        if (o->data->size() < o->pos + n) o->data->resize(o->pos + n);
        readMem(arg(0), o->data->data() + o->pos, n);
        o->pos += n;
        ret(count); return;
    }
    case F_fflush: ret(0); return;
    case F_fseek: {
        Obj* o = obj(arg(0));
        if (!o || o->kind != Kind::File) { ret(uint32_t(-1)); return; }
        int64_t d = int32_t(arg(1)), base = arg(2) == 1 ? o->pos : arg(2) == 2 ? int64_t(o->data->size()) : 0;
        o->pos = uint32_t(std::max<int64_t>(0, base + d));
        ret(0); return;
    }
    case F_ftell: { Obj* o = obj(arg(0)); ret(o && o->kind == Kind::File ? o->pos : uint32_t(-1)); return; }

    // ---- registry (kept in memory) ----
    case F_RegOpenKeyExW: case F_RegCreateKeyExW: {
        std::string base = arg(0) >= 0x80000000u ? std::to_string(arg(0)) : (obj(arg(0)) ? obj(arg(0))->path : "");
        std::string key = base + "\\" + narrow(readWStr(arg(1)));
        for (auto& c : key) c = char(tolower((unsigned char)c));
        if (fn == F_RegOpenKeyExW && !m_reg.count(key)) { ret(2); return; }
        m_reg[key];
        uint32_t h = newObj(Kind::RegKey); m_objs[h].path = key;
        if (fn == F_RegOpenKeyExW) wr32(arg(4), h);
        else { wr32(arg(7), h); if (arg(8)) wr32(arg(8), 1); }
        ret(0); return;
    }
    case F_RegSetValueExW: {
        Obj* o = obj(arg(0));
        if (o) { std::vector<uint8_t> v(arg(5)); readMem(arg(4), v.data(), arg(5)); m_reg[o->path][readWStr(arg(1))] = v; }
        ret(0); return;
    }
    case F_RegQueryInfoKeyW:
        for (int i = 4; i < 12; i++) if (arg(i)) wr32(arg(i), 0);
        ret(0); return;
    case F_RegCloseKey: m_objs.erase(arg(0)); ret(0); return;

    // ---- windows ----
    case F_RegisterClassW: {
        uint32_t wc = arg(0);
        uint32_t proc = rd32(wc + 4), name = rd32(wc + 36);
        std::u16string cls = name < 0x10000 ? widen("#" + std::to_string(name)) : readWStr(name);
        m_classes[wlower(cls)] = proc;
        static uint32_t atom = 0xc000;
        ret(++atom); return;
    }
    case F_UnregisterClassW: ret(1); return;
    case F_CreateWindowExW: {
        uint32_t clsArg = arg(1);
        std::u16string cls = clsArg < 0x10000 ? u"" : wlower(readWStr(clsArg));
        uint32_t proc = 0;
        if (m_classes.count(cls)) proc = m_classes[cls];
        else if (!m_classes.empty()) proc = m_classes.rbegin()->second;
        uint32_t hwnd = m_nextHwnd; m_nextHwnd += 4;
        m_windows[hwnd] = Window{ proc, m_cur, cls };
        if (!m_mainWnd) m_mainWnd = hwnd;
        // WM_CREATE with a CREATESTRUCT
        uint32_t cs = alloc(48);
        const uint32_t v[12] = { arg(11), arg(10), arg(9), arg(8), arg(7), arg(6), arg(5), arg(4), arg(3), arg(2), clsArg, arg(0) };
        for (int i = 0; i < 12; i++) wr32(cs + uint32_t(i * 4), v[i]);
        uint32_t r = proc ? callGuest(proc, { hwnd, WM_CREATE, 0, cs }) : 0;
        release(cs);
        if (int32_t(r) == -1) { m_windows.erase(hwnd); if (m_mainWnd == hwnd) m_mainWnd = 0; ret(0); return; }
        ret(hwnd); return;
    }
    case F_DestroyWindow: {
        uint32_t hwnd = arg(0);
        if (m_windows.count(hwnd)) { sendMessage(hwnd, WM_DESTROY, 0, 0); m_windows.erase(hwnd); }
        if (hwnd == m_mainWnd) m_mainWnd = m_windows.empty() ? 0 : m_windows.begin()->first;
        ret(1); return;
    }
    case F_DefWindowProcW:
        if (arg(1) == WM_CLOSE) { uint32_t h = arg(0); if (m_windows.count(h)) { sendMessage(h, WM_DESTROY, 0, 0); m_windows.erase(h); } }
        ret(0); return;
    case F_ShowWindow: case F_UpdateWindow: case F_SetWindowPos: case F_InvalidateRgn: ret(1); return;
    case F_GetMessageW: case F_PeekMessageW: {
        uint32_t msg = arg(0);
        bool remove = fn == F_GetMessageW || (arg(4) & 1);
        if (!th.queue.empty()) {
            auto m = th.queue.front();
            if (remove) th.queue.pop_front();
            th.waiting = false;
            const uint32_t v[7] = { m[0], m[1], m[2], m[3], uint32_t(m_us / 1000), 160, 120 };
            for (int i = 0; i < 7; i++) wr32(msg + uint32_t(i * 4), v[i]);
            ret(fn == F_GetMessageW ? (m[1] != WM_QUIT) : 1); return;
        }
        if (fn == F_PeekMessageW || m_nested) { ret(0); return; }
        if (!th.waiting) { th.waiting = true; th.waitUntil = ~0ull; }
        block(); return;
    }
    case F_TranslateMessage: ret(0); return;
    case F_DispatchMessageW: {
        uint32_t m = arg(0);
        uint32_t hwnd = rd32(m), msg = rd32(m + 4), w = rd32(m + 8), l = rd32(m + 12);
        ret(sendMessage(hwnd, msg, w, l)); return;
    }
    case F_SendMessageW: ret(sendMessage(arg(0), arg(1), arg(2), arg(3))); return;
    case F_PostMessageW: {
        uint32_t hwnd = arg(0);
        postMessage(hwnd ? windowThread(hwnd) : m_cur, hwnd, arg(1), arg(2), arg(3));
        ret(1); return;
    }
    case F_PostThreadMessageW:
        for (size_t i = 0; i < m_threads.size(); i++)
            if (m_threads[i].id == arg(0)) { postMessage(int(i), 0, arg(1), arg(2), arg(3)); ret(1); return; }
        ret(0); return;
    case F_PostQuitMessage: postMessage(m_cur, 0, WM_QUIT, arg(0), 0); ret(0); return;
    case F_RegisterWindowMessageW: { static uint32_t n = 0xc100; ret(++n); return; }
    case F_GetAsyncKeyState: {
        uint8_t vk = uint8_t(arg(0));
        uint32_t r = (m_vkDown[vk] ? 0x8000u : 0) | (m_vkPressed[vk] ? 1u : 0);
        m_vkPressed[vk] = 0;
        ret(r); return;
    }
    case F_LoadCursorW: ret(1); return;
    case F_ShowCursor: ret(0); return;
    case F_GetCursorPos: wr32(arg(0), 160); wr32(arg(0) + 4, 120); ret(1); return;
    case F_SetCursorPos: ret(1); return;
    // GDI: nothing is drawn (the games draw with OpenGL ES)
    case F_BeginPaint: if (arg(1)) { for (uint32_t i = 0; i < 64; i += 4) wr32(arg(1) + i, 0); wr32(arg(1), 0x5000); } ret(0x5000); return;
    case F_EndPaint: case F_ReleaseDC: case F_BitBlt: case F_TransparentImage: case F_DeleteDC: case F_DeleteObject:
    case F_SetBrushOrgEx: ret(1); return;
    case F_GetDC: case F_CreateCompatibleDC: ret(0x5000); return;
    case F_CreateCompatibleBitmap: case F_GetStockObject: case F_SelectObject: ret(0x5004); return;
    case F_DrawTextW: ret(16); return;

    // ---- sound ----
    case F_waveOutOpen: {
        uint32_t phwo = arg(0), pwfx = arg(2), cb = arg(3), inst = arg(4), flags = arg(5);
        if (flags & 1) { ret(0); return; }          // WAVE_FORMAT_QUERY
        uint32_t h = newObj(Kind::Wave);
        Wave& w = m_waves[h];
        w.handle = h;
        w.channels = std::max<uint32_t>(1, read16(pwfx + 2));
        w.rate = rd32(pwfx + 4);
        w.bits = read16(pwfx + 14) == 8 ? 8 : 16;
        w.cbType = flags & 0x70000; w.callback = cb; w.instance = inst;
        if (phwo) wr32(phwo, h);
        if (dbg()) log("waveOutOpen " + std::to_string(w.rate) + " Hz " + std::to_string(w.channels) + " ch " + std::to_string(w.bits) + " bit, callback type " + std::to_string(w.cbType >> 16));
        if (w.cbType == 0x30000 && cb) callGuest(cb, { h, MM_WOM_OPEN, inst, 0, 0 });
        else if (w.cbType == 0x10000) postMessage(windowThread(cb), cb, MM_WOM_OPEN, h, 0);
        ret(0); return;
    }
    case F_waveOutClose: m_waves.erase(arg(0)); m_objs.erase(arg(0)); ret(0); return;
    case F_waveOutPrepareHeader: wr32(arg(1) + 16, rd32(arg(1) + 16) | 2u); ret(0); return;
    case F_waveOutUnprepareHeader: wr32(arg(1) + 16, rd32(arg(1) + 16) & ~2u); ret(0); return;
    case F_waveOutWrite: {
        auto it = m_waves.find(arg(0));
        if (it == m_waves.end()) { ret(5); return; }
        wr32(arg(1) + 16, (rd32(arg(1) + 16) | 0x10u) & ~1u);
        it->second.queue.push_back(arg(1));
        ret(0); return;
    }
    case F_waveOutReset: {
        auto it = m_waves.find(arg(0));
        if (it != m_waves.end()) { for (uint32_t h : it->second.queue) it->second.done.push_back(h); it->second.queue.clear(); it->second.pos = 0; }
        ret(0); return;
    }
    case F_waveOutGetPosition: {
        auto it = m_waves.find(arg(0));
        uint32_t mt = arg(1);
        uint64_t b = it == m_waves.end() ? 0 : it->second.played;
        uint32_t type = rd32(mt);
        if (type == 2 && it != m_waves.end()) wr32(mt + 4, uint32_t(b / (it->second.channels * it->second.bits / 8)));
        else { wr32(mt, 4); wr32(mt + 4, uint32_t(b)); }
        ret(0); return;
    }
    case F_waveOutGetVolume: if (arg(1)) wr32(arg(1), 0xffffffffu); ret(0); return;
    case F_waveOutSetVolume: ret(0); return;

    default:
        log("not handled: " + m_traps[trap].dll + " " + m_traps[trap].name);
        ret(0);
        return;
    }
}
