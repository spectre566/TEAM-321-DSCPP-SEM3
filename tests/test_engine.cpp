// Unit + integration tests for the simulation engine (no graphics needed).
// Build and run:  cmake --build build --target engine_tests && ctest --test-dir build
#include <cstdio>
#include <cmath>
#include <string>
#include "Core/CityGrid.h"
#include "Graphics/PlayerController.h"

static int passed = 0, failed = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (cond) { passed++; }                                                    \
        else { failed++; std::printf("  FAIL  %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
    } while (0)

static void section(const char* name) { std::printf("[%s]\n", name); }

// ---------------------------------------------------------------- Vehicle
static void testVehicles() {
    section("Vehicle polymorphism");
    PrivateCar car(1);
    PublicBus bus(2, 40);
    AutoRickshaw autoR(3);
    EmergencyVehicle amb(4, "Ambulance"), fire(5, "FireTruck"), police(6, "Police");
    Vehicle* all[6] = {&car, &bus, &autoR, &amb, &fire, &police};

    CHECK(car.getWeight() == 1.0, "car is 1 PCE");
    CHECK(bus.getWeight() == 3.0, "bus is 3 PCE");
    CHECK(std::fabs(autoR.getWeight() - 1.2) < 1e-9, "auto rickshaw is 1.2 PCE");
    CHECK(amb.getPriority() == 0 && fire.getPriority() == 1 && police.getPriority() == 2, "emergency priority order");
    CHECK(car.getPriority() > bus.getPriority(), "bus outranks private car");
    CHECK(amb.isEmergency() && !car.isEmergency(), "emergency flag");
    CHECK(all[1]->getKind() == VehicleKind::Bus && all[3]->getKind() == VehicleKind::Ambulance, "virtual getKind");
    CHECK(all[0]->getName() == "Car 1" && all[3]->getName() == "Ambulance 4", "virtual getName");
    CHECK(amb.getMaxSpeed() > car.getMaxSpeed() && car.getMaxSpeed() > bus.getMaxSpeed(), "speeds differ by type");
}

// ---------------------------------------------------------------- Lane
static void testLane() {
    section("Lane FIFO queue + Min-Heap");
    Lane lane("North", 10);
    PrivateCar a(1), b(2), c(3);
    EmergencyVehicle police(4, "Police"), amb(5, "Ambulance");

    lane.addVehicle(&a, 0);
    lane.addVehicle(&b, 1);
    lane.addVehicle(&police, 2);
    lane.addVehicle(&c, 3);
    lane.addVehicle(&amb, 4);
    CHECK(lane.size() == 5, "five vehicles queued");
    CHECK(std::fabs(lane.getLoad() - 6.0) < 1e-9, "load adds PCE (3 cars + 2 x 1.5)");
    CHECK(lane.peekFront() == &a, "FIFO front is the first arrival");
    CHECK(lane.topEmergency() == &amb, "heap top is the ambulance (priority 0)");

    lane.moveEmergencyToFront();
    CHECK(lane.removeFront() == &amb, "ambulance released first after preemption");
    CHECK(lane.removeFront() == &police, "then the police car");
    CHECK(lane.removeFront() == &a, "then normal FIFO order");
    CHECK(lane.removeFront() == &b && lane.removeFront() == &c, "order of the rest kept");
    CHECK(lane.isEmpty() && !lane.hasEmergency(), "lane and heap empty");
    CHECK(lane.getLoad() == 0, "load back to zero");
    CHECK(lane.removeFront() == NULL, "removing from empty lane is safe");

    Lane sat("S", 5);
    PublicBus b1(10, 30), b2(11, 30);
    sat.addVehicle(&b1, 0);
    CHECK(sat.getSaturation() < 0.8, "3 of 5 PCE is not saturated");
    sat.addVehicle(&b2, 0);
    CHECK(sat.getSaturation() >= 0.8, "6 of 5 PCE is saturated");
}

// ---------------------------------------------------------------- Pathfinder
static void testPathfinder() {
    section("Pathfinder (Dijkstra)");
    //  0 --10-- 1
    //  |        |
    //  10       10
    //  |        |
    //  2 --10-- 3
    Pathfinder pf(4);
    int e[4][2] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};
    for (auto& x : e) { pf.addRoad(x[0], x[1], 10, 10); pf.addRoad(x[1], x[0], 10, 10); }

    std::vector<int> p = pf.findShortestPath(0, 3, true);
    CHECK(p.size() == 3 && p.front() == 0 && p.back() == 3, "path 0 -> 3 has two hops");

    pf.updateRoadLoad(0, 1, 9);   // 90 % full
    CHECK(pf.isRoadSaturated(0, 1), "90% load is saturated");
    CHECK(!pf.isRoadSaturated(0, 2), "empty road is not saturated");
    p = pf.findShortestPath(0, 3, true);
    CHECK(p.size() == 3 && p[1] == 2, "congestion-aware route avoids the saturated road");
    std::vector<int> plain = pf.findShortestPath(0, 3, false);
    CHECK(plain.size() == 3, "static route still finds a shortest path");
    CHECK(pf.getRoadWeight(0, 1) > pf.getRoadWeight(0, 2), "congested edge costs more");

    std::vector<int> noU = pf.findShortestPath(1, 0, true, 0);
    CHECK(noU.size() == 4 && noU[1] == 3, "avoidFirstHop forbids the U-turn");
    CHECK(pf.findShortestPath(0, 7).empty(), "invalid target returns empty path");
}

