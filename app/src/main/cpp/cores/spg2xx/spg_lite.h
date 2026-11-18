// PocketPlug - minimal stand-ins for the parts of MAME's device framework the
// SPG2xx devices use: attotime, timers, the CPU/timer scheduler, the screen
// beam position and devcb-style callbacks.
// SPDX-License-Identifier: BSD-3-Clause
//
// attotime follows MAME's emu/attotime.h (Aaron Giles, BSD-3-Clause); the
// scheduler reproduces device_scheduler::timeslice()/execute_timers() and the
// screen helpers reproduce screen_device::vpos()/hpos()/time_until_pos()
// (BSD-3-Clause) so timings line up with MAME.

#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <functional>
#include <algorithm>

typedef int64_t attoseconds_t;
typedef int32_t seconds_t;
constexpr attoseconds_t ATTOSECONDS_PER_SECOND = 1000000000LL * 1000000000LL;
constexpr seconds_t ATTOTIME_MAX_SECONDS = 1000000000;

template <typename T> inline constexpr attoseconds_t HZ_TO_ATTOSECONDS(T x) { return attoseconds_t(ATTOSECONDS_PER_SECOND / x); }

#ifndef BIT
#define BIT(x, n) (((x) >> (n)) & 1)
#endif
#define LOGMASKED(...) do {} while (0)
#define logerror(...) do {} while (0)
enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };

class attotime
{
public:
    constexpr attotime() noexcept : m_seconds(0), m_attoseconds(0) { }
    constexpr attotime(seconds_t secs, attoseconds_t attos) noexcept : m_seconds(secs), m_attoseconds(attos) { }

    constexpr bool is_zero() const noexcept { return m_seconds == 0 && m_attoseconds == 0; }
    constexpr bool is_never() const noexcept { return m_seconds >= ATTOTIME_MAX_SECONDS; }
    constexpr attoseconds_t attoseconds() const noexcept { return m_attoseconds; }
    constexpr seconds_t seconds() const noexcept { return m_seconds; }
    double as_double() const noexcept { return double(m_seconds) + double(m_attoseconds) * 1e-18; }
    constexpr attoseconds_t as_attoseconds() const noexcept
    {
        return (m_seconds == 0) ? m_attoseconds :
            (m_seconds == -1) ? (m_attoseconds - ATTOSECONDS_PER_SECOND) :
            (m_seconds > 0) ? ATTOSECONDS_PER_SECOND : -ATTOSECONDS_PER_SECOND;
    }

    static attotime from_hz(uint32_t f) { return (f > 1) ? attotime(0, HZ_TO_ATTOSECONDS(f)) : (f == 1) ? attotime(1, 0) : never(); }
    static attotime from_hz(int f) { return (f > 0) ? from_hz(uint32_t(f)) : never(); }
    static attotime from_hz(double f)
    {
        if (f > 1.0) return attotime(0, HZ_TO_ATTOSECONDS(f));
        if (f > 0.0) { double i, fr = modf(1.0 / f, &i); return attotime((seconds_t)i, (attoseconds_t)(fr * ATTOSECONDS_PER_SECOND)); }
        return never();
    }
    static attotime from_ticks(uint64_t ticks, uint32_t frequency)
    {
        if (frequency == 0) return never();
        attoseconds_t apt = HZ_TO_ATTOSECONDS(frequency);
        if (ticks < frequency) return attotime(0, ticks * apt);
        uint32_t rem = uint32_t(ticks % frequency);
        return attotime(seconds_t(ticks / frequency), uint64_t(rem) * apt);
    }
    static constexpr attotime from_msec(int64_t msec) { return attotime(seconds_t(msec / 1000), (msec % 1000) * (ATTOSECONDS_PER_SECOND / 1000)); }
    static constexpr attotime never() { return attotime(ATTOTIME_MAX_SECONDS, 0); }
    static constexpr attotime zero() { return attotime(0, 0); }

    seconds_t m_seconds;
    attoseconds_t m_attoseconds;
};

