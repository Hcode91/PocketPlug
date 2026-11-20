// PocketPlug - Convergent NGEN support chips
// SPDX-License-Identifier: BSD-3-Clause

#include "ngen_dev.h"
#include <cstdlib>
#include <cstdio>

// =================================================================== 8259A

void Pic8259::reset()
{
    irr = isr = lines = 0;
    imr = 0xff;
    icw2 = 0;
    level = aeoi = false;
    single = true;
    needIcw4 = false;
    initStep = 0;
    readIsr = poll = specialMask = false;
    update();
}

int Pic8259::best() const
{
    // level-triggered mode: a request is present for as long as the line is high
    uint8_t req = (level ? (irr | lines) : irr) & ~imr;
    for (int i = 0; i < 8; i++) {
        if (isr & (1 << i)) {
            if (!specialMask) return -1;
            continue;
        }
        if (req & (1 << i)) return i;
    }
    return -1;
}

bool Pic8259::intPending() const { return initStep == 0 && best() >= 0; }

void Pic8259::update()
{
    if (onInt) onInt(intPending());
}

void Pic8259::setIrq(int n, bool lv)
{
    uint8_t bit = 1 << n;
    bool was = lines & bit;
    if (lv) {
        lines |= bit;
        if (!was) irr |= bit;
    } else {
        lines &= ~bit;
        if (level) irr &= ~bit;
    }
    update();
}

uint8_t Pic8259::acknowledge()
{
    int n = best();
    if (n < 0) return icw2 | 7;   // spurious
    uint8_t bit = 1 << n;
    irr &= ~bit;
    if (!aeoi) isr |= bit;
    update();
    return uint8_t(icw2 | n);
}

void Pic8259::write(int a0, uint8_t v)
{
    if (!a0) {
        if (v & 0x10) {   // ICW1
            level = v & 0x08;
            single = v & 0x02;
            needIcw4 = v & 0x01;
            imr = 0; isr = 0;
            irr &= level ? lines : 0;
            readIsr = false; specialMask = false; aeoi = false;
            initStep = 2;
        } else if (v & 0x08) {   // OCW3
            if (v & 0x02) readIsr = v & 0x01;
            poll = v & 0x04;
            if (v & 0x40) specialMask = v & 0x20;
        } else {   // OCW2
            int cmd = (v >> 5) & 7;
            if (cmd == 1 || cmd == 5) {   // non-specific EOI
                for (int i = 0; i < 8; i++) if (isr & (1 << i)) { isr &= ~(1 << i); break; }
            } else if (cmd == 3 || cmd == 7) {
                isr &= ~(1 << (v & 7));
            }
        }
    } else {
        switch (initStep) {
        case 2: icw2 = v & 0xf8; initStep = single ? (needIcw4 ? 4 : 0) : 3; break;
        case 3: initStep = needIcw4 ? 4 : 0; break;
        case 4: aeoi = v & 0x02; initStep = 0; break;
        default: imr = v; break;
        }
    }
    update();
}

uint8_t Pic8259::read(int a0)
{
    if (a0) return imr;
    if (poll) {
        poll = false;
        int n = best();
        if (n < 0) return 0;
        irr &= ~(1 << n);
        if (!aeoi) isr |= 1 << n;
        update();
        return uint8_t(0x80 | n);
    }
    return readIsr ? isr : irr;
}

// =================================================================== 8254

void Pit8254::reset()
{
    for (auto& k : c) k = Counter();
}

void Pit8254::setOut(int ch, bool v)
{
    if (c[ch].out == v) return;
    c[ch].out = v;
    if (onOut) onOut(ch, v);
}

