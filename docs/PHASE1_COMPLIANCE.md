# Phase-I report compliance audit

Every claim in the Phase-I proposal (`Pbl phase 1.pdf`) and the Phase-I PPT (`pbl_ppt_sem_3 (1).pdf`), checked
against the code, with where to find it and how to show it in a demo.

**Status:** ✅ met · 🟡 met with a caveat (explained) · ➕ goes beyond the report

Benchmark numbers below are the mean of 5 seeds, each 10 simulated minutes (`docs/results/*_summary.csv`).

## 1. Proposal: Motivation, goals and approach

| # | Statement in the report | Status | Where it lives / evidence |
|---|---|---|---|
| 1 | Static fixed-time signals cause gridlock, idling and emissions | ✅ | Mode 1 city. Idle CO2 is now measured: 32.2 kg Fixed vs 4.5 kg Smart at rush hour (−86 %) |
| 2 | Traditional systems fail to prioritise emergency vehicles | ✅ | Mode 1 has no preemption. Emergency stopped time 32.0 s Fixed vs 0.5 s Smart |
| 3 | Data-driven signals minimise commuter wait | ✅ | Avg wait per signal 8.35 → 2.19 s (−74 %) at rush hour, 7.55 → 1.64 s (−78 %) normal |
| 4 | Flush bottlenecks before gridlock occurs | ✅ | Rush hour: 113 vehicles backed up at the city gates in Fixed vs 0.4 in Smart. With road incidents, trip delay is 22.3 s vs 6.3 s |
| 5 | Zero-delay emergency corridors | 🟡 | 0.19–0.53 s average stopped time, i.e. near-zero. Wording for the report: "near-zero (< 1 s)" |
| 6 | Validate adaptive control against static systems | ✅ ➕ | Both cities run on the same seed every tick; side-by-side view (`V`); multi-seed `--bench --seeds 5` with ± std |
| 7 | **M1** OOP vehicle hierarchy + FIFO / Min-Heap queues | ✅ | `Entities/Vehicle` (abstract base + 4 derived), `Entities/Lane` (`std::queue` + `std::priority_queue`) |
| 8 | **M2** Adjacency-list graph + dynamic Dijkstra rerouting | ✅ | `Core/Pathfinder` (`vector<vector<RoadEdge>>`, binary-heap Dijkstra), `CityGrid::maybeReroute` |
| 9 | **M3** SCATS-inspired green splits + preemption | ✅ | `TrafficSignal::calcDynamicTime` (PCE split, gap-out, extension), `Intersection::checkPreemption` |
| 10 | **M4** Interactive 2D SFML UI with real-time benchmarking | ✅ | `Graphics/Renderer`, `Graphics/HUD` (live chart, head-to-head table, CSV export `F5`) |
| 11 | C++17/20 | ✅ | C++17 (`CMakeLists.txt`) |
| 12 | Dynamic green split from PCE lane density | ✅ | `green = clamp(min(5 + 20·share, 3 + 1.8·PCE), 5, 25)`; shown live in the DSA inspector |
| 13 | Dijkstra rerouting when lane saturation ≥ 80 % | ✅ | `RoadEdge::isSaturated()` (≥ 0.8), checked on road entry and 120 px before the stop line |
| 14 | 2D top-down simulation with a side-by-side Static vs Dynamic toggle | ✅ | `M` toggles the city, `V` shows both side by side |

## 2. Proposal: architecture diagram (tick flow)

| Box in the diagram | Code |
|---|---|
| Tick start: queue evaluation | `CityGrid::update()` → `syncGraphLoads()` (lane PCE → graph edge loads) |
| Ambulance in Min-Heap? | `Intersection::checkPreemption()` reads `Lane::topEmergency()` of all 4 lanes, O(1) each |
| Yes → preempt signal to GREEN (flush queue) | `TrafficSignal::triggerEmergency()` + `Lane::moveEmergencyToFront()`; cars ahead pull over |
| No → calculate dynamic green splits (PCE) | `TrafficSignal::update()` → `calcDynamicTime()` |
| Dispatch moving vehicles | `CityGrid::releaseVehicles()` + `moveVehicles()` (IDM car-following) |
| Lane ≥ 80 % full? → run Dijkstra rerouting | `CityGrid::maybeReroute()` → `Pathfinder::findShortestPath(…, congestionAware, noUTurn)` |
| Update 2D canvas & HUD | `Renderer::drawCity()`, `HUD::draw()` |