// ---------------------------------------------------------------- TrafficSignal
static void testSignal() {
    section("TrafficSignal timing");
    TrafficSignal s;
    s.reset(0);
    CHECK(s.isGreenFor(0) && !s.isGreenFor(1), "starts N-S green");

    // static mode: 10 s initial green, 2 s yellow, 1 s all red, then 15 s E-W
    for (int i = 0; i < 10 * 60 + 1; i++) s.update(1.0 / 60, 5, 5, false);
    CHECK(s.getIsYellow(), "yellow after the green expires");
    for (int i = 0; i < 3 * 60 + 1; i++) s.update(1.0 / 60, 5, 5, false);
    CHECK(s.isGreenFor(1), "E-W green after yellow + all-red");
    CHECK(std::fabs(s.getPhaseDuration() - TrafficSignal::FIXED_GREEN) < 1e-9, "static green is fixed 15 s");

    TrafficSignal d;
    d.reset(0);
    double heavy = d.calcDynamicTime(12, 2), light = d.calcDynamicTime(1, 12);
    CHECK(heavy > light, "busier axis gets a longer green");
    CHECK(heavy <= TrafficSignal::MAX_GREEN && light >= TrafficSignal::MIN_GREEN, "green split clamped to [min, max]");

    // gap-out: smart mode ends an empty green after MIN_GREEN when the cross road waits
    TrafficSignal g;
    g.reset(0);
    double t = 0;
    while (g.isGreenFor(0) && t < 20) { g.update(1.0 / 60, 0, 6, true); t += 1.0 / 60; }
    CHECK(t < 6.0, "smart mode gaps out an empty green");

    // preemption flips a conflicting green through a short clearance
    TrafficSignal p;
    p.reset(0);
    p.triggerEmergency(1);
    CHECK(p.getIsPreempted() && p.getIsYellow(), "preemption starts the clearance immediately");
    for (int i = 0; i < 3 * 60; i++) { p.triggerEmergency(1); p.update(1.0 / 60, 0, 0, true); }
    CHECK(p.isGreenFor(1), "emergency axis is green within 3 s");
    p.clearPreemption();
    CHECK(!p.getIsPreempted(), "preemption cleared");
}

