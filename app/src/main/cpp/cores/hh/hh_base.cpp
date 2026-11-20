// PocketPlug - dedicated handheld games: shared parts (see hh_base.h)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hh_base.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

bool HhBase::loadScreen(const std::vector<RomFile>& roms, const std::string& svgName, int rows, int cols, std::string& error)
{
    m_disp.init(rows, cols);
    if (svgName.empty()) {
        m_hasSvg = false;
        m_fbW = cols; m_fbH = rows;
        m_fb.assign(size_t(m_fbW) * m_fbH, 0xff000000u);
        return true;
    }
    m_hasSvg = true;
    const RomFile* f = nullptr;
    for (auto& r : roms) if (r.name == svgName) f = &r;
    if (!f) { error = "Missing " + svgName + " (the screen artwork in the MAME zip)"; return false; }
    std::string text(f->data.begin(), f->data.end());
    if (!m_svg.load(text, 960, error)) return false;
    m_fbW = m_svg.width(); m_fbH = m_svg.height();
    m_fb.assign(size_t(m_fbW) * m_fbH, 0xff000000u);
    m_keyOf.assign(PwmDisplay::MAXH * 64, -1);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            m_keyOf[y * 64 + x] = int16_t(m_svg.key(std::to_string(y) + "." + std::to_string(x)));
    m_levels.assign(size_t(m_svg.keyCount()) + 1, 0);
    return true;
}

void HhBase::audioTo(double t)
{
    const double cps = m_hz / 44100.0;
    while (m_spkPos < t) {
        double room = cps - m_spkFill;
        double span = t - m_spkPos;
        if (span < room) {
            m_spkSum += m_spk * span; m_spkFill += span; m_spkPos = t;
            break;
        }
        m_spkSum += m_spk * room;
        m_spkPos += room;
        m_audio.push_back(int16_t(m_spkSum / cps * 8000.0));
        m_spkSum = 0; m_spkFill = 0;
    }
}

void HhBase::speakerLevel(double level)
{
    if (m_opt[5]) level = 0;                // option 5: sound switch off
    if (level == m_spk) return;
    audioTo(double(now()));
    m_spk = level;
}

void HhBase::runFrameCommon()
{
    // option 7 = power switch off: nothing runs, the screen is dark
    if (m_opt[7]) {
        m_wasOff = true;
        std::fill(m_levels.begin(), m_levels.end(), 0);
        if (m_hasSvg) m_svg.render(m_levels.data(), m_fb.data());
        else std::fill(m_fb.begin(), m_fb.end(), 0xff000000u);
        for (int i = 0; i < 735; i++) m_audio.push_back(0);
        return;
    }
    if (m_wasOff) { m_wasOff = false; reset(); }
    m_carry += m_hz / 60.0;
    int n = int(m_carry);
    m_carry -= n;
    uint64_t start = now();
    int done = 0;
    while (done < n) done += runCpu(std::min(n - done, 256));
    audioTo(double(now()));
    m_disp.frame(now(), uint32_t(now() - start));
    if (m_hasSvg) {
        std::fill(m_levels.begin(), m_levels.end(), 0);
        for (int y = 0; y < m_disp.height(); y++)
            for (int x = 0; x < m_disp.width(); x++) {
                int k = m_keyOf[y * 64 + x];
                if (k >= 0 && m_disp.on(y, x)) m_levels[size_t(k)] = 255;
            }
        m_svg.render(m_levels.data(), m_fb.data());
    } else {
        for (int y = 0; y < m_fbH; y++)
            for (int x = 0; x < m_fbW; x++) m_fb[size_t(y) * m_fbW + x] = m_disp.on(y, x) ? 0xffffffffu : 0xff000000u;
    }
#ifndef __ANDROID__
    static int fno = 0; fno++;
    static int cnt[32][64];
    if (getenv("HHCOUNT")) {
        for (int y = 0; y < m_disp.height(); y++) for (int x = 0; x < m_disp.width(); x++) if (m_disp.on(y, x)) cnt[y][x]++;
        if (fno == atoi(getenv("HHCOUNT"))) { for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) if (cnt[y][x]) printf("C %d.%d=%d\n", y, x, cnt[y][x]); }
    }
    if (getenv("HHDUMP") && fno == atoi(getenv("HHDUMP"))) {
        printf("OURS ");
        for (int y = 0; y < m_disp.height(); y++) for (int x = 0; x < m_disp.width(); x++) if (m_disp.on(y, x)) printf("%d.%d ", y, x);
        printf("\n");
    }
#endif
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t HhBase::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}
