#ifndef LANE_H
#define LANE_H

#include <queue>
#include <vector>
#include "Entities/Vehicle.h"
using namespace std;

// used by the priority_queue so the SMALLEST priority number is on top (min-heap)
struct CompareVehicle {
    bool operator()(Vehicle* a, Vehicle* b) {
        if (a->getPriority() != b->getPriority())
            return a->getPriority() > b->getPriority();
        return a->getArrivalTime() > b->getArrivalTime();   // earlier arrival first
    }
};

// One approach lane of an intersection.
// The lane does NOT own its vehicles: CityGrid creates and deletes them,
// the lane only keeps pointers in arrival order.
class Lane {
private:
    string name;
    double capacity;                                                  // in PCE
    double load;                                                      // current PCE in the queue
    queue<Vehicle*> normalQueue;                                      // FIFO
    priority_queue<Vehicle*, vector<Vehicle*>, CompareVehicle> emergencyHeap;  // min-heap

public:
    Lane();
    Lane(string name, double capacity);

    bool addVehicle(Vehicle* v, double time);
    Vehicle* peekFront();            // next vehicle the signal will release
    Vehicle* removeFront();
    void moveEmergencyToFront();

    bool hasEmergency();
    Vehicle* topEmergency();
    vector<Vehicle*> queueSnapshot();   // FIFO order, front first (copy, for display)
    vector<Vehicle*> heapSnapshot();    // Min-Heap order, top first (copy, for display)
    bool isEmpty();
    int size();
    double getLoad();
    double getCapacity();
    void setCapacity(double c);
    double getSaturation();          // load / capacity, 0.8 or more = saturated
    string getName();
    void clear();
};

#endif
