#ifndef HUD_H
#define HUD_H

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include "Graphics/Renderer.h"
#include "Graphics/PlayerController.h"

// Everything the dashboard needs to know about the app state.
struct HudState {
    bool smartShown;       // which city the main view (and the player) is in
    bool split;            // side-by-side comparison
    bool paused;
    double simSpeed;
    int demandLevel;       // 0 light, 1 normal, 2 rush hour
    bool heatmap, graph, night, chase, legend;
    float fps;
    float time;            // real time, for animations
    sf::Vector2f mouse;    // logical coordinates
    HudState() : smartShown(true), split(false), paused(false), simSpeed(1), demandLevel(1), heatmap(false),
                 graph(false), night(false), chase(false), legend(true), fps(60), time(0) {}
};

// Everything a click can do. main.cpp turns these into actions.
enum class UiAction {
    None, ModeFixed, ModeSmart, Demand0, Demand1, Demand2, Split, Heatmap, Graph, Night, Chase, Legend,
    SpeedDown, SpeedUp, Pause, Restart, Ambulance, Incident, Showcase, Help, CloseOverlay, Start, Tour,
    TourNext, TourBack, TourSkip, CloseInspector, PlayerCar, PlayerAmbulance, Siren, Assist, ExportCsv, Quit
};

enum class Icon {
    None, Ambulance, Warning, Star, Play, Pause, Restart, Plus, Minus, Split, Heat, Graph, Moon, Camera,
    Legend, Help, Car, Siren, Shield, Download, Close, Arrow
};

/*
 * HUD: the live on-screen dashboard. Plain-language metric cards with
 * hover explanations, a live Old-vs-Smart chart, a scoreboard, clickable
 * traffic-light tiles, a "what's happening" feed, the player's car panel,
 * a floating action toolbar on the map, and the title / tour / help screens.
 * Every control is a real button (mouse) and still has a keyboard shortcut.
 */
class HUD {
private:
    struct Button {
        sf::FloatRect r;
        UiAction action;
        bool modal;
        std::string tipTitle, tipBody;
    };
    struct Hint {
        sf::FloatRect r;
        std::string title, body;
    };

    Fonts& fonts;
    std::vector<Button> buttons;
    std::vector<Hint> hints;
    bool modalActive;
    std::string hoverKey;
    float hoverSince;
    sf::FloatRect toolbarRect;

    void text(sf::RenderTarget& rt, const sf::Font& f, const std::string& s, unsigned size, float x, float y,
              sf::Color c, int align = 0);
    float textWidth(const sf::Font& f, const std::string& s, unsigned size);
    std::vector<std::string> wrap(const std::string& s, const sf::Font& f, unsigned size, float maxW);
    void card(ShapeBatch& b, float x, float y, float w, float h);
    void arrowDelta(ShapeBatch& b, float x, float y, bool up, sf::Color c);
    void drawIcon(ShapeBatch& b, Icon icon, sf::Vector2f c, float s, sf::Color col);

    enum class Style { Primary, Normal, Ghost, Toggle };
    void button(sf::RenderTarget& rt, const sf::FloatRect& r, const std::string& label, Icon icon, UiAction a,
                Style style, bool active, const std::string& tipTitle = "", const std::string& tipBody = "",
                sf::Color accent = Theme::Smart, unsigned size = 13);
    void hint(const sf::FloatRect& r, const std::string& title, const std::string& body);
    bool hovered(const sf::FloatRect& r) const;
    sf::Vector2f mouse;

    void drawHeader(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, const HudState& s);
    void drawKpis(sf::RenderTarget& rt, CityGrid& shown, CityGrid& other, const HudState& s, float y);
    void drawChart(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, const HudState& s, float x, float y, float w, float h);
    void drawCompare(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, float x, float y, float w, float h);
    void drawJunctions(sf::RenderTarget& rt, CityGrid& g, const HudState& s, float x, float y, float w, float h);
    void drawEvents(sf::RenderTarget& rt, CityGrid& g, float x, float y, float w, float h);
    void drawPlayer(sf::RenderTarget& rt, PlayerController& p, const HudState& s, float x, float y, float w, float h);

public:
    static constexpr float PANEL_X = 900;
    static constexpr int TOUR_STEPS = 8;

    HUD(Fonts& f) : fonts(f), modalActive(false), hoverSince(0) {}

    void beginFrame(const HudState& s);
    UiAction hit(sf::Vector2f logical) const;      // which button is under the mouse
    bool isClickable(sf::Vector2f logical) const;
    bool overUi(sf::Vector2f logical) const;        // mouse is on a panel / button (not the map)
    void mapHint(const std::string& title, const std::string& body);   // tooltip for whatever is under the mouse on the map
    void drawTooltip(sf::RenderTarget& rt, const HudState& s);

    void draw(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, PlayerController& p, const HudState& s);
    void drawToolbar(sf::RenderTarget& rt, const HudState& s, float centerX, float y);
    void drawSplitBar(sf::RenderTarget& rt, CityGrid& fixedG, CityGrid& smartG, PlayerController& p, const HudState& s);
    void drawTitleScreen(sf::RenderTarget& rt, const HudState& s);
    void drawTour(sf::RenderTarget& rt, int step, const HudState& s);
    void drawHelp(sf::RenderTarget& rt);
    void drawPaused(sf::RenderTarget& rt, const HudState& s);
    void drawJunctionInspector(sf::RenderTarget& rt, CityGrid& g, int j, const HudState& s, float x, float y);
    int junctionCellAt(CityGrid& g, sf::Vector2f logical);   // click on a traffic-light tile in the panel
};

#endif
