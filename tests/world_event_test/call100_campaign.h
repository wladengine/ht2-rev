extern "C" int* Node_ByName(int*, const char*);
extern "C" void* Room_FindIdByName(void*, void*);
extern "C" RoadPosition* Road_EdgeStart(RoadPosition*, BYTE*);
extern "C" RoadPosition* Road_EdgeEnd(RoadPosition*, BYTE*);

extern "C" void Road_NodeCost(int, int, int, double*);
extern "C" void* Player_CopyWorldPosition(void*, BYTE*);

namespace call100_campaign {
static void checkNames(DWORD va, bool rooms) {
    BYTE** begin = field<BYTE**>(global(va), 0);
    BYTE** end = field<BYTE**>(global(va + 4), 0);
    int count = 0;
    for (BYTE** it = begin; it < end; ++it) {
        char* name = field<char*>(*it, 0);
        if (!name || !*name)
            continue;
        int id = -1;
        void* result = rooms ? ROUTINE(Room_FindIdByName, 0x4364f0)(&id, name)
                             : ROUTINE(Node_ByName, 0x436510)(&id, name);
        require(result == &id && id >= 0 && id < end - begin, "name lookup");
        require(!strcmp(field<char*>(begin[id], 0), name), "reverse name lookup");
        fprintf(report, "%s %d %s\n", rooms ? "room" : "node", id, name);
        ++count;
    }
    require(count > 0, "named campaign records");
    fprintf(report, "total %s %d\n", rooms ? "rooms" : "nodes", count);
}
static void check() {
    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    int drivers = 0;
    for (BYTE* it = field<BYTE*>(head, 0); it != head; it = field<BYTE*>(it, 0)) {
        Point pt{};
        require(ROUTINE(Player_CopyWorldPosition, 0x40b0d0)(&pt, it) == &pt, "position output");
        unsigned flags = field<unsigned>(it, 96);
        require(!memcmp(&pt, it + ((flags & 1) ? 624 : 312), sizeof(pt)), "position source");
        // Compare each position to its live source fields inside its own host.
        ++drivers;
    }
    require(drivers > 1, "campaign roster");
    fprintf(report, "total drivers %d\n", drivers);
    checkNames(0x68cf00, false);
    checkNames(0x68d0d8, true);
    char missing[] = "AUDIT_NO_SUCH_ROOM";
    int id = 0;
    require(ROUTINE(Room_FindIdByName, 0x4364f0)(&id, missing) == &id && id == -1, "missing room");
    BYTE** begin = field<BYTE**>(global(0x68cf38), 0);
    BYTE** end = field<BYTE**>(global(0x68cf3c), 0);
    require(end > begin, "loaded roads");
    for (BYTE** it = begin; it < end; ++it) {
        BYTE* edge = *it;
        RoadPosition start{}, finish{};
        require(ROUTINE(Road_EdgeStart, 0x47fcf0)(&start, edge) == &start &&
                ROUTINE(Road_EdgeEnd, 0x47fe20)(&finish, edge) == &finish, "edge outputs");
        require(start.node == field<int>(field<BYTE*>(edge, 112), 16) &&
                finish.node == field<int>(field<BYTE*>(edge, 116), 16), "edge endpoints");
        if ((it - begin) % 32 == 0) {
            for (int side = 0; side < 2; ++side) {
                double cost = -1.0;
                ROUTINE(Road_NodeCost, 0x485680)(side, start.node, finish.node, &cost);
                require(cost >= 0.0, "route cost");
                fprintf(report, "cost %d %d %.17g\n", start.road, side, cost);
            }
        }
        for (auto* p : {&start, &finish}) {
            require(p->road == field<int>(edge, 12), "edge id");
            fprintf(report, "edge %d %d %.17g %d %d %d %d\n", p->road, p->node, p->t,
                    p->segment, p->object, p->flags, p->last);
        }
    }
    fprintf(report, "total edges %d\n", int(end - begin));
}
}