void Pit8254::write(int a, uint8_t v)
{
    if (a == 3) {
        int ch = v >> 6;
        if (ch == 3) {   // read-back: latch counts of the selected counters
            for (int i = 0; i < 3; i++)
                if ((v & (2 << i)) && !(v & 0x20) && !c[i].latched) { c[i].latched = true; c[i].latch = uint16_t(c[i].ce); }
            return;
        }
        Counter& k = c[ch];
        int rw = (v >> 4) & 3;
        if (rw == 0) {   // counter latch
            if (!k.latched) { k.latched = true; k.latch = uint16_t(k.ce); }
            return;
        }
        k.rw = uint8_t(rw);
        k.mode = (v >> 1) & 7;
        if (k.mode > 5) k.mode -= 4;
        k.writeHi = false; k.readHi = false; k.latched = false;
        k.loaded = false; k.pendingLoad = false; k.frozen = false;
        setOut(ch, k.mode != 0);
        return;
    }
    Counter& k = c[a];
    bool done = false;
    if (k.rw == 1) { k.count = v; done = true; }
    else if (k.rw == 2) { k.count = uint16_t(v << 8); done = true; }
    else {
        if (!k.writeHi) { k.lsbTmp = v; k.writeHi = true; if (k.mode == 0) setOut(a, false); }
        else { k.count = uint16_t(k.lsbTmp | (v << 8)); k.writeHi = false; done = true; }
    }
    if (done) {
        k.pendingLoad = true;
        k.frozen = false;
        if (k.mode == 0) { k.loaded = false; setOut(a, false); }
    }
}

uint8_t Pit8254::read(int a)
{
    if (a == 3) return 0xff;
    Counter& k = c[a];
    uint16_t v = k.latched ? k.latch : uint16_t(k.ce);
    uint8_t res;
    if (k.rw == 1) { res = uint8_t(v); k.latched = false; }
    else if (k.rw == 2) { res = uint8_t(v >> 8); k.latched = false; }
    else {
        if (!k.readHi) { res = uint8_t(v); k.readHi = true; }
        else { res = uint8_t(v >> 8); k.readHi = false; k.latched = false; }
    }
    return res;
}

void Pit8254::tick(int ch, uint32_t n)
{
    Counter& k = c[ch];
    while (n > 0) {
        if (k.frozen) return;
        if (k.pendingLoad && (!k.loaded || k.mode == 0 || k.mode == 4)) {
            // a new count is moved into the counting element on the next clock
            k.ce = k.count;
            k.pendingLoad = false;
            k.loaded = true;
            k.done = false;
            n--;
            continue;
        }
        if (!k.loaded) return;
        uint32_t period = k.count ? k.count : 0x10000;
        switch (k.mode) {
        case 0: case 1: case 4: case 5: {
            if (k.done) { k.ce = (k.ce - n) & 0xffff; n = 0; break; }
            uint32_t ce = k.ce ? k.ce : 0x10000;
            if (n < ce) { k.ce = ce - n; n = 0; break; }
            n -= ce;
            k.ce = 0;
            k.done = true;
            if (k.mode == 0 || k.mode == 1) setOut(ch, true);
            else { setOut(ch, false); setOut(ch, true); }
            break;
        }
        case 2: {
            // counts N..1 (output low while 1), reloading N on the following clock
            uint32_t ce = k.ce ? k.ce : 0x10000;
            if (n < ce) { k.ce = ce - n; n = 0; break; }
            n -= ce;
            setOut(ch, false);
            setOut(ch, true);
            if (k.pendingLoad) { k.pendingLoad = false; period = k.count ? k.count : 0x10000; }
            k.ce = period & 0xffff;
            break;
        }
        case 3: default: {
            uint32_t half = (period + 1) / 2;
            uint32_t ce = k.ce ? k.ce : 0x10000;
            if (ce > half) ce = half;
            if (n < ce) { k.ce = ce - n; n = 0; break; }
            n -= ce;
            setOut(ch, !k.out);
            if (k.pendingLoad) { k.pendingLoad = false; period = k.count ? k.count : 0x10000; }
            k.ce = (k.out ? (period + 1) / 2 : period / 2);
            if (k.ce == 0) k.ce = 1;
            break;
        }
        }
    }
}