// ---------------------------------------------------------------- Intersection
static void testIntersection() {
    section("Intersection preemption");
    Intersection J;
    PrivateCar c1(1), c2(2);
    EmergencyVehicle amb(3, "Ambulance");
    J.addVehicle(2, &c1);              // East lane (axis 1)
    J.addVehicle(2, &amb);
    J.addVehicle(0, &c2);              // North lane (axis 0, starts green)
    amb.distToStop = 100;              // inside the detection radius

    J.update(1.0 / 60, false);
    CHECK(!J.isPreempted(), "fixed mode never preempts");

    for (int i = 0; i < 4 * 60; i++) J.update(1.0 / 60, true);
    CHECK(J.isPreempted(), "smart mode preempts for the ambulance");
    CHECK(J.isGreenFor(2), "ambulance's approach turned green");
    CHECK(J.getLane(2).peekFront() == &amb, "Min-Heap moved the ambulance to the front");
    CHECK(J.getPreemptions() == 1, "one preemption counted");

    amb.waitAtLight = 1.5;
    double w = 0;
    CHECK(J.releaseFront(2, &w) == &amb && std::fabs(w - 1.5) < 1e-9, "release records the wait");
    CHECK(J.getEmergencyCleared() == 1 && J.getTotalCleared() == 1, "clear counters");
    J.update(1.0 / 60, true);
    CHECK(!J.isPreempted(), "preemption ends once the ambulance left");
}

// ---------------------------------------------------------------- CityGrid
static void testCityGrid() {
    section("CityGrid network");
    CityGrid g(3, 3, true, 7);
    CHECK(g.getNumIntersections() == 9, "3 x 3 junctions");
    CHECK((int)g.getGates().size() == 12, "12 city gates");
    CHECK((int)g.getRoads().size() == 2 * (12 + 12), "24 two-way roads = 48 lanes");
    CHECK((int)g.getPaths().size() == 9 * 4 * 3, "every junction: 4 approaches x 3 exits");
    CHECK((int)g.getBlocks().size() == 16, "16 city blocks");
    int left = 0, right = 0, straight = 0;
    for (TurnPath& p : g.getPaths()) (p.turn < 0 ? left : p.turn > 0 ? right : straight)++;
    CHECK(left == right && right == straight, "equal number of left / right / straight paths");

    Road& r = g.getRoads()[g.getRoadIndex(0, 1)];
    CHECK(r.junction == 1 && r.approach == 3, "road 0->1 arrives at J1 from the West");
    CHECK(r.start.y < g.getNodePos(0).y, "eastbound lane is on the left (north) side - India drives on the left");

    section("CityGrid simulation: Fixed vs Smart");
    CityGrid fixedG(3, 3, false, 99), smartG(3, 3, true, 99);
    for (int i = 0; i < 480 * 60; i++) { fixedG.update(1.0 / 60); smartG.update(1.0 / 60); }
    std::printf("  fixed: wait %.2fs  trips %d  emergency stop %.2fs\n", fixedG.getAverageWaitTime(),
                fixedG.getTripsDone(), fixedG.getEmergencyAvgDelay());
    std::printf("  smart: wait %.2fs  trips %d  emergency stop %.2fs  preempt %d  reroutes %d\n",
                smartG.getAverageWaitTime(), smartG.getTripsDone(), smartG.getEmergencyAvgDelay(),
                smartG.getTotalPreemptions(), smartG.getReroutes());
    CHECK(fixedG.getTripsDone() > 300 && smartG.getTripsDone() > 300, "traffic keeps flowing in both cities");
    CHECK(smartG.getAverageWaitTime() < fixedG.getAverageWaitTime() * 0.6, "smart mode cuts the average wait by > 40%");
    CHECK(smartG.getEmergencyTrips() > 5, "emergency vehicles complete their runs");
    CHECK(smartG.getEmergencyAvgDelay() < 1.0, "near-zero stopped time for emergencies in smart mode");
    CHECK(fixedG.getEmergencyAvgDelay() > smartG.getEmergencyAvgDelay() * 5, "fixed timers trap emergencies");
    CHECK(smartG.getTotalPreemptions() > 0, "preemptions happened");
    CHECK(smartG.getGridlockEvents() == 0, "no gridlock in smart mode");

    // invariants: queues match the vehicles on each road
    bool consistent = true;
    for (Road& rd : smartG.getRoads()) {
        if (rd.junction < 0) continue;
        int waiting = 0;
        for (Vehicle* v : rd.cars) if (!v->released) waiting++;
        if (waiting != smartG.getIntersection(rd.junction).getLane(rd.approach).size()) consistent = false;
    }
    CHECK(consistent, "lane queue sizes match unreleased vehicles on every road");

    smartG.reset();
    CHECK(smartG.getActiveVehicles() == 0 && smartG.getSimTime() == 0, "reset clears the city");
    CHECK(smartG.spawnEmergency(0, 3), "manual dispatch works");
}