## 3. Proposal: deliverables and assumptions

| Statement | Status | Evidence |
|---|---|---|
| Dynamic signal controller that reduces average wait | ✅ | −74 % to −78 % across demand levels |
| Priority-queue module for zero-delay emergency corridors | ✅ | Min-Heap per lane; tests `Intersection preemption`, `Lane FIFO queue + Min-Heap` |
| Dashboard: throughput, average wait reduction, congestion index, toggle | ✅ | KPI cards show the value and the % vs the other mode; head-to-head panel |
| *Assumption:* queues measured by sensors (cameras / induction loops) | ✅ ➕ | Induction loops are now painted in every approach lane; lane load = PCE sum from those detectors |
| *Assumption:* heterogeneous traffic modelled with PCE | ✅ | Car 1.0, auto-rickshaw 1.2, ambulance / police 1.5, bus / fire truck 3.0 (IRC:106-1990) |
| *Assumption:* emergency vehicles detected within a fixed approach radius | ✅ ➕ | `Intersection::DETECT_RADIUS = 230 px`; the zone lights up red/blue on the map when a vehicle is inside it |

## 4. PPT slides

| Slide | Statement | Status | Evidence |
|---|---|---|---|
| 3 | Lights react to real queues and clear paths for emergencies | ✅ | |
| 3 | "An interactive visual test you can actually drive and see" | ✅ | `PlayerController`: WASD driving, drift, siren preemption |
| 4.1 | Playable top-down grid in C++ and SFML | ✅ | 3×3 city by default; `--grid 2 2` … `4 4` |
| 4.2 | Smart green lights from PCE so heavier lanes clear first | ✅ | |
| 4.3 | Min-Heap instantly flips signals and flushes queues for emergencies | ✅ | Clearance is 1.5 s yellow, so it is fast but safe rather than literally instant |
| 4.4 | In-game switch, watch wait times drop in real time | ✅ | Live chart of both cities' rolling wait |
| 4 | Core goal: make the value of data structures obvious and visual | ✅ ➕ | **New DSA inspector:** click a junction to see each lane's FIFO queue, Min-Heap contents, saturation vs the 80 % line, and the green-split formula with live numbers. `G` shows the graph with live Dijkstra weights; clicking a car shows its Dijkstra route |
| 5 | Player drives a car or an ambulance with arrow keys | ✅ | `T` switches vehicle, `E` siren |
| 5 | AI cars spawn, follow waypoints, brake smoothly using raycast sensors | ✅ | Route = Dijkstra node waypoints; forward-corridor sensor + IDM braking |
| 5 | Each road is a FIFO queue tracking vehicle sizes and delays | ✅ | Lane load in PCE, per-vehicle `waitAtLight` |
| 5 | Mode 1: cars quickly pile up into visible gridlock | ✅ ➕ | Clearly visible at rush hour (`3`) and with road incidents (`I`): gate queues of 40–110 cars |
| 5 | Mode 2: signals favour crowded lanes and clear ambulance corridors | ✅ | |
| 5 | Live dashboard: average delays and throughput | ✅ | |
| 6 | Adjacency list, `std::queue`, `std::priority_queue`, Dijkstra | ✅ | See section 5 |
| 6 | Inheritance and polymorphism, encapsulation, game loop with delta timing | ✅ | `Vehicle` virtuals; fixed 1/60 s accumulator loop in `main.cpp` |
| 7 | Heap checks approaching ambulances in O(1) | 🟡 | `top()` is O(1); insertion and removal are O(log k). State both in the report |
| 7 | Clogged lanes trigger alternate routes | ✅ ➕ | Road incidents (`I`, `F2`) make this visible: the Smart ambulance in the showcase is routed around the breakdown |
| 8 | C++17, STL, SFML 2.6, CMake, VS Code | ✅ | `.vscode/tasks.json` has build / test / run / benchmark tasks |
| 8 | Git & GitHub | ⚠️ | This folder is a ZIP download with no `.git`. See recommendation 1 |
| 9 | "Verified ambulance preemption logic in test scripts" | ✅ ➕ | 91 automated tests (`ctest`) |
| 10 | Phase-III: live benchmarking HUD and comparative test runs | ✅ ➕ | Multi-seed bench with mean ± std, CSV export for report graphs |