// =================================================================== 8251A

void Usart8251::reset()
{
    expectMode = true;
    mode = cmd = 0;
    txBufFull = txShifting = false;
    txRemain = 0;
    rxQueue.clear();
    rxRemain = 0;
    rxReady = overrun = false;
    notify();
}

void Usart8251::notify()
{
    bool txrdyPin = (cmd & 0x01) && !txBufFull;
    bool rxrdy = rxReady && (cmd & 0x04);
    if (onLines) onLines(rxrdy, txrdyPin);
}

void Usart8251::write(int a, uint8_t v)
{
    if (a) {
        if (expectMode) { mode = v; expectMode = false; }
        else {
            if (v & 0x40) { expectMode = true; cmd = 0; txBufFull = false; rxReady = false; overrun = false; }
            else {
                cmd = v;
                if (v & 0x10) overrun = false;
            }
        }
    } else {
        txBuf = v;
        txBufFull = true;
        if (!txShifting && (cmd & 0x01)) { txShift = txBuf; txBufFull = false; txShifting = true; txRemain = byteTimeUs; }
    }
    notify();
}

uint8_t Usart8251::read(int a)
{
    if (a) {
        uint8_t s = 0;
        if (!txBufFull) s |= 0x01;
        if (rxReady) s |= 0x02;
        if (!txBufFull && !txShifting) s |= 0x04;
        if (overrun) s |= 0x10;
        s |= 0x80;   // DSR
        return s;
    }
    rxReady = false;
    notify();
    return rxData;
}

void Usart8251::receive(uint8_t v)
{
    rxQueue.push_back(v);
    if (rxQueue.size() == 1) rxRemain = byteTimeUs;
}

void Usart8251::advance(double us)
{
    if (txShifting) {
        txRemain -= us;
        if (txRemain <= 0) {
            txShifting = false;
            uint8_t b = txShift;
            if (txBufFull && (cmd & 0x01)) { txShift = txBuf; txBufFull = false; txShifting = true; txRemain += byteTimeUs; }
            if (onTransmit) onTransmit(b);
            notify();
        }
    } else if (txBufFull && (cmd & 0x01)) {
        txShift = txBuf; txBufFull = false; txShifting = true; txRemain = byteTimeUs;
        notify();
    }
    if (!rxQueue.empty()) {
        rxRemain -= us;
        if (rxRemain <= 0) {
            if (rxReady) overrun = true;
            rxData = rxQueue.front();
            rxQueue.erase(rxQueue.begin());
            rxReady = true;
            rxRemain = rxQueue.empty() ? 0 : byteTimeUs;
            notify();
        }
    }
}

// =================================================================== disk images

bool DiskImage::loadImd(const std::vector<uint8_t>& d)
{
    size_t i = 0;
    while (i < d.size() && d[i] != 0x1a) i++;
    if (i >= d.size()) return false;
    i++;
    tracks.clear();
    cylinders = heads = 0;
    while (i + 5 <= d.size()) {
        int cyl = d[i + 1], head = d[i + 2], nsec = d[i + 3], ssz = d[i + 4];
        i += 5;
        if (i + nsec > d.size() || ssz > 6) return false;
        std::vector<int> smap(d.begin() + i, d.begin() + i + nsec);
        i += nsec;
        if (head & 0x80) i += nsec;
        if (head & 0x40) i += nsec;
        int size = 128 << ssz;
        Track t;
        t.sectorSize = size;
        for (int s : smap) {
            if (i >= d.size()) return false;
            int type = d[i++];
            std::vector<uint8_t> data(size, 0xe5);
            if (type == 0) {}
            else if (type & 1) { if (i + size > d.size()) return false; memcpy(data.data(), &d[i], size); i += size; }
            else { if (i >= d.size()) return false; memset(data.data(), d[i], size); i++; }
            t.sectors[s] = data;
            t.order.push_back(s);
        }
        int h = head & 1;
        tracks[cyl * 16 + h] = t;
        if (cyl + 1 > cylinders) cylinders = cyl + 1;
        if (h + 1 > heads) heads = h + 1;
    }
    return !tracks.empty();
}

