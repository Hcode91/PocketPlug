// PocketPlug - "Egg Pet - Ocean (original)"
// SPDX-License-Identifier: BSD-3-Clause
//
// A built-in egg pet in the spirit of the 1998 ocean model: hunger and
// happiness hearts, discipline, water quality (flush when the skulls go
// dark), a two-chest treasure game with a stray octopus, predators that
// come while it naps (touch the shell to scare them off), sickness and
// medicine, and growth over real days into one of seven sea creatures
// depending on care. All characters and art are PocketPlug originals.
//
// Time runs on the pet's own clock (set from the phone). Everything is
// simulated one second at a time, so days of absence catch up instantly.

#include "ocean.h"
#include "ocean_art.h"
#include <cstring>
#include <algorithm>

enum Stage { ST_EGG, ST_BABY, ST_CHILD, ST_TEEN_GOOD, ST_TEEN_BAD, ST_ADULT };
enum Char { C_EGG, C_SPECK, C_JELLO, C_FINLET, C_PUFFO, C_HIPPA, C_STRIPES, C_CLAMMY, C_BLUEY, C_SKIPPY, C_UNICO };
enum Call { CALL_HUNGER = 1, CALL_HAPPY = 2, CALL_LIGHTS = 4, CALL_BEAR = 8, CALL_DISC = 16, CALL_WATER = 32 };
enum Icon { I_FOOD, I_LIGHT, I_GAME, I_MEDICINE, I_FLUSH, I_STATS, I_DISCIPLINE, I_ATTENTION, I_NONE = 255 };
enum Death { D_NONE, D_SICK, D_DISCIPLINE, D_OLD, D_BEAR };
enum Beep { B_CLICK, B_CALL, B_HAPPY, B_SAD, B_HATCH, B_ALARM, B_DEATH };

static const uint32_t MAGIC = 0x4e41434f; // 'OCAN'
static const uint32_t VERSION = 1;
static const int FPS = 64;

struct CharInfo { int evolve, hungerSec, happySec, sleepH, wakeH, baseWeight, life; };
static const CharInfo kChar[] = {
    /* egg     */ { 0, 0, 0, -1, -1, 0, 0 },
    /* Speck   */ { 55 * 60, 3 * 60, 4 * 60, -1, -1, 5, 0 },
    /* Jello   */ { 49 * 3600, 50 * 60, 45 * 60, 19, 9, 10, 0 },
    /* Finlet  */ { 47 * 3600, 65 * 60, 60 * 60, 21, 9, 20, 0 },
    /* Puffo   */ { 30 * 3600, 55 * 60, 50 * 60, 20, 10, 20, 0 },
    /* Hippa   */ { 0, 80 * 60, 75 * 60, 21, 8, 25, 18 },
    /* Stripes */ { 0, 70 * 60, 65 * 60, 20, 9, 25, 14 },
    /* Clammy  */ { 49 * 3600 + 12 * 60, 60 * 60, 55 * 60, 18, 10, 25, 11 },
    /* Bluey   */ { 0, 45 * 60, 70 * 60, 20, 9, 40, 9 },
    /* Skippy  */ { 0, 60 * 60, 60 * 60, 22, 11, 25, 8 },
    /* Unico   */ { 0, 90 * 60, 85 * 60, 20, 9, 25, 20 },
};

// ---------------------------------------------------------------- fonts
static const char* kFont3x5(char c)
{
    switch (c) {
    case '0': return "###" "#.#" "#.#" "#.#" "###";
    case '1': return ".#." "##." ".#." ".#." "###";
    case '2': return "###" "..#" "###" "#.." "###";
    case '3': return "###" "..#" ".##" "..#" "###";
    case '4': return "#.#" "#.#" "###" "..#" "..#";
    case '5': return "###" "#.." "###" "..#" "###";
    case '6': return "###" "#.." "###" "#.#" "###";
    case '7': return "###" "..#" ".#." ".#." ".#.";
    case '8': return "###" "#.#" "###" "#.#" "###";
    case '9': return "###" "#.#" "###" "..#" "###";
    case 'A': return ".#." "#.#" "###" "#.#" "#.#";
    case 'D': return "##." "#.#" "#.#" "#.#" "##.";
    case 'E': return "###" "#.." "##." "#.." "###";
    case 'F': return "###" "#.." "##." "#.." "#..";
    case 'G': return "###" "#.." "#.#" "#.#" "###";
    case 'H': return "#.#" "#.#" "###" "#.#" "#.#";
    case 'I': return "###" ".#." ".#." ".#." "###";
    case 'L': return "#.." "#.." "#.." "#.." "###";
    case 'M': return "#.#" "###" "###" "#.#" "#.#";
    case 'N': return "##." "#.#" "#.#" "#.#" "#.#";
    case 'O': return "###" "#.#" "#.#" "#.#" "###";
    case 'P': return "###" "#.#" "###" "#.." "#..";
    case 'R': return "##." "#.#" "##." "#.#" "#.#";
    case 'S': return "###" "#.." "###" "..#" "###";
    case 'T': return "###" ".#." ".#." ".#." ".#.";
    case 'W': return "#.#" "#.#" "###" "###" "#.#";
    case 'Y': return "#.#" "#.#" ".#." ".#." ".#.";
    case '/': return "..#" "..#" ".#." "#.." "#..";
    case ':': return "..." ".#." "..." ".#." "...";
    case '!': return ".#." ".#." ".#." "..." ".#.";
    }
    return "..............."; // space
}

