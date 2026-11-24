// Included in the approach fixture. Units and masses here describe the two test bodies.
extern "C" {
extern BYTE KingRdata[];
void MovingItem_BodyContact(BYTE*, BYTE*, DWORD, float);
void __fastcall MovingItem_StepEnd(BYTE*, void*, int);
void __fastcall Car_AfterPhysics(BYTE*);
double __fastcall Car_WorkingOrder(BYTE*);
void Vehicle_MakeDamageKeys(BYTE**, int*);
Pose* Matrix_Inverse(Pose*, const Pose*);
void Car_HitsAndFrame(BYTE*, DWORD*);
BYTE* __fastcall Car_GiveWay(BYTE*, void*);
void __fastcall Item_SetVelocity(BYTE*, void*, const float*, const char*);
void __fastcall Item_SetSpin(BYTE*, void*, const float*, const char*);
int Road_GetRoadParameters(RoadPosition, BYTE*);
void __fastcall NameHash_Add(BYTE*, void*, const char*, void*);
int __cdecl Bodies_Touch(BYTE*, const float*, int, BYTE*, const float*);
}
static void** vehicleTable(int original) {
#ifdef STOCK_WORLD
    return reinterpret_cast<void**>(original);
#else
    return reinterpret_cast<void**>(KingRdata + (original - 0x64b000));
#endif
}
static void __fastcall skipSceneUpdate(BYTE*, void*, float, int) {}

static Pose relativePose(const Pose& base, float right, float forward) {
    Pose pose = base;
    for (int i = 0; i < 3; ++i)
        pose.m[9 + i] += right * base.m[i] + forward * base.m[3 + i];
    return pose;
}
enum class RearImpact { none, mild, strong };

