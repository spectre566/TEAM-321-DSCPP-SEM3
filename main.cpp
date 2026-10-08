// ============================================================================
//  Smart Traffic Flow Optimization & Emergency Preemption Simulation Engine
//  Team FlowOptimizers (DSCPP-321-III) - DSA CS201 Project-Based Learning
//
//  Two identical cities run side by side every tick:
//    Mode 1  fixed-timer signals, no preemption, static routes
//    Mode 2  PCE-adaptive signals, Min-Heap emergency preemption, Dijkstra rerouting
//  Both get the same random seed, so their metrics are a fair comparison.
//
//  Usage:
//    SmartTrafficFlow                     play
//    SmartTrafficFlow --bench 600         headless benchmark (simulated seconds)
//    SmartTrafficFlow --screenshot a.png  render one frame and exit (see --help)
// ============================================================================
#include <SFML/Graphics.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>
#include "Core/CityGrid.h"
#include "Graphics/Renderer.h"
#include "Graphics/PlayerController.h"
#include "Graphics/HUD.h"

namespace {
const double STEP = 1.0 / 60.0;                 // simulation tick (seconds)
const double DEMAND[3] = {0.6, 1.0, 1.5};       // light / normal / rush hour
const double SPEEDS[5] = {0.5, 1, 2, 4, 8};
const char* DEMAND_NAME[3] = {"light", "normal", "rush hour"};

struct Options {
    double bench = -1;
    int demand = 1;
    int seeds = 1;
    unsigned seed = 2026;
    int rows = 3, cols = 3;
    std::string csv;
    std::string screenshot;
    int frames = 90;
    double warmup = 90;
    int inspectJunction = -1;
    bool split = false, night = false, heat = false, graph = false, fixedView = false;
    bool noTitle = false, titleShot = false, help = false, chase = false, select = false, ambulance = false;
    bool incident = false, showcase = false, noLegend = false;
    int tourStep = -1;
    float mouseX = -1, mouseY = -1;
    std::vector<sf::Vector2f> clicks;   // test hook: clicks injected during --screenshot
};

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](double def) { return (i + 1 < argc && argv[i + 1][0] != '-') ? atof(argv[++i]) : def; };
        if (a == "--bench") o.bench = next(600);
        else if (a == "--demand") o.demand = std::max(0, std::min(2, (int)next(1)));
        else if (a == "--seeds") o.seeds = std::max(1, (int)next(5));
        else if (a == "--seed") o.seed = (unsigned)next(2026);
        else if (a == "--grid") {
            o.rows = std::max(2, std::min(4, (int)next(3)));
            o.cols = std::max(2, std::min(4, (int)next(o.rows)));
        }
        else if (a == "--csv" && i + 1 < argc) o.csv = argv[++i];
        else if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (a == "--frames") o.frames = (int)next(90);
        else if (a == "--warmup") o.warmup = next(90);
        else if (a == "--inspect") o.inspectJunction = (int)next(4);
        else if (a == "--split") o.split = true;
        else if (a == "--night") o.night = true;
        else if (a == "--heat") o.heat = true;
        else if (a == "--graph") o.graph = true;
        else if (a == "--fixed") o.fixedView = true;
        else if (a == "--no-title") o.noTitle = true;
        else if (a == "--title") o.titleShot = true;
        else if (a == "--help-overlay") o.help = true;
        else if (a == "--chase") o.chase = true;
        else if (a == "--select") o.select = true;
        else if (a == "--ambulance") o.ambulance = true;
        else if (a == "--incident") o.incident = true;
        else if (a == "--showcase") o.showcase = true;
        else if (a == "--no-legend") o.noLegend = true;
        else if (a == "--tour") o.tourStep = (int)next(0);
        else if (a == "--click") { float cx = (float)next(0); float cy = (float)next(0); o.clicks.push_back(sf::Vector2f(cx, cy)); }
        else if (a == "--mouse") { o.mouseX = (float)next(0); o.mouseY = (float)next(0); }
        else if (a == "--help" || a == "-h") {
            std::cout << "SmartTrafficFlow [--grid ROWS COLS] [--seed N] [--demand 0|1|2]\n"
                         "  --bench SECONDS [--seeds N] [--csv PREFIX]   headless Fixed vs Smart benchmark\n"
                         "  --screenshot FILE [--frames N] [--warmup SECONDS] [--split] [--night] [--heat]\n"
                         "                    [--graph] [--fixed] [--title] [--chase] [--select] [--ambulance]\n"
                         "                    [--inspect JUNCTION] [--incident] [--showcase]\n";
            exit(0);
        }
    }
    return o;
}