inline attotime operator+(const attotime& l, const attotime& r)
{
    if (l.m_seconds >= ATTOTIME_MAX_SECONDS || r.m_seconds >= ATTOTIME_MAX_SECONDS) return attotime::never();
    attotime res(l.m_seconds + r.m_seconds, l.m_attoseconds + r.m_attoseconds);
    if (res.m_attoseconds >= ATTOSECONDS_PER_SECOND) { res.m_attoseconds -= ATTOSECONDS_PER_SECOND; res.m_seconds++; }
    if (res.m_seconds >= ATTOTIME_MAX_SECONDS) return attotime::never();
    return res;
}
inline attotime operator-(const attotime& l, const attotime& r)
{
    if (l.m_seconds >= ATTOTIME_MAX_SECONDS) return attotime::never();
    attotime res(l.m_seconds - r.m_seconds, l.m_attoseconds - r.m_attoseconds);
    if (res.m_attoseconds < 0) { res.m_attoseconds += ATTOSECONDS_PER_SECOND; res.m_seconds--; }
    return res;
}
inline bool operator==(const attotime& l, const attotime& r) { return l.m_seconds == r.m_seconds && l.m_attoseconds == r.m_attoseconds; }
inline bool operator!=(const attotime& l, const attotime& r) { return !(l == r); }
inline bool operator<(const attotime& l, const attotime& r) { return l.m_seconds < r.m_seconds || (l.m_seconds == r.m_seconds && l.m_attoseconds < r.m_attoseconds); }
inline bool operator<=(const attotime& l, const attotime& r) { return !(r < l); }
inline bool operator>(const attotime& l, const attotime& r) { return r < l; }
inline bool operator>=(const attotime& l, const attotime& r) { return !(l < r); }

class SpgMachine;

// ---------------------------------------------------------------------------
// emu_timer: periodic/one-shot timer kept in the scheduler's sorted list
// ---------------------------------------------------------------------------
class emu_timer
{
public:
    typedef void (*Callback)(void* ctx, int param);
    void adjust(attotime start_delay, int param = 0, attotime period = attotime::never());
    attotime remaining() const;
    void enable(bool en = true);

    // state (saved)
    attotime m_start, m_expire, m_period;
    int m_param = 0;
    bool m_enabled = false;
    uint64_t m_seq = 0;            // insertion order among equal expiry times

    SpgMachine* m_machine = nullptr;
    Callback m_cb = nullptr;
    void* m_ctx = nullptr;
};

// ---------------------------------------------------------------------------
// The scheduler: one CPU plus timers, MAME's timeslice rules
// ---------------------------------------------------------------------------
class SpgMachine
{
public:
    attotime time() const;                   // machine().time()
    bool side_effects_disabled() const { return false; }
    const char* describe_context() const { return ""; }

    emu_timer* timer_alloc(emu_timer::Callback cb, void* ctx)
    {
        auto* t = new emu_timer();
        t->m_machine = this; t->m_cb = cb; t->m_ctx = ctx;
        t->m_expire = attotime::never();
        m_timers.push_back(t);
        return t;
    }
    ~SpgMachine() { for (auto* t : m_timers) delete t; }

    // CPU execution hooks (the unsp's icount lives in its own state)
    int* m_icountptr = nullptr;
    std::function<void()> m_cpu_run;
    void set_cpu_clock(uint32_t cycles_per_second)
    {
        m_cycles_per_second = cycles_per_second;
        m_attoseconds_per_cycle = HZ_TO_ATTOSECONDS(cycles_per_second);
        int64_t attos = m_attoseconds_per_cycle;
        m_divshift = 0;
        while (attos >= (1LL << 31)) { m_divshift++; attos >>= 1; }
        m_divisor = (uint32_t)attos;
    }
    attotime cycles_to_attotime(uint64_t cycles) const
    {
        if (cycles < m_cycles_per_second) return attotime(0, cycles * m_attoseconds_per_cycle);
        uint32_t rem = uint32_t(cycles % m_cycles_per_second);
        return attotime(seconds_t(cycles / m_cycles_per_second), uint64_t(rem) * m_attoseconds_per_cycle);
    }
    void abort_timeslice()
    {
        if (!m_executing || !m_icountptr) return;
        int delta = *m_icountptr;
        m_cycles_stolen += delta;
        m_cycles_running -= delta;
        *m_icountptr -= delta;
    }
    // run until 'until' (absolute), firing timers on the way
    void run_until(const attotime& until);
    void timeslice(const attotime& limit);
    void execute_timers();

