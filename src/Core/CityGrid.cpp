#include "Core/CityGrid.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace {
const float PI = 3.14159265f;
const float RELEASE_DIST = 90.0f;   // front vehicle must be this close to the stop line
const float STOPPED_SPEED = 4.0f;   // below this a vehicle counts as waiting
// Idling a petrol car burns ~0.6 L/h -> ~0.39 g CO2 per second; scaled by PCE for bigger vehicles
const double IDLE_CO2_G_PER_PCE_S = 0.39;
const float GATE_OFFSET = 40.0f;    // gates sit just outside the visible map
const float YIELD_SHIFT = 6.0f;     // how far cars pull to the kerb for an emergency
const float EMERGENCY_SHIFT = -6.0f;
const float YIELD_RANGE = 220.0f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Intelligent Driver Model: smooth acceleration towards v0 that brakes for a leader.
float idm(float v, float v0, float amax, float gap, float leaderSpeed, bool hasLeader) {
    const float b = 130.0f, s0 = 4.0f, T = 0.55f;
    float freeTerm = 1.0f - powf(v / v0, 4.0f);
    if (!hasLeader) return amax * freeTerm;
    float dv = v - leaderSpeed;
    float sStar = s0 + max(0.0f, v * T + v * dv / (2.0f * sqrtf(amax * b)));
    float g = max(gap, 0.5f);
    return amax * (freeTerm - (sStar / g) * (sStar / g));
}
}

CityGrid::CityGrid(int rows, int cols, bool smart, unsigned seed)
    : rows(rows), cols(cols), junctions(rows * cols), pathfinder(4),
      smartMode(smart), simTime(0.0), nextId(1), seed(seed), rng(seed), demand(1.0) {
    buildDefaultGrid();
    reset();
}

CityGrid::~CityGrid() {
    for (Vehicle* v : vehicles) delete v;
    for (Gate& g : gates)
        for (Vehicle* v : g.pending) delete v;
}

// ---------------------------------------------------------------------------
// Network construction: an R x C grid of junctions + one gate at the end of
// every boundary road. Every road is a pair of directed edges in the graph.
//
//      G0     G1     G2
//      |      |      |
// G6 - J0 --- J1 --- J2 - G9
//      |      |      |
// G7 - J3 --- J4 --- J5 - G10
//      |      |      |
// G8 - J6 --- J7 --- J8 - G11
//      |      |      |
//      G3     G4     G5
// ---------------------------------------------------------------------------
void CityGrid::buildDefaultGrid() {
    int J = rows * cols;
    worldW = 2 * MARGIN + (cols - 1) * SPACING;
    worldH = 2 * MARGIN + (rows - 1) * SPACING;

    nodePos.clear();
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            nodePos.push_back(Vec2(MARGIN + c * SPACING, MARGIN + r * SPACING));

    gates.clear();
    auto addGate = [&](Vec2 p, int junction, double weight) {
        Gate g;
        g.node = (int)nodePos.size();
        g.inRoad = g.outRoad = -1;
        g.opposite = -1;
        g.weight = weight;
        g.nextArrival = 0;
        nodePos.push_back(p);
        gates.push_back(g);
        return junction;
    };
    vector<int> gateJunction;
    // demand profile: the middle row is the busy East-West arterial
    for (int c = 0; c < cols; c++) gateJunction.push_back(addGate(Vec2(nodePos[c].x, -GATE_OFFSET), c, c == cols / 2 ? 1.0 : 0.6));
    for (int c = 0; c < cols; c++) gateJunction.push_back(addGate(Vec2(nodePos[c].x, worldH + GATE_OFFSET), (rows - 1) * cols + c, c == cols / 2 ? 1.0 : 0.6));
    for (int r = 0; r < rows; r++) gateJunction.push_back(addGate(Vec2(-GATE_OFFSET, nodePos[r * cols].y), r * cols, r == rows / 2 ? 2.0 : 0.8));
    for (int r = 0; r < rows; r++) gateJunction.push_back(addGate(Vec2(worldW + GATE_OFFSET, nodePos[r * cols].y), r * cols + cols - 1, r == rows / 2 ? 2.0 : 0.8));

    for (int i = 0; i < (int)gates.size(); i++) {
        if (i < cols) gates[i].opposite = i + cols;
        else if (i < 2 * cols) gates[i].opposite = i - cols;
        else if (i < 2 * cols + rows) gates[i].opposite = i + rows;
        else gates[i].opposite = i - rows;
    }

    int N = (int)nodePos.size();
    pathfinder.reset(N);
    roads.clear();
    roadIndex.assign(N, vector<int>(N, -1));

    for (int j = 0; j < J; j++) junctions[j].setId(j);

    // Horizontal and vertical roads between junctions
    for (int r = 0; r < rows; r++)
        for (int c = 0; c + 1 < cols; c++) addRoadPair(r * cols + c, r * cols + c + 1);
    for (int r = 0; r + 1 < rows; r++)
        for (int c = 0; c < cols; c++) addRoadPair(r * cols + c, (r + 1) * cols + c);

    // Boundary roads to the gates
    for (int i = 0; i < (int)gates.size(); i++) {
        addRoadPair(gates[i].node, gateJunction[i]);
        gates[i].inRoad = roadIndex[gates[i].node][gateJunction[i]];
        gates[i].outRoad = roadIndex[gateJunction[i]][gates[i].node];
    }

    buildTurnPaths();
    buildBlocks();

    cellSize = 50.0f;
    gridW = (int)((worldW + 2 * 100) / cellSize) + 1;
    gridH = (int)((worldH + 2 * 100) / cellSize) + 1;
    buckets.assign(gridW * gridH, vector<Vehicle*>());
}