// One benchmark run: index 0 = Fixed city, 1 = Smart city
struct RunResult {
    double wait[2], delay[2], emergency[2], trips[2], gates[2], co2[2];
    int preemptions, reroutes;
};

double improvementPct(double f, double s, bool lowerBetter) {
    if (f <= 1e-9) return 0;
    return 100.0 * (lowerBetter ? (f - s) / f : (s - f) / f);
}

// Writes the per-second history of both cities, ready for Excel / matplotlib graphs.
bool writeTimeSeries(const std::string& path, CityGrid& f, CityGrid& s) {
    std::ofstream out(path);
    if (!out) return false;
    out << "time_s,fixed_wait_s,smart_wait_s,fixed_throughput_per_min,smart_throughput_per_min,"
           "fixed_congestion_pct,smart_congestion_pct,fixed_active,smart_active,fixed_idle_co2_kg,smart_idle_co2_kg\n";
    std::vector<MetricSample>& a = f.getHistory();
    std::vector<MetricSample>& b = s.getHistory();
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); i++) {
        out << std::fixed << std::setprecision(2) << a[i].time << "," << a[i].avgWait << "," << b[i].avgWait << ","
            << a[i].throughput << "," << b[i].throughput << "," << a[i].congestion << "," << b[i].congestion << ","
            << a[i].active << "," << b[i].active << "," << a[i].co2 << "," << b[i].co2 << "\n";
    }
    return true;
}

