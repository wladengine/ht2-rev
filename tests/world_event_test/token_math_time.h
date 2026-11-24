extern "C" char* NextToken(char**);
extern "C" void Color_Parse(float*, char*);
extern "C" Vec3* Vec3_Cross(Vec3*, const Vec3*, const Vec3*);
extern "C" Vec3* __fastcall Vec3_MulAssign(Vec3*, void*, Vec3*, float);
extern "C" double Game_FrameTime();
extern "C" double WorldVec3_LengthSquared(const Vec3* v);
extern "C" int Sch_GetUnaryArgument(BYTE*, Value*, BYTE**, int);
extern "C" int Sch_Message2(BYTE*, int, int, int);
extern "C" Vec3* Vec3_Sum(Vec3*, const Vec3*, const Vec3*);

namespace token_math_time {
static void tokens() {
    auto next = ROUTINE(NextToken, 0x5ab1b0);
    for (int byte = 0; byte < 256; ++byte) {
        char buf[] = " \tAxB", *cursor = buf;
        buf[3] = char(byte);
        bool separator = byte == ' ' || byte == '\t';
        char* word = next(&cursor);
        require(word == buf + 2 && cursor == (separator ? buf + 4 : nullptr), "token and cursor offsets");
        require(buf[3] == (byte <= 32 ? 0 : char(byte)) && buf[4] == 'B' && buf[5] == 0,
                "token delimiter and trailing bytes");
        word = next(&cursor);
        require(word == (separator ? buf + 4 : nullptr) && !cursor && !next(&cursor), "second word and exhaustion");
    }
    char empty[] = " \t\rX", *cursor = empty;
    require(!next(&cursor) && !cursor && empty[2] == '\r', "control byte at start of word");
    char spaced[] = "A  \tB \nC";
    cursor = spaced;
    require(next(&cursor) == spaced && cursor == spaced + 2, "cursor before repeated separators");
    require(next(&cursor) == spaced + 4 && !cursor && !strcmp(spaced + 4, "B") && spaced[6] == '\n',
            "control byte after a separator");
    char rgb[] = "\t255   0\t128\r\n";
    float color[3];
    ROUTINE(Color_Parse, 0x5a88f0)(color, rgb);
    DWORD bits[3];
    memcpy(bits, color, sizeof(bits));
    require(color[0] == 1 && color[1] == 0 && bits[2] == 0x3f008081, "RGB uses three words");
    fprintf(report, "tokens: 256 interior bytes, repeated separators, empty word, RGB=%08lx %08lx %08lx\n",
            bits[0], bits[1], bits[2]);
    char unbounded[] = "-255 510 25tail";
    ROUTINE(Color_Parse, 0x5a88f0)(color, unbounded);
    require(color[0] == -1 && color[1] == 2 && color[2] == float(25.0/255.0), "RGB decimal prefix and range");
    memcpy(bits, color, sizeof(bits));
    fprintf(report, "RGB outside nominal range=%08lx %08lx %08lx\n", bits[0],bits[1],bits[2]);
}

static void scaleInPlace() {
    const float cases[][4] = {{1,-2,4,-.5f}, {-0.f,0.f,2.f,-0.f},
                              {1.00000011920928955078125f,-3.25f,.03125f,1.5f}};
    for (const auto& v : cases) {
        for (bool alias : {false, true}) {
            struct Guarded { DWORD left; float xyz[3]; DWORD right; };
            Guarded self = {11, {v[0],v[1],v[2]}, 13}, separate = {17, {}, 19};
            auto* out = reinterpret_cast<Vec3*>(alias ? self.xyz : separate.xyz);
            require(ROUTINE(Vec3_MulAssign, 0x5ab620)(reinterpret_cast<Vec3*>(self.xyz), nullptr, out, v[3]) == out,
                    "in-place scale returns caller result slot");
            for (int i = 0; i < 3; ++i) {
                float expected = float(double(v[i]) * v[3]);
                require(!memcmp(&expected, &self.xyz[i], sizeof(float)), "in-place scale changes receiver");
            }
            require(!memcmp(out, self.xyz, sizeof(self.xyz)) && self.left == 11 && self.right == 13 &&
                    separate.left == 17 && separate.right == 19, "scale copy and write bounds");
            DWORD bits[3];
            memcpy(bits, self.xyz, sizeof(bits));
            fprintf(report, "scale case=%d alias=%d result=%08lx %08lx %08lx\n",
                    int(&v - cases), alias, bits[0], bits[1], bits[2]);
        }
    }
}

static void frameTimeAtPrecision(unsigned precision, unsigned rounding) {
    BYTE clock[2968] = {};
    auto& ms = field<short>(global(0x6974c2), 0);
    auto& clockPtr = field<BYTE*>(global(0x6974c4), 0);
    short savedMs = ms;
    BYTE* savedClock = clockPtr;
    struct Case { short overrideMs; double measured; };
    const Case cases[] = {{0,0}, {0,-.03125}, {0,.0625}, {0,.24999999999999997},
                          {0,.25}, {0,.25000000000000006}, {0,4}, {0,1e-80},
                          {1,4}, {250,4}, {251,4}, {32767,4}, {-500,4}, {0,-0.0}, {-32768,4}};
    decltype(&Game_FrameTime) frame = ROUTINE(Game_FrameTime, 0x4e2749);
    _controlfp(precision | rounding | _MCW_EM, _MCW_PC | _MCW_RC | _MCW_EM);
    for (const auto& c : cases) {
        ms = c.overrideMs;
        clockPtr = ms ? nullptr : clock;
        field<double>(clock, 2960) = c.measured;
        _clearfp();
        BYTE extended[10];
        // Capture the return register before a C++ assignment can round it to binary64.
        __asm {
            call frame
            fstp tbyte ptr extended
        }
        unsigned flags = _statusfp() & _MCW_EM;
        double seconds;
        __asm {
            fld tbyte ptr extended
            fstp qword ptr seconds
        }
        double expected = ms ? double(ms) / 1000.0 : (c.measured > .25 ? .25 : c.measured);
        require(ms ? fabs(seconds - expected) < 1e-14 : seconds == expected, "frame time in seconds");
        fprintf(report, "frame pc=%x rc=%x case=%d ms=%d result=", precision, rounding, int(&c - cases), ms);
        for (int i = 9; i >= 0; --i) fprintf(report, "%02x", extended[i]);
        fprintf(report, " flags=%x\n", flags);
    }
    _clearfp();
    clockPtr = savedClock;
    ms = savedMs;
}

static void frameTime() {
    unsigned saved = _controlfp(0, 0);
    for (unsigned precision : {_PC_53, _PC_64})
        for (unsigned rounding : {_RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP})
            frameTimeAtPrecision(precision, rounding);
    _controlfp(saved, _MCW_PC | _MCW_RC | _MCW_EM);
}

static void unaryArgument() {
    BYTE* interp = global(0x68bb58);
    for (int count : {0,1,2,3}) {
        for (bool borrowed : {false, true}) {
            DWORD oldObject[] = {0,2}, newObject[] = {0,3};
            Value out = {reinterpret_cast<BYTE*>(oldObject), 0, 9};
            Value slots[3] = {{nullptr, 65, 0x10004}, {reinterpret_cast<BYTE*>(newObject), 21,
                                borrowed ? 0x10009u : 9u}, {}};
            BYTE* args[] = {reinterpret_cast<BYTE*>(slots), reinterpret_cast<BYTE*>(slots + count),
                            reinterpret_cast<BYTE*>(slots + 3)};
            field<int>(interp,120) = 0;
            int status = ROUTINE(Sch_GetUnaryArgument, 0x491c00)(interp, &out, args, int("test-unary"));
            bool ok = count == 2;
            require(status == (ok ? 0 : 1) && field<int>(interp,120) == status, "Scheme argument count");
            require(out.object == (ok ? slots[1].object : reinterpret_cast<BYTE*>(oldObject)) &&
                    out.tag == (ok ? slots[1].tag : 9u) && out.aux == (ok && borrowed ? 21 : 0) &&
                    oldObject[1] == (ok ? 1u : 2u) && newObject[1] == (ok && !borrowed ? 4u : 3u),
                    "Scheme operand assignment and ownership");
            fprintf(report,"unary slots=%d borrowed=%d status=%d refs=%lu,%lu\n",
                    count, borrowed, status, oldObject[1], newObject[1]);
        }
    }
}

struct CapturePort {
    void** table;
    int refs;
    char text[256];
    unsigned count;
};

static void __fastcall captureChar(CapturePort* port, void*, int ch) {
    require(port->count + 1 < sizeof(port->text), "Scheme diagnostic length");
    port->text[port->count++] = char(ch);
}

static void schemeErrors() {
    void* methods[8] = {};
    methods[7] = reinterpret_cast<void*>(&captureChar);
    struct Case { int code; const char* arg1; const char* arg2; const char* message; };
    const Case cases[] = {
        {2,"string","symbol","**** [002] Illegal Reference Conversion string to symbol\n"},
        {15,"number","-","**** [015] Non-number argument to -\n"},
        {20,"a","b","**** [020] Syntax error a b\n"}
    };
    for (const Case& c : cases) {
        BYTE interpreter[128] = {};
        CapturePort port = {methods, 1};
        field<Value>(interpreter, 96) = {reinterpret_cast<BYTE*>(&port), 0, 11};
        ROUTINE(Sch_Message2, 0x4a5b50)(interpreter, c.code, int(c.arg1), int(c.arg2));
        char expected[256];
        sprintf(expected, "%s\r\nSTACK TRACE:%s", c.message, c.message);
        require(!strcmp(port.text, expected) && port.count == strlen(expected) && port.refs == 1 &&
                field<int>(interpreter, 120) == 1, "formatted messages, empty trace, port ownership and error flag");
        fprintf(report, "error %d bytes=%u refs=%d text=%s", c.code, port.count, port.refs, c.message);
    }
}

static void worldLengthSquared() {
    auto squareLengthEntry = ROUTINE(WorldVec3_LengthSquared, 0x4e298b);
    const float inputs[][3] = {{3,4,12}, {4096,1,0}, {1.20892582e24f,0,0}, {8.07793567e-28f,0,0}};
    unsigned saved = _controlfp(0, 0);
    _controlfp(_PC_53 | _RC_NEAR | _MCW_EM, _MCW_PC | _MCW_RC | _MCW_EM);
    for (const auto& input : inputs) {
        const float* v = input;
        BYTE extended[10];
        _clearfp();
        __asm {
            push v
            call squareLengthEntry
            add esp, 4
            fstp tbyte ptr extended
        }
        unsigned flags = _statusfp() & _MCW_EM;
        fprintf(report, "squared length case=%d result=", int(&input - inputs));
        for (int i = 9; i >= 0; --i) fprintf(report, "%02x", extended[i]);
        fprintf(report, " flags=%x\n", flags);
    }
    _clearfp();
    _controlfp(saved, _MCW_PC | _MCW_RC | _MCW_EM);
}

static void check() {
    worldLengthSquared();
    tokens();
    vector_binary::checkOperation("cross", ROUTINE(Vec3_Cross, 0x4bde50));
    scaleInPlace();
    frameTime();
    unaryArgument();
    schemeErrors();
    vector_binary::checkOperation("effects sum", ROUTINE(Vec3_Sum, 0x4bfef0));
}
}