void CityGrid::addRoadPair(int u, int v) {
    addRoad(u, v);
    addRoad(v, u);
}

void CityGrid::addRoad(int u, int v) {
    int J = (int)junctions.size();
    Vec2 a = nodePos[u], b = nodePos[v];

    Road r;
    r.id = (int)roads.size();
    r.from = u;
    r.to = v;
    r.dir = (b - a).normalized();
    r.normal = r.dir.leftNormal();
    Vec2 off = r.normal * LANE_OFFSET;
    r.start = a + (u < J ? r.dir * STOP_DIST : Vec2()) + off;
    r.end = b - (v < J ? r.dir * STOP_DIST : Vec2()) + off;
    r.length = (r.end - r.start).dot(r.dir);
    r.capacity = r.length / 27.0;     // a car (22 px) plus a 5 px gap = 1 PCE
    r.fromJunction = u < J ? u : -1;
    r.junction = v < J ? v : -1;
    r.reservedLoad = 0;
    r.incidentS = -1;
    r.incidentUntil = 0;

    r.approach = -1;
    if (r.junction >= 0) {
        if (a.y < b.y - 1) r.approach = 0;       // comes from the North
        else if (a.y > b.y + 1) r.approach = 1;  // from the South
        else if (a.x > b.x) r.approach = 2;      // from the East
        else r.approach = 3;                     // from the West
        junctions[r.junction].getLane(r.approach).setCapacity(r.capacity);
    }

    roadIndex[u][v] = r.id;
    pathfinder.addRoad(u, v, r.length, r.capacity);
    roads.push_back(r);
}

// Every (incoming road, outgoing road) pair except U-turns gets a curved path.
void CityGrid::buildTurnPaths() {
    paths.clear();
    pathIndex.assign(roads.size(), vector<int>(roads.size(), -1));

    for (Road& in : roads) {
        if (in.junction < 0) continue;
        for (Road& out : roads) {
            if (out.from != in.to || out.to == in.from) continue;

            TurnPath p;
            p.junction = in.junction;
            p.inRoad = in.id;
            p.outRoad = out.id;
            float cr = in.dir.cross(out.dir);
            p.turn = fabs(cr) < 0.1f ? 0 : (cr > 0 ? 1 : -1);   // y points down: + = right

            Vec2 a = in.end, c = out.start;
            Vec2 ctrl = (p.turn == 0) ? (a + c) * 0.5f : a + in.dir * (c - a).dot(in.dir);
            const int N = 14;
            float total = 0;
            for (int i = 0; i <= N; i++) {
                float t = (float)i / N;
                float u = 1 - t;
                Vec2 q = a * (u * u) + ctrl * (2 * u * t) + c * (t * t);
                if (i > 0) total += (q - p.pts.back()).length();
                p.pts.push_back(q);
                p.cum.push_back(total);
            }
            p.length = total;
            pathIndex[in.id][out.id] = (int)paths.size();
            paths.push_back(p);
        }
    }
}

// City blocks between the roads (used for buildings and player collisions)
void CityGrid::buildBlocks() {
    blocks.clear();
    float pad = ROAD_HALF + SIDEWALK;
    vector<float> xs, ys;
    xs.push_back(-200);
    for (int c = 0; c < cols; c++) xs.push_back(MARGIN + c * SPACING);
    xs.push_back(worldW + 200);
    ys.push_back(-200);
    for (int r = 0; r < rows; r++) ys.push_back(MARGIN + r * SPACING);
    ys.push_back(worldH + 200);

    for (int i = 0; i + 1 < (int)ys.size(); i++) {
        for (int k = 0; k + 1 < (int)xs.size(); k++) {
            float x0 = xs[k] + (k == 0 ? 0 : pad);
            float x1 = xs[k + 1] - (k + 1 == (int)xs.size() - 1 ? 0 : pad);
            float y0 = ys[i] + (i == 0 ? 0 : pad);
            float y1 = ys[i + 1] - (i + 1 == (int)ys.size() - 1 ? 0 : pad);
            blocks.push_back(Rect(x0, y0, x1 - x0, y1 - y0));
        }
    }
}