static void rearHitThenPass(RoadPosition pos, BYTE* human, BYTE* driver, RearImpact impact) {
    const bool strong = impact == RearImpact::strong;
    const float entrySpeed = strong ? 20.0f : 8.0f;
    const Pose parked = field<Pose>(car, 516);
    BYTE room[64] = {}, description[80] = {}, roadInfo[48] = {};
    ROUTINE(Road_GetRoadParameters, 0x438d70)(pos, roadInfo);
    const char* roomName = field<const char*>(roadInfo, 20);
    require(roomName && *roomName, "resolved ambush road room");
    field<const char*>(room, 12) = roomName;
    field<BYTE*>(description, 64) = room;
    static BYTE* roomBuckets[500];
    field<BYTE**>(global(0x71e820), 8) = roomBuckets;
    ROUTINE(NameHash_Add, 0x5d2540)(global(0x71e820), nullptr, roomName, room);
    field<BYTE*>(global(0x6ef1fc), 0) = view;
    field<BYTE*>(global(0x6974c4), 0) = view;
    field<BYTE*>(global(0x6f33ac), 0) = view;
    field<int>(car, 10088) = 24; // compact and extended frame packet sizes with no simulated wheels
    field<int>(car, 10092) = 34;
    field<double>(view, 2960) = 0.05;
    field<double>(view, 2968) = 1;
    field<float>(global(0x696be0), 0) = 0.5f;
    // Use base moving-item velocity setters without cab animation, and skip the
    // final scene update. Contact forwarding, vehicle hit, fault and AI use the game entries.
    void* bodyMethods[51]; // slots 0..200, including contact forwarding
    memcpy(bodyMethods, vehicleTable(0x64f568), sizeof bodyMethods);
    bodyMethods[144 / 4] = reinterpret_cast<void*>(ROUTINE(Item_SetVelocity, 0x5812a0));
    bodyMethods[148 / 4] = reinterpret_cast<void*>(ROUTINE(Item_SetSpin, 0x581430));
    bodyMethods[80 / 4] = reinterpret_cast<void*>(skipSceneUpdate);
    for (BYTE* vehicle : {truck, car}) {
        bool player = vehicle == truck;
        const float mass = player ? 10000.f : 1500.f;
        BYTE* body = vehicle + 16;
        field<void**>(vehicle, 0) = vehicleTable(0x64f654);
        field<void**>(body, 0) = bodyMethods;
        field<BYTE*>(body, 32) = description;
        field<BYTE*>(body, 800) = field<BYTE*>(body, 804) = room;
        field<int>(body, 392) = field<int>(body, 3568) = 2;
        field<int>(body, 1200) = field<int>(body, 1204) = 1; // awake and eligible for contact processing
        field<int>(vehicle, 20900) |= 128;
        field<float>(vehicle, 21152) = 1;  // hit response ramp, unity
        field<float>(vehicle, 20624) = 60; // standard post-hit timeout at construction
        ROUTINE(Vehicle_MakeDamageKeys, 0x5465c0)(&field<BYTE*>(vehicle, 9536), &field<int>(vehicle, 9532));
        field<float>(body, 396) = field<float>(body, 3268) = mass;
        field<float>(body, 3272) = field<float>(body, 3280) = player ? 4.0f : 2.0f;
        for (int i = 0; i < 3; ++i) {
            field<float>(body, 3288 + i * 4) = field<float>(body, 404 + i * 4) = mass * 3;
            float speed = (player ? entrySpeed : 0.f) * parked.m[3 + i];
            field<float>(body, 416 + i * 4) = field<float>(body, 452 + i * 4) =
                field<float>(body, 3300 + i * 4) = speed;
        }
    }
    field<BYTE*>(global(0x6f653c), 0) = truck + 16 + 380;
    field<BYTE*>(truck + 16, 384) = car + 16 + 380;
    field<BYTE*>(global(0x6ef1f8), 0) = truck + 4;
    field<BYTE*>(truck, 8) = car + 4;
    Pose& playerPose = field<Pose>(truck, 516);
    auto touches = [&] {
        return ROUTINE(Bodies_Touch, 0x592350)(truck + 16, playerPose.m, 0, car + 16, parked.m) != 0;
    };
    playerPose = relativePose(parked, 0, -6.2f);
    require(!touches(), "clear before rear contact");
    playerPose = relativePose(parked, 0, -5.99f);
    require(touches(), "front of truck reached rear of stationary car");
    field<Pose>(truck, 564) = field<Pose>(truck, 3188) = playerPose;
    field<Pose>(car, 564) = field<Pose>(car, 3188) = parked;
    for (BYTE* vehicle : {truck, car})
        ROUTINE(Matrix_Inverse, 0x5ac2e0)(&field<Pose>(vehicle, 612), &field<Pose>(vehicle, 516));
    BYTE contact[208] = {};
    field<BYTE*>(contact, 116) = truck + 16;
    for (int i = 0; i < 3; ++i) {
        field<float>(contact, 52 + i * 4) = parked.m[9 + i] - 2 * parked.m[3 + i];
        field<float>(contact, 64 + i * 4) = parked.m[3 + i];
    }
    double tally = field<double>(human, 224);
    double orderBefore = ROUTINE(Car_WorkingOrder, 0x557090)(car);
    require(orderBefore > 0.3, "mafia car initially operational");
    ROUTINE(MovingItem_BodyContact, 0x581990)(car + 16, contact, 0, 1);
    // BodyContact notifies the receiving truck. Finish the initiating car's physics step
    // to deliver its own change of momentum to Car_Hit at the same bumper contact.
    memcpy(car + 16 + 1516, contact + 52, 12);
    ROUTINE(MovingItem_StepEnd, 0x583c30)(car + 16, nullptr, 0);
    double orderAfter = ROUTINE(Car_WorkingOrder, 0x557090)(car);
    fprintf(report, "mafia order=%.6f->%.6f enabled=%d hit-clock=%.3f\n", orderBefore, orderAfter,
            field<int>(car, 20608), field<float>(car, 20620));
    require((orderAfter <= 0.3) == strong && (field<int>(car, 20608) == 0) == strong, "impact severity");
    BYTE* keys = field<BYTE*>(car, 9536);
    fprintf(report, "mafia rear damage BR=%d/%d BL=%d/%d BC=%d/%d\n", field<int>(keys, 124),
            field<int>(keys, 128), field<int>(keys, 144), field<int>(keys, 148), field<int>(keys, 164),
            field<int>(keys, 168));
    require(field<BYTE*>(truck, 1044) == car + 16 && (field<int>(truck, 908) & 2),
            "player received mafia body contact");
    require(field<int>(truck, 20600) != 0 && field<BYTE*>(truck, 20632) == car + 16,
            "game classified impact partner");
    double truckSpeed = 0, mafiaSpeed = 0;
    for (int i = 0; i < 3; ++i) {
        truckSpeed += field<float>(truck, 432 + i * 4) * parked.m[3 + i];
        mafiaSpeed += field<float>(car, 432 + i * 4) * parked.m[3 + i];
    }
    require(truckSpeed > 0 && truckSpeed < entrySpeed && mafiaSpeed > 0, "impact transferred momentum");
    DWORD flags = 0;
    ROUTINE(Car_HitsAndFrame, 0x4cfe70)(truck, &flags);
    require(field<double>(human, 224) > tally && field<int>(human, 232), "campaign registered the impact");
    fprintf(report, "rear hit room=%s kind=%d flags=%d tally=%.1f->%.1f speed=%.0f/0->%.3f/%.3f phase=%d\n",
            roomName, field<int>(truck, 20600), field<int>(human, 232), tally, field<double>(human, 224),
            entrySpeed, truckSpeed, mafiaSpeed, field<int>(truck, 20944));
    struct Sample {
        float right, forward;
    };
    // Steer by prescribed relative positions. The struck car coasts at the
    // measured outgoing speed; both poses are in the same sloped road frame.
    const Sample path[] = {{0, -5.99f}, {0, -7}, {5, -7}, {5, -1}, {5, 1}, {5, 12}, {5, 32}};
    const int stepsPerLeg = 7;
    bool pursuing = false;
    const double postHitTally = field<double>(human, 224);
    const int lastStep = (int(sizeof(path) / sizeof(path[0])) - 1) * stepsPerLeg;
    for (int step = 0; step <= lastStep; ++step) {
        int leg = step ? (step - 1) / stepsPerLeg : 0;
        double t = double(step - leg * stepsPerLeg) / stepsPerLeg;
        float right = float(path[leg].right + t * (path[leg + 1].right - path[leg].right));
        float forward = float(path[leg].forward + t * (path[leg + 1].forward - path[leg].forward));
        double elapsed = step * 0.1;
        field<double>(view, 2960) = 0.1;
        field<double>(view, 2968) = 1 + elapsed;
        Pose& movingCar = field<Pose>(car, 516);
        movingCar = relativePose(parked, 0, float(mafiaSpeed * elapsed));
        playerPose = relativePose(movingCar, right, forward);
        if (step) {
            require(fabs(forward) > 6 || fabs(right) > 2.5, "test bodies have bumper or lateral clearance");
            // No fresh contact on this frame. Run the normal classification and campaign update.
            field<int>(truck, 912) = 0;
            ROUTINE(Car_GiveWay, 0x54f040)(truck, nullptr);
            ROUTINE(Car_HitsAndFrame, 0x4cfe70)(truck, &flags);
            require(!field<int>(truck, 20600) && field<double>(human, 224) == postHitTally,
                    "original hit counted once");
        }
        field<float>(ccab, 13100) = float(mafiaSpeed);
        double lateralSpeed = (path[leg + 1].right - path[leg].right) / (stepsPerLeg * 0.1);
        double longSpeed = mafiaSpeed + (path[leg + 1].forward - path[leg].forward) / (stepsPerLeg * 0.1);
        field<float>(tcab, 13100) =
            step ? float(sqrt(longSpeed * longSpeed + lateralSpeed * lateralSpeed)) : float(truckSpeed);
        ROUTINE(Car_AfterPhysics, 0x54ff80)(car);
        ROUTINE(Cop_Step, 0x4d1360)(car + 16);
        pursuing = field<BYTE*>(driver, 968) == human;
        fprintf(report, "after hit t=%.1f right=%.3f forward=%.3f phase=%d mines=%u pursuing=%d\n", elapsed,
                right, forward, field<int>(truck, 20944), mines(), pursuing);
        bool ahead = forward > 0;
        bool waiting = (field<DWORD>(driver, 96) & 0x80000) != 0;
        bool attack = ahead && !strong;
        require(field<int>(truck, 20944) == 24 && (ROUTINE(Car_WorkingOrder, 0x557090)(car) <= 0.3) == strong,
                "same encounter phase and measured working order during pass");
        require(mines() == (attack ? 31u : 0u) && pursuing == attack && waiting == !attack,
                "rear impact, clearance and pursuit");
        if (strong)
            require(field<DWORD>(ccab, 16488) & 1, "disabled vehicle took the stop branch");
        if (pursuing)
            break;
    }
    require(pursuing == !strong, "outcome of the overtake");
    if (strong) {
        // Separate clock probes at the last relative poses. An expired timer by itself
        // must not start pursuit once the truck has left the thirty-unit radius.
        for (double elapsed : {60.0, 60.1}) {
            field<double>(view, 2968) = 1 + elapsed;
            ROUTINE(Car_AfterPhysics, 0x54ff80)(car);
            ROUTINE(Cop_Step, 0x4d1360)(car + 16);
            double order = ROUTINE(Car_WorkingOrder, 0x557090)(car);
            fprintf(report,
                    "recovery elapsed=%.1f forward=32 order=%.6f enabled=%d phase=%d mines=%u pursuing=%d\n",
                    elapsed, order, field<int>(car, 20608), field<int>(truck, 20944), mines(),
                    field<BYTE*>(driver, 968) == human);
            require((field<int>(car, 20608) != 0) == (elapsed > 60) && (order > 0.3) == (elapsed > 60) &&
                        !mines() && field<BYTE*>(driver, 968) != human &&
                        (field<DWORD>(driver, 96) & 0x80000),
                    "timed recovery outside trigger");
        }
        playerPose = relativePose(field<Pose>(car, 516), 5, 1);
        ROUTINE(Cop_Step, 0x4d1360)(car + 16);
        require(mines() == 31 && field<BYTE*>(driver, 968) == human && !(field<DWORD>(driver, 96) & 0x80000),
                "recovered ambusher responds to a later return");
        fprintf(report, "return after recovery right=5 forward=1 mines=%u pursuing=1\n", mines());
    }
}