static const char* kBig[10] = {
    ".###." "#...#" "#..##" "#.#.#" "##..#" "#...#" ".###.",
    "..#.." ".##.." "..#.." "..#.." "..#.." "..#.." ".###.",
    ".###." "#...#" "....#" "..##." ".#..." "#...." "#####",
    "####." "....#" "....#" ".###." "....#" "....#" "####.",
    "...#." "..##." ".#.#." "#..#." "#####" "...#." "...#.",
    "#####" "#...." "####." "....#" "....#" "#...#" ".###.",
    ".###." "#...." "####." "#...#" "#...#" "#...#" ".###.",
    "#####" "....#" "...#." "..#.." ".#..." ".#..." ".#...",
    ".###." "#...#" "#...#" ".###." "#...#" "#...#" ".###.",
    ".###." "#...#" "#...#" ".####" "....#" "...#." ".##..",
};

// ---------------------------------------------------------------- setup
OceanPetCore::OceanPetCore()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.rng = 0x12345678;
    m_s.clock = 12 * 3600;  // until the app gives us the real time
    newEgg();
}

bool OceanPetCore::load(const std::vector<RomFile>&, std::string&)
{
    return true;   // nothing to load: the whole game is built in
}

void OceanPetCore::reset()
{
    // the pin hole on the back: a new egg
    newEgg();
}

void OceanPetCore::newEgg()
{
    int64_t clock = m_s.clock;
    uint8_t gentle = m_s.gentle;
    uint32_t rng = m_s.rng ? m_s.rng : 0x9e3779b9;
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.magic = MAGIC;
    m_s.version = VERSION;
    m_s.clock = clock;
    m_s.gentle = gentle;
    m_s.rng = rng;
    m_s.stage = ST_EGG;
    m_s.character = C_EGG;
    m_s.lightsOn = 1;
    m_s.eggTimer = 5 * 60;
    m_s.icon = I_NONE;
    m_s.mode = M_MAIN;
    m_s.petX = 8;
    m_s.petDir = -1;
}

void OceanPetCore::setWallClock(int64_t localSeconds)
{
    m_s.clock = localSeconds;
}

void OceanPetCore::setOption(int id, int value)
{
    if (id == 1) m_s.gentle = value ? 1 : 0;
}

uint32_t OceanPetCore::rnd(uint32_t n)
{
    // xorshift32
    uint32_t x = m_s.rng ? m_s.rng : 1;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    m_s.rng = x;
    return n ? x % n : 0;
}

int OceanPetCore::hour() const { int64_t d = m_s.clock % 86400; if (d < 0) d += 86400; return (int)(d / 3600); }
int OceanPetCore::minute() const { int64_t d = m_s.clock % 3600; if (d < 0) d += 3600; return (int)(d / 60); }

void OceanPetCore::stageTimes(int& evolveSecs, int& hungerSecs, int& happySecs, int& sleepH, int& wakeH) const
{
    const CharInfo& c = kChar[m_s.character];
    evolveSecs = c.evolve; hungerSecs = c.hungerSec; happySecs = c.happySec; sleepH = c.sleepH; wakeH = c.wakeH;
}

bool OceanPetCore::isNight() const
{
    const CharInfo& c = kChar[m_s.character];
    if (c.sleepH < 0) return false;
    int h = hour();
    return c.sleepH > c.wakeH ? (h >= c.sleepH || h < c.wakeH) : (h >= c.sleepH && h < c.wakeH);
}

// ---------------------------------------------------------------- sound
void OceanPetCore::beep(int kind)
{
    if (m_quiet) return;
    auto t = [&](int f, int ms) { m_tones.push_back({ f, 32768 * ms / 1000 }); };
    switch (kind) {
    case B_CLICK: t(4096, 35); break;
    case B_CALL: for (int i = 0; i < 3; i++) { t(2048, 90); t(0, 70); } break;
    case B_HAPPY: t(2048, 80); t(2731, 80); t(3277, 80); t(4096, 160); break;
    case B_SAD: t(1638, 160); t(1365, 240); break;
    case B_HATCH: t(2048, 100); t(2580, 100); t(3070, 100); t(2580, 100); t(4096, 250); break;
    case B_ALARM: for (int i = 0; i < 4; i++) { t(3277, 90); t(2048, 90); } break;
    case B_DEATH: t(2048, 250); t(1820, 250); t(1638, 250); t(1365, 500); break;
    }
}

void OceanPetCore::synthAudio(size_t samples)
{
    // a piezo-ish square wave
    for (size_t i = 0; i < samples; i++) {
        int16_t v = 0;
        while (!m_tones.empty() && m_tones.front().samples <= 0) m_tones.erase(m_tones.begin());
        if (!m_tones.empty()) {
            Tone& t = m_tones.front();
            if (t.freq > 0) {
                m_phase += t.freq;
                v = ((m_phase / 16384) & 1) ? 6000 : 0;   // 32768/2 per half period
            }
            t.samples--;
        }
        m_audio.push_back(v);
    }
}