    // scheduler state (saved)
    attotime m_basetime;
    attotime m_cpu_localtime;
    uint64_t m_seq_counter = 0;

    std::vector<emu_timer*> m_timers;        // allocation order
    emu_timer* first_timer() const;

    bool m_executing = false;
    int m_cycles_running = 0;
    int m_cycles_stolen = 0;
    uint32_t m_cycles_per_second = 1;
    attoseconds_t m_attoseconds_per_cycle = 1;
    int m_divshift = 0;
    uint32_t m_divisor = 1;
    emu_timer* m_callback_timer = nullptr;
    bool m_callback_timer_modified = false;
    attotime m_quantum = attotime(0, HZ_TO_ATTOSECONDS(60));
    mutable emu_timer* m_head_cache = nullptr;
    mutable bool m_head_dirty = true;
};

inline attotime SpgMachine::time() const
{
    if (m_executing)
        return m_cpu_localtime + cycles_to_attotime(uint64_t(m_cycles_running - *m_icountptr));
    return m_basetime;
}

inline emu_timer* SpgMachine::first_timer() const
{
    if (!m_head_dirty) return m_head_cache;
    emu_timer* best = nullptr;
    for (auto* t : m_timers) {
        if (!t->m_enabled) continue;
        if (!best || t->m_expire < best->m_expire || (t->m_expire == best->m_expire && t->m_seq < best->m_seq)) best = t;
    }
    m_head_cache = best;
    m_head_dirty = false;
    return best;
}

inline void emu_timer::adjust(attotime start_delay, int param, attotime period)
{
    SpgMachine& s = *m_machine;
    if (s.m_callback_timer == this) s.m_callback_timer_modified = true;
    m_param = param;
    m_enabled = true;
    if (start_delay.seconds() < 0) start_delay = attotime::zero();
    m_start = s.time();
    m_expire = m_start + start_delay;
    m_period = period;
    m_seq = ++s.m_seq_counter;
    s.m_head_dirty = true;
    if (s.first_timer() == this) s.abort_timeslice();
}

inline attotime emu_timer::remaining() const
{
    attotime curtime = m_machine->time();
    if (!m_enabled || curtime >= m_expire) return attotime::never();
    return m_expire - curtime;
}

inline void emu_timer::enable(bool en)
{
    if (en && !m_enabled) { m_enabled = true; m_seq = ++m_machine->m_seq_counter; }
    else if (!en) m_enabled = false;
    m_machine->m_head_dirty = true;
}

inline void SpgMachine::timeslice(const attotime& limit)
{
    // device_scheduler::timeslice()
    emu_timer* head = first_timer();
    attotime head_expire = head ? head->m_expire : attotime::never();
    while (m_basetime < head_expire && m_basetime < limit)
    {
        attotime target = m_basetime + m_quantum;
        if (head_expire < target) target = head_expire;
        if (limit < target) target = limit;

        if (target.seconds() >= m_cpu_localtime.seconds())
        {
            attoseconds_t delta = target.attoseconds() - m_cpu_localtime.attoseconds();
            if (delta < 0 && target.seconds() > m_cpu_localtime.seconds())
                delta += ATTOSECONDS_PER_SECOND;
            if (delta >= m_attoseconds_per_cycle)
            {
                int ran = m_cycles_running = (int)((uint64_t(delta) >> m_divshift) / m_divisor);
                m_cycles_stolen = 0;
                m_executing = true;
                *m_icountptr = m_cycles_running;
                m_cpu_run();
                m_executing = false;
                ran -= *m_icountptr;
                ran -= m_cycles_stolen;
                m_cpu_localtime = m_cpu_localtime + cycles_to_attotime((uint64_t)ran);
                if (m_cpu_localtime < target)
                    target = std::max(m_cpu_localtime, m_basetime);
            }
        }
        m_basetime = target;
        head = first_timer();
        head_expire = head ? head->m_expire : attotime::never();
    }
}

