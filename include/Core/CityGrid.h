#ifndef CITYGRID_H
#define CITYGRID_H

#include <vector>
#include <deque>
#include <string>
#include <random>
#include <iostream>
#include "Entities/Intersection.h"
#include "Core/Pathfinder.h"

using namespace std;

// Axis-aligned rectangle (city blocks the roads run between)
struct Rect {
    float x, y, w, h;
    Rect() : x(0), y(0), w(0), h(0) {}
    Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}
    bool contains(Vec2 p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; }
};

// One directed road (= one lane, India drives on the left).
struct Road {
    int id;
    int from, to;            // graph nodes
    Vec2 start, end;         // lane centre line
    Vec2 dir, normal;        // travel direction and left normal
    float length;
    double capacity;         // PCE that physically fit on the road
    int junction;            // junction at the END of the road (-1 = leaves the city)
    int approach;            // lane index at that junction (0 N, 1 S, 2 E, 3 W)
    int fromJunction;        // junction at the START (-1 = enters the city)
    vector<Vehicle*> cars;   // vehicles physically on the road
    double reservedLoad;     // PCE already released towards this road (inside a junction)
    float incidentS;         // position of a broken-down vehicle blocking the lane (-1 = none)
    double incidentUntil;    // sim time when the tow truck clears it
};

// Path a vehicle follows inside a junction box (a sampled Bezier curve).
struct TurnPath {
    int junction;
    int inRoad, outRoad;
    int turn;                // -1 left, 0 straight, +1 right
    vector<Vec2> pts;
    vector<float> cum;       // cumulative length at each point
    float length;
};

// City gate = where traffic enters / leaves the map.
struct Gate {
    int node;
    int inRoad;              // road from the gate into the city
    int outRoad;             // road from the city out to the gate
    int opposite;            // gate on the other side of the city
    double weight;           // relative demand
    double nextArrival;
    deque<Vehicle*> pending; // vehicles waiting to enter (spill-back outside the map)
};

enum class EventType { Preempt, Reroute, EmergencySpawn, EmergencyDone, Gridlock, Incident, Info };

struct GridEvent {
    long long seq;
    double time;
    EventType type;
    string text;
    Vec2 pos;
};

struct MetricSample {
    double time;
    double avgWait;          // rolling 60 s average wait per signal crossing
    double throughput;       // vehicles leaving the city per minute
    double congestion;       // % of lane capacity in use
    double co2;              // cumulative idle CO2 (kg)
    int active;
};

// The player's car, so AI traffic can see (and yield to) it.
struct PlayerProbe {
    bool active;
    Vec2 pos;
    float heading;
    float halfLen, halfWid;
    float speed;
    bool emergency;          // driving an ambulance with the siren on
    PlayerProbe() : active(false), heading(0), halfLen(11), halfWid(5.5f), speed(0), emergency(false) {}
};

/*
 * CityGrid manages the network of intersections and road connections.
 * It links physical lane queues with the Pathfinder graph, moves every
 * vehicle each tick, and tracks overall traffic metrics across the city.
 */
class CityGrid {
private:
    int rows, cols;
    vector<Intersection> junctions;
    Pathfinder pathfinder;          // Graph and Dijkstra routing engine
    bool smartMode;                 // true = Mode 2 (Adaptive), false = Mode 1 (Static)
    double simTime;                 // Elapsed simulation time in seconds

    vector<Vec2> nodePos;           // junctions first, then gates
    vector<vector<int>> roadIndex;  // roadIndex[u][v] -> road id or -1
    vector<Road> roads;
    vector<TurnPath> paths;
    vector<vector<int>> pathIndex;  // pathIndex[inRoad][outRoad]
    vector<Gate> gates;
    vector<Rect> blocks;
    float worldW, worldH;

    vector<Vehicle*> vehicles;
    int nextId;
    unsigned seed;
    mt19937 rng;
    double demand;
    double nextEmergency;
    PlayerProbe player;
    vector<int> committed;          // per junction and axis: released vehicles not yet out of the box
    long long releaseSeq;

    // spatial hash for the forward sensors
    float cellSize;
    int gridW, gridH;
    vector<vector<Vehicle*>> buckets;

    // metrics
    deque<pair<double, double>> recentWaits;   // (time, wait) of signal crossings
    deque<double> recentExits;
    int tripsDone;
    double tripDelayTotal;
    int emergencyTrips;
    double emergencyDelayTotal;
    double emergencyTimeTotal;
    int reroutes;
    double idlePceSeconds;          // engine-idling time weighted by vehicle size
    int gridlockEvents;
    double lastGridlockLog;
    double sampleTimer;
    vector<MetricSample> history;
    deque<GridEvent> events;
    long long eventSeq;
    int playerPreemptJunction;

