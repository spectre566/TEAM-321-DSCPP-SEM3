#include "Entities/Intersection.h"

using namespace std;

Intersection::Intersection() {
    id = 0;
    // capacities (in PCE) are set by CityGrid from the real road lengths
    lanes[0] = Lane("North", 9);
    lanes[1] = Lane("South", 9);
    lanes[2] = Lane("East",  9);
    lanes[3] = Lane("West",  9);
    reset();
}

void Intersection::reset() {
    for (int i = 0; i < 4; i++) lanes[i].clear();
    signal.reset(0);       // North-South (axis 0) starts green
    clock = 0.0;
    totalCleared = 0;
    emergencyCleared = 0;
    totalWait = 0.0;
    emergencyWait = 0.0;
    preemptions = 0;
    externalAxis = -1;
    externalTimer = 0;
    lastPreemptId = -1;
    preemptHold = 0;
    pendingEvent = NULL;
}

int Intersection::axisOf(int laneIndex) {
    // Lanes 0 (North) and 1 (South) are Axis 0; Lanes 2 (East) and 3 (West) are Axis 1
    return (laneIndex < 2) ? 0 : 1;
}

double Intersection::axisLoad(int axis) {
    if (axis == 0) {
        return lanes[0].getLoad() + lanes[1].getLoad();
    }
    return lanes[2].getLoad() + lanes[3].getLoad();
}

bool Intersection::axisHasEmergency(int axis) {
    if (axis == 0) {
        return lanes[0].hasEmergency() || lanes[1].hasEmergency();
    }
    return lanes[2].hasEmergency() || lanes[3].hasEmergency();
}

// Looks at the top of every lane's Min-Heap (O(1) each) and preempts the
// signal for the most important emergency vehicle that is inside the radius.
void Intersection::checkPreemption(double dt) {
    CompareVehicle cmp;
    Vehicle* best = NULL;
    int bestLane = -1;

    for (int l = 0; l < 4; l++) {
        Vehicle* e = lanes[l].topEmergency();
        if (e == NULL || e->distToStop > DETECT_RADIUS) continue;
        if (e->getId() == lastPreemptId && preemptHold > MAX_HOLD) continue;   // stuck: give up
        if (best == NULL || cmp(best, e)) {   // cmp(a,b) == true -> b is more important
            best = e;
            bestLane = l;
        }
    }

    if (best != NULL) {
        signal.triggerEmergency(axisOf(bestLane));
        preemptHold += dt;
        if (best->getId() != lastPreemptId) {
            lastPreemptId = best->getId();
            preemptHold = 0;
            preemptions++;
            pendingEvent = best;
        }
    } else if (externalAxis >= 0) {
        signal.triggerEmergency(externalAxis);
    } else if (signal.getIsPreempted()) {
        signal.clearPreemption();
    }

    // Proactively shuffle emergency vehicles to the front of their queues
    for (int l = 0; l < 4; l++) {
        lanes[l].moveEmergencyToFront();
    }
}

bool Intersection::addVehicle(int laneIndex, Vehicle* v) {
    if (laneIndex < 0 || laneIndex >= 4 || v == NULL) {
        return false;
    }
    return lanes[laneIndex].addVehicle(v, clock);
}

void Intersection::update(double dt, bool smartMode) {
    clock += dt;

    if (externalTimer > 0) {
        externalTimer -= dt;
        if (externalTimer <= 0) externalAxis = -1;
    }

    // Mode 2 only: Min-Heap preemption. Mode 1 behaves like a legacy controller
    // that has no idea an ambulance is stuck in the queue.
    if (smartMode) {
        checkPreemption(dt);
    } else if (signal.getIsPreempted()) {
        signal.clearPreemption();
    }

    signal.update(dt, axisLoad(0), axisLoad(1), smartMode);
}

bool Intersection::isGreenFor(int laneIndex) {
    return signal.isGreenFor(axisOf(laneIndex));
}

// Releases the vehicle at the front of the queue and records its wait time
Vehicle* Intersection::releaseFront(int laneIndex, double* waitOut) {
    Vehicle* v = lanes[laneIndex].removeFront();
    if (v == NULL) return NULL;

    double wait = v->waitAtLight;
    totalWait += wait;
    totalCleared++;
    if (v->isEmergency()) {
        emergencyCleared++;
        emergencyWait += wait;
    }
    v->waitAtLight = 0;
    if (waitOut) *waitOut = wait;
    return v;
}

void Intersection::requestPreemption(int axis) {
    externalAxis = axis;
    externalTimer = 0.3;   // must be refreshed by the sensor every frame
}

Vehicle* Intersection::takePreemptEvent() {
    Vehicle* v = pendingEvent;
    pendingEvent = NULL;
    return v;
}

TrafficSignal& Intersection::getSignal() { return signal; }
int Intersection::getGreenAxis() { return signal.getGreenAxis(); }
bool Intersection::isYellow() { return signal.getIsYellow(); }
bool Intersection::isPreempted() { return signal.getIsPreempted(); }
int Intersection::getTotalCleared() { return totalCleared; }
int Intersection::getEmergencyCleared() { return emergencyCleared; }
double Intersection::getTotalWait() { return totalWait; }
double Intersection::getEmergencyWait() { return emergencyWait; }
int Intersection::getPreemptions() { return preemptions; }
double Intersection::getClock() { return clock; }
Lane& Intersection::getLane(int i) { return lanes[i]; }