size_t OceanPetCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    return n;
}

// ---------------------------------------------------------------- life
void OceanPetCore::makeSick(bool neglect)
{
    if (m_s.stage == ST_EGG || m_s.dead) return;
    if (!m_s.sick) {
        m_s.sick = 1;
        m_s.dosesLeft = (uint8_t)(m_s.injured ? 2 : 1 + rnd(2));
        m_s.sickTimer = 0;
    }
    if (neglect) {
        m_s.neglectSick++;
        int evolveSecs, h, hp, s, w;
        stageTimes(evolveSecs, h, hp, s, w);
        bool longLeft = evolveSecs == 0 || evolveSecs - m_s.stageSecs > 6 * 3600;
        if (m_s.neglectSick >= 4 && longLeft) die(D_SICK);
    }
}

void OceanPetCore::die(int cause)
{
    if (m_s.gentle || m_s.dead || m_s.stage == ST_EGG) return;
    m_s.dead = 1;
    m_s.deathCause = (uint8_t)cause;
    m_s.calling = 0;
    m_s.bear = 0;
    m_s.lightsOn = 1;
    setMode(M_DEAD);
    beep(B_DEATH);
}

void OceanPetCore::evolve()
{
    int next = -1;
    switch (m_s.character) {
    case C_SPECK: next = C_JELLO; break;
    case C_JELLO:
        if (m_s.discipline < 6) { die(D_DISCIPLINE); if (!m_s.gentle) return; }
        next = m_s.discMistakes >= 2 ? C_PUFFO : C_FINLET;
        break;
    case C_FINLET:
        if (m_s.discipline < 6) { die(D_DISCIPLINE); if (!m_s.gentle) return; }
        if (m_s.weight >= 99) next = C_BLUEY;
        else if (m_s.careMistakes == 0 && m_s.discMistakes == 0) next = C_HIPPA;
        else if (m_s.discMistakes >= 2) next = C_CLAMMY;
        else next = C_STRIPES;
        break;
    case C_PUFFO:
        if (m_s.discipline < 6) { die(D_DISCIPLINE); if (!m_s.gentle) return; }
        if (m_s.water >= 4) next = C_SKIPPY;
        else if (m_s.weight >= 99) next = C_BLUEY;
        else if (m_s.careMistakes == 0 && m_s.discMistakes == 0) next = C_STRIPES;
        else next = C_CLAMMY;
        break;
    case C_CLAMMY:
        // the secret: flawless care as a clam, never hurt, hearts full
        if (m_s.careMistakes == 0 && m_s.discMistakes == 0 && m_s.injuriesThisStage == 0 &&
            m_s.hunger == 4 && m_s.happy == 4)
            next = C_UNICO;
        else { m_s.stageSecs = -0x7fffffff; return; }   // stays a clam
        break;
    }
    if (next < 0) return;
    m_s.character = (uint8_t)next;
    switch (next) {
    case C_JELLO: m_s.stage = ST_CHILD; break;
    case C_FINLET: m_s.stage = ST_TEEN_GOOD; break;
    case C_PUFFO: m_s.stage = ST_TEEN_BAD; break;
    default: m_s.stage = ST_ADULT; break;
    }
    m_s.stageSecs = 0;
    m_s.careMistakes = m_s.discMistakes = m_s.neglectSick = m_s.injuriesThisStage = 0;
    m_s.weight = (uint8_t)std::max<int>(m_s.weight, kChar[next].baseWeight);
    if (m_s.stage == ST_ADULT) m_s.lifespanDays = m_s.ageYears + kChar[next].life - 3 + (int)rnd(4);
    m_s.hungerTimer = m_s.happyTimer = 0;
    if (!m_quiet) setMode(M_EVOLVE, FPS * 3);
    beep(B_HAPPY);
}

