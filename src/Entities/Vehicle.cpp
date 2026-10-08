#include "Entities/Vehicle.h"

// PCE values follow IRC:106-1990 (Indian urban roads): car 1.0, auto 1.2, bus 3.0.

Vehicle::Vehicle(int id, int priority, bool emergency) {
    this->id = id;
    this->priority = priority;
    this->emergency = emergency;
    arrivalTime = 0;

    state = MotionState::OnRoad;
    heading = 0;
    speed = 0;
    accel = 0;
    lateral = 0;
    lateralTarget = 0;
    roadId = -1;
    s = 0;
    distToStop = 1e9f;
    released = false;
    junction = -1;
    turnPath = -1;
    nextRoad = -1;
    blinker = 0;
    rerouteChecked = false;
    boxSeq = 0;
    routeIdx = 0;
    entryGate = exitGate = -1;
    waitAtLight = 0;
    totalDelay = 0;
    stuckTimer = 0;
    spawnTime = 0;
    colorSeed = id * 7919;
    rerouteFlash = 0;
}

double Vehicle::getWeight() { return 1.0; }
float Vehicle::getMaxSpeed() { return 75.0f; }
float Vehicle::getLength() { return 22.0f; }
float Vehicle::getWidth() { return 11.0f; }
int Vehicle::getId() { return id; }
int Vehicle::getPriority() { return priority; }
double Vehicle::getArrivalTime() { return arrivalTime; }
bool Vehicle::isEmergency() { return emergency; }
void Vehicle::setArrivalTime(double t) { arrivalTime = t; }

// ---- PrivateCar ----
PrivateCar::PrivateCar(int id) : Vehicle(id, 10, false) {}

string PrivateCar::getName() { return "Car " + to_string(id); }
string PrivateCar::getTypeName() { return "Private Car"; }
VehicleKind PrivateCar::getKind() { return VehicleKind::Car; }

// ---- PublicBus ----
PublicBus::PublicBus(int id, int passengers) : Vehicle(id, 5, false) {
    this->passengers = passengers;
}

string PublicBus::getName() { return "Bus " + to_string(id); }
string PublicBus::getTypeName() { return "Public Bus"; }
VehicleKind PublicBus::getKind() { return VehicleKind::Bus; }
double PublicBus::getWeight() { return 3.0; }   // bus is bigger so it counts more
float PublicBus::getMaxSpeed() { return 56.0f; }
float PublicBus::getLength() { return 40.0f; }
float PublicBus::getWidth() { return 13.0f; }
int PublicBus::getPassengers() { return passengers; }

// ---- AutoRickshaw ----
AutoRickshaw::AutoRickshaw(int id) : Vehicle(id, 10, false) {}

string AutoRickshaw::getName() { return "Auto " + to_string(id); }
string AutoRickshaw::getTypeName() { return "Auto Rickshaw"; }
VehicleKind AutoRickshaw::getKind() { return VehicleKind::AutoRickshaw; }
double AutoRickshaw::getWeight() { return 1.2; }
float AutoRickshaw::getMaxSpeed() { return 60.0f; }
float AutoRickshaw::getLength() { return 16.0f; }
float AutoRickshaw::getWidth() { return 10.0f; }

// ---- EmergencyVehicle ----
EmergencyVehicle::EmergencyVehicle(int id, string kind) : Vehicle(id, 2, true) {
    this->kind = kind;
    if (kind == "Ambulance") priority = 0;
    else if (kind == "FireTruck") priority = 1;
    else priority = 2;
}

string EmergencyVehicle::getName() { return getTypeName() + " " + to_string(id); }
string EmergencyVehicle::getTypeName() { return kind == "FireTruck" ? "Fire Truck" : kind; }

VehicleKind EmergencyVehicle::getKind() {
    if (kind == "Ambulance") return VehicleKind::Ambulance;
    if (kind == "FireTruck") return VehicleKind::FireTruck;
    return VehicleKind::Police;
}

double EmergencyVehicle::getWeight() { return kind == "FireTruck" ? 3.0 : 1.5; }
float EmergencyVehicle::getMaxSpeed() { return kind == "FireTruck" ? 95.0f : 110.0f; }
float EmergencyVehicle::getLength() { return kind == "FireTruck" ? 34.0f : 25.0f; }
float EmergencyVehicle::getWidth() { return kind == "FireTruck" ? 13.0f : 12.0f; }