// ---------------------------------------------------------------------------
// Reset / spawning
// ---------------------------------------------------------------------------
void CityGrid::reset() {
    for (Vehicle* v : vehicles) delete v;
    vehicles.clear();
    for (Gate& g : gates) {
        for (Vehicle* v : g.pending) delete v;
        g.pending.clear();
    }
    for (Road& r : roads) {
        r.cars.clear();
        r.reservedLoad = 0;
        r.incidentS = -1;
    }
    for (Intersection& j : junctions) j.reset();
    committed.assign(junctions.size() * 2, 0);
    releaseSeq = 0;
    // stagger the starting phases so the grid does not pulse in sync
    for (int j = 0; j < (int)junctions.size(); j++) junctions[j].getSignal().reset(j % 2);

    rng.seed(seed);
    nextId = 1;
    simTime = 0;
    uniform_real_distribution<double> first(0.0, 2.0);
    for (Gate& g : gates) g.nextArrival = first(rng);
    nextEmergency = 12.0;

    recentWaits.clear();
    recentExits.clear();
    tripsDone = 0;
    tripDelayTotal = 0;
    emergencyTrips = 0;
    emergencyDelayTotal = 0;
    emergencyTimeTotal = 0;
    reroutes = 0;
    idlePceSeconds = 0;
    gridlockEvents = 0;
    lastGridlockLog = -100;
    sampleTimer = 0;
    history.clear();
    events.clear();
    eventSeq = 0;
    playerPreemptJunction = -1;
    syncGraphLoads();
}

Vehicle* CityGrid::makeVehicle(double r) {
    int id = nextId++;
    if (r < 0.66) return new PrivateCar(id);
    if (r < 0.84) return new AutoRickshaw(id);
    return new PublicBus(id, 20 + id % 40);
}

void CityGrid::planRoute(Vehicle* v, int gateIdx, int exitIdx) {
    v->entryGate = gateIdx;
    v->exitGate = gates[exitIdx].node;
    // Mode 1 vehicles follow a static map (plain distance); Mode 2 uses live congestion
    bool aware = smartMode;
    v->route = pathfinder.findShortestPath(gates[gateIdx].node, v->exitGate, aware, -1);
    v->routeIdx = 1;
}

void CityGrid::spawnArrivals(double dt) {
    // Rush-hour waves: demand rises and falls on a 2 minute cycle
    double wave = 1.0 + 0.35 * sin(2.0 * PI * simTime / 120.0);
    uniform_real_distribution<double> uni(0.0, 1.0);

    for (int gi = 0; gi < (int)gates.size(); gi++) {
        Gate& g = gates[gi];
        double rate = 0.105 * demand * g.weight * wave;    // vehicles per second
        while (simTime >= g.nextArrival) {
            Vehicle* v = makeVehicle(uni(rng));
            int exitIdx;
            if (uni(rng) < 0.5) exitIdx = g.opposite;
            else {
                do { exitIdx = (int)(uni(rng) * gates.size()) % (int)gates.size(); } while (exitIdx == gi);
            }
            v->spawnTime = simTime;
            planRoute(v, gi, exitIdx);
            g.pending.push_back(v);
            g.nextArrival += -log(1.0 - uni(rng) * 0.999) / max(rate, 0.01);
        }
    }

    if (simTime >= nextEmergency) {
        int kind = uni(rng) < 0.55 ? 0 : (uni(rng) < 0.5 ? 1 : 2);
        int gi = (int)(uni(rng) * gates.size()) % (int)gates.size();
        spawnEmergency(kind, gi);
        nextEmergency = simTime + 22.0 + uni(rng) * 16.0;
    }
    (void)dt;
}

bool CityGrid::spawnEmergency(int kind, int gateIdx) {
    if (gateIdx < 0 || gateIdx >= (int)gates.size()) return false;
    static const char* names[3] = {"Ambulance", "FireTruck", "Police"};
    Vehicle* v = new EmergencyVehicle(nextId++, names[kind % 3]);
    v->spawnTime = simTime;
    // emergencies always head to the far side of the city
    planRoute(v, gateIdx, gates[gateIdx].opposite);
    gates[gateIdx].pending.push_front(v);   // dispatched ahead of normal traffic
    logEvent(EventType::EmergencySpawn, v->getName() + " called in at the " + nodeName(gates[gateIdx].node) + " gate",
             nodePos[gates[gateIdx].node]);
    return true;
}

bool CityGrid::hasRoom(Road& r, Vehicle* v) {
    double load = r.reservedLoad + v->getWeight();
    double capacity = r.capacity;
    bool blocked = r.incidentS >= 0;
    if (blocked) capacity = (r.incidentS - 14.0) / 27.0;   // only the stretch before the breakdown is usable
    float rearmost = 1e9f;
    for (Vehicle* c : r.cars) {
        if (blocked && c->s >= r.incidentS) continue;     // already past it, driving away
        load += c->getWeight();
        rearmost = min(rearmost, c->s - c->getLength() / 2);
    }
    if (r.junction >= 0 && load > capacity + 0.5) return false;
    return rearmost > v->getLength() + 6.0f;
}

void CityGrid::placeOnRoad(Vehicle* v, int roadId, float s) {
    Road& r = roads[roadId];
    v->state = MotionState::OnRoad;
    v->roadId = roadId;
    v->s = s;
    v->released = false;
    v->rerouteChecked = false;
    v->heading = atan2f(r.dir.y, r.dir.x);
    r.cars.push_back(v);
    if (r.junction >= 0) {
        junctions[r.junction].addVehicle(r.approach, v);
        if (smartMode) maybeReroute(v);
    }
    updatePosition(v);
}