// one simulated second
void OceanPetCore::secondTick()
{
    m_s.clock++;
    if (m_s.dead) return;

    if (m_s.stage == ST_EGG) {
        if (--m_s.eggTimer <= 0) {
            m_s.stage = ST_BABY;
            m_s.character = C_SPECK;
            m_s.weight = 5;
            m_s.hunger = 0; m_s.happy = 0;
            m_s.nextDiscCall = 20 * 60 + (int)rnd(20 * 60);
            m_s.callTimerHunger = m_s.callTimerHappy = 0;
            m_s.calling |= CALL_HUNGER | CALL_HAPPY;
            if (!m_quiet) setMode(M_HATCH, FPS * 3);
            beep(B_HATCH);
        }
        return;
    }

    int evolveSecs, hungerSecs, happySecs, sleepH, wakeH;
    stageTimes(evolveSecs, hungerSecs, happySecs, sleepH, wakeH);

    // ---- sleep, naps and the clock ----
    bool night = isNight();
    int h = hour(), mi = minute();
    bool napTime = m_s.stage >= ST_CHILD && !night && (h == 13 || (h == 14 && mi < 40));
    if (night && !m_s.asleep) {
        m_s.asleep = 1; m_s.napping = 0; m_s.bear = 0; m_s.calling &= ~CALL_BEAR;
        if (m_s.lightsOn) { m_s.calling |= CALL_LIGHTS; m_s.lightsTimer = 0; beep(B_CALL); }
    } else if (!night && m_s.asleep && !m_s.napping) {
        // morning: one year older
        m_s.asleep = 0;
        m_s.lightsOn = 1;
        m_s.calling &= ~CALL_LIGHTS;
        m_s.ageYears++;
        if (m_s.stage == ST_ADULT && m_s.lifespanDays && m_s.ageYears >= m_s.lifespanDays) { die(D_OLD); return; }
    }
    if (!night) {
        if (napTime && !m_s.napping && !m_s.asleep && m_s.mode != M_GAME) {
            m_s.napping = 1; m_s.asleep = 1;
            m_s.napBearAt[0] = (5 + (int)rnd(30)) * 60;
            m_s.napBearAt[1] = (55 + (int)rnd(35)) * 60;
            m_s.bearTimer = 0;
            m_s.lightsTimer = 0;
        } else if (!napTime && m_s.napping) {
            m_s.napping = 0; m_s.asleep = 0; m_s.lightsOn = 1; m_s.bear = 0; m_s.calling &= ~CALL_BEAR;
        }
    }
    if (m_s.asleep && m_s.lightsOn && (m_s.calling & CALL_LIGHTS)) {
        if (++m_s.lightsTimer == 60 * 60) { m_s.careMistakes++; m_s.calling &= ~CALL_LIGHTS; }
    }

    // ---- predators come while it naps (not to babies) ----
    if (m_s.napping) {
        int napSecs = ((h - 13) * 60 + mi) * 60 + (int)(m_s.clock % 60);
        for (int i = 0; i < 2; i++) {
            if (m_s.napBearAt[i] && napSecs >= m_s.napBearAt[i] && !m_s.bear) {
                m_s.napBearAt[i] = 0;
                m_s.bear = 1; m_s.bearTimer = 0;
                m_s.calling |= CALL_BEAR;
                beep(B_ALARM);
            }
        }
    }
    if (m_s.bear && ++m_s.bearTimer >= 90) {
        // too late: it got hurt
        m_s.bear = 0; m_s.calling &= ~CALL_BEAR;
        m_s.injured = 1; m_s.injuriesThisStage++;
        makeSick(true);
        if (m_s.dead) return;
        beep(B_SAD);
    }

    if (m_s.asleep) { m_s.stageSecs++; return; }

    // ---- awake ----
    m_s.stageSecs++;

    if (++m_s.hungerTimer >= hungerSecs) {
        m_s.hungerTimer = 0;
        if (m_s.hunger > 0 && --m_s.hunger == 0) { m_s.calling |= CALL_HUNGER; m_s.callTimerHunger = 0; beep(B_CALL); }
    }
    if (++m_s.happyTimer >= happySecs) {
        m_s.happyTimer = 0;
        if (m_s.happy > 0 && --m_s.happy == 0) { m_s.calling |= CALL_HAPPY; m_s.callTimerHappy = 0; beep(B_CALL); }
    }
    if ((m_s.calling & CALL_HUNGER) && ++m_s.callTimerHunger == 15 * 60) m_s.careMistakes++;
    if ((m_s.calling & CALL_HAPPY) && ++m_s.callTimerHappy == 15 * 60) m_s.careMistakes++;

    // water gets murkier over time (and with every meal)
    if (m_s.stage >= ST_BABY && ++m_s.waterTimer >= 80 * 60) {
        m_s.waterTimer = 0;
        if (m_s.water < 4) {
            m_s.water++;
            if (m_s.water == 4) m_s.blackFlushes = 4;
            if (m_s.water >= 3) { m_s.calling |= CALL_WATER; beep(B_CALL); }
        }
    }
    if (m_s.water >= 3) {
        if (++m_s.waterDirtyTimer >= 90 * 60) { m_s.waterDirtyTimer = 0; makeSick(true); if (m_s.dead) return; }
    } else m_s.waterDirtyTimer = 0;

    // discipline calls
    if (!m_s.discCall && !m_s.sick && --m_s.nextDiscCall <= 0) {
        m_s.discCall = 1; m_s.discCallTimer = 0;
        m_s.calling |= CALL_DISC;
        beep(B_CALL);
    }
    if (m_s.discCall && ++m_s.discCallTimer >= 15 * 60) {
        m_s.discCall = 0; m_s.calling &= ~CALL_DISC;
        m_s.discMistakes++;
        m_s.nextDiscCall = (3 + (int)rnd(5)) * 3600;
    }

    // sickness: chance, weight, overfeeding with snacks; untreated for long = death
    if (m_s.clock % 3600 == 0 && m_s.stage >= ST_CHILD && !m_s.sick) {
        if (rnd(48) == 0) makeSick(false);
        else if (m_s.weight >= kChar[m_s.character].baseWeight + 35 && rnd(6) == 0) makeSick(true);
        if (m_s.dead) return;
    }
    if (m_s.snackTimer > 0 && --m_s.snackTimer == 0) m_s.snackRun = 0;
    if (m_s.sick && ++m_s.sickTimer >= 12 * 3600) { die(D_SICK); if (m_s.dead) return; }

    // ---- growing up ----
    if (evolveSecs && m_s.stageSecs >= evolveSecs) evolve();
}