    void buildRoads();
    void addRoadPair(int u, int v);
    void addRoad(int u, int v);
    void buildTurnPaths();
    void buildBlocks();

    Vehicle* makeVehicle(double r);
    void planRoute(Vehicle* v, int gateIdx, int exitIdx);
    void spawnArrivals(double dt);
    void injectPending();
    bool hasRoom(Road& r, Vehicle* v);
    void placeOnRoad(Vehicle* v, int roadId, float s);
    int plannedNextRoad(Vehicle* v);
    void maybeReroute(Vehicle* v);
    void releaseVehicles();
    void updateYielding();
    void moveVehicles(double dt);
    void rebuildBuckets();
    float sense(Vehicle* v, float& leaderSpeed);
    int committedJunction(Vehicle* v);
    void updatePosition(Vehicle* v);
    void finishTrip(Vehicle* v);
    void syncGraphLoads();
    void handlePlayerPreemption();
    void sampleMetrics(double dt);
    void logEvent(EventType t, const string& text, Vec2 pos);
    string nodeName(int node);

public:
    static constexpr float LANE_OFFSET = 8.0f;    // lane centre from road centre
    static constexpr float ROAD_HALF = 16.0f;     // half width of a two-lane road
    static constexpr float SIDEWALK = 7.0f;
    static constexpr float STOP_DIST = 30.0f;     // stop line distance from junction centre
    static constexpr float SPACING = 300.0f;      // distance between junctions
    static constexpr float MARGIN = 200.0f;       // map edge to first junction

    CityGrid(int rows = 3, int cols = 3, bool smart = true, unsigned seed = 2026);
    ~CityGrid();
    CityGrid(const CityGrid&) = delete;
    CityGrid& operator=(const CityGrid&) = delete;

    // Inline small getters & setters
    bool isSmartMode() { return smartMode; }
    void setSmartMode(bool mode) { smartMode = mode; }
    double getSimTime() { return simTime; }
    int getNumIntersections() { return (int)junctions.size(); }
    int getRows() { return rows; }
    int getCols() { return cols; }
    float getWorldWidth() { return worldW; }
    float getWorldHeight() { return worldH; }
    double getDemand() { return demand; }
    void setDemand(double d) { demand = d; }

    Intersection& getIntersection(int id) { return junctions[id]; }
    Pathfinder& getPathfinder() { return pathfinder; }
    vector<Road>& getRoads() { return roads; }
    vector<TurnPath>& getPaths() { return paths; }
    vector<Gate>& getGates() { return gates; }
    vector<Rect>& getBlocks() { return blocks; }
    vector<Vehicle*>& getVehicles() { return vehicles; }
    deque<GridEvent>& getEvents() { return events; }
    vector<MetricSample>& getHistory() { return history; }
    Vec2 getNodePos(int node) { return nodePos[node]; }
    int getNumNodes() { return (int)nodePos.size(); }
    bool isGateNode(int node) { return node >= (int)junctions.size(); }
    int getRoadIndex(int u, int v) { return roadIndex[u][v]; }
    void setPlayerProbe(const PlayerProbe& p) { player = p; }
    string getNodeName(int node) { return nodeName(node); }

    // Core logic routines implemented in CityGrid.cpp
    void buildDefaultGrid();
    void reset();
    bool addVehicleToGrid(int junctionId, int laneId, Vehicle* v);
    bool spawnEmergency(int kind, int gateIdx);   // 0 ambulance, 1 fire truck, 2 police
    bool createIncident(int roadId, double duration);   // block a lane (breakdown) for a while
    int pickIncidentRoad(unsigned r);                     // an inner road, chosen from a number
    int getActiveIncidents();
    void update(double dt);

    // Road the point is on (driving in direction fwd), or -1.
    int locateRoad(Vec2 pos, Vec2 fwd, float* along);
    Vec2 samplePath(const TurnPath& p, float s, Vec2* tangent);

    int getTotalClearedVehicles();
    double getAverageWaitTime();        // all-time average wait per signal crossing
    double getRollingWaitTime();        // last 60 s
    double getThroughput();             // vehicles per minute (last 60 s)
    double getCongestionIndex();        // % of lane capacity occupied
    double getAverageTripDelay();
    double getIdleCO2kg();              // estimated CO2 from idling engines (incl. queues at the gates)
    double getIdleHours() { return idlePceSeconds / 3600.0; }
    unsigned getSeed() { return seed; }
    double getEmergencyAvgDelay();      // stopped time per emergency trip
    int getEmergencyTrips() { return emergencyTrips; }
    int getTripsDone() { return tripsDone; }
    int getReroutes() { return reroutes; }
    int getGridlockEvents() { return gridlockEvents; }
    int getTotalPreemptions();
    int getPendingVehicles();
    int getActiveVehicles() { return (int)vehicles.size(); }
    void printStatus();
};

#endif