bool DiskImage::loadRaw(const std::vector<uint8_t>& d, int cyls, int hds, int spt, int ssize)
{
    if (d.size() < size_t(cyls) * hds * spt * ssize) return false;
    tracks.clear();
    size_t p = 0;
    for (int c = 0; c < cyls; c++)
        for (int h = 0; h < hds; h++) {
            Track t;
            t.sectorSize = ssize;
            for (int s = 1; s <= spt; s++) {
                t.sectors[s] = std::vector<uint8_t>(d.begin() + p, d.begin() + p + ssize);
                t.order.push_back(s);
                p += ssize;
            }
            tracks[c * 16 + h] = t;
        }
    cylinders = cyls; heads = hds;
    return true;
}

std::vector<uint8_t> DiskImage::saveImd() const
{
    std::string hdr = "IMD 1.18: PocketPlug\r\n";
    std::vector<uint8_t> o(hdr.begin(), hdr.end());
    o.push_back(0x1a);
    for (auto& kv : tracks) {
        const Track& t = kv.second;
        int ssz = 0;
        while ((128 << ssz) < t.sectorSize && ssz < 6) ssz++;
        o.push_back(4);   // 300 kbps MFM
        o.push_back(uint8_t(kv.first / 16));
        o.push_back(uint8_t(kv.first & 15));
        o.push_back(uint8_t(t.order.size()));
        o.push_back(uint8_t(ssz));
        for (int s : t.order) o.push_back(uint8_t(s));
        for (int s : t.order) {
            const std::vector<uint8_t>& data = t.sectors.at(s);
            bool same = true;
            for (size_t k = 1; k < data.size(); k++) if (data[k] != data[0]) { same = false; break; }
            if (same) { o.push_back(2); o.push_back(data[0]); }
            else { o.push_back(1); o.insert(o.end(), data.begin(), data.end()); }
        }
    }
    return o;
}

bool DiskImage::loadHd(const std::vector<uint8_t>& d)
{
    if (d.size() < 8 || memcmp(d.data(), "NGENHD1", 8) != 0) return false;
    tracks.clear();
    cylinders = heads = 0;
    size_t i = 8;
    while (i + 6 <= d.size()) {
        int cyl = d[i] | (d[i + 1] << 8), head = d[i + 2], n = d[i + 3], size = d[i + 4] | (d[i + 5] << 8);
        i += 6;
        if (i + size_t(n) * (1 + size) > d.size() || size == 0) return false;
        Track t;
        t.sectorSize = size;
        for (int k = 0; k < n; k++) {
            int s = d[i++];
            t.sectors[s] = std::vector<uint8_t>(d.begin() + i, d.begin() + i + size);
            t.order.push_back(s);
            i += size;
        }
        tracks[cyl * 16 + head] = t;
        if (cyl + 1 > cylinders) cylinders = cyl + 1;
        if (head + 1 > heads) heads = head + 1;
    }
    return true;
}

std::vector<uint8_t> DiskImage::saveHd() const
{
    std::vector<uint8_t> o = { 'N', 'G', 'E', 'N', 'H', 'D', '1', 0 };
    for (auto& kv : tracks) {
        const Track& t = kv.second;
        int cyl = kv.first / 16, head = kv.first % 16;
        o.push_back(uint8_t(cyl)); o.push_back(uint8_t(cyl >> 8)); o.push_back(uint8_t(head));
        o.push_back(uint8_t(t.order.size()));
        o.push_back(uint8_t(t.sectorSize)); o.push_back(uint8_t(t.sectorSize >> 8));
        for (int s : t.order) {
            o.push_back(uint8_t(s));
            const std::vector<uint8_t>& data = t.sectors.at(s);
            o.insert(o.end(), data.begin(), data.end());
        }
    }
    return o;
}