// ---------------------------------------------------------------- input
void OceanPetCore::setInputs(uint32_t pressed)
{
    m_inputs = pressed;
}

void OceanPetCore::setMode(int m, int frames)
{
    m_s.lastMode = m_s.mode;
    m_s.mode = (uint8_t)m;
    m_s.modeTimer = frames;
}

void OceanPetCore::selectIcon()
{
    switch (m_s.icon) {
    case I_FOOD:
        if (m_s.asleep && !m_s.napping) { beep(B_SAD); return; }
        m_s.foodSel = 0; setMode(M_FOOD); break;
    case I_LIGHT:
        m_s.lightsOn ^= 1;
        if (!m_s.lightsOn) { m_s.calling &= ~CALL_LIGHTS; }
        break;
    case I_GAME:
        if (m_s.asleep || m_s.sick || m_s.stage == ST_EGG) { setMode(M_NO, FPS * 3 / 2); beep(B_SAD); return; }
        m_s.gameRound = 0; m_s.gameWins = 0; m_s.gamePhase = 0; m_s.gameOcto = 0;
        if (m_s.weight > kChar[m_s.character].baseWeight) m_s.weight--;
        setMode(M_GAME);
        break;
    case I_MEDICINE:
        if (!m_s.sick) { setMode(M_NO, FPS * 3 / 2); beep(B_SAD); return; }
        setMode(M_MEDICINE, FPS * 2);
        break;
    case I_FLUSH:
        setMode(M_FLUSH, FPS * 2);
        break;
    case I_STATS:
        m_s.page = 0; setMode(M_STATS); break;
    case I_DISCIPLINE:
        if (m_s.bear) {   // also scares a predator off
            m_s.bear = 0; m_s.calling &= ~CALL_BEAR; setMode(M_BEAR_GONE, FPS * 3 / 2); beep(B_HAPPY); return;
        }
        if (m_s.asleep) { beep(B_SAD); return; }
        setMode(M_SCOLD, FPS * 2);
        if (m_s.discCall) {
            m_s.discCall = 0; m_s.calling &= ~CALL_DISC;
            m_s.discipline = (uint8_t)std::min(8, m_s.discipline + 2);
            m_s.nextDiscCall = (3 + (int)rnd(5)) * 3600;
            if (m_s.stage == ST_BABY) m_s.nextDiscCall = 25 * 60 + (int)rnd(20 * 60);
        } else if (m_s.happy > 0) {
            m_s.happy--;   // scolded for nothing
        }
        break;
    }
}

void OceanPetCore::onPress(uint32_t b)
{
    if (b & BTN_TOUCH) {
        if (m_s.bear) { m_s.bear = 0; m_s.calling &= ~CALL_BEAR; setMode(M_BEAR_GONE, FPS * 3 / 2); beep(B_HAPPY); }
        return;
    }
    beep(B_CLICK);
    switch (m_s.mode) {
    case M_DEAD:
        if (m_s.modeTimer > FPS * 3 || b & BTN_B) { newEgg(); }
        return;
    case M_MAIN:
        if (m_s.stage == ST_EGG) { if (b & BTN_B) setMode(M_CLOCK); return; }
        if (b & BTN_A) m_s.icon = (m_s.icon == I_NONE) ? 0 : (m_s.icon >= I_DISCIPLINE ? I_NONE : m_s.icon + 1);
        else if (b & BTN_B) { if (m_s.icon == I_NONE) setMode(M_CLOCK); else selectIcon(); }
        else if (b & BTN_C) m_s.icon = I_NONE;
        return;
    case M_FOOD:
        if (b & BTN_A) m_s.foodSel ^= 1;
        else if (b & BTN_C) setMode(M_MAIN);
        else if (b & BTN_B) {
            if (m_s.foodSel == 0) {
                if (m_s.hunger >= 4) { setMode(M_NO, FPS * 3 / 2); beep(B_SAD); return; }
                m_s.hunger++; m_s.calling &= ~CALL_HUNGER;
                m_s.weight = (uint8_t)std::min(99, m_s.weight + 1);
                m_s.waterTimer += 6 * 60;
            } else {
                if (m_s.happy < 4) m_s.happy++;
                m_s.calling &= ~CALL_HAPPY;
                m_s.happyTimer = std::max(0, m_s.happyTimer - 10 * 60);
                m_s.weight = (uint8_t)std::min(99, m_s.weight + 2);
                m_s.snackRun++; m_s.snackTimer = 30 * 60;
                if (m_s.snackRun >= 5 && m_s.stage >= ST_CHILD) { m_s.snackRun = 0; makeSick(true); }
            }
            setMode(M_EAT, FPS * 2);
        }
        return;
    case M_STATS:
        if (b & BTN_C) setMode(M_MAIN);
        else if (++m_s.page > 4) setMode(M_MAIN);
        return;
    case M_CLOCK:
        setMode(M_MAIN);
        return;
    case M_GAME:
        if (b & BTN_C) { setMode(M_MAIN); return; }
        if ((m_s.gamePhase == 0 || m_s.gamePhase == 3) && (b & (BTN_A | BTN_B))) {
            m_s.gameGuess = (b & BTN_A) ? 0 : 1;
            m_s.gameAnswer = (uint8_t)rnd(2);
            m_s.gameOcto = rnd(10) == 0;
            m_s.gamePhase = 1;
            m_s.modeTimer = FPS;
        } else if (m_s.gamePhase == 2) {
            setMode(M_MAIN);
        }
        return;
    default:
        return;   // animations ignore buttons
    }
}