inline void SpgMachine::execute_timers()
{
    // device_scheduler::execute_timers()
    for (;;) {
        emu_timer* t = first_timer();
        if (!t || t->m_expire > m_basetime) break;
        if (t->m_period.is_zero() || t->m_period.is_never()) { t->m_enabled = false; m_head_dirty = true; }
        m_callback_timer_modified = false;
        m_callback_timer = t;
        t->m_cb(t->m_ctx, t->m_param);
        if (!m_callback_timer_modified && t->m_enabled) {
            // schedule_next_period
            t->m_start = t->m_expire;
            t->m_expire = t->m_expire + t->m_period;
            t->m_seq = ++m_seq_counter;
            m_head_dirty = true;
        }
        m_callback_timer = nullptr;
    }
}

inline void SpgMachine::run_until(const attotime& until)
{
    while (m_basetime < until) {
        timeslice(until);
        execute_timers();
    }
}

// ---------------------------------------------------------------------------
// screen_device beam helpers (refresh set by Hz, no explicit vblank time)
// ---------------------------------------------------------------------------
struct SpgScreen
{
    SpgMachine* m_machine = nullptr;
    int m_width = 320, m_height = 262;
    int m_vis_top = 0, m_vis_bottom = 239;
    attoseconds_t m_frame_period = 0, m_scantime = 1, m_pixeltime = 1, m_vblank_period = 0;
    attotime m_vblank_start_time, m_vblank_end_time;
    int m_last_partial_scan = 0;
    attotime m_last_partial_reset;
    uint64_t m_frame_number = 0;

    void configure(int width, int height, int vis_bottom, attoseconds_t frame_period)
    {
        m_width = width; m_height = height; m_vis_bottom = vis_bottom;
        m_frame_period = frame_period;
        m_scantime = m_frame_period / height;
        m_pixeltime = m_frame_period / (height * width);
        m_vblank_period = m_scantime * (height - (vis_bottom - m_vis_top + 1));
    }
    int vpos() const
    {
        attoseconds_t delta = (m_machine->time() - m_vblank_start_time).as_attoseconds();
        delta += m_pixeltime / 2;
        if (delta < 0) delta += m_frame_period;
        int vpos = int(delta / m_scantime);
        return (m_vis_bottom + 1 + vpos) % m_height;
    }
    int hpos() const
    {
        attoseconds_t delta = (m_machine->time() - m_vblank_start_time).as_attoseconds();
        delta += m_pixeltime / 2;
        if (delta < 0) delta += m_frame_period;
        int vpos = int(delta / m_scantime);
        delta -= vpos * m_scantime;
        return int(delta / m_pixeltime);
    }
    attotime time_until_pos(int vpos, int hpos = 0) const
    {
        vpos += m_height - (m_vis_bottom + 1);
        vpos %= m_height;
        attoseconds_t targetdelta = (attoseconds_t)vpos * m_scantime + (attoseconds_t)hpos * m_pixeltime;
        attoseconds_t curdelta = (m_machine->time() - m_vblank_start_time).as_attoseconds();
        if (targetdelta <= curdelta + m_pixeltime / 2) targetdelta += m_frame_period;
        while (targetdelta <= curdelta) targetdelta += m_frame_period;
        return attotime(0, targetdelta - curdelta);
    }
    attotime time_until_vblank_start() const { return time_until_pos(m_vis_bottom + 1); }
    bool vblank() const
    {
        attotime t = m_machine->time();
        return t < m_vblank_end_time;
    }
    attotime time_until_vblank_end() const
    {
        attotime target = m_vblank_end_time;
        if (!vblank()) target = target + attotime(0, m_frame_period);
        return target - m_machine->time();
    }
};
