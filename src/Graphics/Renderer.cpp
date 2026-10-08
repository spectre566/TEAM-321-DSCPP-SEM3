#include "Graphics/Renderer.h"
#include "Graphics/PlayerController.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>

namespace {
const float PI = 3.14159265f;

// ---- city palette (day) ----
const sf::Color Ground(46, 54, 50);
const sf::Color Plaza(62, 68, 80);
const sf::Color Sidewalk(104, 111, 124);
const sf::Color Kerb(140, 147, 160);
const sf::Color Asphalt(38, 42, 50);
const sf::Color LaneWhite(226, 230, 236);
const sf::Color LaneEdge(150, 157, 170);
const sf::Color BoxYellow(242, 201, 76);
const sf::Color Grass(64, 122, 76);
const sf::Color GrassLight(84, 146, 92);
const sf::Color TreeDark(38, 92, 52);
const sf::Color TreeLight(82, 150, 88);
const sf::Color Water(43, 110, 143);
const sf::Color Path(196, 184, 146);

const sf::Color RoofColors[] = {
    sf::Color(167, 169, 172), sf::Color(194, 184, 163), sf::Color(159, 168, 181), sf::Color(181, 159, 134),
    sf::Color(143, 155, 138), sf::Color(199, 178, 153), sf::Color(163, 159, 184), sf::Color(156, 176, 184),
    sf::Color(186, 140, 120), sf::Color(128, 140, 160)};
const sf::Color CarColors[] = {
    sf::Color(236, 238, 242), sf::Color(176, 182, 192), sf::Color(200, 40, 52), sf::Color(38, 92, 180),
    sf::Color(30, 32, 38), sf::Color(110, 116, 128), sf::Color(122, 28, 40), sf::Color(22, 128, 128),
    sf::Color(214, 170, 40), sf::Color(84, 60, 140)};
const sf::Color Glass(26, 36, 52);

unsigned nextRand(unsigned& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}
float frand(unsigned& s, float lo, float hi) { return lo + (hi - lo) * (nextRand(s) % 10000) / 10000.0f; }

sf::Vector2f norm(sf::Vector2f v) {
    float l = sqrtf(v.x * v.x + v.y * v.y);
    return l > 1e-6f ? v / l : sf::Vector2f(0, 0);
}

std::string fmt1(double v) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << v;
    return os.str();
}

void drawText(sf::RenderTarget& rt, const sf::Font& f, const std::string& s, unsigned size, sf::Vector2f pos,
              sf::Color col, float originX = 0, float originY = 0, float scale = 1.0f) {
    sf::Text t(s, f, size);
    t.setFillColor(col);
    sf::FloatRect b = t.getLocalBounds();
    t.setOrigin(b.left + b.width * originX, b.top + b.height * originY);
    t.setPosition(pos);
    t.setScale(scale, scale);
    rt.draw(t);
}
}

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------
sf::Color mix(sf::Color a, sf::Color b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return sf::Color((sf::Uint8)(a.r + (b.r - a.r) * t), (sf::Uint8)(a.g + (b.g - a.g) * t),
                     (sf::Uint8)(a.b + (b.b - a.b) * t), (sf::Uint8)(a.a + (b.a - a.a) * t));
}
sf::Color withAlpha(sf::Color c, int a) { c.a = (sf::Uint8)std::max(0, std::min(255, a)); return c; }
sf::Color shade(sf::Color c, float k) {
    if (k <= 1) return sf::Color((sf::Uint8)(c.r * k), (sf::Uint8)(c.g * k), (sf::Uint8)(c.b * k), c.a);
    return mix(c, sf::Color(255, 255, 255, c.a), k - 1);
}

// ---------------------------------------------------------------------------
// ShapeBatch
// ---------------------------------------------------------------------------
void ShapeBatch::tri(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Color ca, sf::Color cb, sf::Color cc) {
    va.append(sf::Vertex(a, ca));
    va.append(sf::Vertex(b, cb));
    va.append(sf::Vertex(c, cc));
}

void ShapeBatch::quad(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d, sf::Color col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
}

void ShapeBatch::rect(float x, float y, float w, float h, sf::Color col) {
    quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, col);
}

void ShapeBatch::fan(sf::Vector2f center, const std::vector<sf::Vector2f>& pts, sf::Color col) {
    for (std::size_t i = 0; i < pts.size(); i++) tri(center, pts[i], pts[(i + 1) % pts.size()], col);
}

void ShapeBatch::circle(sf::Vector2f c, float r, sf::Color col, int seg) {
    ellipse(c, r, r, col, seg);
}

void ShapeBatch::ellipse(sf::Vector2f c, float rx, float ry, sf::Color col, int seg) {
    for (int i = 0; i < seg; i++) {
        float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg;
        tri(c, {c.x + cosf(a0) * rx, c.y + sinf(a0) * ry}, {c.x + cosf(a1) * rx, c.y + sinf(a1) * ry}, col);
    }
}

void ShapeBatch::glow(sf::Vector2f c, float r, sf::Color inner, sf::Color outer, int seg) {
    for (int i = 0; i < seg; i++) {
        float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg;
        tri(c, {c.x + cosf(a0) * r, c.y + sinf(a0) * r}, {c.x + cosf(a1) * r, c.y + sinf(a1) * r}, inner, outer, outer);
    }
}

void ShapeBatch::ring(sf::Vector2f c, float r0, float r1, float a0, float a1, sf::Color col, int seg) {
    for (int i = 0; i < seg; i++) {
        float t0 = a0 + (a1 - a0) * i / seg, t1 = a0 + (a1 - a0) * (i + 1) / seg;
        sf::Vector2f p0(cosf(t0), sinf(t0)), p1(cosf(t1), sinf(t1));
        quad(c + p0 * r0, c + p0 * r1, c + p1 * r1, c + p1 * r0, col);
    }
}

void ShapeBatch::line(sf::Vector2f a, sf::Vector2f b, float thick, sf::Color col) {
    sf::Vector2f d = norm(b - a);
    sf::Vector2f n(-d.y * thick / 2, d.x * thick / 2);
    quad(a + n, b + n, b - n, a - n, col);
}

void ShapeBatch::dashed(sf::Vector2f a, sf::Vector2f b, float thick, float dash, float gap, sf::Color col) {
    sf::Vector2f d = b - a;
    float len = sqrtf(d.x * d.x + d.y * d.y);
    sf::Vector2f u = norm(d);
    for (float s = 0; s < len; s += dash + gap) line(a + u * s, a + u * std::min(len, s + dash), thick, col);
}

void ShapeBatch::cone(sf::Vector2f o, float angle, float halfAngle, float len, sf::Color cin, sf::Color cout, int seg) {
    for (int i = 0; i < seg; i++) {
        float a0 = angle - halfAngle + 2 * halfAngle * i / seg;
        float a1 = angle - halfAngle + 2 * halfAngle * (i + 1) / seg;
        tri(o, o + sf::Vector2f(cosf(a0), sinf(a0)) * len, o + sf::Vector2f(cosf(a1), sinf(a1)) * len, cin, cout, cout);
    }
}

void ShapeBatch::roundRect(float x, float y, float w, float h, float r, sf::Color col, int seg) {
    r = std::min(r, std::min(w, h) / 2);
    std::vector<sf::Vector2f> pts;
    sf::Vector2f corners[4] = {{x + w - r, y + r}, {x + w - r, y + h - r}, {x + r, y + h - r}, {x + r, y + r}};
    float start[4] = {-PI / 2, 0, PI / 2, PI};
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= seg; i++) {
            float a = start[k] + (PI / 2) * i / seg;
            pts.push_back(corners[k] + sf::Vector2f(cosf(a), sinf(a)) * r);
        }
    fan({x + w / 2, y + h / 2}, pts, col);
}

void ShapeBatch::quadL(const Frame& f, float x0, float y0, float x1, float y1, sf::Color col) {
    quad(f.p(x0, y0), f.p(x1, y0), f.p(x1, y1), f.p(x0, y1), col);
}

void ShapeBatch::roundL(const Frame& f, float cx, float cy, float hx, float hy, float r, sf::Color col, int seg) {
    r = std::min(r, std::min(hx, hy));
    std::vector<sf::Vector2f> pts;
    float cxs[4] = {cx + hx - r, cx + hx - r, cx - hx + r, cx - hx + r};
    float cys[4] = {cy - hy + r, cy + hy - r, cy + hy - r, cy - hy + r};
    float start[4] = {-PI / 2, 0, PI / 2, PI};
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= seg; i++) {
            float a = start[k] + (PI / 2) * i / seg;
            pts.push_back(f.p(cxs[k] + cosf(a) * r, cys[k] + sinf(a) * r));
        }
    fan(f.p(cx, cy), pts, col);
}

