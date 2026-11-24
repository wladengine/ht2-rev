extern "C" int Game_PlayerMessage(int*);
extern "C" int Media_AiEvent(BYTE*, int, BYTE*);
extern "C" const char* Message_Text(const char*);
extern "C" int __fastcall Resources_Get(BYTE**, void*, int);
extern "C" int __fastcall Resources_AddRecord(BYTE*, void*, int, BYTE*, int);
extern "C" int __fastcall Resources_Delete(BYTE*, void*, int);
extern "C" Pose* __fastcall SceneObj_Matrix1(BYTE*, void*, Pose*);
extern "C" BYTE* Objects_Find(const char*);
extern "C" void __fastcall Sound_Restart(BYTE*, void*, float, float);
extern "C" void __fastcall Ai_PresentCheck(BYTE*);
extern "C" void __fastcall Trailer_OnHit(BYTE*, void*, int, const float*);
extern "C" void Messages_Add(const char*);
extern "C" void AI_AddToBanner(double);
extern "C" void eventBanner(int, char*);
extern "C" int __fastcall Queue_PostEvent2(BYTE*, void*, int);

namespace tanker_interactions {
static int targetId, hidden, removed, sounds, refused, lossBanners;
static char refusal[256];
static BYTE resource[36], inactive[36], object[64], cab[22000], world[1024];
static void* objectMethods[8], *carMethods[42];
static int __fastcall lookup(BYTE**, void*, int id) { return int(id == targetId && !removed ? resource : inactive); }
static int __fastcall add(BYTE*, void*, int, BYTE* rec, int) { require(rec == resource, "world record"); return 0; }
static int __fastcall erase(BYTE*, void*, int id) { require(id == targetId, "removed resource id"); ++removed; return 0; }
static void __fastcall hide(BYTE* p, void*, int arg) { require(p == object && !arg, "hide container"); ++hidden; }
static Pose* __fastcall nearby(BYTE*, void*, Pose* out) { memset(out, 0, sizeof(*out)); return out; }
static double __fastcall room(BYTE*, void*) { return 100.; }
static double __fastcall used(BYTE*, void*) { return 0.; }
static BYTE* sample(const char* name) { require(!strcmp(name, "orderSound"), "order sound"); return nullptr; }
static void __fastcall play(BYTE*, void*, float, float) { ++sounds; }
static void message(const char* s) { if (!strcmp(s, refusal)) ++refused; }
static void skipUpdate() {}
static void skipAmount(double) {}
static void lossBanner(int id, char*) { require(id == 7, "lost order banner"); ++lossBanners; }

static void checkReceipt(BYTE* human) {
    BYTE notice[80] = {};
    auto& currentNotice = field<BYTE*>(global(0x6f3430), 0);
    BYTE* previous = currentNotice;
    currentNotice = notice;
    alignas(double) BYTE delivery[64] = {};
    field<double>(delivery, 48) = 5944.;
    for (double integrity : {1., .75}) {
        field<int>(notice, 60) = 0;
        field<double>(delivery, 40) = integrity;
        require(ROUTINE(Media_AiEvent, 0x4d76b0)(human, 11, delivery) == 0 &&
                field<int>(notice, 60) == 5 && field<int>(notice, 52) == 0, "ordinary delivery receipt");
        const int percent = field<int>(notice, 56);
        require(percent == (integrity == 1. ? 0 : 25) &&
                field<int>(global(0x6f3480), 0) == 5944 && field<int>(global(0x6f3484), 0) == 0,
                "receipt shows damage percentage, fee and penalty");
        fprintf(report, "receipt integrity=%.2f displayed=%d fee=5944 penalty=0\n", integrity, percent);
    }
    currentNotice = previous;
}

static BYTE* effectSubject;
static int blowCount;
static int __fastcall recordEffect(BYTE* object, void*, int arg) {
    require(arg == 1 && !strcmp(field<char*>(object, 28), "BlowProcessTank") &&
            field<BYTE*>(object, 680) == effectSubject, "tank explosion scheduled for struck trailer");
    ++blowCount;
    return 1;
}

static void checkBlast(BYTE* human, bool loaded) {
    static BYTE rig[22000], tow[22000], state[32];
    auto* hold = field<BYTE*>(human, 520);
    auto* order = field<BYTE*>(hold, 28);
    const float addedMass = loaded ? float(1000. * field<double>(order, 56)) : 0.f;
    const double originalIntegrity = loaded ? field<double>(order, 96) : 0.;
    field<BYTE*>(global(0x696224), 0) = view;
    field<BYTE*>(global(0x6ef1fc), 0) = view;
    field<BYTE*>(global(0x6974c4), 0) = view;
    effectSubject = rig;
    approach::frameDouble(ROUTINE(Queue_PostEvent2, 0x5d33d0), recordEffect);
    struct Hit { int model, normalized; bool tipped, expect; float fixedImpulse = 0.f; };
    const Hit tests[] = {{41,7,false,false}, {41,8,false,true}, {41,11,false,true},
                         {41,4,true,false}, {41,5,true,true}, {41,7,true,true},
                         {42,11,false,false}, {42,36,false,false}, {41,0,false,!loaded,100000.f}};
    for (auto test : tests) {
        memset(rig, 0, sizeof(rig));
        memset(state, 0, sizeof(state));
        field<void**>(rig, 0) = approach::vehicleTable(0x64fb8c);
        field<void**>(rig, 16) = approach::vehicleTable(0x64faa0);
        field<BYTE*>(rig, 21880) = tow;
        field<BYTE*>(tow, 20636) = human;
        field<int>(rig, 10420) = test.model;
        require(field<int>(global(0x6981b0), test.model * 2416) == (test.model == 41 ? 3 : 2), "vehicle.tech trailer type");
        field<BYTE*>(rig, 21568 + 20) = state;
        field<BYTE*>(rig, 21568 + 24) = rig;
        field<int>(rig, 20900) = 328 | 128; // constructor flags and active vehicle damage bit
        field<int>(rig, 908) = 1;           // static contact
        field<float>(rig, 21152) = 1.f;
        field<float>(rig, 412) = 10000.f + addedMass;
        Pose pose = {{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
        if (test.tipped) { pose.m[4] = pose.m[8] = 0; pose.m[5] = 1; pose.m[7] = -1; }
        field<Pose>(rig, 516) = pose;
        ROUTINE(approach::Matrix_Inverse, 0x5ac2e0)(&field<Pose>(rig, 612), &pose);
        const float mass = field<float>(rig, 412);
        const float impulse[] = {test.fixedImpulse ? test.fixedImpulse : test.normalized * mass, 0, 0};
        blowCount = 0;
        using HitFn = void (__fastcall*)(BYTE*, void*, const float*, const float*);
        auto hit = reinterpret_cast<HitFn>(field<void**>(rig, 16)[41]);
        hit(rig + 16, nullptr, pose.m + 6, impulse);
        fprintf(report, "blast loaded=%d type=%d mass=%.1f impulse=%.1f normalized=%.3f tipped=%d scheduled=%d\n",
                loaded, test.model, mass, impulse[0], impulse[0] / mass, test.tipped, blowCount);
        require(blowCount == int(test.expect), "body hit tank explosion");
        // Each controlled impact starts from the same cargo condition.
        if (loaded) {
            require(field<BYTE*>(hold, 32) == order + 144, "cargo survived controlled hit");
            field<double>(order, 96) = originalIntegrity;
        }
    }
}

static void container(BYTE* human, bool tanker) {
    BYTE* head = field<BYTE*>(global(0x68cf6c), 0);
    BYTE* target = nullptr;
    for (BYTE* p = field<BYTE*>(head, 0); p != head; p = field<BYTE*>(p, 0)) {
        BYTE* item = field<BYTE*>(p, 8);
        if ((field<int>(item, 8) & 0x60) == 0x40 && field<BYTE*>(item, 88)) { target = p; break; }
    }
    require(target != nullptr, "world placed a loose cargo container");
    BYTE* goods = field<BYTE*>(global(0x68ccb8), 0);
    BYTE* end = field<BYTE*>(global(0x68ccbc), 0);
    int champagne = -1;
    for (BYTE* g = goods; g < end; g += 104)
        if (!strcmp(field<char*>(g, 88), "\xd8\xe0\xec\xef\xe0\xed\xf1\xea\xee\xe5")) champagne = int(g-goods)/104;
    require(champagne >= 0, "campaign champagne merchandise");
    BYTE* item = field<BYTE*>(target, 8);
    field<int>(field<BYTE*>(item, 88), 0) = champagne;
    // The headless host has no renderer to assign a unique world resource ID.
    targetId = 200001;
    for (BYTE* p = field<BYTE*>(head, 0); p != head; p = field<BYTE*>(p, 0))
        require(field<int>(field<BYTE*>(p, 8), 0) != targetId, "fixture resource id available");
    field<int>(item, 0) = targetId;
    field<int>(resource, 4) = targetId;
    field<BYTE*>(resource, 28) = object;
    objectMethods[7] = reinterpret_cast<void*>(hide);
    field<void**>(object, 0) = objectMethods;
    carMethods[34] = carMethods[35] = reinterpret_cast<void*>(room);
    carMethods[37] = carMethods[41] = reinterpret_cast<void*>(used);
    field<void**>(cab, 0) = carMethods;
    field<BYTE*>(cab, 20636) = human;
    field<BYTE*>(world, 616) = cab;
    field<BYTE*>(global(0x6d2078), 0) = world;
    strcpy(refusal, ROUTINE(Message_Text, 0x40e270)("LOAD_AT_AN_OIL_TANK"));
    approach::frameDouble(ROUTINE(Resources_Get, 0x5e5370), lookup);
    approach::frameDouble(ROUTINE(Resources_AddRecord, 0x5e54c0), add);
    approach::frameDouble(ROUTINE(Resources_Delete, 0x5e5210), erase);
    approach::frameDouble(ROUTINE(SceneObj_Matrix1, 0x57de90), nearby);
    approach::frameDouble(ROUTINE(Objects_Find, 0x5ef3d0), sample);
    approach::frameDouble(ROUTINE(Sound_Restart, 0x5fdaf0), play);
    approach::frameDouble(ROUTINE(Copter_ApplyCargo, 0x56f6d0), skipUpdate);
    approach::frameDouble(ROUTINE(displayScreenMessage, 0x483cb0), message);
    BYTE* hold = field<BYTE*>(human, tanker ? 520 : 516);
    BYTE* start = field<BYTE*>(hold, 28);
    BYTE saved[144];
    if (tanker) {
        require(start && field<BYTE*>(hold, 32) == start + 144 && field<int>(start, 0) == 35, "fuel order on approach");
        memcpy(saved, start, sizeof(saved));
    } else require(start == field<BYTE*>(hold, 32), "ordinary truck starts empty");
    const double cash = field<double>(human, 136);
    field<int>(human, 536) = 9;
    int m[4] = {20};
    field<int>(global(0x6f33f4), 0) = 1;
    require(ROUTINE(Game_PlayerMessage, 0x56ffd0)(m) == 1, "pickup handled");
    require(sounds == 1 && hidden == 1 && removed == 1 && refused == int(tanker), "world pickup and refusal");
    BYTE* queue = field<BYTE*>(global(0x68b99c), 0);
    BYTE* task = field<BYTE*>(field<BYTE*>(queue, 0), 8);
    require(field<BYTE*>(task, 48) == human && field<BYTE*>(task, 52) == target, "queued this container");
    ROUTINE(Ai_PresentCheck, 0x41f940)(task);
    for (BYTE* p = field<BYTE*>(head, 0); p != head; p = field<BYTE*>(p, 0))
        require(field<int>(field<BYTE*>(p, 8), 0) != targetId, "present removed from campaign");
    start = field<BYTE*>(hold, 28);
    require(start && field<BYTE*>(hold, 32) == start + 144 && field<double>(human, 136) == cash, "one order, balance unchanged");
    require(tanker ? !memcmp(start, saved, sizeof(saved)) : field<int>(start, 0) == champagne, "retained fuel or acquired champagne");
    require(field<int>(human, 536) == 0, "container attempt resets the campaign race streak");
    fprintf(report, "container tanker=%d refused=%d sound=%d hidden=%d removed=%d campaign=gone orders=1 goods=%d streak=9->%d\n",
            tanker, refused, sounds, hidden, removed, field<int>(start, 0), field<int>(human, 536));
}

static void damage(BYTE* human, bool loaded) {
    checkBlast(human, loaded);
    static BYTE trailerBody[22000], tractor[22000];
    field<BYTE*>(trailerBody, 21880) = tractor;
    field<BYTE*>(tractor, 20636) = human;
    field<float>(trailerBody, 412) = 10000.f;
    BYTE* hold = field<BYTE*>(human, 520);
    const double initialCash = field<double>(human, 136);
    approach::frameDouble(ROUTINE(Messages_Add, 0x56ae60), message);
    approach::frameDouble(ROUTINE(AI_AddToBanner, 0x483e60), skipAmount);
    approach::frameDouble(ROUTINE(eventBanner, 0x483e80), lossBanner);
    for (int severity : {2, 10, 1000}) {
        const float impulse[] = {10000.f * severity, 0, 0};
        ROUTINE(Trailer_OnHit, 0x55ef90)(trailerBody, nullptr, 0, impulse);
        BYTE* order = field<BYTE*>(hold, 28);
        bool hasOrder = order != field<BYTE*>(hold, 32);
        double integrity = hasOrder ? field<double>(order, 96) : 0.;
        fprintf(report, "impact loaded=%d normalized=%d orders=%d integrity=%.6f money_delta=%.6f loss_banners=%d\n",
                loaded, severity, hasOrder, integrity, field<double>(human, 136)-initialCash, lossBanners);
        require(field<BYTE*>(human, 520) == hold && field<BYTE*>(trailerBody, 21880) == tractor, "rig still attached");
        if (loaded && severity < 1000)
            require(hasOrder && (severity == 2 ? integrity == 1. : integrity > 0 && integrity < 1.), "fuel impact integrity");
        else require(!hasOrder, "no surviving cargo");
    }
    require(loaded ? lossBanners == 1 && field<double>(human, 136) < initialCash :
                     lossBanners == 0 && field<double>(human, 136) == initialCash, "cargo loss charge");
}
}
