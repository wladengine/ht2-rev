// Original WinMain's initializer table contains this contiguous terrain/marine
// slice. Dispatch through the two actual tables, including their slot order.
extern "C" void __cdecl Grid725d28_Destroy();
namespace grid_startup {
using Initializer=void (__cdecl*)();
static int registrations;
static Initializer lastHandler;
static int __cdecl RecordExit(Initializer handler) { ++registrations; lastHandler=handler; return 0; }
bool Check() {
    constexpr DWORD firstSlot=0x66c2e0, endSlot=0x66c3e0;
    constexpr DWORD firstState=0x724000, endState=0x728900;
    constexpr size_t stateSize=endState-firstState;
    auto ref=(BYTE*)firstState;
    auto got=KingBss+(firstState-0x68a820);
    Redirect((void*)0x61cd79,(void*)RecordExit); Redirect((void*)atexit,(void*)RecordExit);
    int snapshots=0, failures=0;
    for (BYTE pattern : {0,0x5a}) {
        memset(ref,pattern,stateSize); memset(got,pattern,stateSize);
        for (DWORD slot=firstSlot;slot<endSlot;slot+=4) {
            registrations=0;lastHandler=nullptr;
            F(Initializer,(BYTE*)slot,0)();
            int previous=registrations;
            bool refHandler=lastHandler==(Initializer)0x60c2a0;
            registrations=0;lastHandler=nullptr;
            F(Initializer,KingData+(slot-0x66c000),0)();
            bool expected=slot==0x66c32c;
            if (memcmp(ref,got,stateSize) || previous!=int(expected) || registrations!=int(expected) ||
                (expected && (!refHandler || lastHandler!=Grid725d28_Destroy))) {
                size_t first=0;
                while (first<stateSize && ref[first]==got[first]) ++first;
                if (++failures<=6) printf("startup slot=%06x seed=%02x first diff=%06x handlers=%d/%d\n",slot,(int)pattern,first<stateSize?firstState+first:0,previous,registrations);
                // Isolate the next slot from an earlier difference.
                memcpy(got,ref,stateSize);
            }
            ++snapshots;
        }
    }
    printf("grid startup slots=64 snapshots=%d mismatches=%d\n",snapshots,failures);
    return !failures;
}

// The 14 control presets store pointers to key states in their own image.
// Canonicalize only the documented primary bindings, leaving other writes
// and any out-of-range binding visible in the full BSS comparison.
static void RelocateExpectedKeyBindings(BYTE* snapshot) {
    constexpr DWORD bssBase = 0x68a820;
    constexpr DWORD keyStates = 0x6d1948;
    const DWORD rebuiltKeys = reinterpret_cast<DWORD>(KingBss) + keyStates - bssBase;
    for (int i = 0; i < 14; ++i) {
        const DWORD preset = i < 13 ? 0x6cf4d8 + i * 696 : 0x6d1db8;
        for (int action = 0; action < 47; ++action) {
            auto& binding = F(DWORD, snapshot, preset - bssBase + 264 + action * 4);
            if (binding - keyStates < 256) binding = rebuiltKeys + (binding - keyStates);
        }
    }
}

// Compare all BSS writes after each selected initializer.
bool CheckOtherSubsystems() {
    const DWORD slots[]={
        0x66c060,0x66c130,0x66c144,0x66c14c,0x66c154,0x66c160,0x66c168,
        0x66c1a4,0x66c1a8,0x66c1b0,0x66c1e0,0x66c1ec,0x66c1fc,0x66c208,
        0x66c21c,0x66c220,0x66c224,0x66c228,0x66c22c,0x66c230,0x66c234,
        0x66c258,0x66c260,0x66c264,0x66c268,0x66c26c,0x66c270,0x66c278,
        0x66c27c,0x66c298,0x66c29c,0x66c2d0,0x66c3e8,0x66c3f8,0x66c400,
        0x66c404,0x66c408,0x66c418,0x66c41c,0x66c428,0x66c438,
        // Effect defaults which read other globals, copy matrices or fill records.
        0x66c148,0x66c150,0x66c158,0x66c15c,0x66c164,0x66c16c,0x66c170,
        0x66c174,0x66c178,0x66c17c,
        // Vehicle tuning, static vehicle and scenery records, RNG and scenery transform.
        0x66c18c,0x66c190,0x66c19c,0x66c1b4,0x66c1b8,0x66c1c0,0x66c1cc,0x66c1c8
    };
    constexpr size_t bssSize=0xf52e4;
    BYTE* ref=(BYTE*)0x68a820;
    static BYTE backup[2][bssSize];
    memcpy(backup[0],ref,bssSize); memcpy(backup[1],KingBss,bssSize);
    int snapshots=0,errors=0;
    for(BYTE seed : {0,0x5a}) {
        memset(ref,seed,bssSize); memset(KingBss,seed,bssSize);
        for (int i=0;i<12;++i) gameplay::SetWorld(0x7025f0+4*i, float(i+seed)*.25f);
        for (int i=0;i<3;++i) gameplay::SetWorld(0x702650+4*i, float(i-seed)*.5f);
        for(DWORD slot : slots) {
            registrations=0;
            F(Initializer,(BYTE*)slot,0)();
            F(Initializer,KingData+(slot-0x66c000),0)();
            const BYTE* expected = ref;
            if (slot == 0x66c1c8) {
                static BYTE snapshot[bssSize];
                memcpy(snapshot, ref, bssSize);
                RelocateExpectedKeyBindings(snapshot);
                expected = snapshot;
            }
            if(registrations || memcmp(expected,KingBss,bssSize)) {
                if(++errors<=3) printf("global initializer slot=%06x seed=%02x registrations=%d\n",slot,seed,registrations);
                memcpy(KingBss,ref,bssSize);
            }
            ++snapshots;
        }
    }
    memcpy(ref,backup[0],bssSize); memcpy(KingBss,backup[1],bssSize);
    printf("global startup slots=%u snapshots=%d mismatches=%d\n",unsigned(sizeof slots/sizeof slots[0]),snapshots,errors);
    return !errors;
}
}
