extern "C" void __cdecl Player_ExpireLicences(BYTE* player);
extern "C" int __cdecl Player_LicenceTimeLeft(BYTE* player, double* remaining);

namespace licence {
bool Check() {
    // The mapped PE uses the host CRT for temporary sort storage. Both endpoints
    // keep their real sort and game clock. A non-human suppresses the HUD message.
    Redirect((void*)0x61d857, (void*)malloc);
    Redirect((void*)0x61d1c8, (void*)free);
    const double early = 223.620549539919;
    const double late = 521.9188245779601;
    struct Tick { double now; int count; double deadline; };
    const Tick ticks[] = {
        {early - 1.25, 3, early}, {early - 0.25, 3, early}, {early, 3, early},
        {early + 0.01, 2, late}, {late - 0.25, 2, late}, {late, 2, late},
        {late + 0.01, 0, 0}, {late + 2, 0, 0}
    };
    int errors = 0;
    for (bool original : {true, false}) {
        auto expire = original ? reinterpret_cast<decltype(&Player_ExpireLicences)>(0x477420) : Player_ExpireLicences;
        auto timeLeft = original ? reinterpret_cast<decltype(&Player_LicenceTimeLeft)>(0x477580) : Player_LicenceTimeLeft;
        BYTE player[160] = {};
        double deadlines[] = {late, early, late};
        F(double*, player, 144) = deadlines;
        F(double*, player, 148) = deadlines + 3;
        for (const Tick& tick : ticks) {
            gameplay::SetWorld(0x68a820, tick.now);
            expire(player);
            double remaining = -1;
            const int status = timeLeft(player, &remaining);
            const int count = int(F(double*, player, 148) - deadlines);
            const double expected = tick.count ? tick.deadline - tick.now : 0.;
            if (count != tick.count || remaining != expected || status != (tick.count ? 0 : -1)) {
                ++errors;
                printf("licence original=%d now=%.8f count=%d status=%d left=%.12g\n",
                       original, tick.now, count, status, remaining);
            }
        }
    }
    printf("licence expiry ticks per implementation=%u mismatches=%d\n", unsigned(sizeof ticks / sizeof *ticks), errors);
    return errors == 0;
}
}