void CityGrid::injectPending() {
    for (Gate& g : gates) {
        if (g.pending.empty()) continue;
        Vehicle* v = g.pending.front();
        Road& r = roads[g.inRoad];
        if (!hasRoom(r, v)) continue;
        g.pending.pop_front();
        v->speed = v->getMaxSpeed() * 0.6f;
        vehicles.push_back(v);
        placeOnRoad(v, g.inRoad, v->getLength() / 2);
    }
}

bool CityGrid::addVehicleToGrid(int junctionId, int laneId, Vehicle* v) {
    if (junctionId < 0 || junctionId >= (int)junctions.size()) return false;
    return junctions[junctionId].addVehicle(laneId, v);
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------
int CityGrid::plannedNextRoad(Vehicle* v) {
    Road& r = roads[v->roadId];
    if (r.junction < 0) return -1;
    if (v->routeIdx + 1 < (int)v->route.size() && v->route[v->routeIdx] == r.to) {
        int nr = roadIndex[r.to][v->route[v->routeIdx + 1]];
        if (nr >= 0) return nr;
    }
    // fallback: replan from here
    v->route = pathfinder.findShortestPath(r.to, v->exitGate, smartMode, r.from);
    v->route.insert(v->route.begin(), r.from);
    v->routeIdx = 1;
    if (v->route.size() >= 3) return roadIndex[r.to][v->route[2]];
    return -1;
}

// Mode 2: if the next road on the plan is >= 80 % saturated, run Dijkstra
// with congestion weights from the upcoming junction and take the new path
// when it is clearly cheaper.
void CityGrid::maybeReroute(Vehicle* v) {
    if (v->released) return;   // already committed to the next road
    Road& r = roads[v->roadId];
    if (r.junction < 0) return;
    int j = r.to;
    if (v->routeIdx + 1 >= (int)v->route.size() || v->route[v->routeIdx] != j) return;
    int next = v->route[v->routeIdx + 1];
    if (!pathfinder.isRoadSaturated(j, next)) return;

    vector<int> remaining(v->route.begin() + v->routeIdx, v->route.end());
    double oldCost = pathfinder.pathCost(remaining);
    vector<int> np = pathfinder.findShortestPath(j, v->exitGate, true, r.from);
    if (np.size() < 2 || np[1] == next) return;
    double newCost = pathfinder.pathCost(np);
    if (newCost >= oldCost * 0.92) return;

    RoadEdge* e = pathfinder.getEdge(j, next);
    int sat = e ? (int)(e->getSaturation() * 100) : 0;
    v->route = np;
    v->route.insert(v->route.begin(), r.from);
    v->routeIdx = 1;
    v->rerouteFlash = 2.0f;
    reroutes++;

    string via;
    for (int i = 1; i < (int)np.size() && i < 4; i++) via += (i > 1 ? ">" : "") + nodeName(np[i]);
    logEvent(EventType::Reroute, v->getName() + " took a detour: road " + nodeName(j) + ">" + nodeName(next) +
             " is " + to_string(sat) + "% full, going via " + via, v->pos);
}

// ---------------------------------------------------------------------------
// Simulation tick
// ---------------------------------------------------------------------------
void CityGrid::update(double dt) {
    simTime += dt;
    for (Road& r : roads) {
        if (r.incidentS >= 0 && simTime >= r.incidentUntil) {
            r.incidentS = -1;
            logEvent(EventType::Info, "Tow truck cleared road " + nodeName(r.from) + ">" + nodeName(r.to),
                     r.start + r.dir * r.length * 0.5f);
        }
    }

    spawnArrivals(dt);
    injectPending();
    syncGraphLoads();

    // 1. Signals + Min-Heap preemption at every junction
    handlePlayerPreemption();
    for (int i = 0; i < (int)junctions.size(); i++) {
        junctions[i].update(dt, smartMode);
        Vehicle* e = junctions[i].takePreemptEvent();
        if (e != NULL) {
            const char* axis = (e->roadId >= 0 && roads[e->roadId].approach < 2) ? "N-S" : "E-W";
            logEvent(EventType::Preempt, "Light J" + to_string(i) + " turned green early (" + axis + ") for " + e->getName(),
                     nodePos[i]);
        }
    }

    // 2. Release queue fronts on green, 3. move everything
    releaseVehicles();
    updateYielding();
    moveVehicles(dt);
    // vehicles queued outside the city are idling too
    for (Gate& gt : gates)
        for (Vehicle* v : gt.pending) idlePceSeconds += dt * v->getWeight();
    sampleMetrics(dt);
}

void CityGrid::syncGraphLoads() {
    for (Road& r : roads) {
        double load = r.reservedLoad;
        if (r.junction >= 0) load += junctions[r.junction].getLane(r.approach).getLoad();
        else for (Vehicle* c : r.cars) load += c->getWeight();
        // a breakdown blocks the only lane: sensors report it as twice over capacity (cost x9)
        if (r.incidentS >= 0 && smartMode) load = max(load, 2.0 * r.capacity);
        pathfinder.updateRoadLoad(r.from, r.to, load);
    }
}

void CityGrid::handlePlayerPreemption() {
    if (!player.active || !player.emergency || !smartMode) {
        playerPreemptJunction = -1;
        return;
    }
    Vec2 fwd(cosf(player.heading), sinf(player.heading));
    float along;
    int rid = locateRoad(player.pos, fwd, &along);
    if (rid < 0 || roads[rid].junction < 0) {
        playerPreemptJunction = -1;
        return;
    }
    Road& r = roads[rid];
    float dist = r.length - along;
    if (dist > Intersection::DETECT_RADIUS) return;
    junctions[r.junction].requestPreemption(r.approach < 2 ? 0 : 1);
    if (playerPreemptJunction != r.junction) {
        playerPreemptJunction = r.junction;
        logEvent(EventType::Preempt, "Light J" + to_string(r.junction) + " turned green for YOUR ambulance",
                 nodePos[r.junction]);
    }
}

void CityGrid::releaseVehicles() {
    for (Intersection& J : junctions) {
        for (int l = 0; l < 4; l++) {
            if (!J.isGreenFor(l)) continue;
            int axis = l < 2 ? 0 : 1;
            // the cross traffic must have left the box before this axis may enter
            if (committed[J.getId() * 2 + (1 - axis)] > 0) continue;
            Vehicle* front = J.getLane(l).peekFront();
            if (front == NULL || front->distToStop > RELEASE_DIST) continue;

            int nr = plannedNextRoad(front);
            if (nr < 0 || !hasRoom(roads[nr], front)) continue;   // don't block the box

            double wait = 0;
            J.releaseFront(l, &wait);
            recentWaits.push_back(make_pair(simTime, wait));
            front->released = true;
            front->boxSeq = ++releaseSeq;
            committed[J.getId() * 2 + axis]++;
            front->nextRoad = nr;
            roads[nr].reservedLoad += front->getWeight();
        }
    }
}

// Mode 2: cars pull towards the kerb when an emergency vehicle comes up
// behind them on the same road, and emergency vehicles drive near the centre.
void CityGrid::updateYielding() {
    for (Road& r : roads) {
        for (Vehicle* v : r.cars) {
            if (!smartMode) { v->lateralTarget = 0; continue; }
            if (v->isEmergency()) {
                // move towards the centre line only while overtaking the queue
                float target = 0;
                for (Vehicle* c : r.cars) {
                    float d = c->s - v->s;
                    if (!c->isEmergency() && d > 0 && d < YIELD_RANGE) { target = EMERGENCY_SHIFT; break; }
                }
                v->lateralTarget = target;
                continue;
            }
            float target = 0;
            for (Vehicle* e : r.cars) {
                if (!e->isEmergency()) continue;
                float d = v->s - e->s;
                if (d > 0 && d < YIELD_RANGE) { target = YIELD_SHIFT; break; }
            }
            if (target == 0 && player.active && player.emergency) {
                Vec2 rel = player.pos - r.start;
                float along = rel.dot(r.dir);
                float lat = fabs(rel.dot(r.normal));
                Vec2 pf(cosf(player.heading), sinf(player.heading));
                float d = v->s - along;
                if (lat < 20 && pf.dot(r.dir) > 0.6f && d > 0 && d < YIELD_RANGE) target = YIELD_SHIFT;
            }
            v->lateralTarget = target;
        }
    }
}

void CityGrid::rebuildBuckets() {
    for (auto& b : buckets) b.clear();
    for (Vehicle* v : vehicles) {
        int cx = (int)((v->pos.x + 100) / cellSize);
        int cy = (int)((v->pos.y + 100) / cellSize);
        if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) continue;
        buckets[cy * gridW + cx].push_back(v);
    }
}

