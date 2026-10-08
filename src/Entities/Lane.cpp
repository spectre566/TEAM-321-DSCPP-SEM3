#include "Entities/Lane.h"

Lane::Lane() {
    name = "";
    capacity = 10;
    load = 0;
}

Lane::Lane(string name, double capacity) {
    this->name = name;
    this->capacity = capacity;
    load = 0;
}

bool Lane::addVehicle(Vehicle* v, double time) {
    if (v == NULL) return false;
    v->setArrivalTime(time);
    normalQueue.push(v);
    load += v->getWeight();

    if (v->isEmergency()) {
        emergencyHeap.push(v);
    }
    return true;
}

Vehicle* Lane::peekFront() {
    if (normalQueue.empty()) return NULL;
    return normalQueue.front();
}

Vehicle* Lane::removeFront() {
    if (normalQueue.empty()) return NULL;

    Vehicle* v = normalQueue.front();
    normalQueue.pop();
    load -= v->getWeight();
    if (load < 1e-9) load = 0;

    if (v->isEmergency()) {
        // priority_queue cant delete from middle so make a new heap without v
        priority_queue<Vehicle*, vector<Vehicle*>, CompareVehicle> temp;
        while (!emergencyHeap.empty()) {
            Vehicle* e = emergencyHeap.top();
            emergencyHeap.pop();
            if (e != v) temp.push(e);
        }
        emergencyHeap = temp;
    }
    return v;
}

void Lane::moveEmergencyToFront() {
    if (emergencyHeap.empty()) return;
    // already in front? nothing to do (keeps this O(1) on most ticks)
    if (normalQueue.front() == emergencyHeap.top()) return;

    // emergency vehicles in priority order
    vector<Vehicle*> first;
    priority_queue<Vehicle*, vector<Vehicle*>, CompareVehicle> copy = emergencyHeap;
    while (!copy.empty()) {
        first.push_back(copy.top());
        copy.pop();
    }

    // everyone else, same order as before
    vector<Vehicle*> rest;
    while (!normalQueue.empty()) {
        Vehicle* v = normalQueue.front();
        normalQueue.pop();
        if (!v->isEmergency()) rest.push_back(v);
    }

    for (int i = 0; i < (int)first.size(); i++) normalQueue.push(first[i]);
    for (int i = 0; i < (int)rest.size(); i++) normalQueue.push(rest[i]);
}

bool Lane::hasEmergency() { return !emergencyHeap.empty(); }

Vehicle* Lane::topEmergency() {
    if (emergencyHeap.empty()) return NULL;
    return emergencyHeap.top();
}

vector<Vehicle*> Lane::queueSnapshot() {
    vector<Vehicle*> out;
    queue<Vehicle*> copy = normalQueue;
    while (!copy.empty()) {
        out.push_back(copy.front());
        copy.pop();
    }
    return out;
}

vector<Vehicle*> Lane::heapSnapshot() {
    vector<Vehicle*> out;
    priority_queue<Vehicle*, vector<Vehicle*>, CompareVehicle> copy = emergencyHeap;
    while (!copy.empty()) {
        out.push_back(copy.top());
        copy.pop();
    }
    return out;
}

bool Lane::isEmpty() { return normalQueue.empty(); }
int Lane::size() { return normalQueue.size(); }
double Lane::getLoad() { return load; }
double Lane::getCapacity() { return capacity; }
void Lane::setCapacity(double c) { capacity = c; }
double Lane::getSaturation() { return capacity > 0 ? load / capacity : 0; }
string Lane::getName() { return name; }

void Lane::clear() {
    while (!normalQueue.empty()) normalQueue.pop();
    while (!emergencyHeap.empty()) emergencyHeap.pop();
    load = 0;
}
