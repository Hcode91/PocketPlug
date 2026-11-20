// PocketPlug - TMS1000 / TMS1100 (port of MAME's tms1k_base / tms1000 / tms1100, Wilbert Pol, hap)
// SPDX-License-Identifier: BSD-3-Clause

#include "tms1k.h"
#include <sstream>

namespace {
enum : uint32_t {
    M_15TN = 1u << 0, M_ATN = 1u << 1, M_AUTA = 1u << 2, M_AUTY = 1u << 3, M_C8 = 1u << 4, M_CIN = 1u << 5,
    M_CKM = 1u << 6, M_CKN = 1u << 7, M_CKP = 1u << 8, M_MTN = 1u << 9, M_MTP = 1u << 10, M_NATN = 1u << 11,
    M_NE = 1u << 12, M_STO = 1u << 13, M_STSL = 1u << 14, M_YTP = 1u << 15,
};
enum : uint64_t {
    F_BR = 1ull << 0, F_CALL = 1ull << 1, F_CLO = 1ull << 2, F_COMC = 1ull << 3, F_COMX = 1ull << 4, F_COMX8 = 1ull << 5,
    F_LDP = 1ull << 6, F_LDX = 1ull << 7, F_RBIT = 1ull << 8, F_RETN = 1ull << 9, F_RSTR = 1ull << 10, F_SBIT = 1ull << 11,
    F_SETR = 1ull << 12, F_TDO = 1ull << 13, F_TPC = 1ull << 14,
};
}

bool Pla::parse(const std::string& text, int inputs, int outputs)
{
    struct Term { uint32_t care, val, out; };
    std::vector<Term> terms;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '.') continue;
        std::istringstream ls(line);
        std::string a, o;
        ls >> a >> o;
        if ((int)a.size() != inputs || (int)o.size() != outputs) continue;
        Term t{0, 0, 0};
        bool inhibit = false;
        for (int i = 0; i < inputs; i++) {
            if (a[i] == '0') t.care |= 1u << i;
            else if (a[i] == '1') { t.care |= 1u << i; t.val |= 1u << i; }
            else if (a[i] == '~') inhibit = true;
        }
        for (int f = 0; f < outputs; f++) if (o[f] == '1') t.out |= 1u << f;
        if (!inhibit) terms.push_back(t);
    }
    if (terms.empty()) return false;
    m_cache.assign(size_t(1) << inputs, 0);
    for (uint32_t v = 0; v < m_cache.size(); v++) {
        uint32_t s = 0;
        for (auto& t : terms) if ((v & t.care) == t.val) s |= t.out;
        m_cache[v] = s;
    }
    return true;
}

void TMS1K::configure(Type t, Io* io)
{
    m_type = t;
    m_io = io ? io : &m_null;
    m_1100 = (t == TMS1100 || t == TMS1300 || t == TMS1170 || t == TMS1370);
    m_oPins = (t == TMS1070 || t == TMS1270) ? (t == TMS1270 ? 10 : 8) : 8;
    m_rPins = (t == TMS1200 || t == TMS1270) ? 13 : (t == TMS1300 || t == TMS1370) ? 16 : 11;
    m_pcBits = 6;
    m_xBits = m_1100 ? 3 : 2;
    m_romMask = m_1100 ? 0x7ff : 0x3ff;
    m_ramMask = m_1100 ? 0x7f : 0x3f;
    m_oMask = (1u << m_oPins) - 1;
    m_rMask = (1u << m_rPins) - 1;
    m_pcMask = (1u << m_pcBits) - 1;
    m_xMask = (1u << m_xBits) - 1;
    std::memset(&s, 0, sizeof(s));
}

