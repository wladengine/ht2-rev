extern "C" void __cdecl Task_PlacePresents();

namespace campaign_clock {

// Leave one parking node available and request one replacement through the actual scheduled task.
static void checkParkingDistance() {
    BYTE** first = field<BYTE**>(global(0x68cf00), 0);
    BYTE** end = field<BYTE**>(global(0x68cf04), 0);
    BYTE* candidate = nullptr;
    for (BYTE** it = first; it != end; ++it) {
        BYTE* node = *it;
        const char* name = field<const char*>(node, 0);
        if (name && !strncmp(name, "pos_parking", 11)) {
            if (!candidate && !(node[12] & 16))
                candidate = node;
            node[12] |= 16;
        }
    }
    require(candidate != nullptr, "available parking node");
    candidate[12] &= ~16;
    for (unsigned address : {0x68a98c, 0x68cf54, 0x68d0a0, 0x68a964})
        field<int>(global(address), 0) = 0;
    int& count = field<int>(global(0x68cf34), 0);
    const int before = count;
    field<int>(global(0x68bb54), 0) = before + 1;
    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    BYTE* player = field<BYTE*>(head, 0);
    while (player != head && field<int>(player, 92) != 1)
        player = field<BYTE*>(player, 0);
    require(player != head, "human driver for parking test");
    double* xyz = reinterpret_cast<double*>(player + ((player[96] & 1) ? 624 : 312));
    double* node = reinterpret_cast<double*>(candidate + 48);
    xyz[1] = node[1];
    xyz[2] = node[2];
    xyz[0] = node[0] + 699.5;
    ROUTINE(Task_PlacePresents, 0x41ed50)();
    require(count == before && !(candidate[12] & 16), "parking below distance threshold");
    xyz[0] = node[0] + 700.0;
    ROUTINE(Task_PlacePresents, 0x41ed50)();
    require(count == before + 1 && (candidate[12] & 16), "parking exactly at 700 world units");
    BYTE* list = field<BYTE*>(global(0x68cf6c), 0);
    BYTE* last = field<BYTE*>(field<BYTE*>(list, 4), 8);
    require(field<int>(last, 24) == field<int>(candidate, 16) && (last[8] & 128), "scheduled parking node identity");
    fprintf(report, "parking replacement at 700 world units, node=%d\n", field<int>(last, 24));
}


static void snapshot(unsigned time, unsigned tick) {
    fprintf(report, "clock %u tick=%u", time, tick);
    for (unsigned i = 0; i < 256; ++i)
        if (eventCounts[i]) fprintf(report, " e%u=%u", i, eventCounts[i]);
    fprintf(report, "\nrng");
    for (int i = 0; i < 57; ++i)
        fprintf(report, " %08x", field<DWORD>(global(0x68b9c8), 4 * i));

    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    BYTE* actors[256];
    int count = 0;
    for (BYTE* p = field<BYTE*>(head, 0); p != head; p = field<BYTE*>(p, 0)) {
        require(count < 256, "campaign clock roster size");
        actors[count++] = p;
    }
    fprintf(report, "\nactors %d ring=%d\n", count, field<int>(global(0x66c6e0), 0));
    for (int i = 0; i < count; ++i) {
        BYTE* p = actors[i];
        BYTE* owner = field<BYTE*>(p, 176);
        int ownerIndex = owner == head ? -1 : (owner ? -2 : -3);
        for (int j = 0; j < count; ++j)
            if (actors[j] == owner) ownerIndex = j;
        BYTE* vehicle = field<BYTE*>(p, 516);
        fprintf(report, "%d role=%d flags=%08x job=%d owner=%d licences=%u staff=%u retiring=%u model=%d wins=%d",
                i, field<int>(p, 92), field<DWORD>(p, 96), field<int>(p, 100), ownerIndex,
                (field<DWORD>(p, 148) - field<DWORD>(p, 144)) / 8,
                (field<DWORD>(p, 164) - field<DWORD>(p, 160)) / 16,
                unsigned(p[496]), vehicle ? field<int>(vehicle, 4) : -1, field<int>(p, 536));
        // Balance, world position and market share, including the exact floating-point bits.
        for (int offset : {136, 624, 632, 640, 768})
            fprintf(report, " %08x%08x", field<DWORD>(p, offset + 4), field<DWORD>(p, offset));
        fputc('\n', report);
    }
}

static void run(unsigned clockSeed) {
    constexpr unsigned unitsPerSecond = 1024, end = 600 * unitsPerSecond;
    unsigned time = 0, nextSample = 0;
    for (unsigned tick = 0;; ++tick) {
        field<double>(view, 2968) = double(time) / unitsPerSecond;
        ROUTINE(Ai_Tick, 0x42d6e0)();
        if (time >= nextSample || time == end || eventCounts[12]) {
            snapshot(time, tick);
            nextSample = time + 30 * unitsPerSecond;
        }
        if (time == end || eventCounts[12]) break;
        // Vary frame spacing independently of the game's RNG.
        clockSeed = clockSeed * 1664525u + 1013904223u;
        time += 12 + ((clockSeed >> 16) % 25);
        if (time > end) time = end;
    }
}

} // namespace campaign_clock
