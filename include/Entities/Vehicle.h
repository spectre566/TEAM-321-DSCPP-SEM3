#ifndef VEHICLE_H
#define VEHICLE_H

#include <string>
#include <vector>
#include <cmath>
using namespace std;

// ---------------------------------------------------------------------------
// Small 2D vector used by the simulation engine (kept free of SFML so the
// engine can run headless for tests and benchmarks).
// ---------------------------------------------------------------------------
struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(float k) const { return Vec2(x * k, y * k); }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    float cross(const Vec2& o) const { return x * o.y - y * o.x; }
    float length() const { return sqrt(x * x + y * y); }
    Vec2 normalized() const { float l = length(); return l > 1e-6f ? Vec2(x / l, y / l) : Vec2(); }
    // Left-hand normal in screen space (y points down). India drives on the LEFT,
    // so a lane sits on the left side of its direction of travel.
    Vec2 leftNormal() const { return Vec2(y, -x); }
};

// Used by the renderer to pick a sprite (polymorphic getKind()).
enum class VehicleKind { Car, Bus, AutoRickshaw, Ambulance, FireTruck, Police };

// Where the vehicle currently is in the network.
enum class MotionState { OnRoad, InJunction, Done };

// ---------------------------------------------------------------------------
// Abstract base class. Cannot be instantiated directly (getName is pure virtual).
// Lower priority number = more important (used by the Min-Heap in Lane).
// ---------------------------------------------------------------------------
class Vehicle {
protected:
    int id;
    int priority;
    double arrivalTime;   // time the vehicle joined its current lane queue
    bool emergency;

public:
    // ---- physical state, written by CityGrid each tick, read by the Renderer ----
    MotionState state;
    Vec2 pos;             // world position (centre of vehicle)
    float heading;        // radians, 0 = +x (east)
    float speed;          // px / s
    float accel;          // last applied acceleration (for brake lights)
    float lateral;        // current sideways offset from lane centre (yielding)
    float lateralTarget;

    int roadId;           // road the vehicle is on (OnRoad)
    float s;              // distance travelled along road or junction path
    float distToStop;     // distance from the front bumper to the stop line
    bool released;       // got the green from the intersection at the end of its road
    int junction;         // junction being crossed (InJunction)
    int turnPath;         // index of the path inside that junction
    int nextRoad;         // road taken after the junction
    int blinker;          // -1 left, 0 none, +1 right
    bool rerouteChecked;  // second (near stop-line) reroute check done
    long long boxSeq;     // release order: earlier released vehicles have right of way in the box

    vector<int> route;    // node ids from Dijkstra, route[routeIdx] = current target node
    int routeIdx;
    int entryGate, exitGate;

    double waitAtLight;   // stopped time at the current signal (reset on release)
    double totalDelay;    // all stopped time over the whole trip
    double stuckTimer;    // time stopped inside a junction box (deadlock breaker)
    double spawnTime;
    int colorSeed;        // cosmetic variation
    float rerouteFlash;   // >0 shortly after a Dijkstra reroute

    Vehicle(int id, int priority, bool emergency);
    virtual ~Vehicle() {}

    virtual string getName() = 0;            // pure virtual
    virtual string getTypeName() = 0;        // "Car", "Bus", ...
    virtual VehicleKind getKind() = 0;
    virtual double getWeight();              // Passenger Car Equivalent (PCE)
    virtual float getMaxSpeed();             // px / s
    virtual float getLength();               // px
    virtual float getWidth();                // px

    int getId();
    int getPriority();
    double getArrivalTime();
    bool isEmergency();
    void setArrivalTime(double t);
};

class PrivateCar : public Vehicle {
public:
    PrivateCar(int id);
    string getName();
    string getTypeName();
    VehicleKind getKind();
};

class PublicBus : public Vehicle {
private:
    int passengers;
public:
    PublicBus(int id, int passengers);
    string getName();
    string getTypeName();
    VehicleKind getKind();
    double getWeight();
    float getMaxSpeed();
    float getLength();
    float getWidth();
    int getPassengers();
};

class AutoRickshaw : public Vehicle {
public:
    AutoRickshaw(int id);
    string getName();
    string getTypeName();
    VehicleKind getKind();
    double getWeight();
    float getMaxSpeed();
    float getLength();
    float getWidth();
};

class EmergencyVehicle : public Vehicle {
private:
    string kind;     // "Ambulance", "FireTruck" or "Police"
public:
    EmergencyVehicle(int id, string kind);
    string getName();
    string getTypeName();
    VehicleKind getKind();
    double getWeight();
    float getMaxSpeed();
    float getLength();
    float getWidth();
};

#endif
