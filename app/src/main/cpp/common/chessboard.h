// PocketPlug - chess computer sensor boards
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of the logic of MAME's sensorboard.cpp (hap): the pieces on the board,
// the piece in the player's hand, and what the sensors under each square read.
// Magnet boards sense pieces standing on a square; button boards only see a
// square being pressed (for a short "sensor delay").
//
// Squares are numbered y * 8 + x, x = file a..h, y = rank 1..8.
// Pieces: 1-6 = white pawn, knight, bishop, rook, queen, king; 7-12 = black.

#pragma once
#include <cstdint>
#include <cstring>

struct ChessBoard {
    uint8_t piece[64];
    uint8_t hand;
    int8_t handPos, sensorPos, dropPos;
    uint8_t magnets;
    int32_t sensorTimer;     // host cycles until the sensor delay ends
    int32_t delayCycles;

    void init(bool magnetBoard, int32_t delay) {
        std::memset(this, 0, sizeof(*this));
        magnets = magnetBoard ? 1 : 0;
        delayCycles = delay;
        handPos = sensorPos = dropPos = -1;
        presetChess();
    }

    void presetChess() {
        std::memset(piece, 0, sizeof(piece));
        static const uint8_t back[8] = { 4, 2, 3, 5, 6, 3, 2, 4 };
        for (int x = 0; x < 8; x++) {
            piece[x] = back[x];
            piece[8 + x] = 1;
            piece[48 + x] = 7;
            piece[56 + x] = uint8_t(back[x] + 6);
        }
        hand = 0; handPos = -1;
        cancelSensor();
    }
    void clear() { std::memset(piece, 0, sizeof(piece)); hand = 0; handPos = -1; cancelSensor(); }

    void cancelSensor() { sensorTimer = 0; sensorPos = dropPos = -1; }
    void removeHand() {
        hand = 0;
        if (handPos != -1) { piece[handPos] = 0; handPos = -1; }
    }

    // the player touched a square
    void click(int pos) {
        if (pos < 0 || pos > 63 || sensorPos != -1) return;
        if (delayCycles > 0 && (magnets || pos != handPos)) { sensorPos = int8_t(pos); sensorTimer = delayCycles; }
        if (hand) {
            uint8_t p = hand;
            removeHand();
            piece[pos] = p;
            if (delayCycles > 0) dropPos = int8_t(pos);   // magnets: a captured piece is lifted first
        } else if (piece[pos]) {
            hand = piece[pos];
            handPos = int8_t(pos);
        }
    }

    // take a new piece into the hand (the spawn panel)
    void spawn(uint8_t p) { cancelSensor(); hand = p; handPos = -1; }

    void tick(int cycles) {
        if (sensorTimer > 0 && (sensorTimer -= cycles) <= 0) { sensorTimer = 0; sensorPos = dropPos = -1; }
    }

    bool sensor(int x, int y) const {
        int pos = y * 8 + x;
        if (magnets) return piece[pos] != 0 && pos != handPos && pos != dropPos;
        return pos == sensorPos;
    }
    // bit n = file n (or 7 - n when reversed), as sensorboard read_rank
    uint8_t readRank(int y, bool reverse = false) const {
        uint8_t d = 0;
        for (int x = 0; x < 8; x++) if (sensor(x, y)) d |= uint8_t(1 << (reverse ? 7 - x : x));
        return d;
    }
};

// Commands from the board view (Core::keyEvent codes)
enum ChessBoardKey {
    CB_SQUARE = 0x00,        // + square 0-63
    CB_SPAWN = 0x40,         // + piece - 1 (0-11)
    CB_REMOVE = 0x50,        // drop the piece in hand off the board
    CB_RESET = 0x51,         // pieces back to the start
    CB_CLEAR = 0x52,         // empty board
};

inline bool chessBoardKey(ChessBoard& b, int code)
{
    if (code >= CB_SQUARE && code < CB_SQUARE + 64) { b.click(code); return true; }
    if (code >= CB_SPAWN && code < CB_SPAWN + 12) { b.spawn(uint8_t(code - CB_SPAWN + 1)); return true; }
    if (code == CB_REMOVE) { b.cancelSensor(); b.removeHand(); return true; }
    if (code == CB_RESET) { b.presetChess(); return true; }
    if (code == CB_CLEAR) { b.clear(); return true; }
    return false;
}