// =================================================================== WD2797

namespace {
const int kByteUs = 32;          // 250 kbit/s MFM
const int kRevUs = 200000;       // 300 rpm
const int kIndexUs = 4000;
}

void Wd2797::reset()
{
    status = 0; track = 0; sector = 1; data = 0; cmd = 0;
    cmdType = 0; state = 0; timer = 0;
    buf.clear(); bufPos = 0;
    stopAfterSector_ = false;
    writing = false;
    forcedIndexIrq = false;
    setIntrq(false);
    setDrq(false);
}

void Wd2797::setIntrq(bool v) { if (intrq != v) { intrq = v; if (onIntrq) onIntrq(v); } }
void Wd2797::setDrq(bool v)
{
    if (v) status |= 0x02; else status &= ~0x02;
    if (drq != v) { drq = v; if (onDrq) onDrq(v); }
}

void Wd2797::typeIStatus()
{
    status &= 0x01;   // keep busy
    if (!disk_) status |= 0x80;
    else {
        if (disk_->writeProtect) status |= 0x40;
        status |= 0x20;   // head loaded
        if (cyl == 0) status |= 0x04;
        if (indexUs < kIndexUs) status |= 0x02;
    }
}

void Wd2797::finish(uint8_t st)
{
    status = st & ~0x01;
    setDrq(false);
    state = 0;
    setIntrq(true);
}

bool Wd2797::findSector(std::vector<uint8_t>** out)
{
    if (!disk_) return false;
    // the side comes from the command's U (SSO) bit
    int side = sideFromCommand ? ((cmd & 0x02) ? 1 : 0) : side_;
    DiskImage::Track* t = disk_->track(cyl, side);
    if (!t) return false;
    if (track != cyl) return false;   // ID field track number does not match
    auto it = t->sectors.find(sector);
    if (it == t->sectors.end()) return false;
    *out = &it->second;
    return true;
}

void Wd2797::startCommand(uint8_t c)
{
    cmd = c;
    stopAfterSector_ = false;
    setIntrq(false);
    if ((c & 0xf0) == 0xd0) {   // force interrupt
        bool busy = status & 0x01;
        state = 0;
        setDrq(false);
        if (!busy) { cmdType = 1; typeIStatus(); }
        status &= ~0x01;
        forcedIndexIrq = c & 0x04;
        if (c & 0x08) setIntrq(true);
        return;
    }
    status = 0x01;
    if (!(c & 0x80)) {   // type I
        cmdType = 1;
        int steps = 0;
        int dir = 0;
        switch (c >> 4) {
        case 0: track = 0xff; data = 0; steps = cyl; cyl = 0; track = 0; break;    // restore
        case 1: dir = data > track ? 1 : -1; steps = data > track ? data - track : track - data;
                cyl += dir * steps; if (cyl < 0) cyl = 0; if (cyl > 85) cyl = 85; track = data; break;
        default: {
            int kind = (c >> 5) & 3;   // 1 step, 2 step in, 3 step out
            if (kind == 2) dir = 1; else if (kind == 3) dir = -1; else dir = dir ? dir : 1;
            cyl += dir; if (cyl < 0) cyl = 0; if (cyl > 85) cyl = 85;
            if (c & 0x10) track = uint8_t(track + dir);
            steps = 1;
            break;
        }
        }
        static const int rate[4] = { 6000, 12000, 20000, 30000 };
        timer = steps * rate[c & 3] / 4 + ((c & 0x04) ? 15000 : 0) + 100;
        state = 1;
        return;
    }
    if ((c & 0xe0) == 0x80 || (c & 0xe0) == 0xa0) {   // type II read / write sector
        cmdType = 2;
        writing = (c & 0xe0) == 0xa0;
        if (!disk_) { finish(0x80); return; }
        if (writing && disk_->writeProtect) { finish(0x40); return; }
        timer = 1500 + ((c & 0x04) ? 15000 : 0);
        state = 10;
        return;
    }
    cmdType = 3;
    if (!disk_) { finish(0x80); return; }
    switch (c & 0xf0) {
    case 0xc0:   // read address
        timer = 1000; state = 30; break;
    case 0xe0:   // read track
        timer = 2000; state = 40; break;
    default:     // write track (format)
        if (disk_->writeProtect) { finish(0x40); return; }
        timer = 2000; state = 50; break;
    }
}

