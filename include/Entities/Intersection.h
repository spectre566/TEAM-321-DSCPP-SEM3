#ifndef INTERSECTION_H
#define INTERSECTION_H

#include "Entities/Lane.h"
#include "Core/TrafficSignal.h"

// lanes: 0 = North, 1 = South, 2 = East, 3 = West  (the side the traffic comes FROM)
// North/South go together (axis 0), East/West go together (axis 1)
// Both axes cross each other so only one can be green at a time.
//
// The intersection is the "controller": it owns the four approach queues and
// the signal, decides when the Min-Heap must preempt the signal, and keeps the
// wait-time statistics. CityGrid moves the vehicles and asks it for permission.
class Intersection {
private:
    int id;
    Lane lanes[4];
    TrafficSignal signal;

    double clock;
    int totalCleared;
    int emergencyCleared;
    double totalWait;
    double emergencyWait;
    int preemptions;

    int externalAxis;        // preemption requested by a sensor outside the queues (player ambulance)
    double externalTimer;
    int lastPreemptId;       // id of the emergency vehicle we last preempted for
    double preemptHold;      // how long we have been holding for that vehicle
    Vehicle* pendingEvent;   // new preemption, picked up by CityGrid for the event log

    int axisOf(int laneIndex);
    void checkPreemption(double dt);

public:
    static constexpr float DETECT_RADIUS = 230.0f;   // emergency detection range (px)
    static constexpr double MAX_HOLD = 20.0;         // safety: never starve the cross road longer

    Intersection();

    void setId(int i) { id = i; }
    int getId() { return id; }

    bool addVehicle(int laneIndex, Vehicle* v);
    void update(double dt, bool smartMode);

    bool isGreenFor(int laneIndex);
    Vehicle* releaseFront(int laneIndex, double* waitOut);
    void requestPreemption(int axis);
    Vehicle* takePreemptEvent();
    void reset();

    double axisLoad(int axis);
    bool axisHasEmergency(int axis);

    TrafficSignal& getSignal();
    int getGreenAxis();
    bool isYellow();
    bool isPreempted();
    int getTotalCleared();
    int getEmergencyCleared();
    double getTotalWait();
    double getEmergencyWait();
    int getPreemptions();
    double getClock();
    Lane& getLane(int i);
};

#endif
