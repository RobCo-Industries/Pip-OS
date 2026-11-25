/*
 * Rebuilds the panel picture from a Pip-OS test build's tftdump reply
 * with RobCo-Core's ILI9341 model (tests/host/ili9341_model.h).
 *
 *   panel_model <rotation> < dump.txt > view.bin
 *
 * Input: the reply lines without the "| " prefix. "C<hex>" lines hold
 * command bytes, "D" lines data words "hhhh" or "hhhh*<count>" and an
 * odd last byte "=hh", and the "end" line closes it. Output: "<w> <h>\n",
 * then w * h RGB565 values, little-endian, row by row, as someone holding
 * the panel turned by <rotation> degrees sees them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ili9341_model.h"

static model_t m;
static unsigned long fed;   /* bytes fed to the model */
static uint16_t view[MODEL_W * MODEL_H];

static unsigned hex(const char *p, unsigned n)
{
    unsigned v = 0;

    while (n-- > 0) {
        char c = *p++;

        v = v << 4 | (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10);
    }
    return v;
}

int main(int argc, char **argv)
{
    static char line[512];
    unsigned rotation = argc > 1 ? (unsigned)atoi(argv[1]) : 90u;
    unsigned w, n, i;
    int ended = 0;

    model_init(&m);
    while (fgets(line, sizeof(line), stdin) != NULL) {
        char *p = line + 1;

        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "end ", 4) == 0) {
            unsigned long want = 0;

            /* A dump that lost a byte on the way must not pass. */
            if (sscanf(line, "end bytes=%lu", &want) != 1 || want != fed) {
                fprintf(stderr, "panel_model: %lu bytes read, the dump says %s\n", fed, line);
                return 1;
            }
            ended = 1;
            break;
        }
        if (line[0] == 'C') {
            for (; p[0] != '\0' && p[1] != '\0'; p += 2) {
                model_byte(&m, 0, (uint8_t)hex(p, 2));
                fed++;
            }
        } else if (line[0] == 'D') {
            while (*p == ' ') {
                p++;
                if (*p == '=') {
                    model_byte(&m, 1, (uint8_t)hex(p + 1, 2));
                    fed++;
                    p += 3;
                    continue;
                }
                w = hex(p, 4);
                p += 4;
                n = 1;
                if (*p == '*') {
                    n = hex(p + 1, 8);
                    p += 9;
                }
                for (i = 0; i < n; i++) {
                    model_byte(&m, 1, (uint8_t)(w >> 8));
                    model_byte(&m, 1, (uint8_t)w);
                }
                fed += 2ul * n;
            }
        } else {
            fprintf(stderr, "panel_model: bad line: %s\n", line);
            return 1;
        }
    }
    if (!ended) {
        fprintf(stderr, "panel_model: no end line\n");
        return 1;
    }
    w = model_view(&m, rotation, view);
    printf("%u %u\n", w, MODEL_W * MODEL_H / w);
    for (i = 0; i < MODEL_W * MODEL_H; i++) {
        putchar(view[i] & 0xff);
        putchar(view[i] >> 8);
    }
    return 0;
}