// ---------------------------------------------------------------- frames
void OceanPetCore::frameTick()
{
    m_frame++;
    if (m_s.modeTimer > 0 && --m_s.modeTimer == 0) {
        switch (m_s.mode) {
        case M_EAT: case M_SCOLD: case M_NO: case M_FLUSH: case M_HATCH: case M_EVOLVE: case M_BEAR_GONE:
            if (m_s.mode == M_FLUSH && m_s.water > 0) {
                // one flush clears murky water; black water takes four
                if (m_s.water >= 4) {
                    if (m_s.blackFlushes == 0) m_s.blackFlushes = 4;
                    if (--m_s.blackFlushes == 0) m_s.water = 0;
                } else m_s.water = 0;
                if (m_s.water < 3) m_s.calling &= ~CALL_WATER;
            }
            setMode(M_MAIN);
            break;
        case M_MEDICINE:
            if (m_s.dosesLeft > 0) m_s.dosesLeft--;
            if (m_s.dosesLeft == 0) { m_s.sick = 0; m_s.injured = 0; beep(B_HAPPY); }
            setMode(M_MAIN);
            break;
        case M_GAME:
            if (m_s.gamePhase == 1) {
                if (m_s.gameOcto) {
                    // the octopus inks the water and spoils the fun
                    m_s.water = (uint8_t)std::min(4, m_s.water + 2);
                    if (m_s.water == 4) m_s.blackFlushes = 4;
                    if (m_s.water >= 3) m_s.calling |= CALL_WATER;
                    m_s.happy = 0;
                    m_s.gamePhase = 2; m_s.gameWins = 0; m_s.gameRound = 5;
                    m_s.modeTimer = 0;
                    beep(B_SAD);
                    break;
                }
                if (m_s.gameGuess == m_s.gameAnswer) m_s.gameWins++;
                m_s.gameRound++;
                if (m_s.gameRound >= 5) {
                    m_s.gamePhase = 2;
                    if (m_s.gameWins >= 3) { if (m_s.happy < 4) m_s.happy++; m_s.calling &= ~CALL_HAPPY; beep(B_HAPPY); }
                    else beep(B_SAD);
                } else {
                    m_s.gamePhase = 3; m_s.modeTimer = FPS;   // show the result briefly
                }
            } else if (m_s.gamePhase == 3) {
                m_s.gamePhase = 0;
            }
            break;
        default: break;
        }
    }
    if (m_s.mode == M_DEAD) m_s.modeTimer++;   // counts up (spirit animation)

    // the pet wanders about
    if (m_s.mode == M_MAIN && !m_s.asleep && m_s.stage != ST_EGG && --m_s.petMoveTimer <= 0) {
        m_s.petMoveTimer = FPS / 2 + (int)rnd(FPS);
        int r = (int)rnd(4);
        if (r == 0) m_s.petDir = -m_s.petDir;
        m_s.petX = (int8_t)std::max(0, std::min(16, m_s.petX + m_s.petDir * 2));
        if (m_s.petX == 0) m_s.petDir = 1;
        if (m_s.petX == 16) m_s.petDir = -1;
    }
}

void OceanPetCore::runFrame()
{
    uint32_t pressed = m_inputs & ~m_prevInputs;
    m_prevInputs = m_inputs;
    for (uint32_t b = 1; b <= BTN_TOUCH; b <<= 1) if (pressed & b) onPress(b);

    frameTick();
    if (m_frame % FPS == 0) secondTick();
    synthAudio(32768 / FPS);
    if (m_audio.size() > 32768) m_audio.erase(m_audio.begin(), m_audio.end() - 4096);
    render();
}

bool OceanPetCore::advanceTime(double seconds)
{
    if (seconds <= 0) return true;
    m_quiet = true;
    long n = (long)seconds;
    for (long i = 0; i < n; i++) {
        secondTick();
        if (m_s.dead) {
            // time keeps passing for the clock
            m_s.clock += n - i - 1;
            break;
        }
    }
    m_quiet = false;
    if (!m_s.dead && m_s.mode != M_MAIN) setMode(M_MAIN);
    render();
    return true;
}

// ---------------------------------------------------------------- drawing
void OceanPetCore::clear() { std::memset(m_lcd, 0, sizeof(m_lcd)); }

void OceanPetCore::px(int x, int y, int on)
{
    if (x >= 0 && x < 32 && y >= 0 && y < 16) m_lcd[y][x] = (uint8_t)on;
}

