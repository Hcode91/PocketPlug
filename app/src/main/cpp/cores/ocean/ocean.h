// PocketPlug - "Egg Pet - Ocean (original)": a built-in LCD pet in the
// spirit of the 1998 ocean-themed egg pets. Our own game, characters and
// art; no ROM. Same 32x16 dot + 8 icon LCD and A/B/C buttons as the others.
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "../../core_api.h"

class OceanPetCore : public Core {
public:
    OceanPetCore();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 17; }
    double refreshRate() const override { return 64.0; }
    int sampleRate() const override { return 32768; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    bool advanceTime(double seconds) override;
    void setWallClock(int64_t localSeconds) override;
    void setOption(int id, int value) override;

    // input bits
    enum { BTN_A = 1, BTN_B = 2, BTN_C = 4, BTN_TOUCH = 8 };

    // for tests
    struct Debug { int stage, character, hunger, happy, discipline, weight, water, sick, care, disc, age, dead, calling, asleep, lightsOn, bear, napping, cause; long long clock; int mode; };
    Debug debug() const;

    enum Mode : uint8_t {
        M_MAIN, M_FOOD, M_EAT, M_STATS, M_GAME, M_MEDICINE, M_FLUSH, M_SCOLD, M_CLOCK,
        M_HATCH, M_EVOLVE, M_DEAD, M_NO, M_BEAR_GONE
    };

    struct State {
        uint32_t magic, version;
        int64_t clock;             // pet's local wall clock (seconds)
        uint8_t gentle;
        uint8_t stage;             // see .cpp
        uint8_t character;
        uint8_t hunger, happy, discipline, weight, water;
        uint8_t asleep, napping, lightsOn;
        uint8_t sick, dosesLeft, injured;
        uint8_t careMistakes, discMistakes, neglectSick, injuriesThisStage;
        uint8_t calling;           // attention reasons (bits)
        uint8_t discCall, bear, snackRun;
        uint8_t dead, deathCause, blackFlushes;
        int32_t stageSecs, ageYears, lifeDays;
        int32_t hungerTimer, happyTimer, waterTimer, waterDirtyTimer;
        int32_t callTimerHunger, callTimerHappy, lightsTimer, discCallTimer, bearTimer;
        int32_t nextDiscCall, sickTimer, snackTimer, eggTimer, lifespanDays;
        int32_t napBearAt[2];
        uint32_t rng;
        // ---- UI ----
        uint8_t mode, icon, page, foodSel, gameRound, gameWins, gameGuess, gameAnswer, gamePhase, gameOcto;
        int32_t modeTimer;         // frames
        int8_t petX, petDir;
        int32_t petMoveTimer;
        uint8_t lastMode;
    };

private:
    State m_s;
    uint32_t m_fb[32 * 17];
    uint8_t m_lcd[17][32];
    uint32_t m_inputs = 0, m_prevInputs = 0;
    int m_frame = 0;
    bool m_quiet = false;

    // sound: queue of (frequency, samples)
    struct Tone { int freq; int samples; };
    std::vector<Tone> m_tones;
    std::vector<int16_t> m_audio;
    int m_phase = 0;

    uint32_t rnd(uint32_t n);
    void newEgg();
    void secondTick();
    void frameTick();
    void onPress(uint32_t b);
    void beep(int kind);
    void setMode(int m, int frames = 0);
    void selectIcon();
    void evolve();
    void die(int cause);
    void makeSick(bool neglect);
    void stageTimes(int& evolveSecs, int& hungerSecs, int& happySecs, int& sleepH, int& wakeH) const;
    int hour() const;
    int minute() const;
    bool isNight() const;
    void render();
    void clear();
    void px(int x, int y, int on = 1);
    void sprite(const struct Sprite& s, int x, int y, bool flip = false, bool invert = false);
    void text(const char* t, int x, int y);
    void number(int n, int x, int y);
    void bigTime(int x, int y);
    const struct Sprite& petSprite() const;
    void synthAudio(size_t samples);
};