// ---------------------------------------------------------------- PlayerController
static void testPlayer() {
    section("PlayerController physics + raycast sensors");
    CityGrid g(3, 3, true, 5);
    PlayerController p;
    DriverInput gas;
    gas.up = true;

    // empty road, accelerate east along the top row
    p.reset(Vec2(250, 192), 0);
    for (int i = 0; i < 60; i++) p.update(1.0 / 60, g, gas);
    CHECK(p.getSpeed() > 60 && p.getPos().x > 280, "accelerates forward");
    CHECK(std::fabs(p.getPos().y - 192) < 1.0f, "drives straight without steering");

    DriverInput turn = gas;
    turn.right = true;
    float h0 = p.getHeading();
    for (int i = 0; i < 20; i++) p.update(1.0 / 60, g, turn);
    CHECK(p.getHeading() > h0 + 0.2f, "steering right turns clockwise");

    // from the vertical road at x = 200, drive east straight into a city block
    p.reset(Vec2(205, 300), 0);
    for (int i = 0; i < 120; i++) p.update(1.0 / 60, g, gas);
    CHECK(p.getCollisions() == 0, "brake assist stops before the wall");
    p.reset(Vec2(205, 300), 0);
    p.toggleAssist();                                   // assist off: now it really crashes
    for (int i = 0; i < 120; i++) p.update(1.0 / 60, g, gas);
    bool inside = false;
    for (const Rect& r : g.getBlocks()) if (r.contains(p.getPos())) inside = true;
    CHECK(!inside, "buildings are solid");
    CHECK(p.getCollisions() >= 1, "crash counted without assist");
    p.toggleAssist();

    // sensors on an empty lane: the buildings beside the road must not count as "ahead"
    PlayerController q;
    q.reset(Vec2(250, 192), 0);
    CityGrid g2(3, 3, true, 5);
    g2.spawnEmergency(0, 0);   // put something on the map so update() has vehicles to scan
    DriverInput none;
    q.update(1.0 / 60, g2, none);
    CHECK(q.nearestObstacle() > 1e8f, "sensors clear on an empty lane (side buildings ignored)");

    // brake assist: drive at a parked AI vehicle and stop before hitting it
    CityGrid g3(3, 3, true, 5);
    for (int i = 0; i < 40 * 60; i++) g3.update(1.0 / 60);
    Vehicle* target = NULL;
    for (Vehicle* v : g3.getVehicles())
        if (v->state == MotionState::OnRoad && v->speed < 1 && v->distToStop < 10) { target = v; break; }
    CHECK(target != NULL, "found a queued vehicle to test against");
    if (target) {
        Vec2 f(cosf(target->heading), sinf(target->heading));
        Vec2 start = target->pos - f * 120.0f;
        PlayerController r;
        r.reset(start, target->heading);
        Vec2 tp = target->pos;
        float th = target->heading;
        float hl = target->getLength() / 2;
        int crashes = 0;
        for (int i = 0; i < 180; i++) {
            r.update(1.0 / 60, g3, gas);
            if (i == 10) CHECK(r.nearestObstacle() < 150, "raycast sees the vehicle ahead");
        }
        Vec2 d = tp - r.getPos();
        float gap = d.dot(Vec2(cosf(th), sinf(th))) - hl - r.getLength() / 2;
        crashes = r.getCollisions();
        std::printf("  brake assist stopped %.1f px behind the vehicle (collisions %d)\n", gap, crashes);
        CHECK(crashes == 0 && gap > -1.0f, "brake assist prevents the rear-end crash");
    }
}

