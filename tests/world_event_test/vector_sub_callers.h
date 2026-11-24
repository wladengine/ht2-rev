extern "C" Matrix* Matrix_SubRows(Matrix*, const Matrix*, const Matrix*);
extern "C" int Trailer_HitchCheck(BYTE*, BYTE*);

namespace vector_sub_callers {
static void check() {
    for (int destination : {0, 1, 2}) {
        Pose a, b, result;
        for (int i = 0; i < 12; ++i) {
            a.m[i] = float(i + 1);
            b.m[i] = float(2 * i - 3);
        }
        Pose* outputs[] = {&result, &a, &b};
        auto* out = reinterpret_cast<Matrix*>(outputs[destination]);
        require(ROUTINE(Matrix_SubRows, 0x4e32e5)(out, reinterpret_cast<const Matrix*>(&a),
                                                  reinterpret_cast<const Matrix*>(&b)) == out, "matrix difference return");
        for (int i = 0; i < 12; ++i)
            require(outputs[destination]->m[i] == (i < 9 ? float(4 - i) : 0.f), "basis difference and zero translation");
        fprintf(report, "matrix difference destination=%d ok\n", destination);
    }

    // Private vector-pass process. Put each model's hitch at its local origin.
    static BYTE tractor[22000], trailer[22000];
    const Pose identity = {{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
    field<void**>(tractor, 0) = approach::vehicleTable(0x64f96c);
    field<void**>(trailer, 0) = approach::vehicleTable(0x64fb8c);
    field<int>(trailer, 10420) = 41;
    BYTE* types = global(0x697f9c);
    memset(types + 156, 0, 3 * sizeof(float));
    field<float>(types, 204) = 0.f;
    memset(types + 41 * 2416 + 168, 0, 3 * sizeof(float));
    for (auto* body : {tractor, trailer}) {
        field<BYTE*>(body, 21600) = body;
        field<Pose>(body, 516) = field<Pose>(body, 708) = identity;
    }
    struct HitchCase { float x, y; int expected; };
    const HitchCase cases[] = {{0,0,0}, {6,8,0}, {-6,-8,0}, {6,8.01f,5}, {-6,-8.01f,5}};
    for (const HitchCase& step : cases) {
        Pose& placement = field<Pose>(trailer, 516);
        placement.m[9] = step.x;
        placement.m[10] = step.y;
        int result = ROUTINE(Trailer_HitchCheck, 0x55f000)(tractor, trailer);
        fprintf(report, "hitch x=%.2f y=%.2f result=%d\n", step.x, step.y, result);
        require(result == step.expected, "hitch point distance");
    }
}
}
