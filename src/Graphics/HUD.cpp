#include "Graphics/HUD.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace {
const float PI = 3.14159265f;
const float X0 = 924, W0 = 652;   // panel content column

std::string fmt(double v, int prec) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    return os.str();
}

std::string clock(double secs) {
    int s = (int)secs;
    std::ostringstream os;
    os << std::setfill('0') << std::setw(2) << s / 60 << ":" << std::setw(2) << s % 60;
    return os.str();
}

// "% better" of smart vs fixed where lower is better
double improvement(double fixedV, double smartV) {
    if (fixedV <= 1e-6) return 0;
    return 100.0 * (fixedV - smartV) / fixedV;
}

sf::Color eventColor(EventType t) {
    switch (t) {
    case EventType::Preempt: return Theme::SirenRed;
    case EventType::Reroute: return sf::Color(90, 200, 255);
    case EventType::EmergencySpawn: return sf::Color(255, 150, 150);
    case EventType::EmergencyDone: return Theme::Good;
    case EventType::Gridlock: return Theme::Warn;
    case EventType::Incident: return sf::Color(255, 150, 60);
    default: return Theme::Text2;
    }
}
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
void HUD::text(sf::RenderTarget& rt, const sf::Font& f, const std::string& s, unsigned size, float x, float y,
               sf::Color c, int align) {
    sf::Text t(s, f, size);
    t.setFillColor(c);
    sf::FloatRect b = t.getLocalBounds();
    float ox = align == 0 ? 0 : (align == 1 ? b.width / 2 : b.width);
    // y is the top of a capital letter regardless of the glyphs used
    float top = size * 0.28f;
    t.setPosition(std::round(x - ox - b.left), std::round(y - top));
    rt.draw(t);
}

float HUD::textWidth(const sf::Font& f, const std::string& s, unsigned size) {
    sf::Text t(s, f, size);
    return t.getLocalBounds().width;
}

