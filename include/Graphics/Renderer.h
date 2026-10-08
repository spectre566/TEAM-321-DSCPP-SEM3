#ifndef RENDERER_H
#define RENDERER_H

#include <SFML/Graphics.hpp>
#include <vector>
#include <string>
#include "Core/CityGrid.h"

class PlayerController;

// ---------------------------------------------------------------------------
// Colour theme shared by the Renderer and the HUD.
// Static/Smart series colours were validated for colour-blind separation
// and contrast on the dark panel surface.
// ---------------------------------------------------------------------------
namespace Theme {
    inline const sf::Color Bg(13, 16, 23);
    inline const sf::Color Panel(18, 22, 31);
    inline const sf::Color Card(26, 31, 45);
    inline const sf::Color CardBorder(40, 48, 70);
    inline const sf::Color Text(232, 236, 243);
    inline const sf::Color Text2(167, 176, 192);
    inline const sf::Color Muted(107, 116, 135);
    inline const sf::Color Static(217, 116, 28);     // Mode 1: fixed timer
    inline const sf::Color Smart(30, 155, 186);      // Mode 2: smart DSA engine
    inline const sf::Color Good(52, 196, 114);
    inline const sf::Color Bad(239, 75, 75);
    inline const sf::Color Warn(242, 201, 76);
    inline const sf::Color SirenRed(255, 59, 59);
    inline const sf::Color SirenBlue(59, 130, 255);
    inline const sf::Color Player(168, 85, 247);
    inline const sf::Color SignalGreen(46, 230, 120);
    inline const sf::Color SignalYellow(255, 196, 40);
    inline const sf::Color SignalRed(255, 64, 64);
}

inline sf::Vector2f toSf(Vec2 v) { return sf::Vector2f(v.x, v.y); }
sf::Color mix(sf::Color a, sf::Color b, float t);
sf::Color withAlpha(sf::Color c, int a);
sf::Color shade(sf::Color c, float k);   // k < 1 darker, k > 1 lighter

// Local frame of a sprite: x = forward, y = right-hand side.
struct Frame {
    sf::Vector2f c;
    float cs, sn;
    Frame(sf::Vector2f c, float angle) : c(c), cs(cosf(angle)), sn(sinf(angle)) {}
    sf::Vector2f p(float x, float y) const { return sf::Vector2f(c.x + x * cs - y * sn, c.y + x * sn + y * cs); }
};

// Batches thousands of triangles into a single draw call.
class ShapeBatch {
private:
    sf::VertexArray va;
public:
    ShapeBatch() : va(sf::Triangles) {}
    void clear() { va.clear(); }
    std::size_t size() const { return va.getVertexCount(); }

    void tri(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Color ca, sf::Color cb, sf::Color cc);
    void tri(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Color col) { tri(a, b, c, col, col, col); }
    void quad(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d, sf::Color col);
    void rect(float x, float y, float w, float h, sf::Color col);
    void fan(sf::Vector2f center, const std::vector<sf::Vector2f>& pts, sf::Color col);
    void circle(sf::Vector2f c, float r, sf::Color col, int seg = 20);
    void ellipse(sf::Vector2f c, float rx, float ry, sf::Color col, int seg = 28);
    void glow(sf::Vector2f c, float r, sf::Color inner, sf::Color outer, int seg = 24);
    void ring(sf::Vector2f c, float r0, float r1, float a0, float a1, sf::Color col, int seg = 32);
    void line(sf::Vector2f a, sf::Vector2f b, float thick, sf::Color col);
    void dashed(sf::Vector2f a, sf::Vector2f b, float thick, float dash, float gap, sf::Color col);
    void cone(sf::Vector2f o, float angle, float halfAngle, float len, sf::Color cin, sf::Color cout, int seg = 8);
    void roundRect(float x, float y, float w, float h, float r, sf::Color col, int seg = 4);

    // sprite helpers in a local frame
    void quadL(const Frame& f, float x0, float y0, float x1, float y1, sf::Color col);
    void roundL(const Frame& f, float cx, float cy, float hx, float hy, float r, sf::Color col, int seg = 3);
    void circleL(const Frame& f, float x, float y, float r, sf::Color col, int seg = 12);

    void draw(sf::RenderTarget& t, const sf::RenderStates& st = sf::RenderStates::Default) const { t.draw(va, st); }
};

struct Fonts {
    sf::Font ui, bold, mono;
    bool ok;
};