void OceanPetCore::sprite(const Sprite& s, int x, int y, bool flip, bool invert)
{
    for (int r = 0; r < s.h; r++)
        for (int c = 0; c < s.w; c++) {
            char ch = s.rows[r][flip ? s.w - 1 - c : c];
            if (ch == '#') px(x + c, y + r, invert ? 0 : 1);
        }
}

void OceanPetCore::text(const char* t, int x, int y)
{
    for (; *t; t++, x += 4) {
        if (*t == ' ') continue;
        const char* g = kFont3x5(*t);
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++) if (g[r * 3 + c] == '#') px(x + c, y + r);
    }
}

void OceanPetCore::number(int n, int x, int y)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", n);
    text(buf, x, y);
}

void OceanPetCore::bigTime(int x, int y)
{
    int h = hour(), m = minute();
    int h12 = h % 12; if (h12 == 0) h12 = 12;
    int digits[4] = { h12 / 10, h12 % 10, m / 10, m % 10 };
    int xs[4] = { x, x + 6, x + 14, x + 20 };
    for (int i = 0; i < 4; i++) {
        if (i == 0 && digits[0] == 0) continue;
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (kBig[digits[i]][r * 5 + c] == '#') px(xs[i] + c, y + r);
    }
    if (m_frame % FPS < FPS / 2) { px(x + 12, y + 2); px(x + 12, y + 4); }
    text(h < 12 ? "AM" : "PM", x + 14, y + 9);
}

const Sprite& OceanPetCore::petSprite() const
{
    switch (m_s.character) {
    case C_SPECK: return sSpeck;
    case C_JELLO: return sJello;
    case C_FINLET: return sFinlet;
    case C_PUFFO: return sPuffo;
    case C_HIPPA: return sHippa;
    case C_STRIPES: return sStripes;
    case C_CLAMMY: return sClammy;
    case C_BLUEY: return sBluey;
    case C_SKIPPY: return sSkippy;
    case C_UNICO: return sUnico;
    }
    return sEgg;
}

