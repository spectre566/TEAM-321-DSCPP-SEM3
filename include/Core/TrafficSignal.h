#ifndef TRAFFICSIGNAL_H
#define TRAFFICSIGNAL_H

#include <iostream>
using namespace std;

enum class SignalPhase { Green, Yellow, AllRed };

// Controls the traffic light for an intersection
// Axis 0 = North-South, Axis 1 = East-West
//
// Mode 1 (static):  fixed 15 s green -> 2 s yellow -> 1 s all-red, no preemption.
// Mode 2 (smart):   SCATS-inspired green split proportional to PCE load,
//                   gap-out when the green road empties, extension while only
//                   the green road has demand, and emergency preemption.
class TrafficSignal {
private:
    int greenAxis;        // 0 for N-S, 1 for E-W
    SignalPhase phase;
    bool isPreempted;     // true if an emergency vehicle forced the green
    int preemptAxis;      // axis to serve after the current clearance (-1 = none)
    double timer;         // time left in current phase (seconds)
    double phaseDuration; // full length of current phase (for the HUD ring)
    double greenElapsed;  // how long the current green has been running
    double lastGreen;     // length allocated to the last green (for the HUD)

    void startPhase(SignalPhase p, double duration);

public:
    static constexpr double YELLOW_TIME = 2.0;
    static constexpr double ALL_RED_TIME = 1.0;
    static constexpr double FIXED_GREEN = 15.0;
    static constexpr double MIN_GREEN = 5.0;
    static constexpr double MAX_GREEN = 25.0;

    TrafficSignal() { reset(0); }

    void reset(int startAxis);
    void update(double dt, double loadNS, double loadEW, bool smartMode);
    void triggerEmergency(int axis);
    void clearPreemption();
    double calcDynamicTime(double load1, double load2);

    int getGreenAxis() { return greenAxis; }
    SignalPhase getPhase() { return phase; }
    bool getIsYellow() { return phase == SignalPhase::Yellow; }
    bool getIsAllRed() { return phase == SignalPhase::AllRed; }
    bool getIsPreempted() { return isPreempted; }
    double getTimer() { return timer; }
    double getPhaseDuration() { return phaseDuration; }
    double getLastGreen() { return lastGreen; }

    // true only while vehicles on this axis may enter the junction
    bool isGreenFor(int axis) { return phase == SignalPhase::Green && greenAxis == axis; }
};

#endif