void Wd2797::write(int reg, uint8_t v)
{
    switch (reg) {
    case 0: startCommand(v); break;
    case 1: track = v; break;
    case 2: sector = v; break;
    case 3:
        data = v;
        if (onDataAccess) onDataAccess();
        if (drq && writing) {
            if (bufPos < buf.size()) buf[bufPos++] = v;
            setDrq(false);
        } else if (drq && state == 51) {
            buf.push_back(v);
            setDrq(false);
        }
        break;
    }
}

uint8_t Wd2797::read(int reg)
{
    switch (reg) {
    case 0: {
        setIntrq(false);
        if (cmdType == 1 && !(status & 0x01)) typeIStatus();
        else if (cmdType == 1) { uint8_t b = status & 0x01; typeIStatus(); status |= b; }
        uint8_t s = status;
        if (!disk_) s |= 0x80;
        return s;
    }
    case 1: return track;
    case 2: return sector;
    default:
        if (onDataAccess) onDataAccess();
        if (drq && !writing) setDrq(false);
        return data;
    }
}

void Wd2797::advance(int us)
{
    indexUs += us;
    while (indexUs >= kRevUs) {
        indexUs -= kRevUs;
        if (forcedIndexIrq) setIntrq(true);
    }
    while (us > 0 && state) {
        if (timer > us) { timer -= us; return; }
        us -= timer;
        timer = 0;
        switch (state) {
        case 1:   // type I done
            status &= ~0x01;
            typeIStatus();
            if ((cmd & 0x04) && disk_ && !disk_->track(cyl, 0)) status |= 0x10;
            state = 0;
            setIntrq(true);
            break;
        case 10: {   // find the sector
            std::vector<uint8_t>* sec;
            if (!findSector(&sec)) { finish(0x10); break; }
            buf = *sec;
            bufPos = 0;
            if (writing) {
                status = 0x01 | ((buf.size() == 128) ? 0 : 0);
                setDrq(true);
                state = 21;
                timer = kByteUs * 2;
            } else {
                state = 11;
                timer = kByteUs;
            }
            break;
        }
        case 11:   // deliver the next byte
            if (drq) status |= 0x04;   // lost data
            data = buf[bufPos++];
            setDrq(true);
            if (bufPos >= buf.size()) { state = 12; timer = kByteUs * 2; }
            else timer = kByteUs;
            break;
        case 12:   // end of sector
            if (drq) { status |= 0x04; setDrq(false); }
            if ((cmd & 0x10) && !stopAfterSector_) {
                sector++;
                state = 10;
                timer = kByteUs * 30;
            } else finish(status & 0x04);
            stopAfterSector_ = false;
            break;
        case 21:   // writing: first byte must have arrived
            if (drq) { finish(0x04); break; }
            state = 22;
            timer = kByteUs;
            break;
        case 22:
            if (bufPos >= buf.size()) {
                std::vector<uint8_t>* sec;
                if (findSector(&sec)) { *sec = buf; disk_->dirty = true; }
                if ((cmd & 0x10) && !stopAfterSector_) {
                    sector++;
                    state = 10;
                    timer = kByteUs * 30;
                } else finish(0);
                stopAfterSector_ = false;
                break;
            }
            if (drq) status |= 0x04, bufPos++;
            setDrq(true);
            timer = kByteUs;
            break;
        case 30: {   // read address
            int rside = sideFromCommand ? ((cmd & 0x02) ? 1 : 0) : side_;
            DiskImage::Track* t = disk_->track(cyl, rside);
            if (!t || t->order.empty()) { finish(0x10); break; }
            int idx = (indexUs / (kRevUs / int(t->order.size()))) % int(t->order.size());
            int s = t->order[idx];
            int ssz = 0;
            while ((128 << ssz) < t->sectorSize) ssz++;
            buf = { uint8_t(cyl), uint8_t(rside), uint8_t(s), uint8_t(ssz), 0, 0 };
            bufPos = 0;
            sector = uint8_t(cyl);
            state = 31;
            timer = kByteUs;
            break;
        }
        case 31:
            data = buf[bufPos++];
            setDrq(true);
            if (bufPos >= buf.size()) { state = 32; timer = kByteUs * 2; }
            else timer = kByteUs;
            break;
        case 32:
            finish(drq ? 0x04 : 0);
            break;
        case 40: {   // read track: sector data back to back (no gaps)
            DiskImage::Track* t = disk_->track(cyl, (cmd & 0x02) ? 1 : 0);
            buf.clear();
            if (t) for (int s : t->order) buf.insert(buf.end(), t->sectors[s].begin(), t->sectors[s].end());
            bufPos = 0;
            if (buf.empty()) { finish(0); break; }
            state = 31;
            timer = kByteUs;
            break;
        }
        case 50:   // write track: collect bytes until the next index pulse
            buf.clear();
            state = 51;
            setDrq(true);
            timer = kByteUs;
            break;
        case 51:
            if (buf.size() < 12500) {
                if (!drq) setDrq(true);
                timer = kByteUs;
                if (buf.size() < 6250 + 600) break;
            }
            {
                // parse ID / data fields from the written stream
                DiskImage::Track nt;
                const std::vector<uint8_t>& w = buf;
                for (size_t k = 0; k + 4 < w.size(); k++) {
                    if (w[k] != 0xfe) continue;
                    int c = w[k + 1], h = w[k + 2], s = w[k + 3], n = w[k + 4] & 3;
                    (void)c; (void)h;
                    size_t j = k + 5;
                    while (j < w.size() && w[j] != 0xfb && w[j] != 0xfe) j++;
                    if (j >= w.size() || w[j] != 0xfb) continue;
                    size_t size = size_t(128) << n;
                    std::vector<uint8_t> sd(size, 0xe5);
                    for (size_t q = 0; q < size && j + 1 + q < w.size(); q++) sd[q] = w[j + 1 + q];
                    nt.sectorSize = int(size);
                    nt.sectors[s] = sd;
                    nt.order.push_back(s);
                    k = j + size;
                }
                if (!nt.order.empty()) {
                    disk_->tracks[cyl * 16 + ((cmd & 0x02) ? 1 : 0)] = nt;
                    if (cyl + 1 > disk_->cylinders) disk_->cylinders = cyl + 1;
                    disk_->dirty = true;
                }
                finish(0);
            }
            break;
        default:
            state = 0;
            break;
        }
    }
}

