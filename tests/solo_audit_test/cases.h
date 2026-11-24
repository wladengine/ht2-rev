#include "gameplay_cases.h"
#include "marine_math_cases.h"
#include "wheel_cases.h"
#include "edge_pair_cases.h"
#include "startup_cases.h"
#include "body_cases.h"
#include "loaded_rig.h"
#include "route_cases.h"
#include "licence_cases.h"
#include "employee_delivery.h"
#include "outline_chain.h"
#include "light_lifetime.h"
#include "bukhta_cases.h"
#include "auxiliary_wheels.h"
#include "bridge_contacts.h"
#include "vehicle_damage.h"
#include "world_reader.h"
#include "fire_frames.h"
#include "water_contact.h"
#include "effect_sound.h"
#include "script_forms.h"

static bool RunSoloAudit() {
    // Each initializer is evaluated, so a negative control still exercises the
    // other contracts. Sample particle velocities before replacing the emitter.
    const bool results[]={
        bukhta::SceneLightCapacity(), bukhta::TrafficDistances(), bukhta::LightOrder(),
        script_forms::Check(true), script_forms::Check(false), bridge_contacts::Box(),
        world_reader::Check(true), world_reader::Check(false), auxiliary_wheels::Check(), light_lifetime::Check(), outline_chain::Check(), body_frames::Check(), loaded_wheels::Check(), suspension::Check(), hitch_events::Check(), gameplay::Saddle(), gameplay::Speed(), chase_route::Check(), traffic_jam::Check(), route_search::Check(),
        loaded_rig::Check(), licence::Check(), company_racers::Check(), company_racers::CheckOrderLists(), gameplay::Advance(), gameplay::FilteredAdvance(),
        marine_math::CheckAngles(), marine_math::CheckTransforms(), marine_math::CheckFrames(),
        wheel_effects::CheckVelocities(), wheel_effects::Check(), dust_puffs::Check(),
        edge_pairs::Check(), transitions::CheckRecords(), transitions::Check(),
        grid_startup::Check(), grid_startup::CheckOtherSubsystems(), employee_delivery::Check(), vehicle_damage::Check(), water_contact::Check(), fire_frames::Check(), effect_sound::Check()
    };
    for (bool passed : results) if (!passed) return false;
    return true;
}