int CityGrid::committedJunction(Vehicle* v) {
    if (v->state == MotionState::InJunction) return v->junction;
    if (v->state == MotionState::OnRoad && v->released) return roads[v->roadId].junction;
    return -1;
}

// Forward "raycast" sensor: nearest obstacle in the vehicle's corridor.
// Returns the bumper-to-bumper gap (or a large number) and the obstacle speed.
float CityGrid::sense(Vehicle* v, float& leaderSpeed) {
    Vec2 f(cosf(v->heading), sinf(v->heading));
    Vec2 n = f.leftNormal();
    float halfL = v->getLength() / 2, halfW = v->getWidth() / 2;
    float range = halfL + 75.0f;
    float best = 1e9f;
    leaderSpeed = 0;
    bool creeping = v->state == MotionState::InJunction && v->stuckTimer > 4.0;
    int myBox = committedJunction(v);
    // Mode 2: the queue ahead pulls over, so an emergency vehicle drives past it
    bool corridor = smartMode && v->isEmergency() && v->state == MotionState::OnRoad;

    auto consider = [&](Vec2 opos, float oheading, float olen, float owid, float ospeed) {
        Vec2 d = opos - v->pos;
        float along = d.dot(f);
        if (along <= 0 || along > range) return;
        Vec2 of(cosf(oheading), sinf(oheading));
        Vec2 on = of.leftNormal();
        float extLat = fabs(of.dot(n)) * olen / 2 + fabs(on.dot(n)) * owid / 2;
        float lat = fabs(d.dot(n));
        if (lat > halfW + extLat - 2.0f) return;
        // ignore traffic coming straight at us (opposite lane edge cases)
        if (of.dot(f) < -0.7f) return;
        float extAlong = fabs(of.dot(f)) * olen / 2 + fabs(on.dot(f)) * owid / 2;
        float gap = along - halfL - extAlong;
        if (gap < best) {
            best = gap;
            leaderSpeed = max(0.0f, ospeed * of.dot(f));
        }
    };

    // only vehicles we can actually run into: same road, our junction box, our exit road
    int outRoad = v->state == MotionState::InJunction ? paths[v->turnPath].outRoad : v->nextRoad;
    int boxJ = v->state == MotionState::InJunction ? v->junction : roads[v->roadId].junction;
    auto relevant = [&](Vehicle* o) {
        if (o->state == MotionState::OnRoad && o->roadId == v->roadId && v->state == MotionState::OnRoad) return true;
        if (v->state == MotionState::OnRoad && !v->released) return false;
        if (o->state == MotionState::InJunction && o->junction == boxJ) return true;
        return o->state == MotionState::OnRoad && o->roadId == outRoad;
    };

    Vec2 probe = v->pos + f * (range * 0.5f);
    int cx = (int)((probe.x + 100) / cellSize);
    int cy = (int)((probe.y + 100) / cellSize);
    for (int y = cy - 2; y <= cy + 2; y++) {
        for (int x = cx - 2; x <= cx + 2; x++) {
            if (x < 0 || y < 0 || x >= gridW || y >= gridH) continue;
            for (Vehicle* o : buckets[y * gridW + x]) {
                if (o == v) continue;
                if (!relevant(o)) continue;
                if (creeping && o->state == MotionState::InJunction) continue;
                // inside the same box, whoever was released first has right of way
                if (myBox >= 0 && o->boxSeq > v->boxSeq && committedJunction(o) == myBox) continue;
                if (corridor && !o->isEmergency() && o->state == MotionState::OnRoad && o->roadId == v->roadId) continue;
                consider(o->pos, o->heading, o->getLength(), o->getWidth(), o->speed);
            }
        }
    }
    if (player.active) {
        consider(player.pos, player.heading, player.halfLen * 2, player.halfWid * 2, player.speed);
    }
    return best;
}