void ShapeBatch::circleL(const Frame& f, float x, float y, float r, sf::Color col, int seg) {
    circle(f.p(x, y), r, col, seg);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
Renderer::Renderer() : worldW(1000), worldH(1000), cityBuilt(false) {
    lastSeq[0] = lastSeq[1] = 0;
}

void Renderer::loadFonts() {
    // Prefer fonts shipped in assets/, fall back to the operating system.
    const char* uiPaths[] = {"assets/fonts/ui.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf"};
    const char* boldPaths[] = {"assets/fonts/bold.ttf", "C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/segoeuib.ttf",
                               "C:/Windows/Fonts/arialbd.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                               "/System/Library/Fonts/Supplemental/Arial Bold.ttf"};
    const char* monoPaths[] = {"assets/fonts/mono.ttf", "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf",
                               "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", "/System/Library/Fonts/Menlo.ttc"};
    auto exists = [](const char* p) { std::ifstream f(p); return f.good(); };
    bool a = false, b = false, c = false;
    for (const char* p : uiPaths) if (!a && exists(p) && fonts.ui.loadFromFile(p)) a = true;
    for (const char* p : boldPaths) if (!b && exists(p) && fonts.bold.loadFromFile(p)) b = true;
    for (const char* p : monoPaths) if (!c && exists(p) && fonts.mono.loadFromFile(p)) c = true;
    if (!b && a) fonts.bold = fonts.ui;
    if (!c && a) fonts.mono = fonts.ui;
    fonts.ok = a;
}

bool Renderer::init(bool visible) {
    loadFonts();

    sf::ContextSettings settings;
    settings.antialiasingLevel = 8;
    sf::VideoMode desk = sf::VideoMode::getDesktopMode();
    unsigned w = LOGICAL_W, h = LOGICAL_H;
    if (w > desk.width * 0.95 || h > desk.height * 0.88) {
        float k = std::min(desk.width * 0.95f / w, desk.height * 0.88f / h);
        w = (unsigned)(w * k);
        h = (unsigned)(h * k);
    }
    window.create(sf::VideoMode(w, h), "Smart Traffic Flow  |  FlowOptimizers  (DSCPP-321-III)",
                  sf::Style::Default, settings);
    window.setVerticalSyncEnabled(true);
    window.setKeyRepeatEnabled(false);
    window.setVisible(visible);

    // Window icon: a tiny traffic light
    sf::Image icon;
    icon.create(32, 32, sf::Color::Transparent);
    for (unsigned y = 2; y < 30; y++)
        for (unsigned x = 9; x < 23; x++) icon.setPixel(x, y, sf::Color(30, 34, 44));
    sf::Color lamps[3] = {Theme::SignalRed, Theme::SignalYellow, Theme::SignalGreen};
    for (int i = 0; i < 3; i++)
        for (int y = -4; y <= 4; y++)
            for (int x = -4; x <= 4; x++)
                if (x * x + y * y <= 16) icon.setPixel(16 + x, 7 + i * 9 + y, lamps[i]);
    window.setIcon(32, 32, icon.getPixelsPtr());

    sf::ContextSettings rtSettings;
    rtSettings.antialiasingLevel = 4;
    if (!canvas.create(LOGICAL_W, LOGICAL_H, rtSettings)) return false;
    canvas.setSmooth(true);
    return true;
}

sf::View Renderer::makeOverview(const sf::FloatRect& vp) const {
    sf::View v(sf::FloatRect(-6, -6, worldW + 12, worldH + 12));
    v.setViewport(sf::FloatRect(vp.left / LOGICAL_W, vp.top / LOGICAL_H, vp.width / LOGICAL_W, vp.height / LOGICAL_H));
    float aspect = vp.width / vp.height;
    v.setSize((worldH + 12) * aspect, worldH + 12);
    return v;
}

sf::View Renderer::makeChase(sf::Vector2f center, float zoom, const sf::FloatRect& vp) const {
    float h = (worldH + 12) / zoom;
    sf::View v(center, sf::Vector2f(h * vp.width / vp.height, h));
    v.setViewport(sf::FloatRect(vp.left / LOGICAL_W, vp.top / LOGICAL_H, vp.width / LOGICAL_W, vp.height / LOGICAL_H));
    return v;
}

sf::Vector2f Renderer::windowToLogical(sf::Vector2i px) const {
    sf::Vector2u ws = window.getSize();
    float k = std::min((float)ws.x / LOGICAL_W, (float)ws.y / LOGICAL_H);
    float ox = (ws.x - LOGICAL_W * k) / 2, oy = (ws.y - LOGICAL_H * k) / 2;
    return sf::Vector2f((px.x - ox) / k, (px.y - oy) / k);
}

sf::Vector2f Renderer::logicalToWorld(sf::Vector2f logical, const sf::View& view) const {
    return canvas.mapPixelToCoords(sf::Vector2i((int)logical.x, (int)logical.y), view);
}

// ---------------------------------------------------------------------------
// Static city layer (built once, 2x supersampled + mipmapped)
// ---------------------------------------------------------------------------
void Renderer::buildCity(CityGrid& g) {
    worldW = g.getWorldWidth();
    worldH = g.getWorldHeight();
    const float pad = 100, scale = 2.0f;
    unsigned texW = (unsigned)((worldW + 2 * pad) * scale), texH = (unsigned)((worldH + 2 * pad) * scale);

    sf::ContextSettings st;
    st.antialiasingLevel = 4;
    cityLayer.create(texW, texH, st);
    cityLayer.setView(sf::View(sf::FloatRect(-pad, -pad, worldW + 2 * pad, worldH + 2 * pad)));
    cityLayer.clear(Ground);

    ShapeBatch b;
    std::vector<std::pair<sf::Vector2f, std::string>> labels;
    streetLights.clear();
    windowGlows.clear();

    std::vector<Rect>& blocks = g.getBlocks();
    for (int i = 0; i < (int)blocks.size(); i++) buildBlock(b, blocks[i], i, g, labels);
    drawRoadsStatic(b, g);
    b.draw(cityLayer);

    // painted labels on special buildings
    for (auto& l : labels) {
        if (!fonts.ok) break;
        bool dark = l.second == "CITY HOSPITAL";
        drawText(cityLayer, fonts.bold, l.second, 24, l.first, dark ? sf::Color(150, 30, 40) : sf::Color(255, 255, 255, 230),
                 0.5f, 0.5f, 0.5f);
    }
    cityLayer.display();
    cityLayer.setSmooth(true);
    cityLayer.generateMipmap();

    citySprite.setTexture(cityLayer.getTexture(), true);
    citySprite.setScale(1 / scale, 1 / scale);
    citySprite.setPosition(-pad, -pad);

    sf::ContextSettings ls;
    lightMap.create((unsigned)(worldW + 2 * pad), (unsigned)(worldH + 2 * pad), ls);
    lightMap.setSmooth(true);
    cityBuilt = true;
}

void Renderer::tree(ShapeBatch& b, sf::Vector2f p, float r) {
    b.circle(p + sf::Vector2f(r * 0.35f, r * 0.45f), r, sf::Color(0, 0, 0, 70), 14);
    b.circle(p, r, TreeDark, 14);
    b.circle(p + sf::Vector2f(-r * 0.25f, -r * 0.25f), r * 0.62f, TreeLight, 12);
    b.circle(p + sf::Vector2f(-r * 0.38f, -r * 0.4f), r * 0.25f, sf::Color(120, 186, 120), 8);
}

void Renderer::building(ShapeBatch& b, float x, float y, float w, float h, int height, unsigned& rng, sf::Color roof, bool details) {
    float sh = 2.0f + height * 2.2f;
    // drop shadow towards the bottom-right (sun from the top-left)
    b.quad({x + w, y}, {x + w + sh, y + sh}, {x + w + sh, y + h + sh}, {x + w, y + h}, sf::Color(0, 0, 0, 80));
    b.quad({x, y + h}, {x + w, y + h}, {x + w + sh, y + h + sh}, {x + sh, y + h + sh}, sf::Color(0, 0, 0, 80));
    // parapet + roof
    b.rect(x, y, w, h, shade(roof, 0.72f));
    b.rect(x + 2, y + 2, w - 4, h - 4, roof);
    b.rect(x + 2, y + 2, w - 4, 2, shade(roof, 1.12f));

    float area = w * h;
    int count = details ? (int)(area / 900) + 1 : 0;
    for (int i = 0; i < count; i++) {
        int kind = nextRand(rng) % 5;
        float dx = frand(rng, x + 6, x + w - 14), dy = frand(rng, y + 6, y + h - 14);
        if (kind == 0) {            // AC unit
            b.rect(dx, dy, 7, 6, sf::Color(205, 210, 216));
            b.rect(dx + 1, dy + 1, 5, 4, sf::Color(170, 176, 186));
            b.circle({dx + 3.5f, dy + 3}, 1.6f, sf::Color(120, 126, 136), 8);
        } else if (kind == 1) {     // black water tank (every Indian rooftop!)
            b.circle({dx + 4, dy + 4}, 4.2f, sf::Color(0, 0, 0, 60), 12);
            b.circle({dx + 3.4f, dy + 3.4f}, 4.0f, sf::Color(34, 36, 40), 12);
            b.circle({dx + 2.6f, dy + 2.6f}, 1.4f, sf::Color(80, 84, 92), 8);
        } else if (kind == 2 && w > 30 && h > 22) {   // solar panels
            for (int k = 0; k < 3; k++) {
                b.rect(dx + k * 5, dy, 4, 8, sf::Color(40, 70, 130));
                b.rect(dx + k * 5, dy + 3.6f, 4, 0.7f, sf::Color(90, 130, 190));
            }
        } else if (kind == 3) {     // stair room
            b.rect(dx, dy, 10, 8, shade(roof, 0.85f));
            b.rect(dx, dy + 6, 10, 2, shade(roof, 0.65f));
        }
    }
    if (nextRand(rng) % 10 < 4) windowGlows.push_back({x + w / 2, y + h / 2});
}

void Renderer::park(ShapeBatch& b, const Rect& r, unsigned& rng, bool pond) {
    b.rect(r.x, r.y, r.w, r.h, Grass);
    // mowing stripes
    for (float yy = r.y; yy < r.y + r.h; yy += 16) b.rect(r.x, yy, r.w, 8, withAlpha(GrassLight, 90));
    sf::Vector2f c(r.x + r.w / 2, r.y + r.h / 2);
    // walking paths
    b.line({r.x + 8, r.y + 8}, {r.x + r.w - 8, r.y + r.h - 8}, 5, Path);
    b.line({r.x + r.w - 8, r.y + 8}, {r.x + 8, r.y + r.h - 8}, 5, Path);
    b.ring(c, std::min(r.w, r.h) * 0.30f, std::min(r.w, r.h) * 0.30f + 5, 0, 2 * PI, Path, 40);
    if (pond) {
        b.ellipse(c, r.w * 0.2f + 4, r.h * 0.15f + 4, sf::Color(70, 140, 168), 36);
        b.ellipse(c, r.w * 0.2f, r.h * 0.15f, Water, 36);
        b.ellipse(c + sf::Vector2f(-r.w * 0.05f, -r.h * 0.04f), r.w * 0.08f, r.h * 0.04f, sf::Color(90, 160, 190, 120), 20);
    } else {
        b.circle(c, 14, sf::Color(170, 176, 186), 24);    // fountain
        b.circle(c, 11, Water, 24);
        b.circle(c, 3, sf::Color(200, 230, 245), 12);
    }
    for (int i = 0; i < (int)(r.w * r.h / 1400); i++) {
        sf::Vector2f p(frand(rng, r.x + 10, r.x + r.w - 10), frand(rng, r.y + 10, r.y + r.h - 10));
        float dx = p.x - c.x, dy = p.y - c.y;
        if (pond && (dx * dx) / ((r.w * 0.26f) * (r.w * 0.26f)) + (dy * dy) / ((r.h * 0.2f) * (r.h * 0.2f)) < 1) continue;
        if (fabs(fabs(dx) - fabs(dy) * r.w / r.h) < 10) continue;   // keep the paths clear
        tree(b, p, frand(rng, 5, 10));
    }
}

void Renderer::parking(ShapeBatch& b, const Rect& r, unsigned& rng) {
    b.rect(r.x, r.y, r.w, r.h, sf::Color(48, 52, 60));
    for (float yy = r.y + 6; yy + 26 < r.y + r.h; yy += 40) {
        for (float xx = r.x + 6; xx + 14 < r.x + r.w; xx += 16) {
            b.rect(xx, yy, 1.2f, 26, sf::Color(200, 204, 210, 160));
            if (nextRand(rng) % 10 < 6) {
                Frame f({xx + 8, yy + 13}, PI / 2 + ((nextRand(rng) % 2) ? PI : 0));
                sf::Color col = CarColors[nextRand(rng) % 10];
                b.roundL(f, 0, 0, 10, 5, 2.5f, col);
                b.quadL(f, 2, -4, 6, 4, Glass);
            }
        }
    }
}

void Renderer::buildBlock(ShapeBatch& b, const Rect& blk, int index, CityGrid& g,
                          std::vector<std::pair<sf::Vector2f, std::string>>& labels) {
    unsigned rng = 1234u + index * 977u;
    b.rect(blk.x, blk.y, blk.w, blk.h, Plaza);

    // clip to the visible part (+ a little) of the city
    float x0 = std::max(blk.x, -60.0f), y0 = std::max(blk.y, -60.0f);
    float x1 = std::min(blk.x + blk.w, worldW + 60), y1 = std::min(blk.y + blk.h, worldH + 60);
    Rect r(x0 + 8, y0 + 8, x1 - x0 - 16, y1 - y0 - 16);
    if (r.w < 20 || r.h < 20) return;

    int cols = g.getCols() + 1;
    int bi = index / cols, bk = index % cols;
    bool interior = bi > 0 && bk > 0 && bi < g.getRows() && bk < g.getCols();
    sf::Vector2f center(r.x + r.w / 2, r.y + r.h / 2);

    // kerb-side trees
    for (float t = 14; t < r.w - 8; t += 34) {
        if (blk.y > -100) tree(b, {r.x + t, blk.y + 5}, 5.5f);
        if (blk.y + blk.h < worldH + 100) tree(b, {r.x + t, blk.y + blk.h - 5}, 5.5f);
    }

    if (interior && bi == 1 && bk == 1) {
        // ---- City Hospital: where the ambulances come from ----
        float hx = r.x + 10, hy = r.y + 10, hw = r.w - 20, hh = r.h * 0.55f;
        building(b, hx, hy, hw, hh, 3, rng, sf::Color(214, 222, 230), false);
        b.rect(hx + 8, hy + 8, hw * 0.3f, hh * 0.28f, sf::Color(196, 206, 216));          // roof plant room
        for (int k = 0; k < 4; k++) b.rect(hx + 12 + k * 9, hy + 12, 6, 6, sf::Color(170, 178, 190));
        b.rect(hx + 10, hy + hh - 24, hw - 20, 16, sf::Color(255, 255, 255));              // name banner
        b.rect(hx + hw / 2 - 16, hy + hh / 2 - 5, 32, 10, sf::Color(220, 40, 50));
        b.rect(hx + hw / 2 - 5, hy + hh / 2 - 16, 10, 32, sf::Color(220, 40, 50));
        sf::Vector2f pad(hx + hw - 26, hy + 24);
        b.circle(pad, 15, sf::Color(70, 76, 88), 28);
        b.ring(pad, 12, 14, 0, 2 * PI, sf::Color(240, 240, 240), 28);
        b.rect(pad.x - 6, pad.y - 7, 2.5f, 14, sf::Color(240, 240, 240));
        b.rect(pad.x + 3.5f, pad.y - 7, 2.5f, 14, sf::Color(240, 240, 240));
        b.rect(pad.x - 4, pad.y - 1.2f, 8, 2.4f, sf::Color(240, 240, 240));
        Rect lot(r.x + 10, hy + hh + 14, r.w - 20, r.h - hh - 34);
        parking(b, lot, rng);
        labels.push_back({sf::Vector2f(hx + hw / 2, hy + hh - 16), "CITY HOSPITAL"});
        return;
    }
    if (interior && bi == 2 && bk == 1) {
        park(b, r, rng, true);
        labels.push_back({sf::Vector2f(center.x, r.y + 14), "CENTRAL PARK"});
        return;
    }
    if (!interior && ((bi == 3 && bk == 3) || (bi == 0 && bk == 0))) {
        park(b, r, rng, false);
        return;
    }

    // ---- generic block: BSP subdivision into building lots ----
    std::vector<Rect> stack{r}, lots;
    while (!stack.empty()) {
        Rect q = stack.back();
        stack.pop_back();
        bool splitX = q.w > q.h;
        float len = splitX ? q.w : q.h;
        if (len < 74 || (lots.size() > 3 && nextRand(rng) % 10 < 2 && len < 120)) {
            lots.push_back(q);
            continue;
        }
        float cut = len * frand(rng, 0.38f, 0.62f);
        const float gap = 7;
        if (splitX) {
            stack.push_back(Rect(q.x, q.y, cut - gap / 2, q.h));
            stack.push_back(Rect(q.x + cut + gap / 2, q.y, q.w - cut - gap / 2, q.h));
        } else {
            stack.push_back(Rect(q.x, q.y, q.w, cut - gap / 2));
            stack.push_back(Rect(q.x, q.y + cut + gap / 2, q.w, q.h - cut - gap / 2));
        }
    }

    bool special = false;
    for (Rect& q : lots) {
        if (q.w < 14 || q.h < 14) continue;
        float dc = sqrtf((q.x + q.w / 2 - worldW / 2) * (q.x + q.w / 2 - worldW / 2) +
                         (q.y + q.h / 2 - worldH / 2) * (q.y + q.h / 2 - worldH / 2));
        int height = 1 + (int)(nextRand(rng) % 3) + (dc < 300 ? 2 : 0);
        sf::Color roof = RoofColors[nextRand(rng) % 10];

        if (!special && q.w > 50 && q.h > 40 && bi == 0 && bk == 2) {
            building(b, q.x, q.y, q.w, q.h, 2, rng, sf::Color(178, 52, 52));
            b.rect(q.x + 6, q.y + q.h - 14, q.w - 12, 8, sf::Color(230, 230, 230));
            labels.push_back({sf::Vector2f(q.x + q.w / 2, q.y + q.h / 2 - 4), "FIRE STATION"});
            special = true;
            continue;
        }
        if (!special && q.w > 50 && q.h > 40 && bi == 2 && bk == 0) {
            building(b, q.x, q.y, q.w, q.h, 2, rng, sf::Color(44, 79, 138));
            labels.push_back({sf::Vector2f(q.x + q.w / 2, q.y + q.h / 2), "POLICE"});
            special = true;
            continue;
        }
        if (interior && bi == 2 && bk == 2 && !special && q.w > 60 && q.h > 50) {
            parking(b, q, rng);
            special = true;
            continue;
        }
        if (q.w * q.h < 900 && nextRand(rng) % 3 == 0) {
            b.rect(q.x, q.y, q.w, q.h, Grass);
            tree(b, {q.x + q.w / 2, q.y + q.h / 2}, std::min(q.w, q.h) * 0.35f);
            continue;
        }
        building(b, q.x + 1, q.y + 1, q.w - 2, q.h - 2, height, rng, roof);
        // tallest downtown towers get a helipad
        if (height >= 4 && q.w > 44 && q.h > 44 && nextRand(rng) % 10 < 3) {
            sf::Vector2f c(q.x + q.w / 2, q.y + q.h / 2);
            b.circle(c, 11, sf::Color(70, 76, 88), 24);
            b.ring(c, 9, 10.5f, 0, 2 * PI, Theme::Warn, 24);
            b.rect(c.x - 4.5f, c.y - 5, 2, 10, sf::Color(240, 240, 240));
            b.rect(c.x + 2.5f, c.y - 5, 2, 10, sf::Color(240, 240, 240));
            b.rect(c.x - 3, c.y - 1, 6, 2, sf::Color(240, 240, 240));
        }
    }
}

void Renderer::drawRoadsStatic(ShapeBatch& b, CityGrid& g) {
    const float RH = CityGrid::ROAD_HALF, SW = CityGrid::SIDEWALK;
    int J = g.getNumIntersections();
    std::vector<Road>& roads = g.getRoads();

    // 1. sidewalks + asphalt for every undirected road
    for (int pass = 0; pass < 2; pass++) {
        for (Road& r : roads) {
            if (r.from > r.to) continue;
            sf::Vector2f a = toSf(g.getNodePos(r.from)), c = toSf(g.getNodePos(r.to));
            sf::Vector2f d = norm(c - a);
            sf::Vector2f ext = d * (RH + SW);
            if (pass == 0) b.line(a - ext, c + ext, 2 * (RH + SW), Sidewalk);
            else b.line(a - d * RH, c + d * RH, 2 * RH, Asphalt);
        }
        if (pass == 0) {
            // kerb edges
            for (Road& r : roads) {
                if (r.from > r.to) continue;
                sf::Vector2f a = toSf(g.getNodePos(r.from)), c = toSf(g.getNodePos(r.to));
                sf::Vector2f d = norm(c - a), n(-d.y, d.x);
                for (int s = -1; s <= 1; s += 2) b.line(a + n * (s * RH), c + n * (s * RH), 1.6f, Kerb);
            }
        }
    }

    // subtle asphalt texture
    unsigned rng = 99;
    for (Road& r : roads) {
        if (r.from > r.to) continue;
        sf::Vector2f a = toSf(g.getNodePos(r.from)), c = toSf(g.getNodePos(r.to));
        sf::Vector2f d = c - a, n = norm(sf::Vector2f(-d.y, d.x));
        for (int k = 0; k < 40; k++) {
            sf::Vector2f p = a + d * frand(rng, 0, 1) + n * frand(rng, -RH + 2, RH - 2);
            b.circle(p, frand(rng, 0.6f, 1.6f), sf::Color(255, 255, 255, 10), 6);
        }
    }

    // 2. junction boxes
    for (int j = 0; j < J; j++) {
        sf::Vector2f c = toSf(g.getNodePos(j));
        b.rect(c.x - RH, c.y - RH, 2 * RH, 2 * RH, Asphalt);
        // yellow box-junction hatching ("keep clear")
        sf::Color hatch = withAlpha(BoxYellow, 70);
        b.line({c.x - RH + 3, c.y - RH + 3}, {c.x + RH - 3, c.y + RH - 3}, 1.2f, hatch);
        b.line({c.x + RH - 3, c.y - RH + 3}, {c.x - RH + 3, c.y + RH - 3}, 1.2f, hatch);
        b.line({c.x - RH + 2, c.y - RH + 2}, {c.x + RH - 2, c.y - RH + 2}, 1.2f, hatch);
        b.line({c.x - RH + 2, c.y + RH - 2}, {c.x + RH - 2, c.y + RH - 2}, 1.2f, hatch);
        b.line({c.x - RH + 2, c.y - RH + 2}, {c.x - RH + 2, c.y + RH - 2}, 1.2f, hatch);
        b.line({c.x + RH - 2, c.y - RH + 2}, {c.x + RH - 2, c.y + RH - 2}, 1.2f, hatch);
    }

    // 3. markings per directed road (one lane each)
    for (Road& r : roads) {
        sf::Vector2f dir = toSf(r.dir), n = toSf(r.normal);
        sf::Vector2f a = toSf(g.getNodePos(r.from)), c = toSf(g.getNodePos(r.to));
        float startCut = r.fromJunction >= 0 ? 31 : 0, endCut = r.junction >= 0 ? 31 : 0;
        sf::Vector2f p0 = a + dir * startCut, p1 = c - dir * endCut;

        // centre dashed line (drawn once per pair) and solid edge lines
        if (r.from < r.to) b.dashed(p0, p1, 1.4f, 9, 8, LaneWhite);
        b.line(p0 + n * (RH - 2), p1 + n * (RH - 2), 1.0f, withAlpha(LaneEdge, 150));

        if (r.junction >= 0) {
            sf::Vector2f jc = c;
            // stop line across this approach lane
            b.line(jc - dir * 31.0f + n * 0.5f, jc - dir * 31.0f + n * (RH - 1), 2.4f, LaneWhite);
            // zebra crossing across the whole road
            for (float k = -RH + 2; k <= RH - 2; k += 4.2f)
                b.line(jc - dir * 28.0f + n * k, jc - dir * 18.0f + n * k, 2.2f, withAlpha(LaneWhite, 200));
            // induction-loop detectors (the queue sensors the report assumes)
            for (float d : {44.0f, 100.0f}) {
                sf::Vector2f lc = jc - dir * d + n * CityGrid::LANE_OFFSET;
                sf::Vector2f a1 = lc - dir * 5.0f - n * 5.0f, a2 = lc + dir * 5.0f - n * 5.0f;
                sf::Vector2f a3 = lc + dir * 5.0f + n * 5.0f, a4 = lc - dir * 5.0f + n * 5.0f;
                sf::Color loop(160, 170, 190, 120);
                b.line(a1, a2, 0.9f, loop);
                b.line(a2, a3, 0.9f, loop);
                b.line(a3, a4, 0.9f, loop);
                b.line(a4, a1, 0.9f, loop);
            }
            // lane arrow
            sf::Vector2f ap = jc - dir * 62.0f + n * CityGrid::LANE_OFFSET;
            b.line(ap - dir * 9.0f, ap + dir * 3.0f, 1.8f, withAlpha(LaneWhite, 190));
            b.tri(ap + dir * 8.0f, ap + dir * 2.0f + n * 3.5f, ap + dir * 2.0f - n * 3.5f, withAlpha(LaneWhite, 190));
        }

        // street lights on the kerb, every ~80 px
        if (r.from < r.to) {
            float len = sqrtf((c - a).x * (c - a).x + (c - a).y * (c - a).y);
            for (float s = 50; s < len - 40; s += 80) {
                sf::Vector2f p = a + dir * s - n * (RH + 4);
                streetLights.push_back(p);
                b.circle(p, 2.2f, sf::Color(30, 32, 36), 8);
                b.circle(p, 1.2f, sf::Color(250, 230, 170), 6);
            }
        }
    }
    (void)J;
}

// ---------------------------------------------------------------------------
// Dynamic layers
// ---------------------------------------------------------------------------
void Renderer::collectEvents(CityGrid& g, int gi) {
    for (GridEvent& e : g.getEvents()) {
        if (e.seq <= lastSeq[gi]) continue;
        lastSeq[gi] = e.seq;
        if (g.getSimTime() - e.time > 3.0) continue;   // too old to animate
        Popup p;
        p.grid = gi;
        p.pos = toSf(e.pos) + sf::Vector2f(0, -14);
        p.life = p.maxLife = 2.6f;
        switch (e.type) {
        case EventType::Preempt: p.text = "EMERGENCY CORRIDOR"; p.color = sf::Color(255, 120, 120); break;
        case EventType::Reroute: p.text = "DIJKSTRA REROUTE"; p.color = sf::Color(120, 220, 255); break;
        case EventType::EmergencySpawn: p.text = "DISPATCH"; p.color = sf::Color(255, 160, 160); break;
        case EventType::EmergencyDone: p.text = "ARRIVED"; p.color = Theme::Good; break;
        case EventType::Gridlock: p.text = "GRIDLOCK!"; p.color = Theme::Warn; break;
        case EventType::Incident: p.text = "LANE BLOCKED"; p.color = sf::Color(255, 150, 60); break;
        case EventType::Info: p.text = "CLEARED"; p.color = Theme::Good; break;
        default: p.text = e.text; p.color = Theme::Text; break;
        }
        if (e.type == EventType::EmergencySpawn) {
            // keep dispatch popups inside the visible map
            p.pos.x = std::max(60.0f, std::min(worldW - 60, p.pos.x));
            p.pos.y = std::max(24.0f, std::min(worldH - 24, p.pos.y));
        }
        popups.push_back(p);
    }
    if (popups.size() > 40) popups.erase(popups.begin(), popups.begin() + (popups.size() - 40));
}

void Renderer::drawHeatmap(CityGrid& g, float t) {
    for (Road& r : g.getRoads()) {
        double sat;
        if (r.junction >= 0) sat = g.getIntersection(r.junction).getLane(r.approach).getSaturation();
        else {
            double l = 0;
            for (Vehicle* v : r.cars) l += v->getWeight();
            sat = l / r.capacity;
        }
        sf::Color c = sat < 0.5 ? mix(sf::Color(40, 200, 110), sf::Color(250, 210, 60), (float)(sat / 0.5))
                                : mix(sf::Color(250, 210, 60), sf::Color(240, 60, 60), (float)((sat - 0.5) / 0.3));
        int alpha = 95;
        if (sat >= 0.8) alpha = 110 + (int)(60 * (0.5f + 0.5f * sinf(t * 8)));
        sf::Vector2f a = toSf(r.start), b = toSf(r.end);
        batch.line(a, b, 13, withAlpha(c, alpha));
    }
}

void Renderer::drawSignals(CityGrid& g, float t) {
    for (int j = 0; j < g.getNumIntersections(); j++) {
        Intersection& J = g.getIntersection(j);
        TrafficSignal& S = J.getSignal();
        sf::Vector2f c = toSf(g.getNodePos(j));

        for (Road& r : g.getRoads()) {
            if (r.junction != j) continue;
            int axis = r.approach < 2 ? 0 : 1;
            sf::Color lamp;
            int lit;   // 0 red, 1 yellow, 2 green
            if (S.getGreenAxis() == axis && S.getPhase() == SignalPhase::Green) { lamp = Theme::SignalGreen; lit = 2; }
            else if (S.getGreenAxis() == axis && S.getPhase() == SignalPhase::Yellow) { lamp = Theme::SignalYellow; lit = 1; }
            else { lamp = Theme::SignalRed; lit = 0; }

            sf::Vector2f dir = toSf(r.dir), n = toSf(r.normal);
            Vehicle* top = J.getLane(r.approach).topEmergency();
            if (g.isSmartMode() && top != NULL && top->distToStop <= Intersection::DETECT_RADIUS) {
                float len = std::min(r.length, Intersection::DETECT_RADIUS);
                sf::Vector2f z0 = toSf(r.end) - dir * len, z1 = toSf(r.end);
                sf::Color zc = fmodf(t, 0.5f) < 0.25f ? Theme::SirenRed : Theme::SirenBlue;
                glowBatch.line(z0, z1, 15, withAlpha(zc, 38));
                glowBatch.dashed(z0 + n * 7.5f, z1 + n * 7.5f, 1.2f, 6, 5, withAlpha(zc, 150));
                glowBatch.dashed(z0 - n * 7.5f, z1 - n * 7.5f, 1.2f, 6, 5, withAlpha(zc, 150));
            }
            // coloured stop-line bar on the approach lane
            sf::Vector2f s0 = c - dir * 33.0f + n * 1.0f, s1 = c - dir * 33.0f + n * (CityGrid::ROAD_HALF - 1);
            batch.line(s0, s1, 2.2f, withAlpha(lamp, 230));
            glowBatch.line(s0 - dir * 2.0f, s1 - dir * 2.0f, 6, withAlpha(lamp, 60));

            // signal head on the kerb (left side of the approaching driver)
            sf::Vector2f head = c - dir * 36.0f + n * (CityGrid::ROAD_HALF + 4.5f);
            Frame f(head, atan2f(dir.y, dir.x) + PI / 2);
            batch.roundL(f, 0, 0, 8.5f, 3.6f, 1.8f, sf::Color(18, 20, 26));
            sf::Color off(55, 58, 66);
            float xs[3] = {-5.2f, 0, 5.2f};
            sf::Color cols[3] = {Theme::SignalRed, Theme::SignalYellow, Theme::SignalGreen};
            for (int k = 0; k < 3; k++) {
                bool on = (k == 0 && lit == 0) || (k == 1 && lit == 1) || (k == 2 && lit == 2);
                batch.circleL(f, xs[k], 0, 2.1f, on ? cols[k] : off, 10);
                if (on) glowBatch.glow(f.p(xs[k], 0), 9, withAlpha(cols[k], 150), withAlpha(cols[k], 0), 16);
            }
        }

        // phase ring in the middle of the junction: remaining time of the phase
        float frac = (float)std::max(0.0, std::min(1.0, S.getTimer() / std::max(0.01, S.getPhaseDuration())));
        sf::Color pc = S.getPhase() == SignalPhase::Green ? Theme::SignalGreen
                     : S.getPhase() == SignalPhase::Yellow ? Theme::SignalYellow : Theme::SignalRed;
        batch.ring(c, 7.5f, 9.5f, 0, 2 * PI, sf::Color(0, 0, 0, 120), 28);
        batch.ring(c, 7.5f, 9.5f, -PI / 2, -PI / 2 + 2 * PI * frac, withAlpha(pc, 230), 28);
        // axis arrow
        if (S.getPhase() == SignalPhase::Green) {
            sf::Vector2f ax = S.getGreenAxis() == 0 ? sf::Vector2f(0, 1) : sf::Vector2f(1, 0);
            batch.line(c - ax * 4.5f, c + ax * 4.5f, 1.6f, withAlpha(pc, 230));
        }

        if (S.getIsPreempted()) {
            float ph = fmodf(t * 1.6f, 1.0f);
            sf::Color sc = fmodf(t, 0.5f) < 0.25f ? Theme::SirenRed : Theme::SirenBlue;
            glowBatch.ring(c, 22 + ph * 26, 25 + ph * 26, 0, 2 * PI, withAlpha(sc, (int)(200 * (1 - ph))), 40);
            glowBatch.ring(c, 26, 28, 0, 2 * PI, withAlpha(sc, 160), 40);
        }
    }
}

void Renderer::drawRoute(CityGrid& g, Vehicle* v, float t) {
    if (v == NULL || v->route.empty()) return;
    std::vector<sf::Vector2f> pts;
    pts.push_back(toSf(v->pos));
    int startIdx = v->state == MotionState::InJunction ? v->routeIdx + 1 : v->routeIdx;
    for (int i = startIdx; i < (int)v->route.size(); i++) pts.push_back(toSf(g.getNodePos(v->route[i])));
    sf::Color c(255, 255, 255, 210);
    for (std::size_t i = 0; i + 1 < pts.size(); i++) {
        glowBatch.line(pts[i], pts[i + 1], 7, sf::Color(120, 200, 255, 50));
        batch.dashed(pts[i], pts[i + 1], 2.2f, 7, 5, c);
    }
    for (std::size_t i = 1; i < pts.size(); i++) {
        float pulse = 4 + 1.5f * sinf(t * 5 + i);
        batch.circle(pts[i], pulse, sf::Color(120, 200, 255, 230), 16);
        batch.circle(pts[i], 2, sf::Color::White, 10);
    }
}

void Renderer::drawVehicle(Vehicle* v, float t, bool night, bool smart) {
    float L = v->getLength(), W = v->getWidth();
    float hx = L / 2, hy = W / 2;
    Frame f(toSf(v->pos), v->heading);
    Frame sh(toSf(v->pos) + sf::Vector2f(2.2f, 2.8f), v->heading);
    bool brake = v->accel < -40 || v->speed < 2;
    bool blinkOn = v->blinker != 0 && fmodf(t + v->getId() * 0.13f, 0.7f) < 0.35f;
    VehicleKind kind = v->getKind();
    unsigned seed = (unsigned)v->colorSeed;

    batch.roundL(sh, 0, 0, hx, hy, 3.5f, sf::Color(0, 0, 0, 85));

    sf::Color body;
    switch (kind) {
    case VehicleKind::Car: {
        body = CarColors[seed % 10];
        batch.roundL(f, 0, 0, hx, hy, 3.6f, shade(body, 0.62f));
        batch.roundL(f, 0, 0, hx - 0.8f, hy - 0.8f, 3.2f, body);
        batch.roundL(f, L * 0.30f, 0, L * 0.14f, hy * 0.8f, 2, shade(body, 1.10f));         // hood highlight
        batch.quadL(f, L * 0.10f, -hy * 0.78f, L * 0.24f, hy * 0.78f, Glass);              // windscreen
        batch.roundL(f, -L * 0.07f, 0, L * 0.17f, hy * 0.74f, 2, shade(body, 0.92f));      // roof
        batch.quadL(f, -L * 0.36f, -hy * 0.7f, -L * 0.26f, hy * 0.7f, Glass);              // rear window
        break;
    }
    case VehicleKind::Bus: {
        sf::Color liveries[3] = {sf::Color(214, 108, 32), sf::Color(38, 140, 86), sf::Color(190, 40, 48)};
        body = liveries[seed % 3];
        batch.roundL(f, 0, 0, hx, hy, 2.5f, shade(body, 0.6f));
        batch.roundL(f, 0, 0, hx - 0.8f, hy - 0.8f, 2.2f, body);
        batch.quadL(f, -hx + 3, -hy + 2.2f, hx - 6, hy - 2.2f, sf::Color(232, 234, 238));  // white roof
        batch.quadL(f, hx - 5.5f, -hy + 1.2f, hx - 1.5f, hy - 1.2f, Glass);               // windscreen
        for (int k = 0; k < 2; k++) {                                                      // roof AC units
            float x = -hx + 8 + k * 13;
            batch.quadL(f, x, -3.5f, x + 8, 3.5f, sf::Color(170, 176, 186));
            batch.quadL(f, x + 1, -2.5f, x + 7, 2.5f, sf::Color(140, 146, 156));
        }
        batch.quadL(f, -hx + 3, -hy + 0.8f, hx - 6, -hy + 2.2f, withAlpha(body, 255));
        break;
    }
    case VehicleKind::AutoRickshaw: {
        body = sf::Color(36, 140, 74);
        // body tapers to a single-wheel nose
        std::vector<sf::Vector2f> pts = {f.p(hx, -2.2f), f.p(hx, 2.2f), f.p(hx * 0.3f, hy), f.p(-hx, hy), f.p(-hx, -hy), f.p(hx * 0.3f, -hy)};
        batch.fan(f.p(0, 0), pts, shade(body, 0.7f));
        batch.roundL(f, -hx * 0.2f, 0, hx * 0.82f, hy - 0.6f, 2.5f, sf::Color(242, 194, 48));  // yellow canopy
        batch.quadL(f, hx * 0.45f, -hy * 0.6f, hx * 0.62f, hy * 0.6f, Glass);
        batch.quadL(f, -hx * 0.95f, -hy * 0.85f, -hx * 0.75f, hy * 0.85f, sf::Color(30, 30, 30));
        break;
    }
    case VehicleKind::Ambulance: {
        body = sf::Color(244, 246, 248);
        batch.roundL(f, 0, 0, hx, hy, 3, sf::Color(170, 176, 186));
        batch.roundL(f, 0, 0, hx - 0.8f, hy - 0.8f, 2.6f, body);
        batch.quadL(f, -hx + 1, -hy + 0.8f, hx - 1, -hy + 2.2f, sf::Color(220, 40, 50));   // side stripes
        batch.quadL(f, -hx + 1, hy - 2.2f, hx - 1, hy - 0.8f, sf::Color(220, 40, 50));
        batch.quadL(f, hx * 0.42f, -hy * 0.78f, hx * 0.62f, hy * 0.78f, Glass);
        batch.quadL(f, -hx * 0.55f, -1.2f, -hx * 0.05f, 1.2f, sf::Color(220, 40, 50));     // red cross
        batch.quadL(f, -hx * 0.3f - 1.2f, -3.6f, -hx * 0.3f + 1.2f, 3.6f, sf::Color(220, 40, 50));
        break;
    }
    case VehicleKind::FireTruck: {
        body = sf::Color(200, 34, 34);
        batch.roundL(f, 0, 0, hx, hy, 2.5f, shade(body, 0.6f));
        batch.roundL(f, 0, 0, hx - 0.8f, hy - 0.8f, 2.2f, body);
        batch.quadL(f, hx - 7, -hy + 1.4f, hx - 2, hy - 1.4f, Glass);
        batch.quadL(f, -hx + 2, -3.4f, hx - 9, -2.2f, sf::Color(200, 204, 210));              // ladder rails
        batch.quadL(f, -hx + 2, 2.2f, hx - 9, 3.4f, sf::Color(200, 204, 210));
        for (float x = -hx + 3; x < hx - 9; x += 3.2f) batch.quadL(f, x, -2.2f, x + 0.9f, 2.2f, sf::Color(170, 176, 186));
        break;
    }
    case VehicleKind::Police: {
        body = sf::Color(240, 242, 246);
        batch.roundL(f, 0, 0, hx, hy, 3.4f, sf::Color(150, 156, 166));
        batch.roundL(f, 0, 0, hx - 0.8f, hy - 0.8f, 3, body);
        batch.quadL(f, -hx + 1, -hy + 0.8f, hx - 1, -hy + 2.4f, sf::Color(30, 64, 175));
        batch.quadL(f, -hx + 1, hy - 2.4f, hx - 1, hy - 0.8f, sf::Color(30, 64, 175));
        batch.quadL(f, L * 0.10f, -hy * 0.78f, L * 0.24f, hy * 0.78f, Glass);
        batch.roundL(f, -L * 0.07f, 0, L * 0.17f, hy * 0.7f, 2, sf::Color(30, 64, 175));
        break;
    }
    }

    // head & tail lights
    sf::Color head(255, 246, 210), tail = brake ? sf::Color(255, 50, 60) : sf::Color(150, 20, 34);
    if (kind != VehicleKind::AutoRickshaw) {
        batch.quadL(f, hx - 1.6f, -hy + 1, hx - 0.2f, -hy * 0.45f, head);
        batch.quadL(f, hx - 1.6f, hy * 0.45f, hx - 0.2f, hy - 1, head);
    } else {
        batch.circleL(f, hx - 0.5f, 0, 1.3f, head, 8);
    }
    batch.quadL(f, -hx + 0.2f, -hy + 1, -hx + 1.6f, -hy * 0.45f, tail);
    batch.quadL(f, -hx + 0.2f, hy * 0.45f, -hx + 1.6f, hy - 1, tail);
    if (brake) {
        glowBatch.glow(f.p(-hx, -hy * 0.6f), night ? 9 : 5, sf::Color(255, 40, 50, night ? 160 : 90), sf::Color(255, 40, 50, 0), 12);
        glowBatch.glow(f.p(-hx, hy * 0.6f), night ? 9 : 5, sf::Color(255, 40, 50, night ? 160 : 90), sf::Color(255, 40, 50, 0), 12);
    }
    if (blinkOn) {
        float side = (float)v->blinker;   // -1 left, +1 right
        sf::Color amber(255, 170, 30);
        batch.circleL(f, hx - 1.2f, side * (hy - 1.2f), 1.3f, amber, 8);
        batch.circleL(f, -hx + 1.2f, side * (hy - 1.2f), 1.3f, amber, 8);
        glowBatch.glow(f.p(hx - 1, side * hy), 6, sf::Color(255, 170, 30, 150), sf::Color(255, 170, 30, 0), 10);
    }

    // emergency light bar + siren pulse
    if (v->isEmergency()) {
        bool phase = fmodf(t * 1.0f + v->getId() * 0.07f, 0.4f) < 0.2f;
        sf::Color c1 = phase ? Theme::SirenRed : sf::Color(90, 20, 20);
        sf::Color c2 = phase ? sf::Color(20, 40, 100) : Theme::SirenBlue;
        float bx = kind == VehicleKind::FireTruck ? hx - 8.5f : L * 0.02f;
        batch.quadL(f, bx - 1.6f, -hy + 1.5f, bx + 1.6f, 0, c1);
        batch.quadL(f, bx - 1.6f, 0, bx + 1.6f, hy - 1.5f, c2);
        glowBatch.glow(f.p(bx, -hy * 0.5f), phase ? 16 : 7, withAlpha(Theme::SirenRed, phase ? 170 : 60), withAlpha(Theme::SirenRed, 0), 16);
        glowBatch.glow(f.p(bx, hy * 0.5f), phase ? 7 : 16, withAlpha(Theme::SirenBlue, phase ? 60 : 170), withAlpha(Theme::SirenBlue, 0), 16);
        if (smart) {
            float ph = fmodf(t * 1.4f + v->getId() * 0.1f, 1.0f);
            glowBatch.ring(f.c, 14 + ph * 30, 15.5f + ph * 30, 0, 2 * PI, sf::Color(255, 255, 255, (int)(90 * (1 - ph))), 32);
        }
    }

    // Dijkstra reroute flash
    if (v->rerouteFlash > 0) {
        float a = std::min(1.0f, v->rerouteFlash);
        glowBatch.ring(f.c, hx + 4, hx + 6, 0, 2 * PI, sf::Color(90, 210, 255, (int)(220 * a)), 24);
        float spin = t * 6;
        glowBatch.ring(f.c, hx + 7, hx + 9, spin, spin + PI * 0.7f, sf::Color(90, 210, 255, (int)(220 * a)), 12);
    }
}

void Renderer::drawPlayer(PlayerController& p, float t, bool night) {
    float L = p.getLength(), W = p.getWidth();
    float hx = L / 2, hy = W / 2;
    Frame f(toSf(p.getPos()), p.getHeading());
    Frame sh(toSf(p.getPos()) + sf::Vector2f(2.2f, 2.8f), p.getHeading());
    bool amb = p.getType() == PlayerController::Type::Ambulance;

    // sensor rays
    for (int i = 0; i < PlayerController::NUM_SENSORS; i++) {
        const SensorRay& s = p.getSensor(i);
        float a = p.getHeading() + s.angle;
        sf::Vector2f nose = f.p(hx, 0);
        sf::Vector2f dir(cosf(a), sinf(a));
        float k = s.hit / s.length;
        sf::Color c = s.hitType == 0 ? sf::Color(120, 255, 170, 70) : mix(sf::Color(255, 60, 60), sf::Color(255, 210, 60), k);
        batch.line(nose, nose + dir * s.hit, 1.2f, withAlpha(c, s.hitType == 0 ? 70 : 200));
        if (s.hitType != 0) {
            batch.circle(nose + dir * s.hit, 2.4f, c, 10);
            glowBatch.glow(nose + dir * s.hit, 8, withAlpha(c, 160), withAlpha(c, 0), 12);
        }
    }

    // halo
    float pulse = 0.5f + 0.5f * sinf(t * 4);
    glowBatch.glow(f.c, 26, withAlpha(Theme::Player, (int)(70 + 50 * pulse)), withAlpha(Theme::Player, 0), 24);
    batch.roundL(sh, 0, 0, hx, hy, 3.5f, sf::Color(0, 0, 0, 90));

    if (amb) {
        batch.roundL(f, 0, 0, hx, hy, 3, Theme::Player);
        batch.roundL(f, 0, 0, hx - 1.2f, hy - 1.2f, 2.6f, sf::Color(244, 246, 248));
        batch.quadL(f, hx * 0.42f, -hy * 0.78f, hx * 0.62f, hy * 0.78f, Glass);
        batch.quadL(f, -hx * 0.55f, -1.2f, -hx * 0.05f, 1.2f, sf::Color(220, 40, 50));
        batch.quadL(f, -hx * 0.3f - 1.2f, -3.6f, -hx * 0.3f + 1.2f, 3.6f, sf::Color(220, 40, 50));
        if (p.isSirenOn()) {
            bool ph = fmodf(t, 0.4f) < 0.2f;
            batch.quadL(f, -1.6f, -hy + 1.5f, 1.6f, 0, ph ? Theme::SirenRed : sf::Color(90, 20, 20));
            batch.quadL(f, -1.6f, 0, 1.6f, hy - 1.5f, ph ? sf::Color(20, 40, 100) : Theme::SirenBlue);
            glowBatch.glow(f.p(0, -3), ph ? 18 : 8, withAlpha(Theme::SirenRed, ph ? 180 : 60), withAlpha(Theme::SirenRed, 0), 16);
            glowBatch.glow(f.p(0, 3), ph ? 8 : 18, withAlpha(Theme::SirenBlue, ph ? 60 : 180), withAlpha(Theme::SirenBlue, 0), 16);
            float r = fmodf(t * 1.4f, 1.0f);
            glowBatch.ring(f.c, 16 + r * 40, 18 + r * 40, 0, 2 * PI, sf::Color(255, 255, 255, (int)(110 * (1 - r))), 36);
        }
    } else {
        // sporty violet coupe with racing stripes
        batch.roundL(f, 0, 0, hx, hy, 4, shade(Theme::Player, 0.55f));
        batch.roundL(f, 0, 0, hx - 0.8f, hy - 0.8f, 3.6f, Theme::Player);
        batch.quadL(f, -hx + 1, -1.8f, hx - 1, -0.8f, sf::Color(255, 255, 255, 230));
        batch.quadL(f, -hx + 1, 0.8f, hx - 1, 1.8f, sf::Color(255, 255, 255, 230));
        batch.quadL(f, L * 0.08f, -hy * 0.8f, L * 0.22f, hy * 0.8f, Glass);
        batch.quadL(f, -L * 0.34f, -hy * 0.7f, -L * 0.26f, hy * 0.7f, Glass);
        batch.quadL(f, -hx - 1.2f, -hy + 0.5f, -hx + 0.6f, hy - 0.5f, sf::Color(40, 30, 60));   // spoiler
    }
    sf::Color tail = p.isBraking() ? sf::Color(255, 50, 60) : sf::Color(150, 20, 34);
    batch.quadL(f, hx - 1.6f, -hy + 1, hx - 0.2f, -hy * 0.45f, sf::Color(255, 250, 225));
    batch.quadL(f, hx - 1.6f, hy * 0.45f, hx - 0.2f, hy - 1, sf::Color(255, 250, 225));
    batch.quadL(f, -hx + 0.2f, -hy + 1, -hx + 1.6f, -hy * 0.45f, tail);
    batch.quadL(f, -hx + 0.2f, hy * 0.45f, -hx + 1.6f, hy - 1, tail);
    if (p.isBraking())
        glowBatch.glow(f.p(-hx, 0), night ? 14 : 8, sf::Color(255, 40, 50, 150), sf::Color(255, 40, 50, 0), 14);
    if (p.getCollisionFlash() > 0)
        glowBatch.ring(f.c, 18, 21, 0, 2 * PI, sf::Color(255, 80, 80, (int)(255 * p.getCollisionFlash() / 0.6f)), 30);
}

void Renderer::drawLights(CityGrid& g, PlayerController* p, const sf::View& view, bool smartMode) {
    const float pad = 100;
    lightMap.setView(sf::View(sf::FloatRect(-pad, -pad, worldW + 2 * pad, worldH + 2 * pad)));
    lightMap.clear(sf::Color(52, 58, 98));
    lightBatch.clear();

    for (sf::Vector2f s : streetLights) lightBatch.glow(s, 46, sf::Color(255, 196, 120, 120), sf::Color(255, 196, 120, 0), 20);
    for (sf::Vector2f w : windowGlows) lightBatch.glow(w, 26, sf::Color(255, 210, 140, 70), sf::Color(255, 210, 140, 0), 16);
    for (int j = 0; j < g.getNumIntersections(); j++)
        lightBatch.glow(toSf(g.getNodePos(j)), 60, sf::Color(200, 210, 255, 60), sf::Color(200, 210, 255, 0), 24);

    for (Vehicle* v : g.getVehicles()) {
        Frame f(toSf(v->pos), v->heading);
        float hx = v->getLength() / 2;
        lightBatch.cone(f.p(hx, 0), v->heading, 0.42f, 85, sf::Color(255, 240, 200, 150), sf::Color(255, 240, 200, 0), 8);
        lightBatch.glow(f.c, 16, sf::Color(255, 255, 255, 40), sf::Color(255, 255, 255, 0), 12);
        if (v->isEmergency()) {
            lightBatch.glow(f.c, 50, sf::Color(255, 80, 80, 90), sf::Color(255, 80, 80, 0), 20);
        }
    }
    if (p != NULL) {
        Frame f(toSf(p->getPos()), p->getHeading());
        lightBatch.cone(f.p(p->getLength() / 2, 0), p->getHeading(), 0.45f, 120, sf::Color(255, 245, 220, 190), sf::Color(255, 245, 220, 0), 10);
        lightBatch.glow(f.c, 30, sf::Color(200, 150, 255, 90), sf::Color(200, 150, 255, 0), 16);
    }
    sf::RenderStates add(sf::BlendAdd);
    lightBatch.draw(lightMap, add);
    lightMap.display();
    (void)view;
    (void)smartMode;
}

void Renderer::drawGraph(sf::RenderTarget& rt, CityGrid& g) {
    ShapeBatch b;
    Pathfinder& pf = g.getPathfinder();
    auto& adj = pf.getAdjacency();
    std::vector<std::pair<sf::Vector2f, std::string>> labels;
    for (int u = 0; u < (int)adj.size(); u++) {
        for (RoadEdge& e : adj[u]) {
            sf::Vector2f a = toSf(g.getNodePos(u)), c = toSf(g.getNodePos(e.target));
            sf::Vector2f d = norm(c - a), n(d.y, -d.x);   // left side, like the lanes
            sf::Vector2f p0 = a + d * 14.0f + n * 5.0f, p1 = c - d * 14.0f + n * 5.0f;
            double sat = e.getSaturation();
            sf::Color col = e.isSaturated() ? sf::Color(255, 90, 90, 230) : mix(sf::Color(130, 220, 255), sf::Color(255, 210, 80), (float)std::min(1.0, sat / 0.8));
            b.line(p0, p1, 1.6f, withAlpha(col, 200));
            b.tri(p1 + d * 2.0f, p1 - d * 6.0f + n * 3.5f, p1 - d * 6.0f - n * 3.5f, col);
            sf::Vector2f mid = (p0 + p1) * 0.5f + n * 7.0f;
            labels.push_back({mid, std::to_string((int)std::round(e.getWeight()))});
        }
    }
    for (int i = 0; i < g.getNumNodes(); i++) {
        sf::Vector2f c = toSf(g.getNodePos(i));
        bool gate = g.isGateNode(i);
        if (gate) {
            c.x = std::max(10.0f, std::min(worldW - 10, c.x));
            c.y = std::max(10.0f, std::min(worldH - 10, c.y));
        }
        b.circle(c, gate ? 8 : 12, sf::Color(10, 14, 22, 230), 24);
        b.ring(c, gate ? 7 : 11, gate ? 8.5f : 12.5f, 0, 2 * PI, gate ? Theme::Muted : sf::Color(130, 220, 255), 24);
        labels.push_back({c, g.getNodeName(i)});
    }
    b.draw(rt);
    if (!fonts.ok) return;
    for (auto& l : labels) {
        bool node = l.second[0] >= 'A';
        drawText(rt, node ? fonts.bold : fonts.mono, l.second, node ? 20 : 18, l.first,
                 node ? Theme::Text : sf::Color(200, 230, 255), 0.5f, 0.5f, 0.45f);
    }
}

void Renderer::drawPopups(sf::RenderTarget& rt, int gi, float dt) {
    if (!fonts.ok) return;
    for (Popup& p : popups) {
        if (p.grid != gi) continue;
        p.life -= dt;
        float k = p.life / p.maxLife;
        float rise = (1 - k) * 22;
        int a = (int)(255 * std::min(1.0f, k * 2.5f));
        sf::Vector2f pos = p.pos - sf::Vector2f(0, rise);
        sf::Text txt(p.text, fonts.bold, 22);
        txt.setScale(0.5f, 0.5f);
        sf::FloatRect bb = txt.getLocalBounds();
        txt.setOrigin(bb.left + bb.width / 2, bb.top + bb.height / 2);
        txt.setPosition(pos);
        ShapeBatch b;
        float w = bb.width * 0.5f + 12, h = bb.height * 0.5f + 8;
        b.roundRect(pos.x - w / 2, pos.y - h / 2, w, h, 4, sf::Color(10, 12, 18, (sf::Uint8)(a * 0.85f)));
        b.roundRect(pos.x - w / 2, pos.y + h / 2 - 1.5f, w, 1.5f, 0, withAlpha(p.color, a));
        b.draw(rt);
        txt.setFillColor(withAlpha(p.color, a));
        rt.draw(txt);
    }
    popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& p) { return p.life <= 0; }), popups.end());
}

