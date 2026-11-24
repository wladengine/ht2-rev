extern "C" void Ai_TakeOrder(int, const BYTE*);
extern "C" int Player_SwapVehicles(BYTE*, BYTE*, int);
extern "C" int Player_Hire(BYTE*, BYTE*);
extern "C" void Player_GrantLicences(BYTE*, double, int);
extern "C" int Ai_LoadPresent(BYTE*, BYTE*, int*);
extern "C" BYTE** Player_RandomTradePartner2(BYTE**, BYTE*, BYTE*, BYTE*, DWORD*);
extern "C" int Ai_GetPlayerOrderInfo(BYTE*, unsigned, BYTE*);
extern "C" BYTE* __fastcall Fx_InitState(BYTE*, void*);
extern "C" void Car_ApplyPlayerSettings(BYTE*);

namespace fleet_review {
static void checkOrderDeadline(BYTE* human) {
    BYTE order[144] = {}, info[192] = {};
    BYTE* hold = field<BYTE*>(human, 520);
    if (!hold || field<int>(hold, 4) == -1)
        hold = field<BYTE*>(human, 516);
    require(hold && field<int>(hold, 4) >= 0, "active vehicle for order deadline");
    field<int>(order, 4) = field<int>(order, 8) = -1;
    field<int>(order, 12) = 1;
    field<double>(order, 56) = 1.;
    field<unsigned long long>(order, 72) = 0x7ff8000000000051ull;
    field<BYTE*>(hold, 28) = order;
    field<BYTE*>(hold, 32) = field<BYTE*>(hold, 36) = order + sizeof order;
    ROUTINE(Fx_InitState, 0x4d06a0)(info, nullptr);
    int result = ROUTINE(Ai_GetPlayerOrderInfo, 0x442350)(human, 0, info);
    unsigned short status;
    __asm fnstsw status
    require(result == 0 && info[184] == 1 && field<unsigned long long>(info, 168) == 0 &&
            (status & 0x4500) == 0x4500, "unordered remaining time becomes positive zero");
    fprintf(report, "order deadline valid=%u remaining=0 relation=%04x\n", info[184], status & 0x4500);
}

static void checkVehicleSettingsRounding() {
    BYTE car[20640] = {};
    field<BYTE*>(car, 20636) = field<BYTE*>(global(0x68b990), 0);
    double value = 1.00000009;
    float rounded;
    unsigned short before, after;
    __asm {
        fld value
        fstp rounded
        fnstsw before
    }
    ROUTINE(Car_ApplyPlayerSettings, 0x520000)(car);
    __asm fnstsw after
    require((before & 0x200) && before == after, "no-driver settings preserve incoming x87 state");
    fprintf(report, "vehicle settings incoming=%04x outgoing=%04x\n", before, after);
}

static int count(BYTE* p, int off, int stride) {
    return int(field<BYTE*>(p, off + 4) - field<BYTE*>(p, off)) / stride;
}
static void hiring(BYTE* human) {
    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    BYTE* ai[3] = {};
    int n = 0;
    for (BYTE* p = field<BYTE*>(head, 0); p != head && n < 3; p = field<BYTE*>(p, 0))
        if (field<int>(p, 92) == 2 && field<BYTE*>(p, 176) == head && !count(p, 160, 16) && field<BYTE*>(p, 516))
            ai[n++] = p;
    require(n == 3, "three independent AI drivers");
    BYTE* boss = ai[0];
    BYTE* driver = ai[1];
    for (BYTE* p : {human, boss}) {
        field<double>(p, 136) = 10000000.;
        ROUTINE(Player_GrantLicences, 0x4774e0)(p, 300., 2);
    }
    require(ROUTINE(Player_Hire, 0x476bc0)(human, driver) == 0, "employee joins human");
    require(ROUTINE(Player_Hire, 0x476bc0)(boss, ai[2]) == 0, "rival forms firm");
    double money = field<double>(boss, 136);
    int licences = count(boss, 144, 8);
    int result = ROUTINE(Player_Hire, 0x476bc0)(boss, driver);
    require(result == -4 && field<BYTE*>(driver, 176) == human && count(boss, 160, 16) == 1 &&
                count(human, 160, 16) == 1 && licences == count(boss, 144, 8) && money == field<double>(boss, 136),
            "rival cannot take contracted employee");
    fprintf(report, "hire free=0 employed=%d original_owner=1 rival_staff=1 fee=0 licences_spent=0\n", result);

    // Limit the real selector to one uninstantiated road vehicle outside the human fleet.
    BYTE* rival = boss;
    field<int>(rival, 84) = -1;
    field<int>(rival, 96) &= ~1;
    rival[496] = 0;
    BYTE* end = field<BYTE*>(rival, 0);
    DWORD* rng = reinterpret_cast<DWORD*>(global(0x68b9c8));
    BYTE* ownHold = field<BYTE*>(human, 516);
    BYTE* otherHold = field<BYTE*>(rival, 516);
    auto roster = reinterpret_cast<BYTE***>(global(0x68a980));
    // One passenger-car competitor (ai[2]) to exercise the mixed-field branch.
    BYTE* row[1] = {ai[2]};
    field<int>(field<BYTE*>(ai[2], 516), 4) = 8;
    for (int actor : {8, 12, 92})
        for (int opponent : {92, 140})
            for (int occupied : {0, 1}) {
                field<int>(ownHold, 4) = actor;
                field<int>(otherHold, 4) = opponent;
                field<int>(rival, 100) = 0;
                roster[0] = row;
                roster[1] = row + occupied;
                BYTE* picked = nullptr;
                ROUTINE(Player_RandomTradePartner2, 0x45e520)(&picked, rival, end, human, rng);
                fprintf(report, "rival player_model=%d candidate_model=%d light_in_field=%d selected=%d\n", actor,
                        opponent, occupied, picked == rival);
                require(picked == ((opponent == 92 && (actor == 92 || occupied)) ? rival : end),
                        "vehicle eligibility");
            }
    field<int>(rival, 100) = 5;
    BYTE* picked;
    ROUTINE(Player_RandomTradePartner2, 0x45e520)(&picked, rival, end, human, rng);
    require(picked == end, "Ring driver excluded");
}
static void salary(BYTE* human) {
    approach::frameDouble(ROUTINE(Copter_ApplyCargo, 0x56f6d0), fuel_freight::skipCabUpdate);
    approach::frameDouble(ROUTINE(displayScreenMessage, 0x483cb0), fuel_freight::skipBanner);
    approach::frameDouble(ROUTINE(Messages_Add, 0x56ae60), fuel_freight::skipBanner);
    BYTE* own = field<BYTE*>(human, 516);
    field<int>(own, 0) = field<int>(own, 4) = 8;
    BYTE* head = field<BYTE*>(global(0x68b990), 0);
    BYTE* driver = field<BYTE*>(head, 0);
    for (; driver != head; driver = field<BYTE*>(driver, 0)) {
        BYTE* hold = field<BYTE*>(driver, 516);
        if (field<int>(driver, 92) == 2 && field<BYTE*>(driver, 176) == head && !count(driver, 160, 16) && hold &&
            !count(hold, 28, 144) && !field<BYTE*>(driver, 520))
            break;
    }
    require(driver != head, "independent driver with empty vehicle");
    field<double>(human, 136) = 10000000.;
    ROUTINE(Player_GrantLicences, 0x4774e0)(human, 300., 1);
    require(ROUTINE(Player_Hire, 0x476bc0)(human, driver) == 0, "employee for garage exchange");
    BYTE* truck = field<BYTE*>(driver, 516);
    field<int>(truck, 0) = field<int>(truck, 4) = 92;
    field<double>(global(0x68a820), 0) += 60.;
    ROUTINE(Task_FormOrders, 0x41b720)();
    BYTE** towns = field<BYTE**>(global(0x68cec0), 0);
    BYTE** end = field<BYTE**>(global(0x68cec4), 0);
    fuel_freight::Gate gate;
    // Use the generated light-car salary offer and its own destination.
    BYTE* chosen = nullptr;
    for (BYTE** t = towns; t != end; ++t) {
        BYTE* first = field<BYTE*>(*t, 28);
        for (BYTE* g = first; g != field<BYTE*>(*t, 32); g += 16) {
            BYTE* offer = field<BYTE*>(g, 12);
            if (!offer || (g[4] & 1) || field<int>(offer, 0) != 68)
                continue;
            chosen = offer;
            gate = {field<int>(*t, 24), int(g - first) / 16};
        }
    }
    require(chosen != nullptr, "light-car freight offer");
    fprintf(report, "salary original_goods=%d\n", field<int>(chosen, 0));
    fprintf(report, "destination=%s\n", field<char*>(towns[field<int>(chosen, 12)], 12));
    require(fuel_freight::transfer(human, gate, 1.) == 0, "salary loads into light car");
    BYTE* order = field<BYTE*>(own, 28);
    const int dest = field<int>(order, 12);
    const double lightAmount = field<double>(order, 56), lightFee = field<double>(order, 64);
    ROUTINE(Ai_TakeOrder, 0x425820)(int(driver), order);
    BYTE* freight = field<BYTE*>(truck, 28);
    const double largeAmount = field<double>(freight, 56), largeFee = field<double>(freight, 64);
    require(largeAmount > lightAmount && largeFee > lightFee, "competitor carries larger salary order");
    require(ROUTINE(Player_SwapVehicles, 0x4738c0)(human, driver, 1) == 0 && field<BYTE*>(human, 516) == truck &&
                count(truck, 28, 144) == 1 && field<double>(field<BYTE*>(truck, 28), 64) == largeFee,
            "garage transfers loaded truck");
    BYTE* town = towns[dest];
    double money = field<double>(human, 136);
    require(fuel_freight::transfer(human, {field<int>(town, 24), 0}, -1.) == 0 && count(truck, 28, 144) == 0,
            "employee salary unloaded by human");
    double paid = field<double>(human, 136) - money;
    fprintf(report, "salary car_quantity=%.6f car_quote=%.2f truck_quantity=%.6f truck_quote=%.2f payout=%.2f\n",
            lightAmount, lightFee, largeAmount, largeFee, paid);
    require(paid > lightFee, "higher salary fee credited");
}
static void cargo(BYTE* human, bool warehouse, bool identical, bool mixed, bool depots) {
    approach::frameDouble(ROUTINE(Copter_ApplyCargo, 0x56f6d0), fuel_freight::skipCabUpdate);
    approach::frameDouble(ROUTINE(displayScreenMessage, 0x483cb0), fuel_freight::skipBanner);
    approach::frameDouble(ROUTINE(Messages_Add, 0x56ae60), fuel_freight::skipBanner);
    BYTE* hold = field<BYTE*>(human, 516);
    require(hold && !count(hold, 28, 144), "empty truck");
    field<int>(hold, 0) = field<int>(hold, 4) = 92;
    BYTE* goods = field<BYTE*>(global(0x68ccb8), 0);
    BYTE* goodsEnd = field<BYTE*>(global(0x68ccbc), 0);
    int light = -1, dense = -1;
    auto density = [&](int id) { return field<double>(goods + id * 104, 16) / field<double>(goods + id * 104, 24); };
    for (int id = 0; goods + id * 104 != goodsEnd; ++id) {
        BYTE* rec = goods + id * 104;
        if ((field<int>(rec, 48) & 5) || field<double>(rec, 16) <= 0 || field<double>(rec, 24) <= 0)
            continue;
        if (light < 0 || density(id) < density(light))
            light = id;
        if (dense < 0 || density(id) > density(dense))
            dense = id;
    }
    require(light >= 0 && dense >= 0, "container goods catalogue");
    const auto transfer = fuel_freight::transfer;
    if (warehouse) {
        field<double>(global(0x68a820), 0) += 60.;
        ROUTINE(Task_FormOrders, 0x41b720)();
        BYTE** towns = field<BYTE**>(global(0x68cec0), 0);
        BYTE** end = field<BYTE**>(global(0x68cec4), 0);
        if (mixed) {
            fuel_freight::Gate gate;
            int chosen = -1, lightOffer = -1;
            fuel_freight::Gate second;
            for (BYTE** t = towns; t != end; ++t) {
                BYTE* first = field<BYTE*>(*t, 28);
                for (BYTE* g = first; g != field<BYTE*>(*t, 32); g += 16) {
                    BYTE* o = field<BYTE*>(g, 8);
                    if (!o || g[4] & 1)
                        continue;
                    int id = field<int>(o, 0);
                    if (goods[id * 104 + 48] & 5)
                        continue;
                    if (lightOffer < 0 || density(id) < density(lightOffer)) {
                        lightOffer = id;
                        second = {field<int>(*t, 24), int(g - first) / 16};
                    }
                    if (chosen >= 0 && density(chosen) >= density(id))
                        continue;
                    chosen = id;
                    gate = {field<int>(*t, 24), int(g - first) / 16};
                }
            }
            require(chosen >= 0 && transfer(human, gate, 1.) == 0, "dense warehouse order");
            fprintf(report, "warehouse goods=%d orders=1\n", chosen);
            if (depots) {
                int result = transfer(human, second, 1.);
                fprintf(report, "second warehouse goods=%d result=%d orders=%d\n", lightOffer, result,
                        count(hold, 28, 144));
                require(result == 0 && count(hold, 28, 144) == 2,
                        "two complete warehouse orders of different densities");
                return;
            }
        } else {
            int tried = 0;
            for (BYTE** t = towns; t != end && tried < 2; ++t) {
                BYTE* start = field<BYTE*>(*t, 28);
                for (BYTE* g = start; g != field<BYTE*>(*t, 32) && tried < 2; g += 16) {
                    BYTE* offer = field<BYTE*>(g, 8);
                    if ((g[4] & 1) || !offer || (goods[field<int>(offer, 0) * 104 + 48] & 4))
                        continue;
                    int result = transfer(human, {field<int>(*t, 24), int(g - start) / 16}, 1.);
                    fprintf(report, "warehouse attempt=%d result=%d orders=%d\n", tried + 1, result,
                            count(hold, 28, 144));
                    require(tried ? result == -8 : result == 0, "two ordinary loading gates");
                    ++tried;
                }
            }
            require(tried == 2, "two distinct ordinary offers");
        }
    }
    BYTE* head = field<BYTE*>(global(0x68cf6c), 0);
    BYTE* box = field<BYTE*>(head, 0);
    while (box != head) {
        BYTE* item = field<BYTE*>(box, 8);
        if ((field<int>(item, 8) & 0xf0) == 0x40 && field<BYTE*>(item, 88))
            break;
        box = field<BYTE*>(box, 0);
    }
    require(box != head, "real roadside cargo template");
    BYTE* tmpl = field<BYTE*>(field<BYTE*>(box, 8), 88);
    int dest = field<int>(tmpl, 12);
    for (int i = 0; i < 4; ++i) {
        // Same merchandise, choose distinct valid towns to count separate orders.
        field<int>(tmpl, 12) = identical ? dest : (dest + i) % 10;
        if (mixed)
            field<int>(tmpl, 0) = ((i + int(warehouse)) % 2) ? light : dense;
        int idx = -1;
        int result = ROUTINE(Ai_LoadPresent, 0x442e10)(human, box, &idx);
        double quantity = idx >= 0 ? field<double>(field<BYTE*>(hold, 28) + idx * 144, 56) : 0.;
        fprintf(report, "container attempt=%d goods=%d result=%d orders=%d index=%d quantity=%.8f\n", i + 1,
                field<int>(tmpl, 0), result, count(hold, 28, 144), idx, quantity);
        require(i || (result == 0 && idx >= 0 && quantity > 0.), "first container loads");
        if (!mixed) {
            int accepted = warehouse ? 1 : 2;
            if (i < accepted)
                require(result == 0 && idx >= 0 && quantity > 0. &&
                            count(hold, 28, 144) == (identical ? 1 : i + 1 + int(warehouse)),
                        "expected load or merge");
            else
                require(result == -3 || result == -4, "capacity reached");
        }
        if (mixed) {
            int accepted = warehouse ? 2 : 3;
            if (i < accepted)
                require(result == 0 && idx >= 0 && quantity > 0. && count(hold, 28, 144) == i + 1 + int(warehouse),
                        "mixed freight accepted");
            else
                require(result == -3 || result == -4, "mixed freight reaches capacity");
        }
        if (result < 0)
            break;
    }
}
} // namespace fleet_review