// ---------------------------------------------------------------- new in Phase II
static void testSnapshotsAndIncidents() {
    section("Lane snapshots (DSA inspector)");
    Lane lane("E", 10);
    PrivateCar a(1), b(2);
    EmergencyVehicle pol(3, "Police"), amb(4, "Ambulance");
    lane.addVehicle(&a, 0);
    lane.addVehicle(&pol, 1);
    lane.addVehicle(&b, 2);
    lane.addVehicle(&amb, 3);
    std::vector<Vehicle*> q = lane.queueSnapshot();
    std::vector<Vehicle*> h = lane.heapSnapshot();
    CHECK(q.size() == 4 && q[0] == &a && q[3] == &amb, "queue snapshot is in FIFO order");
    CHECK(h.size() == 2 && h[0] == &amb && h[1] == &pol, "heap snapshot is in priority order");
    CHECK(lane.size() == 4 && lane.topEmergency() == &amb, "snapshots do not modify the lane");

    section("Road incidents + Dijkstra avoidance");
    CityGrid fixedG(3, 3, false, 11), smartG(3, 3, true, 11);
    int road = smartG.getRoadIndex(3, 4);              // middle-row arterial J3 -> J4
    CHECK(smartG.createIncident(road, 60) && fixedG.createIncident(road, 60), "incident created");
    CHECK(!smartG.createIncident(road, 60), "no second incident on a blocked road");
    CHECK(smartG.getActiveIncidents() == 1, "one active incident");
    smartG.update(1.0 / 60);
    CHECK(smartG.getPathfinder().isRoadSaturated(3, 4), "smart sensors report the blocked lane as saturated");
    std::vector<int> p = smartG.getPathfinder().findShortestPath(3, 5, true);
    CHECK(p.size() >= 3 && p[1] != 4, "Dijkstra avoids the blocked road");
    for (int i = 0; i < 300 * 60; i++) {
        if (i == 120 * 60) { smartG.createIncident(road, 60); fixedG.createIncident(road, 60); }
        smartG.update(1.0 / 60);
        fixedG.update(1.0 / 60);
    }
    std::printf("  with incidents: trip delay fixed %.1fs vs smart %.1fs, reroutes %d\n", fixedG.getAverageTripDelay(),
                smartG.getAverageTripDelay(), smartG.getReroutes());
    CHECK(smartG.getActiveIncidents() == 0, "tow truck clears incidents after their duration");
    CHECK(smartG.getAverageTripDelay() < fixedG.getAverageTripDelay() * 0.6, "smart city copes far better with incidents");
    CHECK(smartG.getGridlockEvents() == 0, "no gridlock in smart mode despite incidents");

    section("Idle CO2 + robustness across seeds");
    CHECK(smartG.getIdleCO2kg() > 0 && smartG.getIdleCO2kg() < fixedG.getIdleCO2kg(), "smart city idles (and emits) less");
    int wins = 0;
    for (unsigned seed : {101u, 202u, 303u}) {
        CityGrid f(3, 3, false, seed), s(3, 3, true, seed);
        for (int i = 0; i < 240 * 60; i++) { f.update(1.0 / 60); s.update(1.0 / 60); }
        if (s.getAverageWaitTime() < f.getAverageWaitTime() * 0.6) wins++;
    }
    CHECK(wins == 3, "smart mode wins on every seed tested");

    section("Other grid sizes");
    CityGrid g44(4, 4, true, 3);
    for (int i = 0; i < 120 * 60; i++) g44.update(1.0 / 60);
    CHECK(g44.getNumIntersections() == 16 && g44.getTripsDone() > 50, "4x4 city runs");
    CityGrid g22(2, 2, true, 3);
    for (int i = 0; i < 120 * 60; i++) g22.update(1.0 / 60);
    CHECK(g22.getNumIntersections() == 4 && g22.getTripsDone() > 30, "2x2 city (the original team design) runs");
}

int main() {
    testVehicles();
    testLane();
    testPathfinder();
    testSignal();
    testIntersection();
    testCityGrid();
    testPlayer();
    testSnapshotsAndIncidents();
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