## 5. Data-structure and algorithm complexity (for the viva)

| Operation | Structure | Complexity |
|---|---|---|
| Vehicle joins a lane | `std::queue::push` (+ `priority_queue::push` if emergency) | O(1) (+ O(log k)) |
| Release queue front | `queue::pop` | O(1); emergency removal rebuilds the heap in O(k log k), with k ≤ 3 in practice |
| "Is there an ambulance?" | `priority_queue::top` | O(1) per lane, 4 lanes per junction |
| Move emergencies to the front | rebuild queue | O(n + k log k); O(1) early exit when already at the front |
| Shortest path / reroute | Dijkstra with binary heap | O((V + E) log V); V = 21, E = 48 for 3×3 |
| Signal update | finite state machine | O(1) |
| Forward sensor of every vehicle | uniform spatial hash, 50 px cells | about O(1) per vehicle, so O(N) per tick |
| Whole tick | | O(N + J + E), with N vehicles and J junctions |

## 6. Inconsistencies found in the two Phase-I documents (fix before Phase-II)

1. **Student IDs differ between the documents.** The proposal has Arsh Singh as 2028663 and Sambhav Jaiswal as
   2028835 (with matching emails). The PPT has 2510370430 and 2510370170. Yashvardhan's 2510015374 matches in both.
2. **Team name and ID:** "Traffic Flow Optimization Team / DSCPP-321" (proposal) vs "FlowOptimizers / DSCPP-321-III" (PPT).
3. **"SFML/Raylib":** the project uses SFML 2.6 only. Say so, to show a decision was made.
4. **"C++17/20":** the build is C++17.
5. **Priorities:** PPT slide 7 says "Ambulance (Priority 1)", but in the code the ambulance is 0, fire truck 1 and
   police 2 (lower number = more urgent, as a Min-Heap expects).
6. **"Instant" / "zero-delay":** measured values are 1.5 s clearance and about 0.2–0.5 s stopped. Report the measured numbers.
7. **Slide layout:** on slides 6 and 10 the last line ("engine.", "through.") overlaps the footer.
8. **Missing references** for methods the code uses: Treiber et al. (2000) for the Intelligent Driver Model,
   IRC:106-1990 for the PCE values, and Dijkstra (1959).

## 7. What else would raise the PBL grade (things the team should do)

1. **Put it on GitHub with real history.** Have each member commit their own folder (`Core/`, `Entities/`,
   `Graphics/`) from their own account. Examiners use commit history as evidence of individual contribution.
2. **Plot the CSVs** in `docs/results/` for the Phase-II report. Useful figures: rolling wait over time
   (Fixed vs Smart) and a bar chart of the six summary metrics with ± std error bars.
3. **Add a "Limitations" section.** One lane per direction, two-phase signals (no protected right turn), no
   pedestrians, synthetic Poisson demand, approximate CO2 factor (0.39 g/s per PCE while idling), pixel units.
   Naming limitations yourselves scores better than an examiner finding them.
4. **Future-work ideas with clear DSA links:**
   - A* with a straight-line distance heuristic, compared with Dijkstra (node expansions)
   - green-wave coordination between adjacent junctions
   - multiple lanes per road
   - importing a real road network from OpenStreetMap
   - reinforcement-learning signal control as a stretch goal
5. **Viva prep.** Each member should be able to answer:
   - "Why a Min-Heap and not a sorted list?" (O(log n) insert, O(1) top)
   - "Why Dijkstra and not BFS?" (weighted edges; congestion makes the weights change)
   - "What is PCE and why use it?"
   - "How did you prevent deadlock inside the junction box?" (release order gives right of way; the cross axis
     must leave the box before the other axis is released)
6. **Demo script (under 3 minutes):**
   1. Title screen, then `V` for side by side.
   2. `F2` showcase: rush hour, a breakdown, and the same ambulance in both cities.
   3. Click a junction to open the DSA inspector.
   4. `G` for the graph with live weights, `N` for night mode.
   5. Drive the ambulance with the siren on (`T`, `E`) and watch lights preempt.
   6. End on the head-to-head table.
7. **Record a backup video** of the demo in case the lab machine lacks drivers.
