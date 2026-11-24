/* PocketPlug - B-em's Mode 7 font builder (m7makechars.c, GPL-2+) made into a
 * function: the text glyphs come from the SAA5050 character ROM the user
 * provides (MAME's saa5050: 96 characters x 10 rows, 5 pixels in bits 4-0),
 * the block graphics are generated, and the result is written as B-em's
 * BEMTTX01 font file.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t teletext_characters[96*60];
static uint8_t *mode7_chars, *mode7_charsi, *mode7_graph, *mode7_graphi, *mode7_sepgraph, *mode7_sepgraphi, *mode7_tempi, *mode7_tempi2;

static void decode_row(uint8_t *dest, unsigned bits)
{
    for (int c = 0; c < 16; c++) {
        *dest++ = (bits & 0x8000) ? 15 : 0;
        bits <<= 1;
    }
}

static void generate_contig(uint8_t *dest, unsigned int ch, unsigned int y)
{
    unsigned left = 0xff00u;
    unsigned right = 0x00ffu;
    unsigned bits = 0;
    if ((y >= 0) && (y <= 5)) {
        if (ch & 1) bits = left;
        if (ch & 2) bits += right;
    }
    else if ((y >= 6) && (y <= 13)) {
        if (ch & 4) bits = left;
        if (ch & 8) bits += right;
    }
    else if ((y >= 14) && (y <= 19)) {
        if (ch & 16) bits = left;
        if (ch & 64) bits += right;
    }
    decode_row(dest, bits);
}

static void generate_balanced(uint8_t *dest, unsigned int ch, unsigned int y)
{
    unsigned bits = 0;
    unsigned left = 0x7e00u;
    unsigned right = 0x007eu;
    if ((y >= 1) && (y <= 4)) {
        if (ch & 1) bits = left;
        if (ch & 2) bits += right;
    }
    else if ((y >= 7) && (y <= 12)) {
        if (ch & 4) bits = left;
        if (ch & 8) bits += right;
    }
    else if ((y >= 15) && (y <= 18)) {
        if (ch & 16) bits = left;
        if (ch & 64) bits += right;
    }
    decode_row(dest, bits);
}

static void generate_right(uint8_t *dest, unsigned int ch, unsigned int y)
{
    unsigned bits = 0;
    unsigned left = 0x3f00u;
    unsigned right = 0x003fu;
    if ((y >= 0) && (y <= 3)) {
        if (ch & 1) bits = left;
        if (ch & 2) bits += right;
    }
    else if ((y >= 6) && (y <= 11)) {
        if (ch & 4) bits = left;
        if (ch & 8) bits += right;
    }
    else if ((y >= 14) && (y <= 17)) {
        if (ch & 16) bits = left;
        if (ch & 64) bits += right;
    }
    decode_row(dest, bits);
}

static void generate_gchars(int right, int ch)
{
    int offs = (ch - 0x20) * 160;
    for (int line = 0; line < 20; line += 2) {
        generate_contig(mode7_graph + offs, ch, line);
        generate_contig(mode7_graphi + offs, ch, line+1);
        if (right) {
            generate_right(mode7_sepgraph + offs, ch, line);
            generate_right(mode7_sepgraphi + offs, ch, line+1);
        }
        else {
            generate_balanced(mode7_sepgraph + offs, ch, line);
            generate_balanced(mode7_sepgraphi + offs, ch, line+1);
        }
        offs += 16;
    }
}

static void generate_graphics(int right)
{
    for (int ch = 0x20; ch < 0x40; ch++)
        generate_gchars(right, ch);
    for (int ch = 0x60; ch < 0x80; ch++)
        generate_gchars(right, ch);
}

static void round_text(uint8_t *p, uint8_t *p2)
{
    for (int c = 0; c < 96; c++) {
        for (int y = 0; y < 10; y++) {
            for (int d = 0; d < 6; d++) {
                int stat = 0;
                if (y < 9 && p[(y * 6) + d] && p[(y * 6) + d + 6])
                    stat |= 3;  /*Above + below - set both */
                if (y < 9 && d > 0 && p[(y * 6) + d] && p[(y * 6) + d + 5] && !p[(y * 6) + d - 1])
                    stat |= 1;  /*Above + left  - set left */
                if (y < 9 && d > 0 && p[(y * 6) + d + 6] && p[(y * 6) + d - 1] && !p[(y * 6) + d + 5])
                    stat |= 1;  /*Below + left  - set left */
                if (y < 9 && d < 5 && p[(y * 6) + d] && p[(y * 6) + d + 7] && !p[(y * 6) + d + 1])
                    stat |= 2;  /*Above + right - set right */
                if (y < 9 && d < 5 && p[(y * 6) + d + 6] && p[(y * 6) + d + 1] && !p[(y * 6) + d + 7])
                    stat |= 2;  /*Below + right - set right */

                p2[0] = (stat & 1) ? 15 : 0;
                p2[1] = (stat & 2) ? 15 : 0;
                p2 += 2;
            }
        }
        p += 60;
    }
}