// =================================================================== WD1010

void Wd1010::reset()
{
    memset(regs, 0, sizeof(regs));
    regs[2] = 1; regs[3] = 1;
    status = disk_ ? 0x50 : 0x00;
    error = 0;
    cmd = 0;
    timer = 0;
    curCyl = 0;
    if (intrq) { intrq = false; if (onIntrq) onIntrq(false); }
}

void Wd1010::write(int reg, uint8_t v)
{
    reg &= 7;
    if (reg != 7) { regs[reg] = v; return; }
    cmd = v;
    if (intrq) { intrq = false; if (onIntrq) onIntrq(false); }
    error = 0;
    bool drive0 = ((regs[6] >> 3) & 3) == 0;
    if (!disk_ || !drive0) {
        // no drive: aborted command
        error = 0x04;
        status = 0x01;
        intrq = true;
        if (onIntrq) onIntrq(true);
        return;
    }
    status = 0x80 | 0x40 | 0x10 | 0x02;   // busy, ready, seek complete, command in progress
    switch (v >> 4) {
    case 0x1: timer = 200 + curCyl * 20; break;         // restore
    case 0x7: timer = 300 + abs(cylReg() - curCyl) * 20; break;   // seek
    default: timer = 1000; break;
    }
}

uint8_t Wd1010::read(int reg)
{
    reg &= 7;
    if (reg == 7) {
        if (intrq) { intrq = false; if (onIntrq) onIntrq(false); }
        // only drive 0 is fitted: the others never report ready
        if (((regs[6] >> 3) & 3) != 0 && !(status & 0x80)) return status & 0x01;
        return status;
    }
    if (reg == 1) return error;
    return regs[reg];
}

