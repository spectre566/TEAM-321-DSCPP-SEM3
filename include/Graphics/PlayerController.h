#ifndef PLAYERCONTROLLER_H
#define PLAYERCONTROLLER_H

#include "Core/CityGrid.h"

// One raycast collision sensor on the player's car.
struct SensorRay {
    float angle;       // relative to the car's heading (radians)
    float length;      // max range (px)
    float hit;         // distance to the first obstacle (== length when clear)
    int hitType;       // 0 none, 1 vehicle, 2 building / kerb
};

struct DriverInput {
    bool up, down, left, right, handbrake;
    DriverInput() : up(false), down(false), left(false), right(false), handbrake(false) {}
};

/*
 * PlayerController: arcade top-down driving physics for the player's vehicle.
 * Acceleration and velocity are vectors (the car can drift when the
 * handbrake is pulled), five raycast sensors scan ahead for obstacles,
 * and the brake-assist uses them to avoid collisions.
 */
class PlayerController {
public:
    enum class Type { Car, Ambulance };

private:
    Type type;
    Vec2 pos;
    Vec2 vel;
    float heading;
    float steer;              // smoothed steering input -1..1
    bool siren;
    bool assist;
    bool braking;
    SensorRay sensors[5];
    int collisions;
    int violations;
    float collisionFlash;
    float violationFlash;
    float assistFlash;
    float collideCooldown;
    float distance;           // odometer (px)
    int lastRoad;
    float lastStopDist;

    void castSensors(CityGrid& grid);
    float castRay(CityGrid& grid, Vec2 origin, Vec2 dir, float maxLen, int& type);
    void resolveCollisions(CityGrid& grid);
    void checkRedLight(CityGrid& grid);

public:
    static constexpr int NUM_SENSORS = 5;

    PlayerController();
    void reset(Vec2 p, float headingRad);
    void update(double dt, CityGrid& grid, const DriverInput& in);

    PlayerProbe probe();
    void toggleType();
    void toggleSiren() { if (type == Type::Ambulance) siren = !siren; }
    void toggleAssist() { assist = !assist; }

    Type getType() { return type; }
    Vec2 getPos() { return pos; }
    float getHeading() { return heading; }
    float getSpeed();                    // forward speed px/s (negative = reversing)
    float getSpeedKmh() { return getSpeed() * 0.72f; }
    float getLength() { return type == Type::Ambulance ? 25.0f : 22.0f; }
    float getWidth() { return type == Type::Ambulance ? 12.0f : 11.0f; }
    bool isSirenOn() { return siren && type == Type::Ambulance; }
    bool isAssistOn() { return assist; }
    bool isBraking() { return braking; }
    const SensorRay& getSensor(int i) { return sensors[i]; }
    float nearestObstacle();
    int getCollisions() { return collisions; }
    int getViolations() { return violations; }
    float getCollisionFlash() { return collisionFlash; }
    float getViolationFlash() { return violationFlash; }
    float getAssistFlash() { return assistFlash; }
    float getDistanceKm() { return distance * 0.2f / 1000.0f; }
};

#endif