// Headless benchmark: runs both modes on N seeds and prints mean +/- standard deviation.
int runBenchmark(const Options& o) {
    std::cout << "Benchmark: " << o.bench << " simulated s x " << o.seeds << " seed(s), " << DEMAND_NAME[o.demand]
              << " demand, " << o.rows << "x" << o.cols << " grid\n";
    std::vector<RunResult> runs;
    for (int k = 0; k < o.seeds; k++) {
        unsigned seed = o.seed + k * 7919u;
        CityGrid fixedGrid(o.rows, o.cols, false, seed), smartGrid(o.rows, o.cols, true, seed);
        fixedGrid.setDemand(DEMAND[o.demand]);
        smartGrid.setDemand(DEMAND[o.demand]);
        long long steps = (long long)(o.bench / STEP);
        for (long long i = 0; i < steps; i++) {
            fixedGrid.update(STEP);
            smartGrid.update(STEP);
        }
        RunResult r;
        CityGrid* g[2] = {&fixedGrid, &smartGrid};
        for (int m = 0; m < 2; m++) {
            r.wait[m] = g[m]->getAverageWaitTime();
            r.delay[m] = g[m]->getAverageTripDelay();
            r.emergency[m] = g[m]->getEmergencyAvgDelay();
            r.trips[m] = g[m]->getTripsDone();
            r.gates[m] = g[m]->getPendingVehicles();
            r.co2[m] = g[m]->getIdleCO2kg();
        }
        r.preemptions = smartGrid.getTotalPreemptions();
        r.reroutes = smartGrid.getReroutes();
        runs.push_back(r);
        std::cout << "  seed " << std::setw(6) << seed << ": wait " << std::fixed << std::setprecision(2) << r.wait[0]
                  << " -> " << r.wait[1] << " s, emergency stop " << r.emergency[0] << " -> " << r.emergency[1] << " s\n";
        if (k == 0 && !o.csv.empty()) {
            std::string path = o.csv + "_timeseries.csv";
            if (writeTimeSeries(path, fixedGrid, smartGrid)) std::cout << "  wrote " << path << "\n";
        }
        if (o.seeds == 1) {
            fixedGrid.printStatus();
            smartGrid.printStatus();
        }
    }

    std::ofstream summary;
    if (!o.csv.empty()) {
        summary.open(o.csv + "_summary.csv");
        summary << "metric,fixed_mean,fixed_sd,smart_mean,smart_sd,improvement_pct\n";
    }
    // mean and sample standard deviation of one metric for one mode
    auto stat = [&](double* (*get)(RunResult&), int mode, double& mean, double& sd) {
        mean = 0;
        for (RunResult& r : runs) mean += get(r)[mode];
        mean /= runs.size();
        sd = 0;
        for (RunResult& r : runs) sd += (get(r)[mode] - mean) * (get(r)[mode] - mean);
        sd = runs.size() > 1 ? std::sqrt(sd / (runs.size() - 1)) : 0;
    };
    auto row = [&](const char* name, double* (*get)(RunResult&), bool lowerBetter) {
        double fm, fs, sm, ss;
        stat(get, 0, fm, fs);
        stat(get, 1, sm, ss);
        double imp = improvementPct(fm, sm, lowerBetter);
        std::ostringstream f, s;
        f << std::fixed << std::setprecision(2) << fm;
        s << std::fixed << std::setprecision(2) << sm;
        if (runs.size() > 1) {
            f << " +/-" << fs;
            s << " +/-" << ss;
        }
        std::cout << "  " << std::left << std::setw(28) << name << std::right << std::setw(19) << f.str()
                  << std::setw(19) << s.str() << std::setw(9) << std::fixed << std::setprecision(1) << imp << "%\n";
        if (summary) summary << name << "," << fm << "," << fs << "," << sm << "," << ss << "," << imp << "\n";
    };
    std::cout << "\n  " << std::left << std::setw(28) << "METRIC" << std::right << std::setw(19) << "FIXED"
              << std::setw(19) << "SMART" << std::setw(10) << "BETTER" << "\n";
    row("Avg wait per signal (s)", [](RunResult& r) { return r.wait; }, true);
    row("Avg trip delay (s)", [](RunResult& r) { return r.delay; }, true);
    row("Emergency stopped time (s)", [](RunResult& r) { return r.emergency; }, true);
    row("Trips completed", [](RunResult& r) { return r.trips; }, false);
    row("Vehicles stuck at gates", [](RunResult& r) { return r.gates; }, true);
    row("Idle CO2 (kg)", [](RunResult& r) { return r.co2; }, true);
    double pre = 0, rr = 0;
    for (RunResult& r : runs) {
        pre += r.preemptions;
        rr += r.reroutes;
    }
    std::cout << "  Smart mode averages " << std::setprecision(0) << pre / runs.size() << " preemptions and "
              << rr / runs.size() << " Dijkstra reroutes per run\n";
    if (summary) std::cout << "  wrote " << o.csv << "_summary.csv\n";
    return 0;
}
}