void Renderer::drawInspector(sf::RenderTarget& rt, CityGrid& g, Vehicle* v, const sf::View& view, const sf::FloatRect& vp) {
    if (v == NULL || !fonts.ok) return;
    sf::Vector2i px = rt.mapCoordsToPixel(toSf(v->pos), view);
    sf::Vector2f p((float)px.x + 22, (float)px.y - 70);
    float w = 236, h = 132;
    if (p.x + w > vp.left + vp.width - 6) p.x = px.x - w - 22;
    if (p.y < vp.top + 6) p.y = vp.top + 6;
    if (p.y + h > vp.top + vp.height - 6) p.y = vp.top + vp.height - h - 6;

    rt.setView(rt.getDefaultView());
    ShapeBatch b;
    b.roundRect(p.x + 3, p.y + 4, w, h, 10, sf::Color(0, 0, 0, 90));
    b.roundRect(p.x, p.y, w, h, 10, Theme::CardBorder);
    b.roundRect(p.x + 1, p.y + 1, w - 2, h - 2, 9, sf::Color(20, 25, 36, 245));
    sf::Color accent = v->isEmergency() ? Theme::SirenRed : sf::Color(120, 200, 255);
    b.roundRect(p.x + 1, p.y + 1, 4, h - 2, 2, accent);
    b.draw(rt);

    drawText(rt, fonts.bold, v->getName(), 17, {p.x + 14, p.y + 10}, Theme::Text);
    drawText(rt, fonts.ui, v->getTypeName() + "  -  size " + fmt1(v->getWeight()) + " cars  -  priority " + std::to_string(v->getPriority()),
             12, {p.x + 14, p.y + 34}, Theme::Text2);
    std::string state = v->state == MotionState::InJunction ? "crossing J" + std::to_string(v->junction)
                      : (v->released ? "cleared to go" : (v->speed < 4 ? "waiting at signal" : "driving"));
    drawText(rt, fonts.ui, "Speed " + std::to_string((int)(v->speed * 0.72f)) + " km/h  -  " + state, 12, {p.x + 14, p.y + 54}, Theme::Text2);
    drawText(rt, fonts.ui, "Stopped " + fmt1(v->totalDelay) + " s this trip", 12, {p.x + 14, p.y + 72}, Theme::Text2);
    std::string route = "Route: ";
    for (int i = std::max(0, v->routeIdx - 1); i < (int)v->route.size(); i++)
        route += (i > std::max(0, v->routeIdx - 1) ? " > " : "") + g.getNodeName(v->route[i]);
    drawText(rt, fonts.mono, route, 12, {p.x + 14, p.y + 94}, sf::Color(120, 200, 255));
    drawText(rt, fonts.ui, "Route planned with Dijkstra  |  click empty road to close", 10, {p.x + 14, p.y + 114}, Theme::Muted);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void Renderer::beginFrame() {
    canvas.setView(canvas.getDefaultView());
    canvas.clear(Theme::Bg);
}

void Renderer::drawCity(CityGrid& g, int gi, PlayerController* player, const sf::View& view,
                        const sf::FloatRect& vp, const ViewOptions& opt, float dt) {
    if (!cityBuilt) buildCity(g);
    collectEvents(g, gi);

    canvas.setView(view);
    canvas.draw(citySprite);

    batch.clear();
    glowBatch.clear();
    float t = opt.time;
    bool smart = g.isSmartMode();

    if (opt.heatmap) drawHeatmap(g, t);
    drawSignals(g, t);

    Vehicle* selected = NULL;
    for (Vehicle* v : g.getVehicles())
        if (v->getId() == opt.selectedId) selected = v;
    if (selected) drawRoute(g, selected, t);

    if (opt.selectedJunction >= 0 && opt.selectedJunction < g.getNumIntersections())
        drawJunctionFocus(g, opt.selectedJunction, t);
    drawIncidents(g, t);
    for (Vehicle* v : g.getVehicles()) drawVehicle(v, t, opt.night, smart);
    for (Vehicle* v : g.getVehicles()) {
        if (v->getId() != opt.hoverId || v == selected) continue;
        batch.ring(toSf(v->pos), v->getLength() / 2 + 5, v->getLength() / 2 + 6.5f, 0, 2 * PI, sf::Color(255, 255, 255, 150), 24);
    }
    if (opt.hoverJunction >= 0 && opt.hoverJunction != opt.selectedJunction && opt.hoverJunction < g.getNumIntersections())
        batch.ring(toSf(g.getNodePos(opt.hoverJunction)), 30, 31.5f, 0, 2 * PI, sf::Color(255, 255, 255, 120), 40);
    if (selected) {
        float r = selected->getLength() / 2 + 6 + sinf(t * 6);
        batch.ring(toSf(selected->pos), r, r + 1.8f, 0, 2 * PI, sf::Color(255, 255, 255, 230), 28);
    }
    if (player && opt.showPlayer) drawPlayer(*player, t, opt.night);

    batch.draw(canvas);

    if (opt.night) {
        drawLights(g, opt.showPlayer ? player : NULL, view, smart);
        sf::Sprite ls(lightMap.getTexture());
        ls.setPosition(-100, -100);
        canvas.draw(ls, sf::RenderStates(sf::BlendMultiply));
    }
    // emissive layer stays bright at night
    glowBatch.draw(canvas, sf::RenderStates(sf::BlendAdd));

    // gate queues (vehicles waiting outside the city)
    if (fonts.ok) {
        ShapeBatch gb;
        std::vector<std::pair<sf::Vector2f, std::string>> gl;
        for (Gate& gate : g.getGates()) {
            if (gate.pending.empty()) continue;
            sf::Vector2f p = toSf(g.getNodePos(gate.node));
            p.x = std::max(16.0f, std::min(worldW - 16, p.x));
            p.y = std::max(12.0f, std::min(worldH - 12, p.y));
            Road& in = g.getRoads()[gate.inRoad];
            p += toSf(in.normal) * 26.0f;
            std::string s = "+" + std::to_string(gate.pending.size());
            float w = 10 + 6.5f * s.size();
            sf::Color c = gate.pending.size() > 10 ? Theme::Bad : Theme::Warn;
            gb.roundRect(p.x - w / 2, p.y - 8, w, 16, 8, withAlpha(c, 235));
            gl.push_back({p, s});
        }
        gb.draw(canvas);
        for (auto& l : gl) drawText(canvas, fonts.bold, l.second, 24, l.first, sf::Color(20, 20, 24), 0.5f, 0.5f, 0.5f);

        // junction ids
        for (int j = 0; j < g.getNumIntersections(); j++) {
            sf::Vector2f c = toSf(g.getNodePos(j)) + sf::Vector2f(-CityGrid::ROAD_HALF - 15, -CityGrid::ROAD_HALF - 15);
            drawText(canvas, fonts.bold, "J" + std::to_string(j), 22, c, sf::Color(255, 255, 255, 150), 0.5f, 0.5f, 0.5f);
        }
    }

    if (opt.graph) drawGraph(canvas, g);
    drawPopups(canvas, gi, dt);

    // map frame + title badge
    canvas.setView(canvas.getDefaultView());
    ShapeBatch frame;
    frame.rect(vp.left, vp.top, vp.width, 1.5f, Theme::CardBorder);
    frame.rect(vp.left, vp.top + vp.height - 1.5f, vp.width, 1.5f, Theme::CardBorder);
    frame.rect(vp.left, vp.top, 1.5f, vp.height, Theme::CardBorder);
    frame.rect(vp.left + vp.width - 1.5f, vp.top, 1.5f, vp.height, Theme::CardBorder);
    if (!opt.title.empty() && fonts.ok) {
        sf::Text tt(opt.title, fonts.bold, 15);
        float w = tt.getLocalBounds().width + 34;
        frame.roundRect(vp.left + 12, vp.top + 12, w, 30, 15, sf::Color(12, 15, 22, 225));
        frame.circle({vp.left + 27, vp.top + 27}, 5, opt.titleColor, 14);
        frame.draw(canvas);
        drawText(canvas, fonts.bold, opt.title, 15, {vp.left + 38, vp.top + 18}, Theme::Text);
    } else {
        frame.draw(canvas);
    }

    if (selected) drawInspector(canvas, g, selected, view, vp);
}

void Renderer::drawLegend(float x, float y, float t, bool smart) {
    canvas.setView(canvas.getDefaultView());
    const float w = 236, rowH = 25;
    struct Row { int kind; std::string label; };
    Row rows[] = {{0, "Car (counts as 1)"}, {1, "Bus (counts as 3)"}, {2, "Auto-rickshaw (1.2)"},
                  {3, "Ambulance - top priority"}, {4, "Fire truck"}, {5, "Police"}, {6, "You"},
                  {7, "Traffic light timer ring"}, {8, "Lane sensor (counts cars)"},
                  {9, "Ambulance detected here"}, {10, "Broken-down car"}, {11, "Cars waiting to enter"}};
    const int N = 12;
    float h = 40 + N * rowH + 6;
    ShapeBatch bg;
    bg.roundRect(x + 3, y + 4, w, h, 12, sf::Color(0, 0, 0, 100));
    bg.roundRect(x, y, w, h, 12, Theme::CardBorder);
    bg.roundRect(x + 1, y + 1, w - 2, h - 2, 11, sf::Color(14, 17, 25, 238));
    bg.draw(canvas);
    if (fonts.ok) drawText(canvas, fonts.bold, "WHAT YOU SEE", 12, {x + 16, y + 14}, Theme::Muted);

    batch.clear();
    glowBatch.clear();
    PrivateCar car(3);
    PublicBus bus(4, 30);
    AutoRickshaw autoR(5);
    EmergencyVehicle amb(6, "Ambulance"), fire(7, "FireTruck"), pol(8, "Police");
    Vehicle* sprites[6] = {&car, &bus, &autoR, &amb, &fire, &pol};
    for (int i = 0; i < N; i++) {
        sf::Vector2f c(x + 36, y + 44 + i * rowH);
        if (i < 6) {
            Vehicle* v = sprites[i];
            v->pos = Vec2(c.x, c.y);
            v->heading = 0;
            v->speed = 30;
            v->accel = 0;
            drawVehicle(v, t, false, false);   // no siren rings in the legend
        } else if (i == 6) {
            Frame f(c, 0);
            batch.roundL(f, 0, 0, 11, 5.5f, 3.6f, Theme::Player);
            batch.quadL(f, -10, -1.8f, 10, -0.8f, sf::Color::White);
            batch.quadL(f, -10, 0.8f, 10, 1.8f, sf::Color::White);
            glowBatch.glow(c, 16, withAlpha(Theme::Player, 120), withAlpha(Theme::Player, 0), 20);
        } else if (i == 7) {
            batch.ring(c, 6.5f, 8.5f, 0, 2 * PI, sf::Color(0, 0, 0, 160), 24);
            batch.ring(c, 6.5f, 8.5f, -PI / 2, -PI / 2 + 2 * PI * (0.5f + 0.5f * sinf(t)), Theme::SignalGreen, 24);
        } else if (i == 8) {
            sf::Color loop(190, 200, 220);
            batch.line({c.x - 6, c.y - 6}, {c.x + 6, c.y - 6}, 1.2f, loop);
            batch.line({c.x + 6, c.y - 6}, {c.x + 6, c.y + 6}, 1.2f, loop);
            batch.line({c.x + 6, c.y + 6}, {c.x - 6, c.y + 6}, 1.2f, loop);
            batch.line({c.x - 6, c.y + 6}, {c.x - 6, c.y - 6}, 1.2f, loop);
        } else if (i == 9) {
            sf::Color zc = fmodf(t, 0.5f) < 0.25f ? Theme::SirenRed : Theme::SirenBlue;
            batch.line({c.x - 14, c.y}, {c.x + 14, c.y}, 9, withAlpha(zc, 90));
            batch.dashed({c.x - 14, c.y - 5}, {c.x + 14, c.y - 5}, 1.2f, 4, 3, zc);
            batch.dashed({c.x - 14, c.y + 5}, {c.x + 14, c.y + 5}, 1.2f, 4, 3, zc);
        } else if (i == 10) {
            Frame f(c + sf::Vector2f(4, 0), 0.2f);
            batch.roundL(f, 0, 0, 9, 4.6f, 3, sf::Color(128, 132, 140));
            batch.circle(c + sf::Vector2f(-11, 3), 2.6f, sf::Color(255, 120, 30), 10);
            if (fmodf(t, 0.6f) < 0.3f) glowBatch.glow(f.p(8, 0), 8, sf::Color(255, 170, 30, 170), sf::Color(255, 170, 30, 0), 10);
        } else {
            batch.roundRect(c.x - 13, c.y - 8, 26, 16, 8, Theme::Warn);
        }
    }
    batch.draw(canvas);
    glowBatch.draw(canvas, sf::RenderStates(sf::BlendAdd));
    if (!fonts.ok) return;
    drawText(canvas, fonts.bold, "+5", 22, {x + 36, y + 44 + 11 * rowH}, sf::Color(20, 20, 24), 0.5f, 0.5f, 0.5f);
    for (int i = 0; i < N; i++)
        drawText(canvas, fonts.ui, rows[i].label, 13, {x + 66, y + 44 + i * rowH}, Theme::Text2, 0, 0.5f);
}

int Renderer::pickJunction(CityGrid& g, sf::Vector2f world) {
    for (int j = 0; j < g.getNumIntersections(); j++) {
        Vec2 c = g.getNodePos(j);
        float dx = c.x - world.x, dy = c.y - world.y;
        if (dx * dx + dy * dy < 34 * 34) return j;
    }
    return -1;
}

// Broken-down car with hazard lights, cones and a tow-truck countdown
void Renderer::drawIncidents(CityGrid& g, float t) {
    for (Road& r : g.getRoads()) {
        if (r.incidentS < 0) continue;
        sf::Vector2f dir = toSf(r.dir), n = toSf(r.normal);
        sf::Vector2f p = toSf(r.start) + dir * r.incidentS + n * 2.0f;
        float ang = atan2f(dir.y, dir.x) + 0.22f;
        Frame f(p, ang);
        Frame sh(p + sf::Vector2f(2.2f, 2.8f), ang);
        batch.roundL(sh, 0, 0, 11, 5.5f, 3.5f, sf::Color(0, 0, 0, 85));
        batch.roundL(f, 0, 0, 11, 5.5f, 3.6f, sf::Color(70, 74, 82));
        batch.roundL(f, 0, 0, 10.2f, 4.7f, 3.2f, sf::Color(128, 132, 140));
        batch.quadL(f, 4.5f, -4.4f, 10.5f, 4.4f, sf::Color(170, 174, 182));   // bonnet up
        batch.quadL(f, 2.0f, -4.0f, 4.0f, 4.0f, Glass);
        bool on = fmodf(t, 0.6f) < 0.3f;
        sf::Color amber(255, 170, 30);
        for (float sx : {9.8f, -9.8f})
            for (float sy : {-4.4f, 4.4f}) {
                batch.circleL(f, sx, sy, 1.3f, on ? amber : sf::Color(120, 80, 20), 8);
                if (on) glowBatch.glow(f.p(sx, sy), 8, sf::Color(255, 170, 30, 160), sf::Color(255, 170, 30, 0), 10);
            }
        // cones fanning out behind the car
        for (int k = 1; k <= 3; k++) {
            sf::Vector2f c = p - dir * (14.0f + k * 7.0f) + n * (k * 2.5f - 5.0f);
            batch.circle(c, 2.8f, sf::Color(255, 120, 30), 10);
            batch.circle(c, 1.2f, sf::Color(255, 255, 255), 8);
        }
        // warning triangle
        sf::Vector2f w = p - dir * 44.0f;
        batch.tri(w + dir * 4.5f, w - dir * 3.0f + n * 4.5f, w - dir * 3.0f - n * 4.5f, sf::Color(230, 40, 40));
        batch.tri(w + dir * 2.4f, w - dir * 1.8f + n * 2.4f, w - dir * 1.8f - n * 2.4f, sf::Color(255, 255, 255));
        if (fonts.ok) {
            int left = (int)std::max(0.0, r.incidentUntil - g.getSimTime());
            sf::Vector2f lp = p + n * 18.0f;
            ShapeBatch lb;
            lb.roundRect(lp.x - 20, lp.y - 7, 40, 14, 7, sf::Color(255, 150, 60, 230));
            lb.draw(canvas);
            drawText(canvas, fonts.bold, "TOW " + std::to_string(left) + "s", 18, lp, sf::Color(30, 20, 10), 0.5f, 0.5f, 0.5f);
        }
    }
}

// Highlights the junction open in the DSA inspector and its four queues
void Renderer::drawJunctionFocus(CityGrid& g, int j, float t) {
    sf::Vector2f c = toSf(g.getNodePos(j));
    float pulse = 0.5f + 0.5f * sinf(t * 4);
    batch.ring(c, 30, 32.5f, 0, 2 * PI, sf::Color(255, 255, 255, (sf::Uint8)(150 + 90 * pulse)), 48);
    for (Road& r : g.getRoads()) {
        if (r.junction != j) continue;
        double sat = g.getIntersection(j).getLane(r.approach).getSaturation();
        sf::Color col = sat >= 0.8 ? Theme::Bad : (sat >= 0.5 ? Theme::Warn : sf::Color(120, 200, 255));
        sf::Vector2f n = toSf(r.normal);
        sf::Vector2f a = toSf(r.start), b = toSf(r.end);
        batch.line(a + n * 8.5f, b + n * 8.5f, 1.4f, withAlpha(col, 210));
        batch.line(a - n * 8.5f, b - n * 8.5f, 1.4f, withAlpha(col, 210));
    }
}

Vehicle* Renderer::pickVehicle(CityGrid& g, sf::Vector2f world) {
    Vehicle* best = NULL;
    float bestD = 18;
    for (Vehicle* v : g.getVehicles()) {
        float dx = v->pos.x - world.x, dy = v->pos.y - world.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < bestD) { bestD = d; best = v; }
    }
    return best;
}

void Renderer::endFrame() {
    canvas.display();
    window.clear(sf::Color::Black);
    sf::Vector2u ws = window.getSize();
    window.setView(sf::View(sf::FloatRect(0, 0, (float)ws.x, (float)ws.y)));
    float k = std::min((float)ws.x / LOGICAL_W, (float)ws.y / LOGICAL_H);
    sf::Sprite s(canvas.getTexture());
    s.setScale(k, k);
    s.setPosition((ws.x - LOGICAL_W * k) / 2, (ws.y - LOGICAL_H * k) / 2);
    window.draw(s);
    window.display();
}

bool Renderer::saveScreenshot(const std::string& path) {
    canvas.display();
    return canvas.getTexture().copyToImage().saveToFile(path);
}