bool TMS1K::setRom(const std::vector<uint8_t>& rom, const std::string& microPla, const std::string& outputPla, std::string& error)
{
    m_rom.assign(size_t(m_romMask) + 1, 0);
    for (size_t i = 0; i < rom.size() && i < m_rom.size(); i++) m_rom[i] = rom[i];
    if (!m_mpla.parse(microPla, 8, 16)) { error = "The microinstructions PLA could not be read"; return false; }
    int oouts = (m_type == TMS1070 || m_type == TMS1270) ? 10 : 8;
    if (!m_otable && !m_opla.parse(outputPla, 5, oouts)) { error = "The output PLA could not be read"; return false; }

    const uint32_t md[16] = { M_STSL, M_AUTY, M_AUTA, M_CIN, M_C8, M_NE, M_CKN, M_15TN, M_MTN, M_NATN, M_ATN, M_MTP, M_YTP, M_CKP, M_CKM, M_STO };
    for (int op = 0; op < 256; op++) {
        uint32_t mask = m_mpla.read(uint32_t(op)) ^ 0x3fc8;
        uint32_t d = 0;
        for (int b = 0; b < 16; b++) if (mask >> b & 1) d |= md[b];
        m_microDecode[op] = d;
    }
    std::memset(m_fixedDecode, 0, sizeof(m_fixedDecode));
    m_fixedDecode[0x00] = F_COMX;
    m_fixedDecode[0x0a] = F_TDO;
    m_fixedDecode[0x0b] = F_CLO;
    m_fixedDecode[0x0c] = F_RSTR;
    m_fixedDecode[0x0d] = F_SETR;
    m_fixedDecode[0x0f] = F_RETN;
    for (int i = 0x10; i < 0x20; i++) m_fixedDecode[i] = F_LDP;
    for (int i = 0x30; i < 0x34; i++) m_fixedDecode[i] = F_SBIT;
    for (int i = 0x34; i < 0x38; i++) m_fixedDecode[i] = F_RBIT;
    for (int i = 0x3c; i < 0x40; i++) m_fixedDecode[i] = F_LDX;
    for (int i = 0x80; i < 0xc0; i++) m_fixedDecode[i] = F_BR;
    for (int i = 0xc0; i < 0x100; i++) m_fixedDecode[i] = F_CALL;
    if (m_1100) {
        m_fixedDecode[0x00] = 0;
        m_fixedDecode[0x09] = F_COMX8;
        m_fixedDecode[0x0b] = F_COMC;
        for (int i = 0x28; i < 0x30; i++) m_fixedDecode[i] = F_LDX;
        for (int i = 0x3c; i < 0x40; i++) m_fixedDecode[i] = 0;
    }
    return true;
}

void TMS1K::reset()
{
    s.pa = 0xf; s.pb = 0xf; s.pc = 0;
    s.ca = s.cb = s.cs = 0;
    s.eac = s.bl = s.add = s.status = s.clatch = 0;
    s.opcode = 0; s.micro = 0; s.fixed = 0; s.subcycle = 0;
    s.r = 0;
    m_io->writeR(0);
    writeOReg(0);
    m_io->writeR(0);
}

void TMS1K::nextPc()
{
    int high = 1 << (m_pcBits - 1);
    int fb = (s.pc << 1 & high) == (s.pc & high);
    if (s.pc == (m_pcMask >> 1)) fb = 1;
    else if (s.pc == m_pcMask) fb = 0;
    s.pc = uint16_t((s.pc << 1 | fb) & m_pcMask);
}

void TMS1K::readOpcode()
{
    s.opcode = m_rom[s.romAddress & m_romMask];
    uint8_t o = s.opcode;
    s.c4 = uint8_t((o & 1) << 3 | (o & 2) << 1 | (o & 4) >> 1 | (o & 8) >> 3);
    s.fixed = m_fixedDecode[o];
    s.micro = m_microDecode[o];
    nextPc();
}

void TMS1K::writeOReg(uint8_t index)
{
    s.oIndex = index;
    s.o = uint16_t(m_otable ? m_otable[index & 0x1f] : m_opla.read(index & 0x1f));
    m_io->writeO(uint16_t(s.o & m_oMask));
}

void TMS1K::setCki()
{
    switch (s.opcode & 0xf8) {
    case 0x08: s.cki = m_io->readK() & 0xf; break;
    case 0x30: case 0x38: s.cki = uint8_t((1 << (s.c4 >> 2)) ^ 0xf); break;
    case 0x00: case 0x40: case 0x48: case 0x50: case 0x58: case 0x60: case 0x68: case 0x70: case 0x78: s.cki = s.c4; break;
    default: s.cki = 0; break;
    }
}

int TMS1K::run(int clocks)
{
    m_icount = clocks;
    while (m_icount > 0) {
        m_icount--;
        s.total++;
        execOne(s.subcycle);
        s.subcycle = uint8_t((s.subcycle + 1) % 6);
    }
    return clocks - m_icount;
}