void CityGrid::moveVehicles(double dt) {
    rebuildBuckets();
    float fdt = (float)dt;
    vector<Vehicle*> finished;

    for (Vehicle* v : vehicles) {
        float v0 = v->getMaxSpeed();
        float amax = v->isEmergency() ? 110.0f : (v->getKind() == VehicleKind::Bus ? 50.0f : 75.0f);
        float stopGap = 1e9f;

        if (v->state == MotionState::OnRoad) {
            Road& r = roads[v->roadId];
            v->distToStop = r.junction >= 0 ? r.length - v->s - v->getLength() / 2 : 1e9f;

            if (r.incidentS >= 0 && v->s < r.incidentS) {
                stopGap = min(stopGap, r.incidentS - v->s - v->getLength() / 2 - 14.0f);
            }
            if (r.junction >= 0 && !v->released) {
                stopGap = min(stopGap, v->distToStop);
                if (smartMode && !v->rerouteChecked && v->distToStop < 120) {
                    v->rerouteChecked = true;
                    maybeReroute(v);
                }
            }
            // slow down before turning
            int nr = r.junction >= 0 ? (v->released ? v->nextRoad : plannedNextRoad(v)) : -1;
            int turn = 0;
            if (nr >= 0 && pathIndex[v->roadId][nr] >= 0) turn = paths[pathIndex[v->roadId][nr]].turn;
            v->blinker = (r.junction >= 0 && v->distToStop < 110) ? turn : 0;
            if (turn != 0 && r.junction >= 0) v0 = min(v0, v0 * 0.5f + max(0.0f, v->distToStop) * 0.6f);
        } else if (v->state == MotionState::InJunction) {
            TurnPath& p = paths[v->turnPath];
            if (p.turn != 0) v0 *= 0.55f;
            v->blinker = p.turn;
        }

        float leaderSpeed = 0;
        float gap = sense(v, leaderSpeed);
        if (stopGap < gap) { gap = stopGap; leaderSpeed = 0; }
        bool hasLeader = gap < 1e8f;

        float a = idm(v->speed, v0, amax, gap, leaderSpeed, hasLeader);
        a = clampf(a, -450.0f, amax);
        v->accel = a;
        v->speed = max(0.0f, v->speed + a * fdt);
        v->lateral += clampf(v->lateralTarget - v->lateral, -25.0f * fdt, 25.0f * fdt);
        if (v->rerouteFlash > 0) v->rerouteFlash -= fdt;

        // waiting time bookkeeping
        if (v->speed < STOPPED_SPEED) {
            v->totalDelay += dt;
            idlePceSeconds += dt * v->getWeight();
            if (v->state == MotionState::OnRoad && !v->released) v->waitAtLight += dt;
            if (v->state == MotionState::InJunction) v->stuckTimer += dt;
        } else {
            v->stuckTimer = 0;
        }

        float ds = v->speed * fdt;
        if (v->state == MotionState::OnRoad) {
            Road& r = roads[v->roadId];
            v->s += ds;
            if (r.junction >= 0 && !v->released) {
                v->s = min(v->s, r.length - v->getLength() / 2);
            }
            float endS = r.junction >= 0 ? r.length : r.length + v->getLength();
            if (v->s >= endS) {
                float over = v->s - endS;
                r.cars.erase(find(r.cars.begin(), r.cars.end(), v));
                if (r.junction < 0) {
                    finished.push_back(v);
                    continue;
                }
                // enter the junction box
                v->state = MotionState::InJunction;
                v->junction = r.junction;
                v->turnPath = pathIndex[v->roadId][v->nextRoad];
                v->s = over;
                v->stuckTimer = 0;
            }
        } else if (v->state == MotionState::InJunction) {
            v->s += ds;
            TurnPath& p = paths[v->turnPath];
            if (v->s >= p.length) {
                float over = v->s - p.length;
                Road& next = roads[p.outRoad];
                next.reservedLoad = max(0.0, next.reservedLoad - v->getWeight());
                committed[p.junction * 2 + (roads[p.inRoad].approach < 2 ? 0 : 1)]--;
                v->routeIdx++;
                placeOnRoad(v, p.outRoad, over);
                continue;
            }
        }
        updatePosition(v);
    }

    for (Vehicle* v : finished) finishTrip(v);

    // gridlock watchdog (logged at most every 20 s)
    for (Vehicle* v : vehicles) {
        if (v->state == MotionState::InJunction && v->stuckTimer > 6.0 && simTime - lastGridlockLog > 20) {
            gridlockEvents++;
            lastGridlockLog = simTime;
            logEvent(EventType::Gridlock, "Traffic jam is blocking crossing J" + to_string(v->junction),
                     nodePos[v->junction]);
            break;
        }
    }
}