// What to draw on top of the city.
struct ViewOptions {
    bool heatmap;
    bool graph;
    bool night;
    bool showPlayer;
    int selectedId;
    int selectedJunction;       // junction shown in the DSA inspector (-1 = none)
    int hoverId;                // vehicle under the mouse
    int hoverJunction;          // junction under the mouse
    float time;
    std::string title;          // badge in the corner of the map (split view)
    sf::Color titleColor;
    ViewOptions() : heatmap(false), graph(false), night(false), showPlayer(true), selectedId(-1), selectedJunction(-1), hoverId(-1), hoverJunction(-1), time(0) {}
};

// Floating text that rises and fades (reroutes, preemptions, ...)
struct Popup {
    int grid;
    sf::Vector2f pos;
    std::string text;
    sf::Color color;
    float life, maxLife;
};

/*
 * Renderer: owns the SFML window and the 1600x900 logical canvas, builds the
 * static city layer (roads, buildings, parks) once, and draws every dynamic
 * element per frame: signals, vehicle sprites, overlays, night lighting.
 */
class Renderer {
private:
    sf::RenderWindow window;
    sf::RenderTexture canvas;      // logical 1600 x 900 frame, letterboxed into the window
    sf::RenderTexture cityLayer;   // pre-rendered static city
    sf::RenderTexture lightMap;    // night lighting (multiplied over the city)
    sf::Sprite citySprite;
    Fonts fonts;
    ShapeBatch batch, glowBatch, lightBatch;
    std::vector<sf::Vector2f> streetLights;
    std::vector<sf::Vector2f> windowGlows;
    std::vector<Popup> popups;
    long long lastSeq[2];
    float worldW, worldH;
    bool cityBuilt;

    void loadFonts();
    void buildBlock(ShapeBatch& b, const Rect& r, int index, CityGrid& g, std::vector<std::pair<sf::Vector2f, std::string>>& labels);
    void building(ShapeBatch& b, float x, float y, float w, float h, int height, unsigned& rng, sf::Color roof, bool details = true);
    void tree(ShapeBatch& b, sf::Vector2f p, float r);
    void park(ShapeBatch& b, const Rect& r, unsigned& rng, bool pond);
    void parking(ShapeBatch& b, const Rect& r, unsigned& rng);
    void drawRoadsStatic(ShapeBatch& b, CityGrid& g);

    void drawSignals(CityGrid& g, float t);
    void drawHeatmap(CityGrid& g, float t);
    void drawGraph(sf::RenderTarget& rt, CityGrid& g);
    void drawRoute(CityGrid& g, Vehicle* v, float t);
    void drawIncidents(CityGrid& g, float t);
    void drawJunctionFocus(CityGrid& g, int j, float t);
    void drawVehicle(Vehicle* v, float t, bool night, bool smart);
    void drawPlayer(PlayerController& p, float t, bool night);
    void drawLights(CityGrid& g, PlayerController* p, const sf::View& view, bool smartMode);
    void drawPopups(sf::RenderTarget& rt, int gridIndex, float dt);
    void drawInspector(sf::RenderTarget& rt, CityGrid& g, Vehicle* v, const sf::View& view, const sf::FloatRect& vp);

public:
    static constexpr unsigned LOGICAL_W = 1600;
    static constexpr unsigned LOGICAL_H = 900;

    Renderer();
    bool init(bool visible = true);
    sf::RenderWindow& getWindow() { return window; }
    sf::RenderTexture& getCanvas() { return canvas; }
    Fonts& getFonts() { return fonts; }

    void buildCity(CityGrid& g);
    void collectEvents(CityGrid& g, int gridIndex);

    sf::View makeOverview(const sf::FloatRect& viewportPx) const;
    sf::View makeChase(sf::Vector2f center, float zoom, const sf::FloatRect& viewportPx) const;

    void beginFrame();
    void drawCity(CityGrid& g, int gridIndex, PlayerController* player, const sf::View& view,
                  const sf::FloatRect& viewportPx, const ViewOptions& opt, float dt);
    void endFrame();

    sf::Vector2f windowToLogical(sf::Vector2i px) const;
    sf::Vector2f logicalToWorld(sf::Vector2f logical, const sf::View& view) const;
    Vehicle* pickVehicle(CityGrid& g, sf::Vector2f world);
    int pickJunction(CityGrid& g, sf::Vector2f world);
    void drawLegend(float x, float y, float t, bool smart);   // what every sprite and symbol means
    bool saveScreenshot(const std::string& path);
    void clearPopups() { popups.clear(); lastSeq[0] = lastSeq[1] = 0; }
};

#endif
