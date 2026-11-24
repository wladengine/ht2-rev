// Steam 8.1 terrain query at 0x615da0. Run against x86 production objects.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

using GridByte = unsigned char;
extern "C" double __fastcall Grid_HeightAt(GridByte*, void*, const float*);

// The worker can also call the original entry in an initialized 8.1 process.
using HeightQuery = decltype(&Grid_HeightAt);

static int checkGridHeightStatus(HeightQuery query)
{
    static float tile[257 * 257 * 4];
    GridByte grid[40200] = {};
    float* tiles[] = {tile, tile, tile};
    float** table = tiles;
    memcpy(grid + 40192, &table, sizeof table);
    for (unsigned i = 0; i < sizeof(tile) / sizeof(float); i += 4)
        tile[i] = 2.0f;
    const float boundsAndScale[] = {0, 21, 21, 0, 1, 1};
    memcpy(grid + 40136, boundsAndScale, sizeof boundsAndScale);
    struct Point { float x, y; double height; bool inexact; };
    const Point cases[] = {
        {4, 8, 2, false}, {1.75f, 4.5f, 2, true}, {5.75f, 8.5f, 2, true},
        {1.75f, 8, 2, true}, {4, 8.5f, 2, true},
        {-1, 8, 450, false}, {21, 8, 450, false}, {4, 0, 450, false}
    };
    GridByte saved[108];
    __asm fnsave saved
    int failures = 0;
    const unsigned precisions[] = {0, 0x200, 0x300};
    for (unsigned precision : precisions) {
        for (unsigned direction = 0; direction < 4; ++direction) {
            const unsigned short control = 0x007f | precision | (direction << 10);
            for (unsigned prior = 0; prior < 2; ++prior) {
                for (const Point& p : cases) {
                    float position[] = {p.x, p.y, 0};
                    unsigned short afterControl, immediate, following;
                    __asm fninit
                    __asm fldcw control
                    if (prior) {
                        const double fraction = 1.5;
                        __asm fld fraction
                        __asm frndint
                        __asm fstp st(0)
                    }
                    double h = query(grid, nullptr, position);
                    __asm fnstsw immediate
                    __asm fnstcw afterControl
                    double next;
                    __asm fld h
                    __asm fld1
                    __asm faddp st(1), st(0)
                    __asm fstp next
                    __asm fnstsw following
                    unsigned expectedFlags = (prior || p.inexact) ? 0x20 : 0;
                    if (h != p.height || next != p.height + 1 || afterControl != control ||
                        (immediate & 0x3f) != expectedFlags || (following & 0x3f) != expectedFlags)
                        ++failures;
                }
            }
        }
    }
    __asm frstor saved
    return failures;
}

#ifndef GRID_HEIGHT_TEST_WORKER
int main()
{
    int failures = checkGridHeightStatus(Grid_HeightAt);
    printf("Grid height and x87 status: %d of 192 cases failed\n", failures);
    return failures != 0;
}
#endif