int main(int argc, char** argv) {
    Options opt = parse(argc, argv);
    if (opt.bench > 0) return runBenchmark(opt);

    bool shotMode = !opt.screenshot.empty();
    CityGrid fixedGrid(opt.rows, opt.cols, false, opt.seed), smartGrid(opt.rows, opt.cols, true, opt.seed);
    fixedGrid.setDemand(DEMAND[opt.demand]);
    smartGrid.setDemand(DEMAND[opt.demand]);

    Renderer renderer;
    if (!renderer.init(!shotMode)) {
        std::cerr << "Could not create the window / render texture\n";
        return 1;
    }
    if (!renderer.getFonts().ok) std::cerr << "Warning: no font found, text will be missing\n";
    renderer.buildCity(smartGrid);
    HUD hud(renderer.getFonts());
    PlayerController player;
    if (opt.ambulance) player.toggleType();

    sf::Cursor arrowCursor, handCursor;
    bool cursors = arrowCursor.loadFromSystem(sf::Cursor::Arrow) && handCursor.loadFromSystem(sf::Cursor::Hand);
    bool handShown = false;

    HudState st;
    st.demandLevel = opt.demand;
    st.split = opt.split;
    st.night = opt.night;
    st.heatmap = opt.heat;
    st.graph = opt.graph;
    st.smartShown = !opt.fixedView;
    st.chase = opt.chase;
    st.legend = !opt.noLegend;
    bool title = (!opt.noTitle && !shotMode) || opt.titleShot;
    bool help = opt.help;
    int tourStep = opt.tourStep;                 // -1 = no guided tour
    int speedIdx = 1;
    int selectedId = -1;
    int selectedJunction = opt.inspectJunction;  // DSA inspector
    int inspectGrid = 1;                         // 0 fixed / 1 smart (split view)
    float zoom = 1.8f;
    sf::Vector2f camCenter(300, 192);
    std::mt19937 rng((unsigned)time(NULL));

    auto resetAll = [&]() {
        fixedGrid.reset();
        smartGrid.reset();
        player.reset(Vec2(300, 192), 0);
        renderer.clearPopups();
        selectedId = -1;
        selectedJunction = -1;
    };

    // Same breakdown in both cities: Smart reroutes around it with Dijkstra, Fixed piles up behind it
    auto incident = [&](int road) {
        if (road < 0) road = smartGrid.pickIncidentRoad((unsigned)rng());
        smartGrid.createIncident(road, 60);
        fixedGrid.createIncident(road, 60);
    };

    auto dispatchAmbulance = [&]() {
        // same dispatch in both cities keeps the comparison fair
        int gate = (int)(rng() % smartGrid.getGates().size());
        smartGrid.spawnEmergency(0, gate);
        fixedGrid.spawnEmergency(0, gate);
        selectedId = smartGrid.getGates()[gate].pending.front()->getId();   // highlight it so people can follow it
        selectedJunction = -1;
    };

    // The demo: rush hour + a breakdown on the main road + an ambulance that must cross it
    auto showcase = [&]() {
        st.demandLevel = 2;
        fixedGrid.setDemand(DEMAND[2]);
        smartGrid.setDemand(DEMAND[2]);
        st.split = true;
        st.paused = false;
        int row = smartGrid.getRows() / 2, c = smartGrid.getCols();
        incident(smartGrid.getRoadIndex(row * c, row * c + 1));
        int gate = 2 * c + row;                       // West gate of the middle row
        smartGrid.spawnEmergency(0, gate);
        fixedGrid.spawnEmergency(0, gate);
        selectedId = smartGrid.getGates()[gate].pending.front()->getId();
        selectedJunction = -1;
    };

    auto exportCsv = [&]() {
        std::string name = "metrics_" + std::to_string((long long)time(NULL)) + "_timeseries.csv";
        if (writeTimeSeries(name, fixedGrid, smartGrid)) std::cout << "Saved " << name << "\n";
    };

    auto setDemand = [&](int level) {
        st.demandLevel = level;
        fixedGrid.setDemand(DEMAND[level]);
        smartGrid.setDemand(DEMAND[level]);
    };

    // Everything a button or key can do, in one place
    auto perform = [&](UiAction a) {
        switch (a) {
        case UiAction::ModeFixed: st.smartShown = false; selectedId = -1; inspectGrid = 0; break;
        case UiAction::ModeSmart: st.smartShown = true; selectedId = -1; inspectGrid = 1; break;
        case UiAction::Demand0: setDemand(0); break;
        case UiAction::Demand1: setDemand(1); break;
        case UiAction::Demand2: setDemand(2); break;
        case UiAction::Split: st.split = !st.split; break;
        case UiAction::Heatmap: st.heatmap = !st.heatmap; break;
        case UiAction::Graph: st.graph = !st.graph; break;
        case UiAction::Night: st.night = !st.night; break;
        case UiAction::Chase: st.chase = !st.chase; break;
        case UiAction::Legend: st.legend = !st.legend; break;
        case UiAction::SpeedDown: speedIdx = std::max(0, speedIdx - 1); break;
        case UiAction::SpeedUp: speedIdx = std::min(4, speedIdx + 1); break;
        case UiAction::Pause: st.paused = !st.paused; break;
        case UiAction::Restart: resetAll(); break;
        case UiAction::Ambulance: dispatchAmbulance(); break;
        case UiAction::Incident: incident(-1); break;
        case UiAction::Showcase: title = false; tourStep = -1; help = false; showcase(); break;
        case UiAction::Help: help = true; break;
        case UiAction::CloseOverlay: help = false; break;
        case UiAction::Start: title = false; break;
        case UiAction::Tour: title = false; help = false; st.split = false; tourStep = 0; break;
        case UiAction::TourNext: tourStep = tourStep + 1 >= HUD::TOUR_STEPS ? -1 : tourStep + 1; break;
        case UiAction::TourBack: tourStep = std::max(0, tourStep - 1); break;
        case UiAction::TourSkip: tourStep = -1; break;
        case UiAction::CloseInspector: selectedJunction = -1; break;
        case UiAction::PlayerCar: if (player.getType() != PlayerController::Type::Car) player.toggleType(); break;
        case UiAction::PlayerAmbulance: if (player.getType() != PlayerController::Type::Ambulance) player.toggleType(); break;
        case UiAction::Siren: player.toggleSiren(); break;
        case UiAction::Assist: player.toggleAssist(); break;
        case UiAction::ExportCsv: exportCsv(); break;
        case UiAction::Quit: renderer.getWindow().close(); break;
        default: break;
        }
    };

    // Simulation tick: the player drives in the city that is on screen
    auto tick = [&](double dt, bool playerActive) {
        CityGrid& active = st.smartShown ? smartGrid : fixedGrid;
        CityGrid& other = st.smartShown ? fixedGrid : smartGrid;
        if (playerActive) {
            DriverInput in;
            if (renderer.getWindow().hasFocus() && !shotMode) {
                using K = sf::Keyboard;
                in.up = K::isKeyPressed(K::W) || K::isKeyPressed(K::Up);
                in.down = K::isKeyPressed(K::S) || K::isKeyPressed(K::Down);
                in.left = K::isKeyPressed(K::A) || K::isKeyPressed(K::Left);
                in.right = K::isKeyPressed(K::D) || K::isKeyPressed(K::Right);
                in.handbrake = K::isKeyPressed(K::Space);
            }
            player.update(dt, active, in);
            active.setPlayerProbe(player.probe());
        } else {
            active.setPlayerProbe(PlayerProbe());
        }
        other.setPlayerProbe(PlayerProbe());
        fixedGrid.update(dt);
        smartGrid.update(dt);
    };

    if (shotMode) {
        for (int i = 0; i < (int)(opt.warmup / STEP); i++) tick(STEP, false);
        if (opt.select) {
            // pick an emergency vehicle if there is one, otherwise any car
            CityGrid& g = st.smartShown ? smartGrid : fixedGrid;
            for (Vehicle* v : g.getVehicles())
                if (selectedId < 0 || v->isEmergency()) { selectedId = v->getId(); if (v->isEmergency()) break; }
        }
        if (opt.incident) incident(smartGrid.getRoadIndex(4 % smartGrid.getNumIntersections(), 5 % smartGrid.getNumIntersections()));
        if (opt.showcase) {
            showcase();
            for (int i = 0; i < 8 * 60; i++) tick(STEP, false);   // let the scenario develop a little
        }
    }

    sf::Clock frameClock, realClock;
    double accumulator = 0;
    float fpsSmooth = 60;
    int frame = 0;
    const sf::FloatRect mapVp(0, 0, 900, 900), leftVp(6, 6, 788, 788), rightVp(806, 6, 788, 788);

    while (renderer.getWindow().isOpen()) {
        float realDt = std::min(0.1f, frameClock.restart().asSeconds());
        if (shotMode) realDt = (float)STEP;
        fpsSmooth = fpsSmooth * 0.95f + (realDt > 0 ? 1.0f / realDt : 60) * 0.05f;
        st.time = shotMode ? (float)(frame * STEP) : realClock.getElapsedTime().asSeconds();

        CityGrid& active = st.smartShown ? smartGrid : fixedGrid;
        Vec2 pp = player.getPos();
        camCenter += (sf::Vector2f(pp.x, pp.y) - camCenter) * std::min(1.0f, realDt * 5);
        sf::View mainView = st.chase ? renderer.makeChase(camCenter, zoom, mapVp) : renderer.makeOverview(mapVp);
        bool overlay = title || help || tourStep >= 0;

        // which map (if any) is under a point, and the world position there
        auto mapAt = [&](sf::Vector2f lp, CityGrid*& g, sf::Vector2f& world, int& gi) {
            g = NULL;
            if (st.split) {
                if (leftVp.contains(lp)) { g = &fixedGrid; world = renderer.logicalToWorld(lp, renderer.makeOverview(leftVp)); gi = 0; }
                else if (rightVp.contains(lp)) { g = &smartGrid; world = renderer.logicalToWorld(lp, renderer.makeOverview(rightVp)); gi = 1; }
            } else if (mapVp.contains(lp)) {
                g = &active;
                world = renderer.logicalToWorld(lp, mainView);
                gi = st.smartShown ? 1 : 0;
            }
        };

        // A left click: buttons first, then traffic-light tiles, then the map
        auto handleClick = [&](sf::Vector2f lp) {
            UiAction a = hud.hit(lp);
            if (a != UiAction::None) { perform(a); return; }
            if (overlay || hud.overUi(lp)) return;
            if (!st.split) {
                int cell = hud.junctionCellAt(active, lp);
                if (cell >= 0) { selectedJunction = cell; selectedId = -1; inspectGrid = st.smartShown ? 1 : 0; return; }
            }
            CityGrid* g;
            sf::Vector2f world;
            int gi = 0;
            mapAt(lp, g, world, gi);
            if (g) {
                Vehicle* hitV = renderer.pickVehicle(*g, world);
                selectedId = hitV ? hitV->getId() : -1;
                selectedJunction = hitV ? -1 : renderer.pickJunction(*g, world);
                inspectGrid = gi;
            }
        };

        // ---------------- input ----------------
        sf::Event ev;
        while (renderer.getWindow().pollEvent(ev)) {
            if (ev.type == sf::Event::Closed) renderer.getWindow().close();
            if (ev.type == sf::Event::KeyPressed) {
                using K = sf::Keyboard;
                if (title) {
                    if (ev.key.code == K::Enter || ev.key.code == K::Space) title = false;
                    else if (ev.key.code == K::Escape) renderer.getWindow().close();
                    continue;
                }
                if (tourStep >= 0) {
                    if (ev.key.code == K::Escape) tourStep = -1;
                    else if (ev.key.code == K::Enter || ev.key.code == K::Right || ev.key.code == K::Space) perform(UiAction::TourNext);
                    else if (ev.key.code == K::Left || ev.key.code == K::BackSpace) perform(UiAction::TourBack);
                    continue;
                }
                switch (ev.key.code) {
                case K::Escape:
                    if (help) help = false;
                    else if (selectedJunction >= 0) selectedJunction = -1;
                    else st.paused = !st.paused;
                    break;
                case K::F1: help = !help; break;
                case K::P: perform(UiAction::Pause); break;
                case K::M: perform(st.smartShown ? UiAction::ModeFixed : UiAction::ModeSmart); break;
                case K::V: perform(UiAction::Split); break;
                case K::H: perform(UiAction::Heatmap); break;
                case K::G: perform(UiAction::Graph); break;
                case K::N: perform(UiAction::Night); break;
                case K::C: perform(UiAction::Chase); break;
                case K::L: perform(UiAction::Legend); break;
                case K::T: player.toggleType(); break;
                case K::E: player.toggleSiren(); break;
                case K::B: player.toggleAssist(); break;
                case K::R: perform(UiAction::Restart); break;
                case K::X: perform(UiAction::Ambulance); break;
                case K::I: perform(UiAction::Incident); break;
                case K::F2: perform(UiAction::Showcase); break;
                case K::F5: perform(UiAction::ExportCsv); break;
                case K::Num1: perform(UiAction::Demand0); break;
                case K::Num2: perform(UiAction::Demand1); break;
                case K::Num3: perform(UiAction::Demand2); break;
                case K::LBracket: perform(UiAction::SpeedDown); break;
                case K::RBracket: perform(UiAction::SpeedUp); break;
                case K::F12: {
                    std::string name = "screenshot_" + std::to_string((long long)time(NULL)) + ".png";
                    if (renderer.saveScreenshot(name)) std::cout << "Saved " << name << "\n";
                    break;
                }
                default: break;
                }
            }
            if (ev.type == sf::Event::MouseWheelScrolled && !overlay) {
                zoom = std::max(1.0f, std::min(4.0f, zoom * (ev.mouseWheelScroll.delta > 0 ? 1.15f : 1 / 1.15f)));
                if (!st.chase && ev.mouseWheelScroll.delta > 0) st.chase = true;
            }
            if (ev.type == sf::Event::MouseButtonPressed && ev.mouseButton.button == sf::Mouse::Left)
                handleClick(renderer.windowToLogical(sf::Vector2i(ev.mouseButton.x, ev.mouseButton.y)));
        }
        // test hook: injected clicks, one every 20 frames
        if (shotMode && frame > 0 && frame % 20 == 0 && (std::size_t)(frame / 20 - 1) < opt.clicks.size())
            handleClick(opt.clicks[frame / 20 - 1]);
        st.mouse = shotMode && opt.mouseX >= 0 ? sf::Vector2f(opt.mouseX, opt.mouseY)
                                               : renderer.windowToLogical(sf::Mouse::getPosition(renderer.getWindow()));

        // what is under the mouse on the map (hover highlight + tooltip)
        int hoverId = -1, hoverJunction = -1;
        std::string mapTipTitle, mapTipBody;
        if (!overlay && !hud.overUi(st.mouse)) {
            CityGrid* g;
            sf::Vector2f world;
            int gi = 0;
            mapAt(st.mouse, g, world, gi);
            if (g) {
                Vehicle* v = renderer.pickVehicle(*g, world);
                if (v) {
                    hoverId = v->getId();
                    mapTipTitle = v->getName() + "  (" + v->getTypeName() + ")";
                    mapTipBody = std::string(v->isEmergency() ? "Emergency vehicle: smart lights turn green for it. "
                                                              : "Size: " + std::to_string(v->getWeight()).substr(0, 3) + " car-equivalents. ") +
                                 (v->speed < 4 ? "Waiting right now. " : "Moving at " + std::to_string((int)(v->speed * 0.72f)) + " km/h. ") +
                                 "Click to see its route.";
                } else {
                    hoverJunction = renderer.pickJunction(*g, world);
                    if (hoverJunction >= 0) {
                        Intersection& J = g->getIntersection(hoverJunction);
                        int waiting = 0;
                        for (int l = 0; l < 4; l++) waiting += J.getLane(l).size();
                        mapTipTitle = "Traffic light J" + std::to_string(hoverJunction);
                        mapTipBody = std::to_string(waiting) + " vehicles queued here. Click to look inside its queues and priority heap.";
                    }
                }
            }
        }

        // ---------------- fixed-step simulation ----------------
        st.simSpeed = SPEEDS[speedIdx];
        if (!st.paused && !help) {
            accumulator += realDt * st.simSpeed;
            int steps = 0;
            while (accumulator >= STEP && steps < 48) {
                tick(STEP, !title && !(shotMode && !opt.chase));
                accumulator -= STEP;
                steps++;
            }
            if (steps == 48) accumulator = 0;
        }
        st.fps = fpsSmooth;

        // ---------------- render ----------------
        hud.beginFrame(st);
        renderer.beginFrame();
        ViewOptions vo;
        vo.heatmap = st.heatmap;
        vo.graph = st.graph;
        vo.night = st.night;
        vo.time = st.time;
        vo.selectedId = selectedId;
        vo.hoverId = hoverId;
        vo.hoverJunction = hoverJunction;
        CityGrid& inspected = st.split ? (inspectGrid == 0 ? fixedGrid : smartGrid) : active;
        float popupDt = (st.paused || help) ? 0 : realDt;
        if (st.split) {
            ViewOptions a = vo, b = vo;
            a.title = "OLD WAY  |  fixed timers";
            a.titleColor = Theme::Static;
            a.showPlayer = !st.smartShown;
            a.selectedJunction = inspectGrid == 0 ? selectedJunction : -1;
            b.title = "SMART WAY  |  our DSA engine";
            b.titleColor = Theme::Smart;
            b.showPlayer = st.smartShown;
            b.selectedJunction = inspectGrid == 1 ? selectedJunction : -1;
            renderer.drawCity(fixedGrid, 0, &player, renderer.makeOverview(leftVp), leftVp, a, popupDt);
            renderer.drawCity(smartGrid, 1, &player, renderer.makeOverview(rightVp), rightVp, b, popupDt);
            hud.drawSplitBar(renderer.getCanvas(), fixedGrid, smartGrid, player, st);
            hud.drawToolbar(renderer.getCanvas(), st, 800, 748);
            if (selectedJunction >= 0)
                hud.drawJunctionInspector(renderer.getCanvas(), inspected, selectedJunction, st, inspectGrid == 0 ? 14 : 814, 400);
        } else {
            vo.showPlayer = !title;
            vo.selectedJunction = selectedJunction;
            vo.title = st.smartShown ? "SMART WAY  |  our DSA engine" : "OLD WAY  |  fixed timers";
            vo.titleColor = st.smartShown ? Theme::Smart : Theme::Static;
            renderer.drawCity(active, st.smartShown ? 1 : 0, &player, mainView, mapVp, vo, popupDt);
            if (st.legend && selectedJunction < 0) renderer.drawLegend(12, 52, st.time, st.smartShown);
            hud.draw(renderer.getCanvas(), fixedGrid, smartGrid, player, st);
            hud.drawToolbar(renderer.getCanvas(), st, 450, 852);
            if (selectedJunction >= 0)
                hud.drawJunctionInspector(renderer.getCanvas(), active, selectedJunction, st, 14, 498);
        }
        if (!mapTipTitle.empty()) hud.mapHint(mapTipTitle, mapTipBody);
        if (st.paused && !overlay) hud.drawPaused(renderer.getCanvas(), st);
        if (title) hud.drawTitleScreen(renderer.getCanvas(), st);
        else if (tourStep >= 0) hud.drawTour(renderer.getCanvas(), tourStep, st);
        else if (help) hud.drawHelp(renderer.getCanvas());
        hud.drawTooltip(renderer.getCanvas(), st);

        // hand cursor over anything clickable
        bool hand = hud.isClickable(st.mouse) || (!overlay && (hoverId >= 0 || hoverJunction >= 0)) ||
                    (!overlay && !st.split && hud.junctionCellAt(active, st.mouse) >= 0);
        if (cursors && hand != handShown && !shotMode) {
            renderer.getWindow().setMouseCursor(hand ? handCursor : arrowCursor);
            handShown = hand;
        }
        renderer.endFrame();
        frame++;

        if (shotMode && frame >= opt.frames) {
            bool ok = renderer.saveScreenshot(opt.screenshot);
            std::cout << (ok ? "Saved " : "Failed to save ") << opt.screenshot << "\n";
            renderer.getWindow().close();
        }
    }
    return 0;
}