std::vector<std::string> HUD::wrap(const std::string& s, const sf::Font& f, unsigned size, float maxW) {
    std::vector<std::string> lines;
    std::istringstream in(s);
    std::string word, line;
    while (in >> word) {
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(f, trial, size) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void HUD::card(ShapeBatch& b, float x, float y, float w, float h) {
    b.roundRect(x, y, w, h, 10, Theme::CardBorder);
    b.roundRect(x + 1, y + 1, w - 2, h - 2, 9, Theme::Card);
}

void HUD::arrowDelta(ShapeBatch& b, float x, float y, bool up, sf::Color c) {
    if (up) b.tri({x, y + 7}, {x + 8, y + 7}, {x + 4, y}, c);
    else b.tri({x, y}, {x + 8, y}, {x + 4, y + 7}, c);
}

bool HUD::hovered(const sf::FloatRect& r) const { return r.contains(mouse); }

void HUD::hint(const sf::FloatRect& r, const std::string& title, const std::string& body) {
    hints.push_back({r, title, body});
}

void HUD::mapHint(const std::string& title, const std::string& body) {
    hints.push_back({sf::FloatRect(mouse.x - 3, mouse.y - 3, 6, 6), title, body});
}

void HUD::beginFrame(const HudState& s) {
    buttons.clear();
    hints.clear();
    modalActive = false;
    mouse = s.mouse;
}

UiAction HUD::hit(sf::Vector2f m) const {
    bool anyModal = false;
    for (const Button& b : buttons) anyModal = anyModal || b.modal;
    for (int i = (int)buttons.size() - 1; i >= 0; i--) {
        if (anyModal && !buttons[i].modal) continue;
        if (buttons[i].r.contains(m)) return buttons[i].action;
    }
    return UiAction::None;
}

bool HUD::isClickable(sf::Vector2f m) const { return hit(m) != UiAction::None; }

bool HUD::overUi(sf::Vector2f m) const {
    for (const Button& b : buttons)
        if (b.r.contains(m)) return true;
    return false;
}

// Simple vector icons (no image files needed)
void HUD::drawIcon(ShapeBatch& b, Icon icon, sf::Vector2f c, float s, sf::Color col) {
    float h = s / 2;
    switch (icon) {
    case Icon::Ambulance:
        b.rect(c.x - h * 0.3f, c.y - h, h * 0.6f, s, col);
        b.rect(c.x - h, c.y - h * 0.3f, s, h * 0.6f, col);
        break;
    case Icon::Warning:
        b.tri({c.x, c.y - h}, {c.x + h * 1.1f, c.y + h * 0.85f}, {c.x - h * 1.1f, c.y + h * 0.85f}, col);
        b.rect(c.x - 1, c.y - h * 0.35f, 2, h * 0.65f, sf::Color(30, 20, 10));
        b.rect(c.x - 1, c.y + h * 0.45f, 2, 2, sf::Color(30, 20, 10));
        break;
    case Icon::Star: {
        std::vector<sf::Vector2f> pts;
        for (int i = 0; i < 10; i++) {
            float r = (i % 2 == 0) ? h : h * 0.45f;
            float a = -PI / 2 + i * PI / 5;
            pts.push_back(c + sf::Vector2f(cosf(a), sinf(a)) * r);
        }
        b.fan(c, pts, col);
        break;
    }
    case Icon::Play:
        b.tri({c.x - h * 0.6f, c.y - h}, {c.x + h, c.y}, {c.x - h * 0.6f, c.y + h}, col);
        break;
    case Icon::Pause:
        b.rect(c.x - h * 0.8f, c.y - h, h * 0.55f, s, col);
        b.rect(c.x + h * 0.25f, c.y - h, h * 0.55f, s, col);
        break;
    case Icon::Restart:
        b.ring(c, h * 0.62f, h, -PI * 0.35f, PI * 1.45f, col, 24);
        b.tri({c.x + h * 0.25f, c.y - h * 1.2f}, {c.x + h * 1.05f, c.y - h * 0.55f}, {c.x + h * 0.1f, c.y - h * 0.15f}, col);
        break;
    case Icon::Plus:
        b.rect(c.x - h, c.y - 1.2f, s, 2.4f, col);
        b.rect(c.x - 1.2f, c.y - h, 2.4f, s, col);
        break;
    case Icon::Minus:
        b.rect(c.x - h, c.y - 1.2f, s, 2.4f, col);
        break;
    case Icon::Split:
        b.rect(c.x - h, c.y - h * 0.7f, h * 0.85f, h * 1.4f, col);
        b.rect(c.x + h * 0.15f, c.y - h * 0.7f, h * 0.85f, h * 1.4f, col);
        break;
    case Icon::Heat:
        b.rect(c.x - h, c.y + h * 0.2f, h * 0.5f, h * 0.8f, Theme::Good);
        b.rect(c.x - h * 0.25f, c.y - h * 0.3f, h * 0.5f, h * 1.3f, Theme::Warn);
        b.rect(c.x + h * 0.5f, c.y - h, h * 0.5f, s, Theme::Bad);
        break;
    case Icon::Graph:
        b.line({c.x - h * 0.7f, c.y + h * 0.5f}, {c.x, c.y - h * 0.6f}, 1.4f, col);
        b.line({c.x, c.y - h * 0.6f}, {c.x + h * 0.7f, c.y + h * 0.5f}, 1.4f, col);
        b.line({c.x - h * 0.7f, c.y + h * 0.5f}, {c.x + h * 0.7f, c.y + h * 0.5f}, 1.4f, col);
        b.circle({c.x - h * 0.7f, c.y + h * 0.5f}, h * 0.32f, col, 10);
        b.circle({c.x, c.y - h * 0.6f}, h * 0.32f, col, 10);
        b.circle({c.x + h * 0.7f, c.y + h * 0.5f}, h * 0.32f, col, 10);
        break;
    case Icon::Moon:
        b.ring(c, h * 0.45f, h, PI * 0.35f, PI * 1.65f, col, 20);
        break;
    case Icon::Camera:
        b.ring(c, h * 0.55f, h * 0.8f, 0, 2 * PI, col, 20);
        b.rect(c.x - 1, c.y - h, 2, h * 0.4f, col);
        b.rect(c.x - 1, c.y + h * 0.6f, 2, h * 0.4f, col);
        b.rect(c.x - h, c.y - 1, h * 0.4f, 2, col);
        b.rect(c.x + h * 0.6f, c.y - 1, h * 0.4f, 2, col);
        break;
    case Icon::Legend:
        for (int i = 0; i < 3; i++) {
            float y = c.y - h * 0.7f + i * h * 0.7f;
            b.circle({c.x - h * 0.7f, y}, h * 0.2f, col, 8);
            b.rect(c.x - h * 0.3f, y - 1, h * 1.2f, 2, col);
        }
        break;
    case Icon::Help:
        b.ring(c, h * 0.8f, h, 0, 2 * PI, col, 20);
        break;
    case Icon::Car:
        b.roundRect(c.x - h, c.y - h * 0.5f, s, h, 3, col);
        b.rect(c.x + h * 0.15f, c.y - h * 0.38f, h * 0.35f, h * 0.76f, sf::Color(30, 36, 50));
        break;
    case Icon::Siren:
        b.ring(c + sf::Vector2f(0, h * 0.3f), 0, h * 0.6f, PI, 2 * PI, col, 12);
        b.rect(c.x - h * 0.8f, c.y + h * 0.3f, h * 1.6f, h * 0.3f, col);
        b.line({c.x - h, c.y - h * 0.5f}, {c.x - h * 0.7f, c.y - h * 0.2f}, 1.3f, col);
        b.line({c.x + h, c.y - h * 0.5f}, {c.x + h * 0.7f, c.y - h * 0.2f}, 1.3f, col);
        b.line({c.x, c.y - h}, {c.x, c.y - h * 0.6f}, 1.3f, col);
        break;
    case Icon::Shield: {
        std::vector<sf::Vector2f> pts = {{c.x - h * 0.8f, c.y - h * 0.8f}, {c.x + h * 0.8f, c.y - h * 0.8f},
                                         {c.x + h * 0.7f, c.y + h * 0.1f}, {c.x, c.y + h}, {c.x - h * 0.7f, c.y + h * 0.1f}};
        b.fan(c, pts, col);
        break;
    }
    case Icon::Download:
        b.rect(c.x - 1.2f, c.y - h, 2.4f, h * 1.1f, col);
        b.tri({c.x - h * 0.6f, c.y - h * 0.1f}, {c.x + h * 0.6f, c.y - h * 0.1f}, {c.x, c.y + h * 0.55f}, col);
        b.rect(c.x - h, c.y + h * 0.75f, s, 2, col);
        break;
    case Icon::Close:
        b.line({c.x - h * 0.7f, c.y - h * 0.7f}, {c.x + h * 0.7f, c.y + h * 0.7f}, 2.2f, col);
        b.line({c.x + h * 0.7f, c.y - h * 0.7f}, {c.x - h * 0.7f, c.y + h * 0.7f}, 2.2f, col);
        break;
    case Icon::Arrow:
        b.rect(c.x - h, c.y - 1.2f, h * 1.3f, 2.4f, col);
        b.tri({c.x + h, c.y}, {c.x + h * 0.2f, c.y - h * 0.6f}, {c.x + h * 0.2f, c.y + h * 0.6f}, col);
        break;
    default: break;
    }
}

void HUD::button(sf::RenderTarget& rt, const sf::FloatRect& r, const std::string& label, Icon icon, UiAction a,
                 Style style, bool active, const std::string& tipTitle, const std::string& tipBody, sf::Color accent,
                 unsigned size) {
    bool hov = hovered(r);
    float rad = std::min(r.height / 2, 10.0f);
    ShapeBatch b;
    sf::Color fg;
    switch (style) {
    case Style::Primary:
        b.roundRect(r.left, r.top + 2, r.width, r.height, rad, shade(accent, 0.55f));
        b.roundRect(r.left, r.top + (hov ? 1 : 0), r.width, r.height, rad, hov ? shade(accent, 1.18f) : accent);
        fg = sf::Color::White;
        break;
    case Style::Normal:
        b.roundRect(r.left, r.top, r.width, r.height, rad, hov ? sf::Color(70, 82, 116) : sf::Color(48, 57, 82));
        b.roundRect(r.left + 1, r.top + 1, r.width - 2, r.height - 2, rad - 1, hov ? sf::Color(58, 69, 99) : sf::Color(36, 43, 62));
        fg = Theme::Text;
        break;
    case Style::Ghost:
        if (hov) b.roundRect(r.left, r.top, r.width, r.height, rad, sf::Color(48, 57, 82));
        fg = hov ? Theme::Text : Theme::Text2;
        break;
    case Style::Toggle:
        if (active) {
            b.roundRect(r.left, r.top, r.width, r.height, rad, hov ? shade(accent, 1.15f) : accent);
            fg = sf::Color::White;
        } else {
            b.roundRect(r.left, r.top, r.width, r.height, rad, hov ? sf::Color(62, 73, 104) : sf::Color(40, 48, 70));
            b.roundRect(r.left + 1, r.top + 1, r.width - 2, r.height - 2, rad - 1, hov ? sf::Color(44, 52, 76) : sf::Color(26, 31, 45));
            fg = hov ? Theme::Text : Theme::Text2;
        }
        break;
    }
    float iconW = icon == Icon::None ? 0 : 16;
    float tw = label.empty() ? 0 : textWidth(fonts.bold, label, size);
    float content = iconW + (iconW > 0 && tw > 0 ? 7 : 0) + tw;
    float startX = r.left + (r.width - content) / 2;
    float cy = r.top + r.height / 2 + (style == Style::Primary && hov ? 1 : 0);
    if (icon != Icon::None) drawIcon(b, icon, {startX + 8, cy}, 13, fg);
    b.draw(rt);
    if (icon == Icon::Help) text(rt, fonts.bold, "?", 11, startX + 8, cy - 4, fg, 1);
    if (!label.empty()) text(rt, fonts.bold, label, size, startX + iconW + (iconW > 0 ? 7 : 0), cy - size * 0.36f, fg);
    buttons.push_back({r, a, modalActive, tipTitle, tipBody});
}

void HUD::drawTooltip(sf::RenderTarget& rt, const HudState& s) {
    rt.setView(rt.getDefaultView());
    std::string title, body;
    bool anyModal = false;
    for (const Button& b : buttons) anyModal = anyModal || b.modal;
    for (int i = (int)buttons.size() - 1; i >= 0; i--) {
        if (anyModal && !buttons[i].modal) continue;
        if (buttons[i].r.contains(mouse)) {
            title = buttons[i].tipTitle;
            body = buttons[i].tipBody;
            break;
        }
    }
    if (title.empty() && body.empty() && !anyModal) {
        for (int i = (int)hints.size() - 1; i >= 0; i--)
            if (hints[i].r.contains(mouse)) {
                title = hints[i].title;
                body = hints[i].body;
                break;
            }
    }
    std::string key = title + "|" + body;
    if (title.empty() && body.empty()) { hoverKey.clear(); return; }
    if (key != hoverKey) { hoverKey = key; hoverSince = s.time; return; }
    if (s.time - hoverSince < 0.3f) return;

    std::vector<std::string> lines = wrap(body, fonts.ui, 12, 270);
    float w = textWidth(fonts.bold, title, 13);
    for (auto& l : lines) w = std::max(w, textWidth(fonts.ui, l, 12));
    w += 26;
    float h = 16 + (title.empty() ? 0 : 20) + lines.size() * 17 + 6;
    float x = mouse.x + 16, y = mouse.y + 20;
    if (x + w > 1592) x = mouse.x - w - 10;
    if (y + h > 892) y = mouse.y - h - 10;
    ShapeBatch b;
    b.roundRect(x + 3, y + 4, w, h, 8, sf::Color(0, 0, 0, 110));
    b.roundRect(x, y, w, h, 8, sf::Color(90, 104, 140));
    b.roundRect(x + 1, y + 1, w - 2, h - 2, 7, sf::Color(14, 17, 25, 250));
    b.draw(rt);
    float ty = y + 12;
    if (!title.empty()) {
        text(rt, fonts.bold, title, 13, x + 13, ty, Theme::Text);
        ty += 20;
    }
    for (auto& l : lines) {
        text(rt, fonts.ui, l, 12, x + 13, ty, Theme::Text2);
        ty += 17;
    }
}

// ---------------------------------------------------------------------------
// Main side panel
// ---------------------------------------------------------------------------
void HUD::draw(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, PlayerController& p, const HudState& s) {
    rt.setView(rt.getDefaultView());
    ShapeBatch bg;
    bg.rect(PANEL_X, 0, 700, 900, Theme::Panel);
    bg.rect(PANEL_X, 0, 1, 900, Theme::CardBorder);
    bg.draw(rt);

    CityGrid& shown = s.smartShown ? smartG : fixedG;
    CityGrid& other = s.smartShown ? fixedG : smartG;
    drawHeader(rt, fixedG, smartG, s);
    drawKpis(rt, shown, other, s, 184);
    drawChart(rt, fixedG, smartG, s, X0, 330, W0, 164);
    drawCompare(rt, fixedG, smartG, X0, 502, W0, 124);
    drawJunctions(rt, shown, s, X0, 634, 256, 170);
    drawEvents(rt, shown, 1190, 634, 386, 170);
    drawPlayer(rt, p, s, X0, 812, W0, 78);
}

void HUD::drawHeader(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, const HudState& s) {
    text(rt, fonts.bold, "SMART TRAFFIC FLOW", 26, X0, 16, Theme::Text);

    // one plain-English sentence that sums up the result so far
    std::string verdict = "Two identical cities, same cars: old fixed-timer lights vs. our smart engine.";
    sf::Color vc = Theme::Text2;
    if (smartG.getSimTime() > 30 && fixedG.getTotalClearedVehicles() > 20) {
        double w = improvement(fixedG.getAverageWaitTime(), smartG.getAverageWaitTime());
        verdict = "So far smart lights cut waiting by " + fmt(std::max(0.0, w), 0) + "%";
        if (fixedG.getEmergencyTrips() > 0 && smartG.getEmergencyTrips() > 0) {
            double e = improvement(fixedG.getEmergencyAvgDelay(), smartG.getEmergencyAvgDelay());
            verdict += " and ambulances get stuck " + fmt(std::max(0.0, e), 0) + "% less";
        }
        verdict += ".";
        vc = sf::Color(140, 225, 170);
    }
    text(rt, fonts.ui, verdict, 13, X0, 51, vc);

    text(rt, fonts.mono, clock(smartG.getSimTime()), 22, 1576, 16, Theme::Text, 2);
    std::string sub = s.paused ? "PAUSED" : "simulated time  |  " + fmt(s.simSpeed, s.simSpeed < 1 ? 1 : 0) + "x speed";
    text(rt, fonts.ui, sub, 11, 1576, 46, s.paused ? Theme::Warn : Theme::Muted, 2);

    // which city is on screen
    button(rt, sf::FloatRect(X0, 74, 322, 42), "OLD WAY  -  fixed timers", Icon::None, UiAction::ModeFixed, Style::Toggle,
           !s.smartShown, "Old way (Mode 1)",
           "Classic traffic lights: every green lasts a fixed 15 s no matter how many cars wait. No help for ambulances. Key: M",
           Theme::Static, 15);
    button(rt, sf::FloatRect(X0 + 330, 74, 322, 42), "SMART WAY  -  our DSA engine", Icon::None, UiAction::ModeSmart,
           Style::Toggle, s.smartShown, "Smart way (Mode 2)",
           "Lights give longer greens to busier roads, turn green for ambulances (Min-Heap) and send cars around jams "
           "(Dijkstra). Key: M", Theme::Smart, 15);

    // row 1: traffic level, side by side, legend, tour
    text(rt, fonts.bold, "Traffic", 12, X0, 130, Theme::Muted);
    const char* lv[3] = {"Light", "Normal", "Rush hour"};
    UiAction la[3] = {UiAction::Demand0, UiAction::Demand1, UiAction::Demand2};
    const char* lt[3] = {"Few cars on the road. Key: 1", "Everyday traffic. Key: 2", "Lots of cars: the old way starts to jam. Key: 3"};
    float wds[3] = {62, 72, 92};
    float bx = X0 + 52;
    for (int i = 0; i < 3; i++) {
        button(rt, sf::FloatRect(bx, 122, wds[i], 26), lv[i], Icon::None, la[i], Style::Toggle, s.demandLevel == i,
               std::string(lv[i]) + " traffic", lt[i], sf::Color(84, 98, 140), 12);
        bx += wds[i] + 4;
    }
    button(rt, sf::FloatRect(1220, 122, 132, 26), "Side by side", Icon::Split, UiAction::Split, Style::Toggle, s.split,
           "Side by side", "Watch both cities at the same time with a live scoreboard. Key: V", Theme::Smart, 12);
    button(rt, sf::FloatRect(1358, 122, 96, 26), "Legend", Icon::Legend, UiAction::Legend, Style::Toggle, s.legend,
           "Map legend", "Shows what every vehicle and symbol on the map means.", Theme::Smart, 12);
    button(rt, sf::FloatRect(1460, 122, 116, 26), "Take a tour", Icon::Help, UiAction::Tour, Style::Toggle, false,
           "Guided tour", "A one-minute walkthrough of everything on screen.", Theme::Smart, 12);

    // row 2: map overlays
    text(rt, fonts.bold, "Show", 12, X0, 162, Theme::Muted);
    struct T { const char* label; Icon icon; UiAction a; bool on; const char* tip; const char* body; };
    T ts[4] = {
        {"Traffic heatmap", Icon::Heat, UiAction::Heatmap, s.heatmap, "Traffic heatmap",
         "Colours every road by how full it is: green = free, yellow = busy, red = jammed (80%+ full triggers a detour). Key: H"},
        {"Road graph", Icon::Graph, UiAction::Graph, s.graph, "Road graph",
         "Shows the city as a graph: crossings are nodes, roads are edges. The numbers are the live travel costs Dijkstra uses. Key: G"},
        {"Night mode", Icon::Moon, UiAction::Night, s.night, "Night mode", "Headlights, street lamps and glowing signals. Key: N"},
        {"Follow my car", Icon::Camera, UiAction::Chase, s.chase, "Follow my car",
         "Zooms the camera in on your car. Scroll the mouse wheel to zoom. Key: C"},
    };
    bx = X0 + 52;
    for (int i = 0; i < 4; i++) {
        float w = (W0 - 52 - 3 * 6) / 4;
        button(rt, sf::FloatRect(bx, 154, w, 26), ts[i].label, ts[i].icon, ts[i].a, Style::Toggle, ts[i].on, ts[i].tip,
               ts[i].body, Theme::Smart, 12);
        bx += w + 6;
    }
}

void HUD::drawKpis(sf::RenderTarget& rt, CityGrid& shown, CityGrid& other, const HudState& s, float y0) {
    struct Kpi { std::string label; double v, o; std::string unit; int prec; bool lowerBetter; std::string sub, tip; };
    Kpi k[6] = {
        {"WAIT AT RED LIGHTS", shown.getRollingWaitTime(), other.getRollingWaitTime(), "s", 1, true, "per car, last minute",
         "How long a car waits at each red light, on average, over the last minute. Lower is better."},
        {"CARS THROUGH", shown.getThroughput(), other.getThroughput(), "/min", 0, false, "finishing their trip",
         "How many cars finish their trip and leave the city each minute. Higher is better."},
        {"ROADS FILLED", shown.getCongestionIndex(), other.getCongestionIndex(), "%", 0, true, "of road space in use",
         "Congestion index: how much of the waiting space on all roads is taken, counted in car-equivalents (a bus = 3 cars). Lower is better."},
        {"AMBULANCE TIME STUCK", shown.getEmergencyAvgDelay(), other.getEmergencyAvgDelay(), "s", 1, true,
         std::to_string(shown.getEmergencyTrips()) + " runs so far",
         "Average seconds an ambulance, fire truck or police car spent standing still on its trip. Every second counts!"},
        {"POLLUTION FROM IDLING", shown.getIdleCO2kg(), other.getIdleCO2kg(), "kg CO2", 1, true, "engines running in queues",
         "Estimated CO2 from engines idling in traffic (about 0.39 g per second per car). Less waiting = cleaner air."},
        {"CARS STUCK OUTSIDE", (double)shown.getPendingVehicles(), (double)other.getPendingVehicles(), "cars", 0, true,
         std::to_string(shown.getActiveVehicles()) + " cars inside the city",
         "Cars that cannot even enter the city because the entry roads are full. Shown as orange badges at the map edges."},
    };
    std::string otherName = s.smartShown ? "old way" : "smart way";

    ShapeBatch b;
    for (int i = 0; i < 6; i++) {
        float x = X0 + (i % 3) * 222, y = y0 + (i / 3) * 72;
        sf::FloatRect r(x, y, 212, 64);
        card(b, x, y, 212, 64);
        if (hovered(r)) b.roundRect(x, y, 212, 64, 10, sf::Color(255, 255, 255, 14));
    }
    b.draw(rt);

    for (int i = 0; i < 6; i++) {
        float x = X0 + (i % 3) * 222, y = y0 + (i / 3) * 72;
        hint(sf::FloatRect(x, y, 212, 64), k[i].label, k[i].tip);
        text(rt, fonts.bold, k[i].label, 10, x + 12, y + 10, Theme::Muted);
        std::string val = fmt(k[i].v, k[i].prec);
        text(rt, fonts.bold, val, 23, x + 12, y + 27, Theme::Text);
        float vw = textWidth(fonts.bold, val, 23);
        text(rt, fonts.ui, k[i].unit, 12, x + 16 + vw, y + 36, Theme::Text2);

        double a = k[i].v, o = k[i].o;
        if (shown.getSimTime() > 20 && (a > 0.05 || o > 0.05)) {
            bool better = k[i].lowerBetter ? a < o : a > o;
            bool same = fabs(a - o) < 1e-3 + 0.03 * std::max(a, o);
            std::string d;
            if (same) d = "same as " + otherName;
            else if (o < 0.05 * a) d = "vs " + fmt(o, k[i].prec) + " in " + otherName;
            else if (a > 3 * o) d = fmt(a / o, 1) + "x the " + otherName;
            else d = fmt(fabs(100.0 * (a - o) / o), 0) + (a < o ? "% less than " : "% more than ") + otherName;
            sf::Color c = same ? Theme::Muted : (better ? Theme::Good : Theme::Bad);
            float dw = textWidth(fonts.ui, d, 11);
            ShapeBatch ab;
            if (!same) arrowDelta(ab, x + 200 - dw - 12, y + 48, a > o, c);
            ab.draw(rt);
            text(rt, fonts.ui, d, 11, x + 200, y + 48, c, 2);
        } else {
            text(rt, fonts.ui, k[i].sub, 11, x + 200, y + 48, Theme::Muted, 2);
        }
    }
}

void HUD::drawChart(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, const HudState& s,
                    float x, float y, float w, float h) {
    ShapeBatch b;
    card(b, x, y, w, h);
    b.draw(rt);
    text(rt, fonts.bold, "How long cars wait at red lights", 14, x + 16, y + 13, Theme::Text);
    text(rt, fonts.ui, "Both cities, live. Lower is better. Hover the lines for exact values.", 11, x + 16, y + 32, Theme::Muted);
    hint(sf::FloatRect(x, y, 330, 44), "Waiting-time chart",
         "Each line shows the average wait at a red light over the last minute. The gap between orange and blue is the "
         "time our engine saves every driver.");

    float lx = x + w - 16;
    const char* names[2] = {"Smart way", "Old way"};
    sf::Color cols[2] = {Theme::Smart, Theme::Static};
    ShapeBatch lb;
    std::vector<std::pair<float, int>> legendPos;
    for (int i = 0; i < 2; i++) {
        float tw = textWidth(fonts.ui, names[i], 12);
        lx -= tw;
        legendPos.push_back({lx, i});
        lb.roundRect(lx - 22, y + 18, 16, 3, 1.5f, cols[i]);
        lx -= 36;
    }
    lb.draw(rt);
    for (auto& lp : legendPos) text(rt, fonts.ui, names[lp.second], 12, lp.first, y + 14, Theme::Text2);

    float px = x + 46, py = y + 52, pw = w - 110, ph = h - 78;
    std::vector<MetricSample>& hf = fixedG.getHistory();
    std::vector<MetricSample>& hs = smartG.getHistory();
    double now = std::max(fixedG.getSimTime(), 1.0);
    const double window = 300;
    double t0 = std::max(0.0, now - window);
    double maxV = 4;
    for (auto& m : hf) if (m.time >= t0) maxV = std::max(maxV, m.avgWait);
    for (auto& m : hs) if (m.time >= t0) maxV = std::max(maxV, m.avgWait);
    double step = maxV <= 6 ? 2 : (maxV <= 12 ? 4 : (maxV <= 30 ? 10 : (maxV <= 60 ? 20 : 50)));
    double yMax = std::ceil(maxV * 1.1 / step) * step;

    auto X = [&](double t) { return px + (float)((t - t0) / window) * pw; };
    auto Y = [&](double v) { return py + ph - (float)(v / yMax) * ph; };

    ShapeBatch gb;
    for (double v = 0; v <= yMax + 1e-6; v += step) gb.rect(px, Y(v), pw, 1, v == 0 ? sf::Color(70, 80, 104) : sf::Color(40, 48, 66));
    gb.draw(rt);
    for (double v = 0; v <= yMax + 1e-6; v += step) text(rt, fonts.mono, fmt(v, 0) + "s", 11, px - 8, Y(v) - 4, Theme::Muted, 2);
    text(rt, fonts.mono, now > window ? "5 min ago" : "start", 11, px, py + ph + 8, Theme::Muted);
    text(rt, fonts.mono, "now", 11, px + pw, py + ph + 8, Theme::Muted, 2);

    ShapeBatch lines;
    std::vector<MetricSample>* series[2] = {&hf, &hs};
    sf::Color sc[2] = {Theme::Static, Theme::Smart};
    float endY[2] = {0, 0};
    double endV[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        sf::Vector2f prev;
        bool has = false;
        for (auto& m : *series[k]) {
            if (m.time < t0) continue;
            sf::Vector2f p(X(m.time), Y(m.avgWait));
            if (has) lines.line(prev, p, 2.2f, sc[k]);
            prev = p;
            has = true;
            endV[k] = m.avgWait;
        }
        if (has) {
            lines.circle(prev, 5, Theme::Card, 14);
            lines.circle(prev, 3.5f, sc[k], 14);
            endY[k] = prev.y;
        }
    }
    lines.draw(rt);
    if (!hf.empty() && !hs.empty()) {
        float a = endY[0], c = endY[1];
        if (fabs(a - c) < 14) {
            float mid = (a + c) / 2;
            if (a <= c) { a = mid - 7; c = mid + 7; } else { a = mid + 7; c = mid - 7; }
        }
        text(rt, fonts.bold, fmt(endV[0], 1) + "s", 12, px + pw + 10, a - 5, Theme::Text);
        text(rt, fonts.bold, fmt(endV[1], 1) + "s", 12, px + pw + 10, c - 5, Theme::Text);
    }

    sf::Vector2f m = s.mouse;
    if (m.x >= px && m.x <= px + pw && m.y >= py - 6 && m.y <= py + ph + 6 && !hf.empty()) {
        double t = t0 + (m.x - px) / pw * window;
        auto nearest = [&](std::vector<MetricSample>& hv) -> MetricSample* {
            MetricSample* best = NULL;
            double bd = 1e9;
            for (auto& smp : hv) {
                double d = fabs(smp.time - t);
                if (d < bd) { bd = d; best = &smp; }
            }
            return best;
        };
        MetricSample* a = nearest(hf);
        MetricSample* c = nearest(hs);
        if (a && c) {
            float cxp = X(a->time);
            ShapeBatch hb;
            hb.rect(cxp, py, 1, ph, sf::Color(200, 210, 230, 120));
            hb.circle({cxp, Y(a->avgWait)}, 4.5f, Theme::Static, 12);
            hb.circle({X(c->time), Y(c->avgWait)}, 4.5f, Theme::Smart, 12);
            float tx = std::min(cxp + 12, px + pw - 160), ty = py + 2;
            hb.roundRect(tx, ty, 160, 62, 7, sf::Color(8, 10, 16, 240));
            hb.roundRect(tx + 10, ty + 30, 10, 3, 1.5f, Theme::Static);
            hb.roundRect(tx + 10, ty + 48, 10, 3, 1.5f, Theme::Smart);
            hb.draw(rt);
            text(rt, fonts.mono, "at " + clock(a->time), 11, tx + 10, ty + 8, Theme::Text2);
            text(rt, fonts.ui, "Old way", 12, tx + 26, ty + 26, Theme::Text2);
            text(rt, fonts.bold, fmt(a->avgWait, 1) + " s", 12, tx + 150, ty + 26, Theme::Text, 2);
            text(rt, fonts.ui, "Smart way", 12, tx + 26, ty + 44, Theme::Text2);
            text(rt, fonts.bold, fmt(c->avgWait, 1) + " s", 12, tx + 150, ty + 44, Theme::Text, 2);
        }
    }
}

void HUD::drawCompare(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, float x, float y, float w, float h) {
    ShapeBatch b;
    card(b, x, y, w, h);
    b.draw(rt);
    text(rt, fonts.bold, "Scoreboard since the start", 14, x + 16, y + 11, Theme::Text);
    hint(sf::FloatRect(x, y, w, h), "Scoreboard",
         "Totals since the simulation started. Orange bar = old way, blue bar = smart way. Green percentage = how much "
         "better the smart city did.");

    struct Row { std::string name; double f, s; bool lowerBetter; std::string unit; };
    const int N = 5;
    Row rows[N] = {
        {"Wait at red lights", fixedG.getAverageWaitTime(), smartG.getAverageWaitTime(), true, "s"},
        {"Delay per trip", fixedG.getAverageTripDelay(), smartG.getAverageTripDelay(), true, "s"},
        {"Ambulance time stuck", fixedG.getEmergencyAvgDelay(), smartG.getEmergencyAvgDelay(), true, "s"},
        {"Pollution (CO2)", fixedG.getIdleCO2kg(), smartG.getIdleCO2kg(), true, "kg"},
        {"Trips completed", (double)fixedG.getTripsDone(), (double)smartG.getTripsDone(), false, ""},
    };
    float barX = x + 170, barW = w - 310;
    ShapeBatch bars;
    for (int i = 0; i < N; i++) {
        float ry = y + 34 + i * 17.5f;
        double mx = std::max(std::max(rows[i].f, rows[i].s), 1e-6);
        float wf = (float)(rows[i].f / mx) * barW, ws = (float)(rows[i].s / mx) * barW;
        bars.rect(barX, ry, barW, 6, sf::Color(34, 40, 56));
        bars.rect(barX, ry + 8, barW, 6, sf::Color(34, 40, 56));
        if (wf > 0.5f) bars.roundRect(barX, ry, std::max(wf, 3.0f), 6, 2, Theme::Static);
        if (ws > 0.5f) bars.roundRect(barX, ry + 8, std::max(ws, 3.0f), 6, 2, Theme::Smart);
    }
    bars.draw(rt);
    for (int i = 0; i < N; i++) {
        float ry = y + 34 + i * 17.5f;
        text(rt, fonts.ui, rows[i].name, 12, x + 16, ry + 2, Theme::Text2);
        int prec = rows[i].unit.empty() ? 0 : 1;
        text(rt, fonts.mono, fmt(rows[i].f, prec) + " / " + fmt(rows[i].s, prec) + rows[i].unit, 11, barX + barW + 10, ry + 2, Theme::Text2);
        double imp = rows[i].lowerBetter ? improvement(rows[i].f, rows[i].s)
                                         : (rows[i].f > 0 ? 100.0 * (rows[i].s - rows[i].f) / rows[i].f : 0);
        if (fabs(imp) >= 0.5 && smartG.getSimTime() > 20) {
            bool good = imp > 0;
            std::string t = (rows[i].lowerBetter ? (good ? "-" : "+") : (good ? "+" : "-")) + fmt(fabs(imp), 0) + "%";
            text(rt, fonts.bold, t, 12, x + w - 16, ry + 2, good ? Theme::Good : Theme::Bad, 2);
        }
    }
    ShapeBatch lg;
    lg.roundRect(x + w - 190, y + 16, 10, 4, 2, Theme::Static);
    lg.roundRect(x + w - 104, y + 16, 10, 4, 2, Theme::Smart);
    lg.draw(rt);
    text(rt, fonts.ui, "Old way", 11, x + w - 176, y + 13, Theme::Muted);
    text(rt, fonts.ui, "Smart", 11, x + w - 90, y + 13, Theme::Muted);
}

void HUD::drawJunctions(sf::RenderTarget& rt, CityGrid& g, const HudState& s, float x, float y, float w, float h) {
    ShapeBatch b;
    card(b, x, y, w, h);
    b.draw(rt);
    text(rt, fonts.bold, "Traffic lights", 14, x + 14, y + 11, Theme::Text);
    text(rt, fonts.ui, "click one to look inside", 11, x + w - 14, y + 13, Theme::Muted, 2);

    int R = g.getRows(), C = g.getCols();
    float gap = 6, cw = (w - 28 - gap * (C - 1)) / C, ch = (h - 42 - gap * (R - 1)) / R;
    ShapeBatch cells;
    for (int j = 0; j < g.getNumIntersections(); j++) {
        Intersection& J = g.getIntersection(j);
        TrafficSignal& S = J.getSignal();
        float cx = x + 14 + (j % C) * (cw + gap), cy = y + 34 + (j / C) * (ch + gap);
        sf::FloatRect cell(cx, cy, cw, ch);
        sf::Color pc = S.getPhase() == SignalPhase::Green ? Theme::SignalGreen
                     : S.getPhase() == SignalPhase::Yellow ? Theme::SignalYellow : Theme::SignalRed;
        if (S.getIsPreempted()) {
            sf::Color sc = fmodf(s.time, 0.5f) < 0.25f ? Theme::SirenRed : Theme::SirenBlue;
            cells.roundRect(cx - 1.5f, cy - 1.5f, cw + 3, ch + 3, 7, sc);
        }
        cells.roundRect(cx, cy, cw, ch, 6, hovered(cell) ? sf::Color(44, 54, 80) : sf::Color(18, 22, 32));
        sf::Vector2f c(cx + 15, cy + ch / 2);
        if (S.getGreenAxis() == 0) {
            cells.line({c.x, c.y - 8}, {c.x, c.y + 8}, 2, pc);
            cells.tri({c.x, c.y - 11}, {c.x - 4, c.y - 5}, {c.x + 4, c.y - 5}, pc);
            cells.tri({c.x, c.y + 11}, {c.x - 4, c.y + 5}, {c.x + 4, c.y + 5}, pc);
        } else {
            cells.line({c.x - 8, c.y}, {c.x + 8, c.y}, 2, pc);
            cells.tri({c.x - 11, c.y}, {c.x - 5, c.y - 4}, {c.x - 5, c.y + 4}, pc);
            cells.tri({c.x + 11, c.y}, {c.x + 5, c.y - 4}, {c.x + 5, c.y + 4}, pc);
        }
        double load = J.axisLoad(0) + J.axisLoad(1);
        float lw = (float)std::min(1.0, load / 24.0) * (cw - 36);
        cells.rect(cx + 30, cy + ch - 9, cw - 36, 3, sf::Color(40, 48, 66));
        cells.rect(cx + 30, cy + ch - 9, lw, 3, load > 16 ? Theme::Bad : (load > 8 ? Theme::Warn : Theme::Good));

        std::string dir = S.getGreenAxis() == 0 ? "north-south" : "east-west";
        std::string ph = S.getPhase() == SignalPhase::Green ? "Green for " + dir
                       : S.getPhase() == SignalPhase::Yellow ? "Yellow, changing soon" : "All red (clearing the crossing)";
        hint(cell, "Junction J" + std::to_string(j) + (S.getIsPreempted() ? "  -  AMBULANCE PRIORITY" : ""),
             ph + ", " + fmt(std::max(0.0, S.getTimer()), 0) + " s left. " + fmt(load, 1) +
             " car-equivalents waiting. Click to look inside its queues.");
    }
    cells.draw(rt);
    for (int j = 0; j < g.getNumIntersections(); j++) {
        Intersection& J = g.getIntersection(j);
        float cx = x + 14 + (j % C) * (cw + gap), cy = y + 34 + (j / C) * (ch + gap);
        if (cw >= 64) text(rt, fonts.bold, "J" + std::to_string(j), 11, cx + 30, cy + 6, Theme::Text2);
        text(rt, fonts.mono, fmt(std::max(0.0, J.getSignal().getTimer()), 0) + "s", 11, cx + cw - 6, cy + 6, Theme::Text, 2);
    }
}

void HUD::drawEvents(sf::RenderTarget& rt, CityGrid& g, float x, float y, float w, float h) {
    ShapeBatch b;
    card(b, x, y, w, h);
    b.draw(rt);
    text(rt, fonts.bold, "What's happening", 14, x + 14, y + 11, Theme::Text);
    std::string stats = std::to_string(g.getTotalPreemptions()) + " green lights for ambulances  |  " +
                        std::to_string(g.getReroutes()) + " detours";
    text(rt, fonts.ui, stats, 11, x + w - 14, y + 13, Theme::Muted, 2);
    hint(sf::FloatRect(x, y, w, 30), "What's happening",
         "A live feed of what the city is doing. Red = a light turned green for an emergency vehicle, blue = a car took "
         "a detour, orange = breakdown, green = an ambulance arrived.");

    std::deque<GridEvent>& ev = g.getEvents();
    int shown = 0;
    ShapeBatch dots;
    std::vector<std::pair<float, const GridEvent*>> rows;
    for (int i = (int)ev.size() - 1; i >= 0 && shown < 7; i--, shown++) {
        float ry = y + 36 + shown * 18.5f;
        dots.circle({x + 18, ry + 5}, 3.5f, eventColor(ev[i].type), 10);
        rows.push_back({ry, &ev[i]});
    }
    dots.draw(rt);
    for (auto& r : rows) {
        text(rt, fonts.mono, clock(r.second->time), 11, x + 28, r.first + 1, Theme::Muted);
        std::string t = r.second->text;
        bool cut = false;
        while (t.size() > 4 && textWidth(fonts.ui, t, 12) > w - 90) { t = t.substr(0, t.size() - 4); cut = true; }
        if (cut) t += "...";
        text(rt, fonts.ui, t, 12, x + 74, r.first, Theme::Text2);
        if (cut) hint(sf::FloatRect(x + 70, r.first - 3, w - 80, 18), "", r.second->text);
    }
    if (ev.empty()) text(rt, fonts.ui, "Quiet so far... try the Send ambulance button on the map.", 12, x + 14, y + 40, Theme::Muted);
}

void HUD::drawPlayer(sf::RenderTarget& rt, PlayerController& p, const HudState& s, float x, float y, float w, float h) {
    ShapeBatch b;
    card(b, x, y, w, h);
    if (p.getCollisionFlash() > 0) b.roundRect(x, y, w, h, 10, sf::Color(255, 60, 60, (int)(90 * p.getCollisionFlash())));
    sf::Vector2f c(x + 40, y + h / 2 + 2);
    float kmh = fabs(p.getSpeedKmh());
    float frac = std::min(1.0f, kmh / 120.0f);
    float a0 = PI * 0.75f, a1 = PI * 2.25f;
    b.ring(c, 22, 26, a0, a1, sf::Color(40, 48, 66), 36);
    b.ring(c, 22, 26, a0, a0 + (a1 - a0) * frac, frac > 0.8f ? Theme::Bad : Theme::Player, 36);
    b.draw(rt);
    text(rt, fonts.bold, std::to_string((int)kmh), 18, c.x, c.y - 8, Theme::Text, 1);
    text(rt, fonts.ui, "km/h", 10, c.x, c.y + 12, Theme::Muted, 1);

    bool amb = p.getType() == PlayerController::Type::Ambulance;
    text(rt, fonts.bold, amb ? "YOUR AMBULANCE" : "YOUR CAR", 13, x + 80, y + 12, amb ? sf::Color(255, 150, 150) : Theme::Text);
    text(rt, fonts.ui, "Drive: W A S D or arrow keys", 11, x + 80, y + 31, Theme::Text2);
    std::string sensor = p.nearestObstacle() < 1e8f ? "Sensor: obstacle " + fmt(p.nearestObstacle() * 0.2f, 1) + " m ahead"
                                                     : "Sensors: road clear";
    sf::Color scol = p.nearestObstacle() < 30 ? Theme::Bad : (p.nearestObstacle() < 1e8f ? Theme::Warn : Theme::Good);
    if (p.getAssistFlash() > 0) { sensor = "Auto-brake saved you!"; scol = Theme::Warn; }
    text(rt, fonts.ui, sensor, 11, x + 80, y + 50, scol);
    hint(sf::FloatRect(x, y, 290, h), "Your vehicle",
         "W / Up = accelerate, S / Down = brake and reverse, A D = steer, Space = handbrake (drift). Five sensor rays "
         "look ahead; auto-brake stops you before a crash.");

    button(rt, sf::FloatRect(x + 296, y + 9, 84, 26), "Car", Icon::Car, UiAction::PlayerCar, Style::Toggle, !amb,
           "Drive a car", "Switch to a normal car. Key: T", Theme::Player, 12);
    button(rt, sf::FloatRect(x + 384, y + 9, 112, 26), "Ambulance", Icon::Ambulance, UiAction::PlayerAmbulance,
           Style::Toggle, amb, "Drive an ambulance", "Switch to an ambulance. Turn on the siren and smart lights go green for you! Key: T",
           Theme::Player, 12);
    button(rt, sf::FloatRect(x + 296, y + 42, 98, 26), p.isSirenOn() ? "Siren on" : "Siren", Icon::Siren, UiAction::Siren,
           Style::Toggle, p.isSirenOn(), "Siren",
           amb ? "Siren on = traffic lights ahead turn green for you (in the smart city) and cars pull over. Key: E"
               : "Switch to the ambulance first.", Theme::SirenRed, 12);
    button(rt, sf::FloatRect(x + 398, y + 42, 98, 26), "Auto-brake", Icon::Shield, UiAction::Assist, Style::Toggle,
           p.isAssistOn(), "Auto-brake", "Uses the car's raycast sensors to brake before you hit something. Key: B",
           Theme::Good, 12);

    text(rt, fonts.ui, "Crashes", 11, x + 520, y + 14, Theme::Muted);
    text(rt, fonts.bold, std::to_string(p.getCollisions()), 20, x + 520, y + 34, p.getCollisionFlash() > 0 ? Theme::Bad : Theme::Text);
    text(rt, fonts.ui, "Red lights", 11, x + 584, y + 14, Theme::Muted);
    text(rt, fonts.bold, std::to_string(p.getViolations()), 20, x + 584, y + 34, p.getViolationFlash() > 0 ? Theme::Bad : Theme::Text);
    (void)s;
}

// ---------------------------------------------------------------------------
// Floating action toolbar on the map
// ---------------------------------------------------------------------------
void HUD::drawToolbar(sf::RenderTarget& rt, const HudState& s, float centerX, float y) {
    rt.setView(rt.getDefaultView());
    float wAmb = 168, wInc = 172, wDemo = 132, wIcon = 36, gap = 6, h = 34;
    float total = wAmb + gap + wInc + gap + wDemo + gap + 8 + 5 * wIcon + 44 + 4 + 4 * gap;
    float x = centerX - total / 2;
    toolbarRect = sf::FloatRect(x - 8, y - 7, total + 16, h + 14);
    ShapeBatch bg;
    bg.roundRect(x - 8 + 3, y - 7 + 4, total + 16, h + 14, 16, sf::Color(0, 0, 0, 110));
    bg.roundRect(x - 8, y - 7, total + 16, h + 14, 16, sf::Color(14, 17, 25, 235));
    bg.draw(rt);
    buttons.push_back({toolbarRect, UiAction::None, modalActive, "", ""});   // swallow clicks between buttons

    button(rt, sf::FloatRect(x, y, wAmb, h), "Send ambulance", Icon::Ambulance, UiAction::Ambulance, Style::Primary, false,
           "Send an ambulance",
           "Dispatches the same ambulance into BOTH cities. Watch the smart city turn its lights green while the old "
           "one keeps it waiting. Key: X", sf::Color(200, 50, 60));
    x += wAmb + gap;
    button(rt, sf::FloatRect(x, y, wInc, h), "Cause breakdown", Icon::Warning, UiAction::Incident, Style::Normal, false,
           "Cause a breakdown",
           "A car breaks down and blocks a road for 60 s in both cities. The smart city sends traffic around it using "
           "Dijkstra's shortest path; the old one jams. Key: I");
    x += wInc + gap;
    button(rt, sf::FloatRect(x, y, wDemo, h), "Play demo", Icon::Star, UiAction::Showcase, Style::Primary, false,
           "Play the demo",
           "Rush hour + a breakdown + an ambulance, shown side by side. The best 60 seconds to show someone. Key: F2",
           Theme::Smart);
    x += wDemo + gap + 8;
    button(rt, sf::FloatRect(x, y, wIcon, h), "", s.paused ? Icon::Play : Icon::Pause, UiAction::Pause, Style::Normal, false,
           s.paused ? "Resume" : "Pause", "Freeze both cities. Key: P or Esc");
    x += wIcon + gap;
    button(rt, sf::FloatRect(x, y, wIcon, h), "", Icon::Minus, UiAction::SpeedDown, Style::Normal, false, "Slower",
           "Slow the simulation down. Key: [");
    x += wIcon + 2;
    hint(sf::FloatRect(x, y, 44, h), "Simulation speed", "How fast time runs. Use - and + to change it.");
    text(rt, fonts.bold, fmt(s.simSpeed, s.simSpeed < 1 ? 1 : 0) + "x", 14, x + 22, y + 11, Theme::Text, 1);
    x += 44 + 2;
    button(rt, sf::FloatRect(x, y, wIcon, h), "", Icon::Plus, UiAction::SpeedUp, Style::Normal, false, "Faster",
           "Speed the simulation up to 8x. Key: ]");
    x += wIcon + gap;
    button(rt, sf::FloatRect(x, y, wIcon, h), "", Icon::Restart, UiAction::Restart, Style::Normal, false, "Restart",
           "Start both cities again from zero. Key: R");
    x += wIcon + gap;
    button(rt, sf::FloatRect(x, y, wIcon, h), "", Icon::Help, UiAction::Help, Style::Normal, false, "Help",
           "All mouse and keyboard controls. Key: F1");
}

// ---------------------------------------------------------------------------
// Side-by-side bottom bar
// ---------------------------------------------------------------------------
void HUD::drawSplitBar(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, PlayerController& p, const HudState& s) {
    rt.setView(rt.getDefaultView());
    ShapeBatch b;
    b.rect(0, 800, 1600, 100, Theme::Panel);
    b.rect(0, 800, 1600, 1, Theme::CardBorder);
    b.draw(rt);
    text(rt, fonts.bold, "SIDE BY SIDE", 18, 24, 816, Theme::Text);
    text(rt, fonts.mono, clock(smartG.getSimTime()) + (s.paused ? "  PAUSED" : ""), 14, 190, 819, s.paused ? Theme::Warn : Theme::Text2);
    text(rt, fonts.ui, "Same cars, same moment, both cities.", 12, 24, 842, Theme::Muted);
    button(rt, sf::FloatRect(24, 860, 170, 28), "Back to one city", Icon::Close, UiAction::Split, Style::Normal, false,
           "Back to one city", "Leave side-by-side view and show the full dashboard again. Key: V", Theme::Smart, 12);

    struct M { std::string name; double f, sm; bool lower; int prec; std::string unit; std::string tip; };
    M ms[5] = {
        {"Wait at red lights", fixedG.getRollingWaitTime(), smartG.getRollingWaitTime(), true, 1, "s", "Average wait per red light over the last minute."},
        {"Cars through", fixedG.getThroughput(), smartG.getThroughput(), false, 0, "/min", "Cars finishing their trip each minute."},
        {"Roads filled", fixedG.getCongestionIndex(), smartG.getCongestionIndex(), true, 0, "%", "Share of road waiting space in use."},
        {"Ambulance time stuck", fixedG.getEmergencyAvgDelay(), smartG.getEmergencyAvgDelay(), true, 1, "s", "Seconds an emergency vehicle stood still per trip."},
        {"Cars stuck outside", (double)fixedG.getPendingVehicles(), (double)smartG.getPendingVehicles(), true, 0, "", "Cars that cannot enter the city yet."},
    };
    float x = 330, cw = 238;
    ShapeBatch cards;
    for (int i = 0; i < 5; i++) {
        card(cards, x + i * (cw + 8), 812, cw, 78);
        cards.roundRect(x + i * (cw + 8) + 14, 852, 10, 4, 2, Theme::Static);
        cards.roundRect(x + i * (cw + 8) + 14, 872, 10, 4, 2, Theme::Smart);
    }
    cards.draw(rt);
    for (int i = 0; i < 5; i++) {
        float cx = x + i * (cw + 8);
        hint(sf::FloatRect(cx, 812, cw, 78), ms[i].name, ms[i].tip);
        text(rt, fonts.bold, ms[i].name, 12, cx + 14, 822, Theme::Text2);
        text(rt, fonts.ui, "Old way", 12, cx + 30, 848, Theme::Muted);
        text(rt, fonts.bold, fmt(ms[i].f, ms[i].prec) + ms[i].unit, 14, cx + 150, 847, Theme::Text, 2);
        text(rt, fonts.ui, "Smart", 12, cx + 30, 868, Theme::Muted);
        text(rt, fonts.bold, fmt(ms[i].sm, ms[i].prec) + ms[i].unit, 14, cx + 150, 867, Theme::Text, 2);
        double imp = ms[i].lower ? improvement(ms[i].f, ms[i].sm) : (ms[i].f > 0 ? 100 * (ms[i].sm - ms[i].f) / ms[i].f : 0);
        if (fabs(imp) >= 1 && smartG.getSimTime() > 20)
            text(rt, fonts.bold, (imp > 0 ? "better " : "worse ") + fmt(fabs(imp), 0) + "%", 12, cx + cw - 12, 858,
                 imp > 0 ? Theme::Good : Theme::Bad, 2);
    }
    (void)p;
}

// ---------------------------------------------------------------------------
// Title screen
// ---------------------------------------------------------------------------
void HUD::drawTitleScreen(sf::RenderTarget& rt, const HudState& s) {
    rt.setView(rt.getDefaultView());
    modalActive = true;
    ShapeBatch b;
    b.rect(0, 0, 1600, 900, sf::Color(6, 8, 14, 175));
    float w = 900, h = 560, x = (1600 - w) / 2, y = (900 - h) / 2;
    b.roundRect(x + 6, y + 10, w, h, 18, sf::Color(0, 0, 0, 120));
    b.roundRect(x, y, w, h, 18, Theme::CardBorder);
    b.roundRect(x + 1.5f, y + 1.5f, w - 3, h - 3, 17, sf::Color(16, 20, 30, 248));
    sf::Vector2f e(x + w / 2, y + 58);
    b.roundRect(e.x - 46, e.y - 16, 92, 32, 16, sf::Color(10, 12, 18));
    sf::Color lamps[3] = {Theme::SignalRed, Theme::SignalYellow, Theme::SignalGreen};
    for (int i = 0; i < 3; i++) {
        float pulse = 0.5f + 0.5f * sinf(s.time * 2.5f - i * 1.2f);
        b.glow({e.x - 28 + i * 28, e.y}, 22, withAlpha(lamps[i], (int)(70 + 90 * pulse)), withAlpha(lamps[i], 0), 20);
        b.circle({e.x - 28 + i * 28, e.y}, 9, lamps[i], 20);
    }
    b.draw(rt);
    buttons.push_back({sf::FloatRect(x, y, w, h), UiAction::None, true, "", ""});

    text(rt, fonts.bold, "SMART TRAFFIC FLOW", 46, x + w / 2, y + 100, Theme::Text, 1);
    text(rt, fonts.ui, "What if traffic lights could think? Watch an old city and a smart city race - with the same cars.",
         16, x + w / 2, y + 156, Theme::Text2, 1);

    struct Feat { std::string title, body; sf::Color c; };
    Feat feats[3] = {
        {"Smart traffic lights", "Busy roads get longer greens. Lights measure the queue in car-equivalents (a bus counts as 3 cars).", Theme::SignalGreen},
        {"Ambulances first", "A priority queue (Min-Heap) spots ambulances instantly and turns their lights green.", Theme::SirenRed},
        {"Clever detours", "Dijkstra's shortest-path algorithm sends cars around jammed or blocked roads.", sf::Color(90, 200, 255)},
    };
    ShapeBatch fb;
    for (int i = 0; i < 3; i++) {
        float fx = x + 36 + i * 280;
        fb.roundRect(fx, y + 192, 268, 116, 12, sf::Color(26, 31, 45));
        fb.roundRect(fx, y + 192, 5, 116, 2.5f, feats[i].c);
    }
    fb.draw(rt);
    for (int i = 0; i < 3; i++) {
        float fx = x + 36 + i * 280;
        text(rt, fonts.bold, feats[i].title, 17, fx + 20, y + 208, Theme::Text);
        std::vector<std::string> ls = wrap(feats[i].body, fonts.ui, 13, 232);
        for (std::size_t k = 0; k < ls.size(); k++) text(rt, fonts.ui, ls[k], 13, fx + 20, y + 240 + k * 19.0f, Theme::Text2);
    }

    float by = y + 336;
    button(rt, sf::FloatRect(x + w / 2 - 395, by, 260, 52), "Start exploring", Icon::Play, UiAction::Start, Style::Primary,
           false, "", "", Theme::Smart, 17);
    button(rt, sf::FloatRect(x + w / 2 - 125, by, 250, 52), "Take the 1-min tour", Icon::Help, UiAction::Tour,
           Style::Normal, false, "", "", Theme::Smart, 16);
    button(rt, sf::FloatRect(x + w / 2 + 135, by, 260, 52), "Watch the demo", Icon::Star, UiAction::Showcase,
           Style::Normal, false, "", "", Theme::Smart, 16);
    text(rt, fonts.ui, "Tip: everything on screen is clickable - hover anything to see what it means.", 13, x + w / 2,
         by + 70, Theme::Muted, 1);

    text(rt, fonts.bold, "Team FlowOptimizers  -  DSCPP-321-III", 14, x + w / 2, y + h - 74, Theme::Text2, 1);
    text(rt, fonts.ui, "Yashvardhan Singh (Lead)   |   Arsh Singh   |   Sambhav Jaiswal", 13, x + w / 2, y + h - 52, Theme::Muted, 1);
    text(rt, fonts.ui, "Graphic Era (Deemed to be University), Dehradun  -  DSA (CS201) Project-Based Learning", 12,
         x + w / 2, y + h - 30, Theme::Muted, 1);
}

// ---------------------------------------------------------------------------
// Guided tour with a spotlight on each part of the screen
// ---------------------------------------------------------------------------
void HUD::drawTour(sf::RenderTarget& rt, int step, const HudState& s) {
    rt.setView(rt.getDefaultView());
    modalActive = true;
    struct Step { sf::FloatRect target; std::string title, body; };
    Step steps[TOUR_STEPS] = {
        {sf::FloatRect(0, 0, 0, 0), "Welcome!",
         "Two copies of the same city are running right now. They get exactly the same cars at exactly the same moments. "
         "The only difference: one uses old fixed-timer traffic lights, the other uses our smart engine built from data "
         "structures. Let's look around."},
        {sf::FloatRect(0, 0, 900, 900), "This is the city",
         "Cars drive on the left, like in India. Every crossing has a traffic light; the coloured ring in its middle shows "
         "how long the light will stay. Hover any car or crossing for details and click it to look closer."},
        {sf::FloatRect(918, 70, 664, 50), "Old way or smart way",
         "Click these to choose which city you are looking at. Or press Side by side to watch both at once."},
        {sf::FloatRect(918, 178, 664, 148), "Live scoreboard",
         "Each card compares this city with the other one. Green means better, red means worse. Hover a card to learn "
         "what it measures."},
        {sf::FloatRect(918, 324, 664, 176), "Waiting time, live",
         "Orange = old fixed timers, blue = smart lights. The gap between the lines is the time our engine saves every driver."},
        {toolbarRect, "Make something happen",
         "Send an ambulance, cause a breakdown, or press Play demo to see both at once. Watch how differently the two "
         "cities react. You can also pause, change speed and restart here."},
        {sf::FloatRect(918, 628, 268, 182), "Look inside a traffic light",
         "Click a tile here (or any crossing on the map). You will see the queue of waiting cars (first come, first "
         "served) and the emergency list (a Min-Heap) that lets ambulances jump ahead."},
        {sf::FloatRect(918, 806, 664, 90), "Drive it yourself",
         "Use W A S D or the arrow keys. Switch to the ambulance, turn on the siren, and the smart traffic lights will "
         "turn green just for you. Have fun!"},
    };
    step = std::max(0, std::min(TOUR_STEPS - 1, step));
    Step& st = steps[step];

    // spotlight: dim everything except the target
    ShapeBatch b;
    sf::Color dim(4, 6, 12, 185);
    sf::FloatRect t = st.target;
    if (t.width <= 0) {
        b.rect(0, 0, 1600, 900, dim);
    } else {
        b.rect(0, 0, 1600, t.top, dim);
        b.rect(0, t.top + t.height, 1600, 900 - t.top - t.height, dim);
        b.rect(0, t.top, t.left, t.height, dim);
        b.rect(t.left + t.width, t.top, 1600 - t.left - t.width, t.height, dim);
        float pulse = 0.5f + 0.5f * sinf(s.time * 4);
        sf::Color edge = withAlpha(Theme::Smart, (int)(150 + 100 * pulse));
        b.rect(t.left - 2, t.top - 2, t.width + 4, 2.5f, edge);
        b.rect(t.left - 2, t.top + t.height - 0.5f, t.width + 4, 2.5f, edge);
        b.rect(t.left - 2, t.top - 2, 2.5f, t.height + 4, edge);
        b.rect(t.left + t.width - 0.5f, t.top - 2, 2.5f, t.height + 4, edge);
    }

    // card next to the target
    float cw = 420;
    std::vector<std::string> lines = wrap(st.body, fonts.ui, 14, cw - 44);
    float ch = 136 + lines.size() * 21.0f;
    float cx, cy;
    if (t.width <= 0) { cx = 800 - cw / 2; cy = 450 - ch / 2; }
    else if (t.left >= 900) { cx = t.left - cw - 30; cy = std::max(20.0f, std::min(880 - ch, t.top + t.height / 2 - ch / 2)); }
    else if (t.width >= 900) { cx = 1100; cy = 450 - ch / 2; }
    else { cx = t.left + t.width / 2 - cw / 2; cy = t.top - ch - 24; }
    cx = std::max(16.0f, std::min(1584 - cw, cx));
    b.roundRect(cx + 4, cy + 6, cw, ch, 14, sf::Color(0, 0, 0, 120));
    b.roundRect(cx, cy, cw, ch, 14, Theme::Smart);
    b.roundRect(cx + 1.5f, cy + 1.5f, cw - 3, ch - 3, 13, sf::Color(16, 20, 30, 252));
    for (int i = 0; i < TOUR_STEPS; i++)
        b.circle({cx + 24 + i * 14.0f, cy + ch - 30}, 4, i == step ? Theme::Smart : sf::Color(60, 70, 96), 12);
    b.draw(rt);
    buttons.push_back({sf::FloatRect(0, 0, 1600, 900), UiAction::None, true, "", ""});   // block clicks behind the tour

    text(rt, fonts.mono, "STEP " + std::to_string(step + 1) + " OF " + std::to_string(TOUR_STEPS), 11, cx + 22, cy + 18, Theme::Smart);
    text(rt, fonts.bold, st.title, 21, cx + 22, cy + 38, Theme::Text);
    for (std::size_t k = 0; k < lines.size(); k++) text(rt, fonts.ui, lines[k], 14, cx + 22, cy + 72 + k * 21.0f, Theme::Text2);

    button(rt, sf::FloatRect(cx + cw - 22 - 104, cy + ch - 46, 104, 32), step == TOUR_STEPS - 1 ? "Let's go!" : "Next",
           step == TOUR_STEPS - 1 ? Icon::Play : Icon::Arrow, UiAction::TourNext, Style::Primary, false, "", "", Theme::Smart, 14);
    if (step > 0)
        button(rt, sf::FloatRect(cx + cw - 22 - 104 - 84, cy + ch - 46, 78, 32), "Back", Icon::None, UiAction::TourBack,
               Style::Ghost, false, "", "", Theme::Smart, 13);
    button(rt, sf::FloatRect(cx + cw - 96, cy + 10, 82, 24), "Skip tour", Icon::None, UiAction::TourSkip, Style::Ghost,
           false, "", "", Theme::Smart, 11);
}

// ---------------------------------------------------------------------------
// Help, pause
// ---------------------------------------------------------------------------
void HUD::drawHelp(sf::RenderTarget& rt) {
    rt.setView(rt.getDefaultView());
    modalActive = true;
    ShapeBatch b;
    b.rect(0, 0, 1600, 900, sf::Color(6, 8, 14, 170));
    float w = 820, h = 640, x = (1600 - w) / 2, y = (900 - h) / 2;
    b.roundRect(x, y, w, h, 16, Theme::CardBorder);
    b.roundRect(x + 1.5f, y + 1.5f, w - 3, h - 3, 15, sf::Color(16, 20, 30, 250));
    b.draw(rt);
    buttons.push_back({sf::FloatRect(0, 0, 1600, 900), UiAction::CloseOverlay, true, "", ""});   // click outside closes
    buttons.push_back({sf::FloatRect(x, y, w, h), UiAction::None, true, "", ""});

    text(rt, fonts.bold, "How to use it", 26, x + 30, y + 26, Theme::Text);
    button(rt, sf::FloatRect(x + w - 54, y + 18, 34, 34), "", Icon::Close, UiAction::CloseOverlay, Style::Ghost, false,
           "Close", "Close this window (Esc or F1).");

    text(rt, fonts.bold, "WITH THE MOUSE", 12, x + 30, y + 78, Theme::Smart);
    const char* mouseRows[][2] = {
        {"Hover anything", "see what it means"},
        {"Click a car", "see where it is going (its Dijkstra route)"},
        {"Click a crossing", "look inside its queues and Min-Heap"},
        {"Scroll wheel", "zoom in and follow your car"},
        {"Toolbar on the map", "ambulance, breakdown, demo, pause, speed"},
        {"Buttons on the right", "switch city, traffic level, overlays"},
    };
    for (int i = 0; i < 6; i++) {
        text(rt, fonts.bold, mouseRows[i][0], 13, x + 30, y + 104 + i * 26, Theme::Text);
        text(rt, fonts.ui, mouseRows[i][1], 13, x + 200, y + 104 + i * 26, Theme::Text2);
    }

    text(rt, fonts.bold, "KEYBOARD SHORTCUTS", 12, x + 30, y + 274, Theme::Smart);
    struct K { const char* key; const char* what; };
    K keys[] = {{"W A S D", "drive"}, {"Space", "handbrake"}, {"T", "car / ambulance"}, {"E", "siren"},
                {"B", "auto-brake"}, {"C", "follow my car"}, {"M", "old / smart city"}, {"V", "side by side"},
                {"H", "heatmap"}, {"G", "road graph"}, {"N", "night"}, {"X", "send ambulance"},
                {"I", "breakdown"}, {"F2", "play demo"}, {"1 2 3", "traffic level"}, {"[ ]", "speed"},
                {"P", "pause"}, {"R", "restart"}, {"L", "legend"}, {"F5", "save data (CSV)"}, {"F12", "screenshot"}};
    for (int i = 0; i < 21; i++) {
        float kx = x + 30 + (i % 3) * 260, ky = y + 300 + (i / 3) * 34;
        float kw = std::max(26.0f, textWidth(fonts.bold, keys[i].key, 12) + 14);
        ShapeBatch kb;
        kb.roundRect(kx, ky + 1.5f, kw, 22, 5, sf::Color(10, 12, 18));
        kb.roundRect(kx, ky, kw, 21, 5, sf::Color(54, 62, 84));
        kb.draw(rt);
        text(rt, fonts.bold, keys[i].key, 12, kx + kw / 2, ky + 6, Theme::Text, 1);
        text(rt, fonts.ui, keys[i].what, 13, kx + kw + 10, ky + 5, Theme::Text2);
    }
    text(rt, fonts.ui, "Both cities always run in parallel with identical traffic, so every number on the dashboard is a fair comparison.",
         12, x + w / 2, y + h - 34, Theme::Muted, 1);
}

void HUD::drawPaused(sf::RenderTarget& rt, const HudState& s) {
    rt.setView(rt.getDefaultView());
    float cx = s.split ? 800 : 450;
    ShapeBatch b;
    b.roundRect(cx - 150, 380, 300, 110, 18, sf::Color(10, 12, 18, 225));
    b.rect(cx - 52, 402, 7, 24, Theme::Text);
    b.rect(cx - 40, 402, 7, 24, Theme::Text);
    b.draw(rt);
    text(rt, fonts.bold, "PAUSED", 22, cx - 22, 405, Theme::Text);
    button(rt, sf::FloatRect(cx - 70, 442, 140, 34), "Resume", Icon::Play, UiAction::Pause, Style::Primary, false, "",
           "", Theme::Smart, 14);
}

// ---------------------------------------------------------------------------
// DSA inspector: the live data structures behind one junction
// ---------------------------------------------------------------------------
int HUD::junctionCellAt(CityGrid& g, sf::Vector2f m) {
    // same layout as drawJunctions(X0, 634, 256, 170)
    float x = X0, y = 634, w = 256, h = 170, gap = 6;
    int R = g.getRows(), C = g.getCols();
    float cw = (w - 28 - gap * (C - 1)) / C, ch = (h - 42 - gap * (R - 1)) / R;
    for (int j = 0; j < g.getNumIntersections(); j++) {
        float cx = x + 14 + (j % C) * (cw + gap), cy = y + 34 + (j / C) * (ch + gap);
        if (m.x >= cx && m.x <= cx + cw && m.y >= cy && m.y <= cy + ch) return j;
    }
    return -1;
}

void HUD::drawJunctionInspector(sf::RenderTarget& rt, CityGrid& g, int j, const HudState& s, float x, float y) {
    if (j < 0 || j >= g.getNumIntersections()) return;
    rt.setView(rt.getDefaultView());
    Intersection& J = g.getIntersection(j);
    TrafficSignal& S = J.getSignal();
    const float w = 500, h = 338;

    ShapeBatch b;
    b.roundRect(x + 4, y + 6, w, h, 12, sf::Color(0, 0, 0, 110));
    b.roundRect(x, y, w, h, 12, Theme::CardBorder);
    b.roundRect(x + 1, y + 1, w - 2, h - 2, 11, sf::Color(17, 21, 31, 248));
    b.roundRect(x + 1, y + 1, 5, h - 2, 2.5f, g.isSmartMode() ? Theme::Smart : Theme::Static);
    b.draw(rt);
    buttons.push_back({sf::FloatRect(x, y, w, h), UiAction::None, modalActive, "", ""});   // clicks stay in the panel

    text(rt, fonts.bold, "Inside traffic light J" + std::to_string(j), 17, x + 18, y + 12, Theme::Text);
    text(rt, fonts.ui, std::string(g.isSmartMode() ? "Smart city" : "Old-way city") +
                       "  |  who is waiting, and in what order", 11, x + 18, y + 34, Theme::Muted);
    button(rt, sf::FloatRect(x + w - 40, y + 10, 28, 28), "", Icon::Close, UiAction::CloseInspector, Style::Ghost, false,
           "Close", "Close the inspector.");

    sf::Color pc = S.getPhase() == SignalPhase::Green ? Theme::SignalGreen
                 : S.getPhase() == SignalPhase::Yellow ? Theme::SignalYellow : Theme::SignalRed;
    std::string phase = S.getPhase() == SignalPhase::Green ? "GREEN" : S.getPhase() == SignalPhase::Yellow ? "YELLOW" : "ALL RED";
    std::string axis = S.getGreenAxis() == 0 ? "for North-South" : "for East-West";
    if (S.getPhase() == SignalPhase::AllRed) axis = "(crossing clears)";
    ShapeBatch sb;
    sb.circle({x + 24, y + 61}, 5, pc, 14);
    if (S.getIsPreempted()) sb.roundRect(x + w - 150, y + 52, 134, 18, 9, Theme::SirenRed);
    sb.draw(rt);
    text(rt, fonts.bold, phase + " " + axis, 13, x + 36, y + 56, Theme::Text);
    text(rt, fonts.mono, fmt(std::max(0.0, S.getTimer()), 1) + " s left", 12, x + 250, y + 56, Theme::Text2);
    if (S.getIsPreempted()) text(rt, fonts.bold, "AMBULANCE PRIORITY", 10, x + w - 83, y + 57, sf::Color::White, 1);

    std::string formula;
    if (g.isSmartMode()) {
        int nextAxis = 1 - S.getGreenAxis();
        double mine = J.axisLoad(nextAxis), total = J.axisLoad(0) + J.axisLoad(1);
        double share = total > 0 ? mine / total : 0;
        double split = 5 + 20 * share, discharge = 3 + 1.8 * mine;
        double green = total > 0 ? std::max(5.0, std::min(25.0, std::min(split, discharge))) : 10.0;
        formula = "next green = clamp(min(5+20x" + fmt(share, 2) + ", 3+1.8x" + fmt(mine, 1) + "), 5, 25) = " + fmt(green, 1) + " s";
        hint(sf::FloatRect(x + 14, y + 74, w - 28, 18), "How long the next green lasts",
             "5 s plus a share of 20 s proportional to how many cars wait on that road, but never longer than it takes to "
             "empty the queue (3 s + 1.8 s per car), and always between 5 and 25 s.");
    } else {
        formula = "Old way: every green is a fixed 15 s - no sensors, no ambulance priority";
    }
    text(rt, fonts.mono, formula, 11, x + 18, y + 80, sf::Color(150, 210, 255));

    const char* names[4] = {"N", "S", "E", "W"};
    const char* full[4] = {"North", "South", "East", "West"};
    for (int l = 0; l < 4; l++) {
        Lane& lane = J.getLane(l);
        float ry = y + 104 + l * 48;
        std::vector<Vehicle*> q = lane.queueSnapshot();
        std::vector<Vehicle*> heap = lane.heapSnapshot();
        bool green = J.isGreenFor(l);
        hint(sf::FloatRect(x + 14, ry, w - 28, 42), std::string("Cars arriving from the ") + full[l],
             "Coloured blocks = the queue, front first (FIFO: first come, first served). Grey = car, orange = bus, yellow "
             "= auto-rickshaw, red/blue = emergency. Block width = size in car-equivalents. The bar on the right shows "
             "how full the road is; past the white mark (80%) the smart city starts detours.");

        ShapeBatch lb;
        lb.roundRect(x + 14, ry, w - 28, 42, 8, sf::Color(26, 31, 45));
        lb.roundRect(x + 14, ry, 30, 42, 8, green ? withAlpha(Theme::SignalGreen, 70) : withAlpha(Theme::SignalRed, 60));
        float cx = x + 52, maxX = x + w - 150;
        int hidden = 0;
        std::vector<std::pair<float, Vehicle*>> placed;
        for (Vehicle* v : q) {
            float cw = 6 + (float)v->getWeight() * 9;
            if (cx + cw > maxX) { hidden++; continue; }
            sf::Color c;
            switch (v->getKind()) {
            case VehicleKind::Bus: c = sf::Color(214, 108, 32); break;
            case VehicleKind::AutoRickshaw: c = sf::Color(242, 194, 48); break;
            case VehicleKind::Ambulance: case VehicleKind::FireTruck: c = Theme::SirenRed; break;
            case VehicleKind::Police: c = Theme::SirenBlue; break;
            default: c = sf::Color(176, 182, 192); break;
            }
            lb.roundRect(cx, ry + 7, cw, 13, 3, c);
            if (v->isEmergency()) {
                lb.rect(cx + cw / 2 - 1, ry + 9, 2, 9, sf::Color::White);
                lb.rect(cx + cw / 2 - 4, ry + 12.5f, 8, 2, sf::Color::White);
            }
            placed.push_back({cx, v});
            cx += cw + 2;
        }
        double sat = lane.getSaturation();
        float bx = x + w - 140, bw = 100;
        lb.rect(bx, ry + 27, bw, 5, sf::Color(40, 48, 66));
        lb.rect(bx, ry + 27, (float)std::min(1.0, sat) * bw, 5, sat >= 0.8 ? Theme::Bad : (sat >= 0.5 ? Theme::Warn : Theme::Good));
        lb.rect(bx + 0.8f * bw, ry + 24, 1.5f, 11, Theme::Text);
        lb.draw(rt);

        text(rt, fonts.bold, names[l], 16, x + 29, ry + 13, Theme::Text, 1);
        if (q.empty()) text(rt, fonts.ui, "nobody waiting", 11, x + 52, ry + 9, Theme::Muted);
        if (!placed.empty()) text(rt, fonts.ui, "front", 9, placed.front().first, ry - 1, Theme::Muted);
        if (hidden > 0) text(rt, fonts.ui, "+" + std::to_string(hidden), 11, cx + 2, ry + 9, Theme::Text2);
        text(rt, fonts.mono, fmt(lane.getLoad(), 1) + "/" + fmt(lane.getCapacity(), 1) + " cars", 11, bx, ry + 8, Theme::Text2);
        text(rt, fonts.bold, sat >= 0.8 ? fmt(sat * 100, 0) + "% FULL" : fmt(sat * 100, 0) + "%", 10, x + w - 22, ry + 9,
             sat >= 0.8 ? Theme::Bad : Theme::Muted, 2);

        std::string hs = "Emergency heap: ";
        if (heap.empty()) hs += "empty";
        for (std::size_t k = 0; k < heap.size() && k < 2; k++)
            hs += (k ? ", " : "top = ") + heap[k]->getName() + " (priority " + std::to_string(heap[k]->getPriority()) + ")";
        if (heap.size() > 2) hs += " ...";
        text(rt, fonts.mono, hs, 10, x + 52, ry + 27, heap.empty() ? Theme::Muted : sf::Color(255, 140, 140));
    }

    text(rt, fonts.ui, "Queue = first come, first served (FIFO).  Heap = emergency vehicles, most urgent on top.", 11,
         x + 18, y + h - 38, Theme::Text2);
    std::string foot = std::to_string(J.getTotalCleared()) + " cars let through  |  average wait " +
                       fmt(J.getTotalCleared() ? J.getTotalWait() / J.getTotalCleared() : 0, 1) + " s  |  " +
                       std::to_string(J.getPreemptions()) + " times turned green for an ambulance";
    text(rt, fonts.ui, foot, 11, x + 18, y + h - 20, Theme::Muted);
    (void)s;
}