Vec2 CityGrid::samplePath(const TurnPath& p, float s, Vec2* tangent) {
    s = clampf(s, 0, p.length);
    int i = 1;
    while (i < (int)p.cum.size() - 1 && p.cum[i] < s) i++;
    float segLen = p.cum[i] - p.cum[i - 1];
    float t = segLen > 1e-5f ? (s - p.cum[i - 1]) / segLen : 0;
    Vec2 d = p.pts[i] - p.pts[i - 1];
    if (tangent) *tangent = d.normalized();
    return p.pts[i - 1] + d * t;
}

void CityGrid::updatePosition(Vehicle* v) {
    if (v->state == MotionState::OnRoad) {
        Road& r = roads[v->roadId];
        v->pos = r.start + r.dir * v->s + r.normal * v->lateral;
        v->heading = atan2f(r.dir.y, r.dir.x);
    } else if (v->state == MotionState::InJunction) {
        Vec2 tan;
        Vec2 p = samplePath(paths[v->turnPath], v->s, &tan);
        // fade the lateral offset out while crossing
        v->lateral *= 0.9f;
        v->pos = p + tan.leftNormal() * v->lateral;
        v->heading = atan2f(tan.y, tan.x);
    }
}

void CityGrid::finishTrip(Vehicle* v) {
    vehicles.erase(find(vehicles.begin(), vehicles.end(), v));
    tripsDone++;
    tripDelayTotal += v->totalDelay;
    recentExits.push_back(simTime);
    if (v->isEmergency()) {
        emergencyTrips++;
        emergencyDelayTotal += v->totalDelay;
        double trip = simTime - v->spawnTime;
        emergencyTimeTotal += trip;
        ostringstream os;
        os << fixed << setprecision(1) << v->getName() << " arrived in " << trip << " s (stood still "
           << v->totalDelay << " s)";
        logEvent(EventType::EmergencyDone, os.str(), v->pos);
    }
    delete v;
}

