#include "Core/TrafficSignal.h"
#include <algorithm>

void TrafficSignal::reset(int startAxis) {
    greenAxis = startAxis;
    isPreempted = false;
    preemptAxis = -1;
    greenElapsed = 0;
    lastGreen = 10.0;
    startPhase(SignalPhase::Green, 10.0);   // start with 10 seconds green
}

void TrafficSignal::startPhase(SignalPhase p, double duration) {
    phase = p;
    timer = duration;
    phaseDuration = duration;
}

// Calculates dynamic green time based on road load in PCE (Mode 2)
double TrafficSignal::calcDynamicTime(double load1, double load2) {
    double total = load1 + load2;
    if (total <= 0) {
        return 10.0; // default if no cars
    }

    // Give more time to the busier road ...
    double myLoad = (greenAxis == 0) ? load1 : load2;
    double split = MIN_GREEN + (myLoad / total) * (MAX_GREEN - MIN_GREEN);

    // ... but never more than it takes to discharge the queue (~1.8 s per PCE)
    double discharge = 3.0 + myLoad * 1.8;
    double time = min(split, discharge);

    return max(MIN_GREEN, min(MAX_GREEN, time));
}

// Emergency vehicle forces this axis to turn green as soon as it is safe
void TrafficSignal::triggerEmergency(int axis) {
    isPreempted = true;

    if (phase == SignalPhase::Green) {
        if (greenAxis == axis) {
            // already green: hold it until the vehicle has passed
            if (timer < 6.0) { timer = 6.0; phaseDuration = max(phaseDuration, greenElapsed + timer); }
        } else {
            // cut the conflicting green short with a quick clearance
            preemptAxis = axis;
            startPhase(SignalPhase::Yellow, 1.5);
        }
    } else if (phase == SignalPhase::Yellow) {
        if (greenAxis == axis) {
            startPhase(SignalPhase::Green, 6.0);  // re-green the axis that was ending
        } else {
            preemptAxis = axis;
        }
    } else {
        preemptAxis = axis;                        // all-red: serve this axis next
    }
}

// Emergency vehicle has passed: give back control to the normal cycle
void TrafficSignal::clearPreemption() {
    if (!isPreempted) return;
    isPreempted = false;
    if (phase == SignalPhase::Green && timer > 3.0) timer = 3.0;
}

// Called every tick to count down timers and switch lights
void TrafficSignal::update(double dt, double loadNS, double loadEW, bool smartMode) {
    timer -= dt;
    if (phase == SignalPhase::Green) greenElapsed += dt;

    if (smartMode && phase == SignalPhase::Green && !isPreempted) {
        double mine = (greenAxis == 0) ? loadNS : loadEW;
        double other = (greenAxis == 0) ? loadEW : loadNS;

        // gap-out: nobody left on the green road but the cross road is waiting
        if (greenElapsed >= MIN_GREEN && mine < 0.01 && other > 0.01) {
            timer = 0;
        }
        // extension: green road still busy and nobody is waiting on the cross road
        if (timer <= 0 && mine > 0.01 && other < 0.01 && greenElapsed < MAX_GREEN) {
            timer = 1.0;
            phaseDuration += 1.0;
        }
    }

    if (timer > 0) return;

    switch (phase) {
    case SignalPhase::Green:
        // Green is finished -> show yellow for 2 seconds
        lastGreen = greenElapsed;
        startPhase(SignalPhase::Yellow, YELLOW_TIME);
        break;

    case SignalPhase::Yellow:
        startPhase(SignalPhase::AllRed, ALL_RED_TIME);
        break;

    case SignalPhase::AllRed: {
        // switch green to the other road (or to the emergency axis)
        greenAxis = (preemptAxis >= 0) ? preemptAxis : 1 - greenAxis;
        preemptAxis = -1;
        greenElapsed = 0;

        double g;
        if (isPreempted) g = 8.0;
        else if (smartMode) g = calcDynamicTime(loadNS, loadEW);
        else g = FIXED_GREEN;     // Mode 1: traditional fixed 15 seconds
        startPhase(SignalPhase::Green, g);
        break;
    }
    }
}