static void resample_text(int width)
{
    int wminus = width - 1;
    int offs1 = 0, offs2 = 0;
    for (int c = 0; c < 960; c++) {
        float x = 0;
        int x2 = 0;
        for (int d = 0; d < width; d++) {
            mode7_chars[offs2 + d] = (int) (((float) mode7_tempi2[offs1 + x2] * (1.0 - x)) + ((float) mode7_tempi2[offs1 + x2 + 1] * x));
            mode7_charsi[offs2 + d] = (int) (((float) mode7_tempi[offs1 + x2] * (1.0 - x)) + ((float) mode7_tempi[offs1 + x2 + 1] * x));
            x += (11.0 / wminus);
            if (x >= 1.0) {
                x2++;
                x -= 1.0;
            }
        }
        offs1 += 12;
        offs2 += width;
    }
}

static void text_blastthrough(int width)
{
    int bytes_per_char = width * 10;
    int offs = 0x20 * bytes_per_char;
    memcpy(mode7_graph + offs, mode7_chars + offs, offs);
    memcpy(mode7_graphi + offs, mode7_charsi + offs, offs);
    memcpy(mode7_sepgraph + offs, mode7_chars + offs, offs);
    memcpy(mode7_sepgraphi + offs, mode7_charsi + offs, offs);
}

static void save_bank(FILE *fp, int width, uint8_t *glyph, uint8_t *field1, uint8_t *field2)
{
    for (int ch = 0; ch < 96; ch++) {
        uint8_t *gptr = glyph;
        for (int row = 0; row < 10; row++)
            for (int col = 0; col < width; col++)
                *gptr++ = *field1++ | (*field2++ << 4);
        fwrite(glyph, 10 * width, 1, fp);
    }
}

int bem_make_font(const uint8_t *saa, size_t n, const char *fn)
{
    const int width = 16;
    if (n < 960) return 1;
    /* the ROM's 5 columns go to columns 1-5 of the 6-wide cell */
    for (int ch = 0; ch < 96; ch++)
        for (int row = 0; row < 10; row++) {
            uint8_t bits = saa[ch * 10 + row];
            for (int col = 0; col < 6; col++)
                teletext_characters[ch * 60 + row * 6 + col] = (col >= 1 && (bits >> (5 - col) & 1)) ? 1 : 0;
        }
    int char_size = 10 * width;
    int bank_size = char_size * 96;
    int total_size = bank_size * 8 + char_size;
    uint8_t *mem = malloc(total_size);
    if (!mem) return 3;
    FILE *fp = fopen(fn, "wb");
    if (!fp) { free(mem); return 2; }
    memset(mem, 0xff, total_size);
    char hdr[11];
    memcpy(hdr, "BEMTTX01", 8);
    hdr[8] = width;
    hdr[9] = 10;
    hdr[10] = 0;
    fwrite(hdr, sizeof(hdr), 1, fp);
    mode7_chars = mem;
    mode7_charsi = mem + bank_size;
    mode7_graph = mem + bank_size * 2;
    mode7_graphi = mem + bank_size * 3;
    mode7_sepgraph = mem + bank_size * 4;
    mode7_sepgraphi = mem + bank_size * 5;
    mode7_tempi = mem + bank_size * 6;
    mode7_tempi2 = mem + bank_size * 7;
    generate_graphics(0);
    for (int c = 0; c < (96 * 60); c++)
        teletext_characters[c] *= 15;
    for (int c = 0; c < (96 * 120); c++)
        mode7_tempi2[c] = teletext_characters[c >> 1];
    round_text(teletext_characters, mode7_tempi);
    resample_text(width);
    text_blastthrough(width);
    uint8_t *glyph = mem + bank_size * 8;
    save_bank(fp, width, glyph, mode7_chars, mode7_charsi);
    save_bank(fp, width, glyph, mode7_graph, mode7_graphi);
    save_bank(fp, width, glyph, mode7_sepgraph, mode7_sepgraphi);
    fclose(fp);
    free(mem);
    return 0;
}
