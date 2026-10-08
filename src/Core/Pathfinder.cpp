#include "Core/Pathfinder.h"

RoadEdge* Pathfinder::getEdge(int from, int to) {
    if (from < 0 || from >= totalNodes) return NULL;
    for (int i = 0; i < (int)adj[from].size(); i++) {
        if (adj[from][i].target == to) return &adj[from][i];
    }
    return NULL;
}

// Updates traffic load on a specific road edge
void Pathfinder::updateRoadLoad(int from, int to, double load) {
    RoadEdge* e = getEdge(from, to);
    if (e != NULL) e->currentLoad = load;
}

// Checks if a specific road edge has hit >= 80% capacity
bool Pathfinder::isRoadSaturated(int from, int to) {
    RoadEdge* e = getEdge(from, to);
    return e != NULL && e->isSaturated();
}

double Pathfinder::getRoadWeight(int from, int to) {
    RoadEdge* e = getEdge(from, to);
    return e != NULL ? e->getWeight() : 1e9;
}

// Standard Dijkstra's Algorithm to find the shortest/least congested path
vector<int> Pathfinder::findShortestPath(int start, int target, bool congestionAware, int avoidFirstHop) {
    if (start < 0 || start >= totalNodes || target < 0 || target >= totalNodes) {
        return {};
    }
    searches++;

    // 1. Initialize distances with a large number (infinity) and parent tracking
    vector<double> dist(totalNodes, 1e18);
    vector<int> parent(totalNodes, -1);

    // Min-heap storing pair<distance, node>
    priority_queue<pair<double, int>, vector<pair<double, int>>, greater<pair<double, int>>> pq;

    dist[start] = 0.0;
    pq.push({0.0, start});

    // 2. Dijkstra search loop
    while (!pq.empty()) {
        double d = pq.top().first;
        int u = pq.top().second;
        pq.pop();

        if (d > dist[u]) continue;
        if (u == target) break; // Reached destination

        // Relax neighbors
        for (int i = 0; i < (int)adj[u].size(); i++) {
            RoadEdge& edge = adj[u][i];
            int v = edge.target;
            if (u == start && v == avoidFirstHop) continue;  // no U-turn

            double weight = congestionAware ? edge.getWeight() : edge.length;

            if (dist[u] + weight < dist[v]) {
                dist[v] = dist[u] + weight;
                parent[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    // 3. Reconstruct path from destination backwards to start
    vector<int> path;
    if (dist[target] >= 1e18) {
        return path; // No path found
    }

    int curr = target;
    while (curr != -1) {
        path.push_back(curr);
        curr = parent[curr];
    }

    // Reverse to get path from start -> target
    reverse(path.begin(), path.end());
    return path;
}

double Pathfinder::pathCost(const vector<int>& path) {
    double total = 0;
    for (int i = 0; i + 1 < (int)path.size(); i++) {
        total += getRoadWeight(path[i], path[i + 1]);
    }
    return total;
}