void Wd1010::advance(int us)
{
    if (!(status & 0x80)) return;
    timer -= us;
    if (timer > 0) return;
    execute();
}

void Wd1010::done(uint8_t err)
{
    error = err;
    status = 0x50 | (err ? 0x01 : 0);
    intrq = true;
    if (onIntrq) onIntrq(true);
}

void Wd1010::execute()
{
    int c = cylReg(), h = head();
    switch (cmd >> 4) {
    case 0x1:   // restore
        curCyl = 0;
        regs[4] = regs[5] = 0;
        done(0);
        return;
    case 0x7:   // seek
        curCyl = c;
        done(0);
        return;
    case 0x2: case 0x3: {   // read / write sector(s)
        curCyl = c;
        bool writing = (cmd >> 4) == 0x3;
        bool multi = cmd & 0x04;
        int count = multi ? (regs[2] ? regs[2] : 256) : 1;
        while (count-- > 0) {
            DiskImage::Track* t = disk_->track(c, h);
            std::vector<uint8_t>* sec = nullptr;
            if (t) { auto it = t->sectors.find(regs[3]); if (it != t->sectors.end()) sec = &it->second; }
            if (!sec) { done(0x10); return; }
            if (writing) {
                if (!dmaTransfer || !dmaTransfer(false, sec->data(), sec->size())) { done(0x04); return; }
                disk_->dirty = true;
            } else {
                if (!dmaTransfer || !dmaTransfer(true, sec->data(), sec->size())) { done(0x04); return; }
            }
            if (multi) { regs[3]++; regs[2]--; }
        }
        done(0);
        return;
    }
    case 0x4: {   // scan ID: report the next sector header under the head
        DiskImage::Track* t = disk_->track(c, h);
        if (!t || t->order.empty()) { done(0x10); return; }
        regs[3] = uint8_t(t->order[0]);
        int sz = t->sectorSize == 512 ? 1 : t->sectorSize == 1024 ? 2 : t->sectorSize == 128 ? 3 : 0;
        regs[6] = uint8_t((regs[6] & 0x9f) | (sz << 5));
        done(0);
        return;
    }
    case 0x5: {   // format track: the buffer holds (flag, sector number) pairs
        int n = regs[2] ? regs[2] : 256;
        std::vector<uint8_t> fmt(size_t(n) * 2, 0);
        if (dmaTransfer) dmaTransfer(false, fmt.data(), fmt.size());
        DiskImage::Track nt;
        nt.sectorSize = sizeBytes();
        for (int i = 0; i < n; i++) {
            int s = fmt[i * 2 + 1];
            if (nt.sectors.count(s)) continue;
            nt.sectors[s] = std::vector<uint8_t>(nt.sectorSize, 0x00);
            nt.order.push_back(s);
        }
        disk_->tracks[c * 16 + h] = nt;
        if (c + 1 > disk_->cylinders) disk_->cylinders = c + 1;
        if (h + 1 > disk_->heads) disk_->heads = h + 1;
        disk_->dirty = true;
        done(0);
        return;
    }
    default:
        done(0x04);
        return;
    }
}