void OceanPetCore::render()
{
    clear();
    bool blink = (m_frame / (FPS / 2)) & 1;
    int t = m_s.modeTimer;
    bool invertAll = false;

    switch (m_s.mode) {
    case M_MAIN:
        if (m_s.stage == ST_EGG) {
            int wob = (m_frame / (FPS / 2)) % 4;
            sprite(sEgg, 8 + (wob == 1 ? 1 : wob == 3 ? -1 : 0), 0);
            break;
        }
        if (!m_s.lightsOn) {
            if (m_s.asleep && blink) { sprite(sZ, 20, 3); sprite(sZ, 25, 0); }
            break;
        }
        if (m_s.bear) {
            sprite(petSprite(), 0, 0, false);
            if (blink) sprite(sBear, 16, 0);
            break;
        }
        if (m_s.asleep) {
            sprite(petSprite(), 8, blink ? 1 : 0);
            sprite(sZ, 26, blink ? 0 : 2);
        } else {
            sprite(petSprite(), m_s.petX, blink ? 1 : 0, m_s.petDir > 0);
        }
        if (m_s.sick) sprite(sGerm, m_s.petX < 8 ? 25 : 1, 1);
        // murky water: specks drifting
        {
            static const int sx[12] = { 2, 29, 6, 24, 13, 19, 1, 30, 9, 22, 15, 27 };
            static const int sy[12] = { 13, 3, 6, 12, 14, 1, 2, 9, 11, 14, 0, 6 };
            int n = m_s.water >= 4 ? 0 : m_s.water * 4;
            for (int i = 0; i < n; i++) px(sx[i], (sy[i] + m_frame / FPS) % 16);
        }
        if (m_s.water >= 4) invertAll = true;   // the water has turned black
        break;

    case M_FOOD:
        sprite(sMeal, 12, 1);
        sprite(sSnack, 12, 8);
        // selection arrow
        for (int i = 0; i < 4; i++) { px(4 + i, (m_s.foodSel ? 11 : 3) + 0); }
        px(7, m_s.foodSel ? 10 : 2); px(7, m_s.foodSel ? 12 : 4); px(6, m_s.foodSel ? 9 : 1); px(6, m_s.foodSel ? 13 : 5);
        break;

    case M_EAT: {
        const Sprite& food = m_s.foodSel ? sSnack : sMeal;
        int bites = 3 - t * 3 / (FPS * 2 + 1);   // 0..2 bites taken
        sprite(food, 2, 5);
        for (int b = 0; b < bites; b++) for (int r = 0; r < 8; r++) for (int c = 0; c < 3; c++) px(2 + 5 - b * 3 + c, 5 + r, 0);
        sprite(petSprite(), 14, (t / 8) & 1, false);
        break;
    }

    case M_STATS:
        switch (m_s.page) {
        case 0: text("AGE", 0, 1); number(m_s.ageYears, 18, 1); text("WT", 0, 9); number(m_s.weight, 14, 9); text("G", 27, 9); break;
        case 1: text("FOOD", 0, 0); for (int i = 0; i < 4; i++) sprite(i < m_s.hunger ? sHeartFull : sHeartEmpty, i * 8, 9); break;
        case 2: text("HAPPY", 0, 0); for (int i = 0; i < 4; i++) sprite(i < m_s.happy ? sHeartFull : sHeartEmpty, i * 8, 9); break;
        case 3:
            text("TRAIN", 0, 0);
            for (int i = 0; i < 8; i++) for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++)
                if (i < m_s.discipline || r == 0 || r == 4 || c == 0) px(i * 4 + c, 9 + r);
            break;
        case 4: text("WATER", 0, 0); for (int i = 0; i < 4; i++) sprite(i < m_s.water ? sSkullDirty : sSkullClean, i * 8, 9); break;
        }
        break;

    case M_GAME:
        if (m_s.gamePhase == 2) {
            if (m_s.gameOcto && m_s.gameWins == 0 && m_s.gameRound == 5 && m_s.happy == 0) {
                sprite(sOcto, 10, 2);
                for (int i = 0; i < 20; i++) px((int)((i * 7 + m_frame / 4) % 32), (i * 5) % 16);
            } else {
                text(m_s.gameWins >= 3 ? "WIN" : "LOSE", 0, 0);
                number(m_s.gameWins, 10, 9); text("/5", 14, 9);
            }
        } else {
            // round dots
            for (int i = 0; i < 5; i++) { px(11 + i * 2, 0, i < m_s.gameRound ? 1 : (blink ? 1 : 0)); }
            for (int side = 0; side < 2; side++) {
                int x = side ? 20 : 2;
                bool open = (m_s.gamePhase == 1 || m_s.gamePhase == 3) && m_s.gameGuess == side;
                sprite(open ? sChestOpen : sChest, x, 9);
                if (open) {
                    if (m_s.gamePhase == 1 && m_s.gameOcto) sprite(sOcto, x - 1, 0);
                    else if (m_s.gameAnswer == side) sprite(sGem, x + 2, 3);
                }
            }
            if (m_s.gamePhase == 0) { sprite(sStar, 13, 7 + (blink ? 0 : 1)); }
        }
        break;

    case M_MEDICINE: {
        int x = 30 - (FPS * 2 - t) / 4;
        sprite(petSprite(), 0, 0);
        sprite(sSyringe, std::max(12, x), 6);
        break;
    }

    case M_FLUSH: {
        sprite(petSprite(), m_s.petX, 0);
        int x = 32 - (FPS * 2 - t) / 2;
        for (int y = 0; y < 16; y += 4) sprite(sWave, x + (y & 4 ? 3 : 0), y);
        for (int y = 0; y < 16; y++) for (int c = x + 8; c < 32; c++) if ((c + y) % 3 == 0) px(c, y, 0);
        break;
    }

    case M_SCOLD:
        sprite(petSprite(), 8, 0);
        if (blink) sprite(sAnger, 25, 1);
        break;

    case M_NO:
        sprite(petSprite(), 8, 0, (t / 8) & 1);
        break;

    case M_CLOCK:
        bigTime(3, 1);
        break;

    case M_HATCH:
        sprite(t > FPS ? sEggCrack : sSpeck, 8, 0);
        if (t <= FPS && blink) sprite(sStar, 26, 2);
        break;

    case M_EVOLVE:
        sprite(petSprite(), 8, 0);
        if (t > FPS && blink) invertAll = true;
        break;

    case M_BEAR_GONE:
        sprite(petSprite(), 0, 0);
        sprite(sBear, 16 + (FPS * 3 / 2 - t) / 3, 0);
        break;

    case M_DEAD: {
        sprite(sShell, 8, 0);
        int rise = std::min(10, t / 16);
        if (t < FPS * 10) sprite(sSpirit, 22, 8 - rise);
        break;
    }
    }

    if (invertAll) for (int y = 0; y < 16; y++) for (int x = 0; x < 32; x++) m_lcd[y][x] ^= 1;

    // icons: the selected one, plus attention when it's calling
    std::memset(m_lcd[16], 0, 32);
    if (m_s.icon != I_NONE && m_s.icon < 7) m_lcd[16][m_s.icon] = 1;
    if (m_s.calling && !m_s.dead) m_lcd[16][I_ATTENTION] = 1;

    for (int y = 0; y < 17; y++) for (int x = 0; x < 32; x++) m_fb[y * 32 + x] = m_lcd[y][x] ? 0xff000000 : 0;
}

// ---------------------------------------------------------------- state
OceanPetCore::Debug OceanPetCore::debug() const
{
    return { m_s.stage, m_s.character, m_s.hunger, m_s.happy, m_s.discipline, m_s.weight, m_s.water, m_s.sick,
             m_s.careMistakes, m_s.discMistakes, m_s.ageYears, m_s.dead, m_s.calling, m_s.asleep, m_s.lightsOn,
             m_s.bear, m_s.napping, m_s.deathCause, (long long)m_s.clock, m_s.mode };
}

bool OceanPetCore::saveState(std::vector<uint8_t>& out)
{
    out.assign((const uint8_t*)&m_s, (const uint8_t*)&m_s + sizeof(m_s));
    return true;
}

bool OceanPetCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != sizeof(State)) return false;
    State s;
    std::memcpy(&s, in.data(), sizeof(s));
    if (s.magic != MAGIC || s.version != VERSION) return false;
    m_s = s;
    m_tones.clear();
    render();
    return true;
}
