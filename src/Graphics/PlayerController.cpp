#include "Graphics/PlayerController.h"
#include <algorithm>

namespace {
const float PI = 3.14159265f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Oriented box used for collision tests
struct Box {
    Vec2 c, ax, ay;   // centre, forward axis, right axis
    float hx, hy;
};

Box makeBox(Vec2 c, float heading, float hx, float hy) {
    Box b;
    b.c = c;
    b.ax = Vec2(cosf(heading), sinf(heading));
    b.ay = Vec2(-b.ax.y, b.ax.x);
    b.hx = hx;
    b.hy = hy;
    return b;
}

float projectRadius(const Box& b, Vec2 axis) {
    return fabs(b.ax.dot(axis)) * b.hx + fabs(b.ay.dot(axis)) * b.hy;
}

// Separating Axis Theorem: returns true on overlap and the push-out vector for a
bool overlapSAT(const Box& a, const Box& b, Vec2& mtv) {
    Vec2 axes[4] = {a.ax, a.ay, b.ax, b.ay};
    float best = 1e9f;
    Vec2 d = b.c - a.c;
    for (Vec2 ax : axes) {
        float dist = fabs(d.dot(ax));
        float overlap = projectRadius(a, ax) + projectRadius(b, ax) - dist;
        if (overlap <= 0) return false;
        if (overlap < best) {
            best = overlap;
            mtv = ax * (d.dot(ax) > 0 ? -overlap : overlap);
        }
    }
    return true;
}

// Ray vs oriented box (slab method in the box's local frame)
float rayBox(Vec2 o, Vec2 dir, const Box& b) {
    Vec2 rel = o - b.c;
    float ox = rel.dot(b.ax), oy = rel.dot(b.ay);
    float dx = dir.dot(b.ax), dy = dir.dot(b.ay);
    float tmin = -1e9f, tmax = 1e9f;
    float org[2] = {ox, oy}, dd[2] = {dx, dy}, h[2] = {b.hx, b.hy};
    for (int i = 0; i < 2; i++) {
        if (fabs(dd[i]) < 1e-6f) {
            if (org[i] < -h[i] || org[i] > h[i]) return -1;
        } else {
            float t1 = (-h[i] - org[i]) / dd[i];
            float t2 = (h[i] - org[i]) / dd[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1;
        }
    }
    if (tmax < 0) return -1;
    return tmin >= 0 ? tmin : 0;
}

// Ray vs axis aligned rectangle
float rayRect(Vec2 o, Vec2 dir, const Rect& r) {
    Box b;
    b.c = Vec2(r.x + r.w / 2, r.y + r.h / 2);
    b.ax = Vec2(1, 0);
    b.ay = Vec2(0, 1);
    b.hx = r.w / 2;
    b.hy = r.h / 2;
    return rayBox(o, dir, b);
}
}

PlayerController::PlayerController() {
    type = Type::Car;
    assist = true;
    float angles[5] = {-60, -25, 0, 25, 60};
    float lens[5] = {90, 120, 150, 120, 90};
    for (int i = 0; i < NUM_SENSORS; i++) {
        sensors[i].angle = angles[i] * PI / 180.0f;
        sensors[i].length = lens[i];
        sensors[i].hit = lens[i];
        sensors[i].hitType = 0;
    }
    reset(Vec2(300, 192), 0);
}

void PlayerController::reset(Vec2 p, float headingRad) {
    pos = p;
    vel = Vec2();
    heading = headingRad;
    steer = 0;
    siren = false;
    braking = false;
    collisions = 0;
    violations = 0;
    collisionFlash = violationFlash = assistFlash = 0;
    collideCooldown = 0;
    distance = 0;
    lastRoad = -1;
    lastStopDist = 1e9f;
}

void PlayerController::toggleType() {
    type = (type == Type::Car) ? Type::Ambulance : Type::Car;
    siren = (type == Type::Ambulance);
}

float PlayerController::getSpeed() {
    return vel.dot(Vec2(cosf(heading), sinf(heading)));
}

// Distance to the nearest obstacle that is actually in the car's path:
// a hit only counts when it lies within the car's width (plus a margin).
float PlayerController::nearestObstacle() {
    float best = 1e9f;
    for (int i = 0; i < NUM_SENSORS; i++) {
        if (sensors[i].hitType == 0) continue;
        float ahead = sensors[i].hit * cosf(sensors[i].angle);
        float side = fabs(sensors[i].hit * sinf(sensors[i].angle));
        if (ahead > 0 && side < getWidth() / 2 + 5) best = std::min(best, ahead);
    }
    return best;
}

PlayerProbe PlayerController::probe() {
    PlayerProbe p;
    p.active = true;
    p.pos = pos;
    p.heading = heading;
    p.halfLen = getLength() / 2;
    p.halfWid = getWidth() / 2;
    p.speed = std::max(0.0f, getSpeed());
    p.emergency = isSirenOn();
    return p;
}

void PlayerController::update(double dtd, CityGrid& grid, const DriverInput& in) {
    float dt = (float)dtd;
    bool amb = type == Type::Ambulance;
    float maxSpeed = amb ? 165.0f : 150.0f;
    float engine = amb ? 150.0f : 140.0f;
    float brakeForce = 280.0f;

    Vec2 fwd(cosf(heading), sinf(heading));
    Vec2 right(-fwd.y, fwd.x);
    float vf = vel.dot(fwd);
    float vr = vel.dot(right);
    braking = false;

    // --- longitudinal (acceleration vector along the heading) ---
    if (in.up) {
        if (vf < maxSpeed) vf += engine * (1.0f - 0.6f * vf / maxSpeed) * dt;
    } else if (in.down) {
        if (vf > 5) { vf -= brakeForce * dt; braking = true; }
        else if (vf > -55) vf -= engine * 0.6f * dt;
    } else {
        float roll = 45.0f * dt;
        vf = fabs(vf) < roll ? 0 : vf - (vf > 0 ? roll : -roll);
    }

    // --- brake assist driven by the raycast sensors ---
    castSensors(grid);
    float obstacle = nearestObstacle();
    if (assist && vf > 20) {
        float stopping = vf * vf / (2 * brakeForce) + 10;
        if (obstacle < stopping) {
            vf = std::max(0.0f, vf - brakeForce * 1.2f * dt);
            braking = true;
            assistFlash = 0.4f;
        }
    }

    if (in.handbrake) {
        vf *= expf(-2.2f * dt);
        braking = true;
    }

    // --- steering (turn rate scales with speed, flips when reversing) ---
    float target = (in.left ? -1.0f : 0.0f) + (in.right ? 1.0f : 0.0f);
    steer += clampf(target - steer, -6.0f * dt, 6.0f * dt);
    float turnRate = 2.9f * clampf(vf / 70.0f, -1.0f, 1.0f);
    if (in.handbrake) turnRate *= 1.5f;
    heading += steer * turnRate * dt;

    // --- lateral grip: the sideways part of the velocity decays (drift with handbrake) ---
    float grip = in.handbrake ? 1.8f : 14.0f;
    vr *= expf(-grip * dt);

    fwd = Vec2(cosf(heading), sinf(heading));
    right = Vec2(-fwd.y, fwd.x);
    vel = fwd * vf + right * vr;
    Vec2 step = vel * dt;
    pos += step;
    distance += step.length();

    resolveCollisions(grid);
    checkRedLight(grid);

    collisionFlash = std::max(0.0f, collisionFlash - dt);
    violationFlash = std::max(0.0f, violationFlash - dt);
    assistFlash = std::max(0.0f, assistFlash - dt);
    collideCooldown = std::max(0.0f, collideCooldown - dt);
}

float PlayerController::castRay(CityGrid& grid, Vec2 origin, Vec2 dir, float maxLen, int& type) {
    float best = maxLen;
    type = 0;
    for (Vehicle* v : grid.getVehicles()) {
        Vec2 d = v->pos - origin;
        if (d.length() > maxLen + 30) continue;
        Box b = makeBox(v->pos, v->heading, v->getLength() / 2, v->getWidth() / 2);
        float t = rayBox(origin, dir, b);
        if (t >= 0 && t < best) { best = t; type = 1; }
    }
    for (const Rect& r : grid.getBlocks()) {
        float t = rayRect(origin, dir, r);
        if (t >= 0 && t < best) { best = t; type = 2; }
    }
    return best;
}

void PlayerController::castSensors(CityGrid& grid) {
    Vec2 fwd(cosf(heading), sinf(heading));
    Vec2 nose = pos + fwd * (getLength() / 2);
    for (int i = 0; i < NUM_SENSORS; i++) {
        float a = heading + sensors[i].angle;
        Vec2 dir(cosf(a), sinf(a));
        int t;
        sensors[i].hit = castRay(grid, nose, dir, sensors[i].length, t);
        sensors[i].hitType = t;
    }
}

void PlayerController::resolveCollisions(CityGrid& grid) {
    bool hit = false;
    float hx = getLength() / 2, hy = getWidth() / 2;
    float impactSpeed = vel.length();

    // buildings / kerbs
    for (const Rect& r : grid.getBlocks()) {
        Box me = makeBox(pos, heading, hx, hy);
        Box blk;
        blk.c = Vec2(r.x + r.w / 2, r.y + r.h / 2);
        blk.ax = Vec2(1, 0);
        blk.ay = Vec2(0, 1);
        blk.hx = r.w / 2;
        blk.hy = r.h / 2;
        Vec2 mtv;
        if (overlapSAT(me, blk, mtv)) {
            pos += mtv;
            Vec2 n = mtv.normalized();
            float vn = vel.dot(n);
            if (vn < 0) vel = vel - n * (1.4f * vn);   // bounce off with some energy loss
            vel = vel * 0.7f;
            hit = true;
        }
    }

    // AI vehicles (they are heavier: only the player is pushed)
    for (Vehicle* v : grid.getVehicles()) {
        if ((v->pos - pos).length() > 40) continue;
        Box me = makeBox(pos, heading, hx, hy);
        Box other = makeBox(v->pos, v->heading, v->getLength() / 2, v->getWidth() / 2);
        Vec2 mtv;
        if (overlapSAT(me, other, mtv)) {
            pos += mtv;
            Vec2 n = mtv.normalized();
            float vn = vel.dot(n);
            if (vn < 0) vel = vel - n * (1.3f * vn);
            vel = vel * 0.6f;
            hit = true;
        }
    }

    // stay inside the map
    float W = grid.getWorldWidth(), H = grid.getWorldHeight();
    if (pos.x < 5) { pos.x = 5; vel.x = fabs(vel.x) * 0.3f; }
    if (pos.y < 5) { pos.y = 5; vel.y = fabs(vel.y) * 0.3f; }
    if (pos.x > W - 5) { pos.x = W - 5; vel.x = -fabs(vel.x) * 0.3f; }
    if (pos.y > H - 5) { pos.y = H - 5; vel.y = -fabs(vel.y) * 0.3f; }

    if (hit && collideCooldown <= 0 && impactSpeed > 25) {
        collisions++;
        collisionFlash = 0.6f;
        collideCooldown = 0.8f;
    }
}

// Detects the player crossing a stop line while the signal shows red
void PlayerController::checkRedLight(CityGrid& grid) {
    Vec2 fwd(cosf(heading), sinf(heading));
    float along = 0;
    int rid = grid.locateRoad(pos, fwd, &along);
    if (rid < 0 || grid.getRoads()[rid].junction < 0) {
        lastRoad = -1;
        return;
    }
    Road& r = grid.getRoads()[rid];
    float d = r.length - along - getLength() / 2;
    if (rid == lastRoad && lastStopDist > 0 && d <= 0 && getSpeed() > 15 && !isSirenOn()) {
        if (!grid.getIntersection(r.junction).isGreenFor(r.approach)) {
            violations++;
            violationFlash = 1.2f;
        }
    }
    lastRoad = rid;
    lastStopDist = d;
}
