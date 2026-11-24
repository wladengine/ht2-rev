struct Matrix;
extern "C" Vec3* Vec3_Scaled(Vec3*, const Vec3*, float);
extern "C" Matrix* Matrix_ScaleRows(Matrix*, const Matrix*, float);

namespace vector_scale {
static void check() {
    // Binary32 inputs keep signed zero, NaN payloads and halfway products reproducible.
    const DWORD inputs[][4] = {
        {0x3f800000, 0xc0000000, 0x3f000000, 0xc0000000},
        {0x80000000, 0x00000000, 0xbf800000, 0x80000000},
        {0x3f800001, 0xbf800001, 0x3f800003, 0x3fc00000},
        {0x00800000, 0x00000001, 0x80000001, 0x3f000000},
        {0x7f7fffff, 0xff7fffff, 0x00000001, 0x40000000},
        {0x7f800000, 0xff800000, 0x00000000, 0x00000000},
        {0x7fc01111, 0xffc02222, 0x3f800000, 0x7fc03333},
        {0x7f800123, 0x3f800000, 0x80000000, 0x40000000}
    };
    const unsigned saved = _controlfp(0, 0);
    for (unsigned rounding : {_RC_NEAR, _RC_UP, _RC_DOWN, _RC_CHOP}) {
        _controlfp(rounding | _PC_53 | _MCW_EM, _MCW_RC | _MCW_PC | _MCW_EM);
        for (const auto& input : inputs) {
            for (int outIndex : {1, 2, 3, 6}) {
                DWORD words[10], before[10];
                for (auto& word : words) word = 0xdeadbeef;
                memcpy(words + 2, input, 3 * sizeof(DWORD));
                memcpy(before, words, sizeof(words));
                float factor;
                memcpy(&factor, input + 3, sizeof(factor));
                auto* out = reinterpret_cast<Vec3*>(words + outIndex);
                _clearfp();
                auto* returned = ROUTINE(Vec3_Scaled, 0x4bdd60)(out, reinterpret_cast<Vec3*>(words + 2), factor);
                const unsigned flags = _statusfp() & _MCW_EM;
                require(returned == out, "vector returns output address");
                for (int i = 0; i < 10; ++i)
                    if (i < outIndex || i >= outIndex + 3) require(words[i] == before[i], "vector write extent");
                fprintf(report, "rc=%x out=%d vector=%08lx %08lx %08lx flags=%x\n",
                        rounding, outIndex, words[outIndex], words[outIndex+1], words[outIndex+2], flags);
            }
        }
    }
    _clearfp();
    _controlfp(saved, _MCW_RC | _MCW_PC | _MCW_EM);
    for (bool inPlace : {false, true}) {
        float source[12], result[12] = {};
        for (int i = 0; i < 12; ++i) source[i] = float(i + 1);
        auto* output = inPlace ? source : result;
        auto* out = reinterpret_cast<Matrix*>(output);
        require(ROUTINE(Matrix_ScaleRows, 0x4e33a0)(out, reinterpret_cast<Matrix*>(source), -2.f) == out,
                "matrix returns output address");
        for (int i = 0; i < 12; ++i) {
            require(output[i] == (i < 9 ? -2.f * (i + 1) : 0.f), "basis scaled; translation cleared");
            if (!inPlace) require(source[i] == float(i + 1), "matrix input unchanged");
        }
        fprintf(report, "matrix in_place=%d rows=scaled translation=zero\n", inPlace);
    }
}
}
