// PocketPlug - the host side the OpenGL ES renderer (wce_gles.cpp) talks to
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../gpl32/arm926.h"
#include <cstring>
#include <functional>
#include <string>

namespace didj {

struct GizSys {
    u64 frame_count = 0;
    std::function<void(const u32*, int, int)> present;
    void present_gl(const u32* argb, int w, int h) { if (present) present(argb, w, h); }
};

class Hle {
public:
    bool log_calls = false;
    std::function<void(const std::string&)> log;
    std::function<bool(u32, void*, u32)> readMem;
    std::function<bool(u32, const void*, u32)> writeMem;
    std::function<u32(u32)> scratch;

    bool read_mem(u32 va, void* dst, u32 len) { return readMem(va, dst, len); }
    bool write_mem(u32 va, const void* src, u32 len) { return writeMem(va, src, len); }
    u32 rd32(u32 va) { u32 v = 0; read_mem(va, &v, 4); return v; }
    void wr32(u32 va, u32 v) { write_mem(va, &v, 4); }
    u32 arg(int i) { return i < 4 ? cpu->r[i] : rd32(cpu->r[13] + u32(i - 4) * 4); }
    float argf(int i) { u32 v = arg(i); float f; std::memcpy(&f, &v, 4); return f; }
    u32 scratch_alloc(u32 len) { return scratch ? scratch(len) : 0; }

    ARM926* cpu = nullptr;
    GizSys* sys = nullptr;
};

} // namespace didj