void TMS1K::execOne(int sub)
{
    switch (sub) {
    case 0: {
        // branch / call / return (one stack level)
        if (s.fixed & F_BR) {
            if (s.status) { if (s.clatch == 0) s.pa = s.pb; s.ca = s.cb; s.pc = s.opcode & m_pcMask; }
        }
        if (s.fixed & F_CALL) {
            if (s.status) {
                uint16_t prevPa = s.pa;
                if (!s.clatch) { s.clatch = 1; s.sr = s.pc; s.pa = s.pb; s.cs = s.ca; }
                s.ca = s.cb; s.pb = prevPa; s.pc = s.opcode & m_pcMask;
            }
        }
        if (s.fixed & F_RETN) {
            if (s.clatch) { s.clatch = 0; s.pc = s.sr; s.ca = s.cs; }
            s.add = 0; s.bl = 0; s.pa = s.pb;
        }
        setCki();
        s.ramIn = s.ram[s.ramAddress & m_ramMask] & 0xf;
        s.damIn = s.ram[(s.ramAddress | (0x10 << (m_xBits - 1))) & m_ramMask] & 0xf;
        s.p = 0; s.n = 0; s.carryIn = 0;
        break;
    }
    case 1: {
        s.romAddress = uint16_t((s.ca << (m_pcBits + 4)) | (s.pa << m_pcBits) | s.pc);
        uint32_t m = s.micro;
        if (m & M_15TN) s.n |= 0xf;
        if (m & M_ATN) s.n |= s.a;
        if (m & M_NATN) s.n |= (~s.a & 0xf);
        if (m & M_CKN) s.n |= s.cki;
        if (m & M_MTN) s.n |= s.ramIn;
        if (m & M_CKP) s.p |= s.cki;
        if (m & M_MTP) s.p |= s.ramIn;
        if (m & M_YTP) s.p |= s.y;
        if (m & M_CIN) s.carryIn |= 1;
        break;
    }
    case 2: {
        uint32_t m = s.micro;
        s.adderOut = uint8_t(s.p + s.n + s.carryIn);
        int carryOut = s.adderOut >> 4 & 1;
        int status = 1;
        s.ramOut = -1;
        if (m & M_C8) status &= carryOut;
        if (m & M_NE) status &= (s.n != s.p);
        if (m & M_CKM) s.ramOut = s.cki;
        s.carryOut = uint8_t(carryOut);
        if (m & M_STO) s.ramOut = s.a;
        uint64_t f = s.fixed;
        if (f & F_SBIT) { if (s.ramOut == -1) s.ramOut = s.ramIn; s.ramOut = int16_t(s.ramOut | (s.cki ^ 0xf)); }
        if (f & F_RBIT) { if (s.ramOut == -1) s.ramOut = s.ramIn; s.ramOut = int16_t(s.ramOut & s.cki); }
        if (f & (F_SETR | F_RSTR)) {
            int index = m_1100 ? ((s.x >> (m_xBits - 1) & 1) << 4 | s.y) : s.y;
            if (f & F_SETR) s.r |= (1u << index); else s.r &= ~(1u << index);
            m_io->writeR(s.r & m_rMask);
        }
        if (f & F_TDO) writeOReg(uint8_t(s.statusLatch << 4 | s.a));
        if (f & F_CLO) writeOReg(0);
        if (f & F_LDX) s.x = uint8_t(s.c4 >> (4 - m_xBits));
        if (f & F_COMX) s.x ^= m_xMask;
        if (f & F_COMX8) s.x ^= uint8_t(1 << (m_xBits - 1));
        if (f & F_LDP) s.pb = s.c4;
        if (f & F_COMC) s.cb ^= 1;
        if (f & F_TPC) s.cb = s.pb & 3;
        s.status = uint8_t(status);
        if (s.ramOut != -1) s.ram[s.ramAddress & m_ramMask] = uint8_t(s.ramOut & 0xf);
        break;
    }
    case 4: {
        uint32_t m = s.micro;
        if (m & M_AUTA) s.a = s.adderOut & 0xf;
        if (m & M_AUTY) s.y = s.adderOut & 0xf;
        if (m & M_STSL) s.statusLatch = s.status;
        readOpcode();
        s.ramAddress = uint16_t(s.x << 4 | s.y);
        break;
    }
    default: break;
    }
}