int CityGrid::locateRoad(Vec2 pos, Vec2 fwd, float* along) {
    int best = -1;
    float bestLat = 1e9f;
    for (Road& r : roads) {
        if (r.dir.dot(fwd) < 0.5f) continue;
        Vec2 rel = pos - r.start;
        float a = rel.dot(r.dir);
        if (a < -10 || a > r.length + 2) continue;
        float lat = fabs(rel.dot(r.normal));
        if (lat < LANE_OFFSET + 6 && lat < bestLat) {
            bestLat = lat;
            best = r.id;
            if (along) *along = a;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
void CityGrid::sampleMetrics(double dt) {
    while (!recentWaits.empty() && recentWaits.front().first < simTime - 60) recentWaits.pop_front();
    while (!recentExits.empty() && recentExits.front() < simTime - 60) recentExits.pop_front();

    sampleTimer += dt;
    if (sampleTimer >= 1.0) {
        sampleTimer -= 1.0;
        MetricSample m;
        m.time = simTime;
        m.avgWait = getRollingWaitTime();
        m.throughput = getThroughput();
        m.congestion = getCongestionIndex();
        m.co2 = getIdleCO2kg();
        m.active = (int)vehicles.size();
        history.push_back(m);
        if (history.size() > 600) history.erase(history.begin());
    }
}

void CityGrid::logEvent(EventType t, const string& text, Vec2 pos) {
    GridEvent e;
    e.seq = ++eventSeq;
    e.time = simTime;
    e.type = t;
    e.text = text;
    e.pos = pos;
    events.push_back(e);
    if (events.size() > 60) events.pop_front();
}

string CityGrid::nodeName(int node) {
    int J = (int)junctions.size();
    if (node < J) return "J" + to_string(node);
    int g = node - J;
    if (g < cols) return "N" + to_string(g + 1);
    if (g < 2 * cols) return "S" + to_string(g - cols + 1);
    if (g < 2 * cols + rows) return "W" + to_string(g - 2 * cols + 1);
    return "E" + to_string(g - 2 * cols - rows + 1);
}

// Computes total vehicles cleared across all junctions
int CityGrid::getTotalClearedVehicles() {
    int total = 0;
    for (int i = 0; i < (int)junctions.size(); i++) {
        total += junctions[i].getTotalCleared();
    }
    return total;
}

// Computes average wait time across all cleared vehicles
double CityGrid::getAverageWaitTime() {
    int totalCleared = getTotalClearedVehicles();
    if (totalCleared == 0) return 0.0;

    double totalWait = 0.0;
    for (int i = 0; i < (int)junctions.size(); i++) {
        totalWait += junctions[i].getTotalWait();
    }
    return totalWait / totalCleared;
}

double CityGrid::getRollingWaitTime() {
    if (recentWaits.empty()) return 0.0;
    double sum = 0;
    for (auto& w : recentWaits) sum += w.second;
    return sum / recentWaits.size();
}

double CityGrid::getThroughput() {
    double window = min(60.0, simTime);
    if (window < 1) return 0;
    return recentExits.size() * 60.0 / window;
}

double CityGrid::getCongestionIndex() {
    double load = 0, cap = 0;
    for (Intersection& J : junctions)
        for (int l = 0; l < 4; l++) {
            load += J.getLane(l).getLoad();
            cap += J.getLane(l).getCapacity();
        }
    return cap > 0 ? min(100.0, 100.0 * load / cap) : 0;
}

double CityGrid::getAverageTripDelay() {
    return tripsDone > 0 ? tripDelayTotal / tripsDone : 0;
}

bool CityGrid::createIncident(int roadId, double duration) {
    if (roadId < 0 || roadId >= (int)roads.size()) return false;
    Road& r = roads[roadId];
    if (r.junction < 0 || r.fromJunction < 0 || r.incidentS >= 0) return false;
    // put the breakdown just upstream of every vehicle already near the far end
    r.incidentS = r.length * 0.45f;
    r.incidentUntil = simTime + duration;
    logEvent(EventType::Incident, "A car broke down on road " + nodeName(r.from) + ">" + nodeName(r.to) + ", blocked for " +
             to_string((int)duration) + " s", r.start + r.dir * r.incidentS);
    return true;
}

int CityGrid::pickIncidentRoad(unsigned n) {
    vector<int> inner;
    for (Road& r : roads)
        if (r.junction >= 0 && r.fromJunction >= 0 && r.incidentS < 0) inner.push_back(r.id);
    if (inner.empty()) return -1;
    return inner[n % inner.size()];
}

int CityGrid::getActiveIncidents() {
    int n = 0;
    for (Road& r : roads) if (r.incidentS >= 0) n++;
    return n;
}

double CityGrid::getIdleCO2kg() {
    return idlePceSeconds * IDLE_CO2_G_PER_PCE_S / 1000.0;
}

double CityGrid::getEmergencyAvgDelay() {
    return emergencyTrips > 0 ? emergencyDelayTotal / emergencyTrips : 0;
}

int CityGrid::getTotalPreemptions() {
    int total = 0;
    for (Intersection& J : junctions) total += J.getPreemptions();
    return total;
}

int CityGrid::getPendingVehicles() {
    int total = 0;
    for (Gate& g : gates) total += (int)g.pending.size();
    return total;
}

// Prints a live terminal summary of all intersections and metrics
void CityGrid::printStatus() {
    cout << "\n=======================================================\n";
    cout << " CITY GRID SIMULATION | Time: " << fixed << setprecision(1) << simTime << "s\n";
    cout << " Mode: " << (smartMode ? "[MODE 2: SMART ADAPTIVE]" : "[MODE 1: STATIC FIXED]") << "\n";
    cout << " Cleared: " << getTotalClearedVehicles()
         << " | Avg Wait: " << getAverageWaitTime() << "s"
         << " | Trips: " << tripsDone
         << " | Throughput: " << getThroughput() << "/min\n";
    cout << " Emergency trips: " << emergencyTrips << " | Emergency stopped time: " << getEmergencyAvgDelay()
         << "s | Preemptions: " << getTotalPreemptions() << " | Reroutes: " << reroutes << "\n";
    cout << "-------------------------------------------------------\n";

    for (int i = 0; i < (int)junctions.size(); i++) {
        cout << " Junction " << i << ": ";
        cout << (junctions[i].getGreenAxis() == 0 ? "GREEN: N-S" : "GREEN: E-W");
        if (junctions[i].isYellow()) cout << " [YELLOW]";
        if (junctions[i].isPreempted()) cout << " [EMERGENCY PREEMPTION]";
        cout << "  |";
        for (int l = 0; l < 4; l++) {
            Lane& lane = junctions[i].getLane(l);
            cout << " " << lane.getName()[0] << ":" << lane.size() << "(" << setprecision(1) << lane.getLoad() << ")";
        }
        cout << "\n";
    }
    cout << "=======================================================\n";
}
