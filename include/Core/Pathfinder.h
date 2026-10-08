#ifndef PATHFINDER_H
#define PATHFINDER_H

#include <vector>
#include <queue>
#include <algorithm>
#include <iostream>

using namespace std;

// Represents a road segment between two nodes (intersections or city gates)
struct RoadEdge {
    int target;          // Destination node ID
    double length;       // Base road distance
    double capacity;     // Max load in PCE
    double currentLoad;  // Current traffic load (PCE)

    // Inline constructor
    RoadEdge(int to, double len, double cap) {
        target = to;
        length = len;
        capacity = cap;
        currentLoad = 0.0;
    }

    // Dynamic travel cost: increases when road is congested
    double getWeight() {
        if (capacity <= 0) return length;
        double ratio = currentLoad / capacity;
        return length * (1.0 + 2.0 * (ratio * ratio)); // quadratic congestion delay
    }

    double getSaturation() {
        if (capacity <= 0) return 0.0;
        return currentLoad / capacity;
    }

    // Check if lane is at or above 80% saturation (triggers rerouting)
    bool isSaturated() {
        if (capacity <= 0) return false;
        return (currentLoad / capacity) >= 0.8;
    }
};

/*
 * Pathfinder manages the road graph (adjacency list) and runs Dijkstra's
 * algorithm to find the fastest route or reroute around bottlenecks.
 */
class Pathfinder {
private:
    int totalNodes;
    vector<vector<RoadEdge>> adj; // Adjacency list: adj[u] holds edges from node u
    long long searches;           // how many times Dijkstra ran (shown on the HUD)

public:
    // Inline constructor
    Pathfinder(int nodes = 4) {
        reset(nodes);
    }

    void reset(int nodes) {
        totalNodes = nodes;
        adj.assign(nodes, vector<RoadEdge>());
        searches = 0;
    }

    // Inline small getters & helpers
    int getTotalNodes() { return totalNodes; }
    long long getSearchCount() { return searches; }
    vector<vector<RoadEdge>>& getAdjacency() { return adj; }

    void addRoad(int from, int to, double length, double capacity = 10) {
        if (from >= 0 && from < totalNodes) {
            adj[from].push_back(RoadEdge(to, length, capacity));
        }
    }

    // Larger logic functions implemented in Pathfinder.cpp
    RoadEdge* getEdge(int from, int to);
    void updateRoadLoad(int from, int to, double load);
    bool isRoadSaturated(int from, int to);
    double getRoadWeight(int from, int to);

    // congestionAware = false uses plain lengths (how a static map app routes)
    // avoidFirstHop   = node the vehicle just came from (no U-turns)
    vector<int> findShortestPath(int start, int target, bool congestionAware = true, int avoidFirstHop = -1);
    double pathCost(const vector<int>& path);
};

#endif
