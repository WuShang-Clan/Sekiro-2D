#include <SFML/Audio.hpp>
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <functional>

namespace cfg {
    constexpr int   W = 1280, H = 720;
    constexpr int   LEVELS = 6;
    constexpr float WORLD_W = 2600.f;
    constexpr float GROUND = 640.f;
    constexpr float SPRITE_H = 250.f;
    constexpr bool  PLAYER_ART_FACES_RIGHT = true;   
    constexpr bool  BOSS_ART_FACES_RIGHT = true;
    constexpr bool  SMOOTH_TEXTURES = true;
    constexpr float GRAVITY = 3400.f;
    constexpr float JUMP_VEL = -1060.f;              
    constexpr float AIR_CONTROL = 1.4f;              
    constexpr float WALK = 270.f;
    constexpr float PARRY_WINDOW = 0.18f;
    constexpr float PARRY_SPAM_GUARD = 0.45f;
    constexpr float DASH_SPEED = 620.f;
    constexpr float DASH_TOTAL = 0.50f;
    constexpr float MIKIRI_WINDOW = 0.28f;
    constexpr float MIKIRI_POSE = 0.35f;
    constexpr float REVIVE_FRAC = 0.60f;
    constexpr float BARRAGE_INTERVAL = 0.46f;
    constexpr float BARRAGE_FIRST = 0.45f;
    constexpr float BARRAGE_POSTURE_BLOCK = 30.f;
    constexpr float BARRAGE_POSTURE_DEFLECT = 32.f;
    constexpr float GUN_WINDUP = 0.50f;
    constexpr float GUN_TOTAL = 0.95f;
    constexpr float GUN_COOLDOWN = 5.0f;
    constexpr float BULLET_SPEED = 950.f;
    constexpr float BULLET_DMG = 14.f;
    constexpr float BULLET_CHIP = 4.f;
    constexpr float EXEC_STRIKE = 0.10f;             // deathblow: sim-seconds until the cut lands
    constexpr float EXEC_TOTAL = 0.95f;              // deathblow: total length
    constexpr float LORE_CPS = 55.f;                 // typewriter speed (characters / second)
    constexpr float MUSIC_VOLUME = 60.f;
    const std::string ASSETS = "assets/";
    constexpr float BREAK_SOUND_LEN = 0.25f;    
    constexpr float HIT_EXTRA_SKIP = 0.06f;  
    // extra seconds trimmed from the start of impact sounds (0 = off)
}  // namespace cfg


//  UTIL

static std::mt19937 g_rng{ std::random_device{}() };
static float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g_rng); }
static float rnd01() { return rnd(0.f, 1.f); }
static float lerp(float a, float b, float t) { return a + (b - a) * std::clamp(t, 0.f, 1.f); }
static float hash01(int i, float seed) {
    float h = std::sin(i * 12.9898f + seed * 78.233f) * 43758.5453f;
    return h - std::floor(h);
}
static std::string asset(const std::string& rel) {
    std::string p = cfg::ASSETS + rel;
#ifdef _WIN32
    for (char& c : p) if (c == '/') c = '\\';
#endif
    return p;
}
static bool assetExists(const std::string& rel) { std::ifstream f(asset(rel)); return f.good(); }
// ---- save files (3 slots) ----
static const int SAVE_SLOTS = 3;
static std::string saveFile(int slot) { return "savegame" + std::to_string(slot + 1) + ".sav"; }
struct SaveData { int level = 0, deaths = 0, rezz = 0; float time = 0.f; bool valid = false; };
static SaveData readSave(int slot) {
    SaveData s; int ver = 0;
    std::ifstream f(saveFile(slot));
    if (!f && slot == 0) { f.clear(); f.open("savegame.sav"); }      // old single-slot save -> slot 1
    if (f >> ver >> s.level >> s.deaths >> s.rezz >> s.time && ver == 1) {
        s.level = std::clamp(s.level, 0, cfg::LEVELS - 1); s.valid = true;
    }
    return s;
}
static bool writeSave(int slot, const SaveData& s) {
    std::ofstream f(saveFile(slot));
    if (!f) return false;
    f << 1 << ' ' << s.level << ' ' << s.deaths << ' ' << s.rezz << ' ' << s.time << '\n';
    return f.good();
}
static std::string fmtTime(float t) {
    char b[24]; int s = (int)t; std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60); return b;
}

// ---- completed-run history ----
static const char* RUNS_FILE = "runs.sav";
struct RunRecord { int deaths = 0, rezz = 0; float time = 0.f; };
static std::vector<RunRecord> readRuns() {
    std::vector<RunRecord> v; std::ifstream f(RUNS_FILE); RunRecord r;
    while (f >> r.deaths >> r.rezz >> r.time) v.push_back(r);
    return v;
}
static void appendRun(const RunRecord& r) {
    std::ofstream f(RUNS_FILE, std::ios::app);
    if (f) f << r.deaths << ' ' << r.rezz << ' ' << r.time << '\n';
}

//  SYNTHESIZED SOUNDS (used whenever assets/sounds/<name>.wav|ogg is missing)

static sf::SoundBuffer makeTone(float f0, float f1, float dur, float vol) {
    const unsigned SR = 44100; const size_t n = (size_t)(dur * SR);
    std::vector<sf::Int16> s(n);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / SR, k = (float)i / (float)n, f = f0 + (f1 - f0) * k;
        float env = (1.f - k) * std::min(1.f, t * 400.f);
        s[i] = (sf::Int16)(std::sin(6.2832f * f * t) * env * vol * 32000.f);
    }
    sf::SoundBuffer b; b.loadFromSamples(s.data(), n, 1, SR); return b;
}
static sf::SoundBuffer synth(float dur, const std::function<float(float, float)>& fn) {
    const unsigned SR = 44100; const size_t n = (size_t)(dur * SR);
    std::vector<sf::Int16> s(n);
    for (size_t i = 0; i < n; i++) {
        float t = (float)i / SR, k = (float)i / (float)n;
        float v = fn(t, k) * std::min(1.f, t * 800.f) * std::min(1.f, (1.f - k) * 40.f);   // click-free edges
        s[i] = (sf::Int16)(std::clamp(v, -1.f, 1.f) * 30000.f);
    }
    sf::SoundBuffer b; b.loadFromSamples(s.data(), n, 1, SR); return b;
}
static float nz() { return rnd(-1.f, 1.f); }
static float ringS(float t, float f, float decay) { return std::sin(6.2832f * f * t) * std::exp(-t * decay); }

static sf::SoundBuffer synthSfx(const std::string& n) {
    if (n == "swing")                                            // sword whoosh
        return synth(0.28f, [y = 0.f](float, float k) mutable { y += (0.04f + 0.5f * k) * (nz() - y); return y * 2.6f * std::sin(3.1416f * k); });
    if (n == "dash")
        return synth(0.20f, [y = 0.f](float, float k) mutable { y += (0.08f + 0.4f * (1.f - k)) * (nz() - y); return y * 2.4f * std::sin(3.1416f * k); });
    if (n == "jump")
        return synth(0.18f, [y = 0.f](float t, float k) mutable { y += 0.1f * (nz() - y); return (0.5f * y + 0.35f * std::sin(6.2832f * (180.f + 320.f * k) * t)) * (1.f - k); });
    if (n == "hit")                                              // flesh thump + cut
        return synth(0.30f, [](float t, float) { return ringS(t, 150.f - 300.f * t, 16.f) * 0.9f + nz() * std::exp(-t * 45.f) * 0.7f; });
    if (n == "block")                                            // dull metal knock
        return synth(0.25f, [](float t, float) { return nz() * std::exp(-t * 60.f) * 0.5f + ringS(t, 310.f, 22.f) * 0.5f + ringS(t, 520.f, 28.f) * 0.3f; });
    if (n == "parry")                                            // sharp transient + metal body + long steel ring
        return synth(0.85f, [](float t, float) {
        float ping = ringS(t, 2480.f, 7.f) + 0.8f * ringS(t, 3720.f, 9.f) + 0.6f * ringS(t, 5310.f, 12.f) + 0.4f * ringS(t, 7420.f, 18.f);
        float body = ringS(t, 880.f, 18.f) * 0.6f + ringS(t, 1320.f, 24.f) * 0.4f;
        return ping * 0.42f + body * 0.5f + nz() * std::exp(-t * 140.f) * 0.7f; });
    if (n == "clash")                                            // two blades meeting
        return synth(0.80f, [](float t, float) {
        return (ringS(t, 1400.f, 6.f) + 0.8f * ringS(t, 2100.f, 8.f) + 0.6f * ringS(t, 3300.f, 11.f) + 0.5f * ringS(t, 4700.f, 14.f)) * 0.4f
            + nz() * std::exp(-t * 40.f) * 0.8f + ringS(t, 420.f, 20.f) * 0.5f; });
    if (n == "mikiri")                                           // rising "shing" + ring
        return synth(0.55f, [](float t, float k) { return std::sin(6.2832f * (500.f + 2200.f * std::min(1.f, k * 2.5f)) * t) * std::exp(-t * 6.f) * 0.35f + ringS(t, 3000.f, 9.f) * 0.35f + nz() * std::exp(-t * 30.f) * 0.3f; });
    if (n == "jumpcounter")                                      // stomp + rising chime
        return synth(0.55f, [](float t, float k) { return ringS(t, 110.f, 12.f) * 0.9f + std::sin(6.2832f * (700.f + 900.f * k) * t) * std::exp(-t * 7.f) * 0.35f + nz() * std::exp(-t * 50.f) * 0.3f; });
    if (n == "break")                                            // posture break: crack + falling tone
        return synth(0.60f, [](float t, float k) { return nz() * std::exp(-t * 14.f) * (std::sin(t * 220.f) > 0.f ? 0.7f : 0.25f) + std::sin(6.2832f * (900.f - 700.f * k) * t) * std::exp(-t * 5.f) * 0.4f; });
    if (n == "deathblow")                                        // boom + slash + long ring
        return synth(1.10f, [y = 0.f](float t, float) mutable {
        y += (0.05f + 0.4f * std::min(1.f, t * 6.f)) * (nz() - y);
        float whoosh = (t < 0.25f) ? y * 2.2f * std::sin(3.1416f * t / 0.25f) : 0.f;
        float boom = (t > 0.12f) ? ringS(t - 0.12f, 62.f, 3.5f) * 1.1f : 0.f;
        float ring = (t > 0.12f) ? ringS(t - 0.12f, 2600.f, 4.f) * 0.3f + ringS(t - 0.12f, 3900.f, 6.f) * 0.2f : 0.f;
        return whoosh + boom + ring; });
    if (n == "revive")                                           // heartbeat + temple bell + breath + dark drone
        return synth(2.6f, [lp = 0.f, lp2 = 0.f](float t, float) mutable {
        // taiko-like drum with a falling pitch
        auto drum = [](float tt, float amp) -> float {
            if (tt < 0.f) return 0.f;
            float f = 48.f + 70.f * std::exp(-tt * 28.f);
            return (std::sin(6.2832f * f * tt) * std::exp(-tt * 6.f) + nz() * std::exp(-tt * 70.f) * 0.25f) * amp;
            };
        // bonsho (temple bell): inharmonic partials, slow decay
        auto bell = [](float tt, float f0) -> float {
            if (tt < 0.f) return 0.f;
            static const float r[6] = { 1.f, 2.01f, 2.76f, 4.07f, 5.43f, 6.8f };
            static const float d[6] = { 1.1f, 1.5f, 2.0f, 2.8f, 3.6f, 4.5f };
            static const float a[6] = { 1.f, 0.55f, 0.4f, 0.28f, 0.15f, 0.08f };
            float v = 0.f;
            for (int i = 0; i < 6; i++) v += a[i] * ringS(tt, f0 * r[i], d[i]);
            return v * std::min(1.f, tt * 200.f);
            };

        // lub-dub heartbeat: the shadow's heart starts again
        float beat = drum(t, 1.0f) + drum(t - 0.30f, 0.6f);

        // low, solemn bell strike, then a quieter echo an octave up
        float tolling = bell(t - 0.50f, 147.f) * 0.55f + bell(t - 1.10f, 220.f) * 0.2f;

        // exhale: low-passed noise swelling between 0.3s and 1.8s
        lp += 0.06f * (nz() - lp);
        lp2 += 0.30f * (lp - lp2);
        float breath = 0.f;
        if (t > 0.3f && t < 1.8f) {
            float e = std::sin(3.1416f * (t - 0.3f) / 1.5f);
            breath = lp2 * 5.f * e * e * 0.35f;
        }

        // dark drone (D minor-ish): fades in, then dies away
        float denv = std::min(1.f, t / 0.8f) * std::exp(-std::max(0.f, t - 1.2f) * 1.0f);
        float drone = (std::sin(6.2832f * 73.4f * t) + 0.5f * std::sin(6.2832f * 110.f * t)
            + 0.3f * std::sin(6.2832f * 146.8f * t)) * denv * 0.18f;

        return std::tanh((beat * 0.9f + tolling + breath + drone) * 1.1f);   // soft limiter
            });
    if (n == "gun")                                              // matchlock bang
        return synth(0.45f, [y = 0.f](float t, float) mutable { y += 0.5f * (nz() - y); return y * std::exp(-t * 13.f) * 1.4f + ringS(t, 90.f, 9.f) * 0.8f; });
    if (n == "select")  return makeTone(900.f, 1100.f, 0.06f, 0.35f);
    if (n == "confirm") return makeTone(560.f, 1000.f, 0.14f, 0.45f);
    return sf::SoundBuffer();
}
static sf::Font g_font, g_titleFont;
static bool g_fontOk = false, g_titleFontOk = false;

static void txt(sf::RenderTarget& rt, const std::string& s, float x, float y, unsigned size,
    sf::Color col = sf::Color::White, int align = 0, bool title = false) {
    bool useTitle = title && g_titleFontOk;
    if (!g_fontOk && !useTitle) return;
    sf::Text t(s, useTitle ? g_titleFont : g_font, size);
    t.setFillColor(col);
    t.setOutlineColor(sf::Color(0, 0, 0, 220));
    t.setOutlineThickness(2.f);
    sf::FloatRect b = t.getLocalBounds();
    float ox = 0;
    if (align == 1) ox = b.left + b.width / 2.f;
    if (align == 2) ox = b.left + b.width;
    t.setOrigin(ox, 0);
    t.setPosition(x, y);
    rt.draw(t);
}

//  RIG / DRAW HELPERS
static sf::Color mixc(sf::Color a, sf::Color b, float t) {
    return sf::Color((sf::Uint8)(a.r + (b.r - a.r) * t), (sf::Uint8)(a.g + (b.g - a.g) * t),
        (sf::Uint8)(a.b + (b.b - a.b) * t), (sf::Uint8)(a.a + (b.a - a.a) * t));
}
static sf::Color shade(sf::Color c, float k) {
    return sf::Color((sf::Uint8)std::clamp(c.r * k, 0.f, 255.f), (sf::Uint8)std::clamp(c.g * k, 0.f, 255.f),
        (sf::Uint8)std::clamp(c.b * k, 0.f, 255.f), c.a);
}
static void limb(sf::RenderTarget& rt, sf::Vector2f a, sf::Vector2f b, float th, sf::Color c) {
    sf::Vector2f d = b - a;
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 0.01f || th < 0.5f) return;
    sf::RectangleShape r({ len, th });
    r.setOrigin(0, th / 2.f); r.setPosition(a);
    r.setRotation(std::atan2(d.y, d.x) * 57.29578f);
    r.setFillColor(c); rt.draw(r);
    sf::CircleShape j(th / 2.f); j.setOrigin(th / 2.f, th / 2.f); j.setFillColor(c);
    j.setPosition(a); rt.draw(j); j.setPosition(b); rt.draw(j);
}
static sf::Vector2f ik(sf::Vector2f s, sf::Vector2f& t, float l1, float l2, float bend) {
    sf::Vector2f d = t - s;
    float dist = std::sqrt(d.x * d.x + d.y * d.y);
    float maxD = l1 + l2 - 0.5f;
    if (dist > maxD) { t = s + d * (maxD / dist); d = t - s; dist = maxD; }
    if (dist < 1.f) dist = 1.f;
    float a = (l1 * l1 - l2 * l2 + dist * dist) / (2.f * dist);
    float h = std::sqrt(std::max(0.f, l1 * l1 - a * a));
    sf::Vector2f dir = d / dist;
    return s + dir * a + sf::Vector2f(-dir.y, dir.x) * (h * bend);
}
// quad with a colour per corner (gradients)
static void quadV(sf::RenderTarget& rt, sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d,
    sf::Color ca, sf::Color cb, sf::Color cc, sf::Color cd) {
    sf::VertexArray va(sf::Quads, 4);
    va[0] = sf::Vertex(a, ca); va[1] = sf::Vertex(b, cb); va[2] = sf::Vertex(c, cc); va[3] = sf::Vertex(d, cd);
    rt.draw(va);
}
static void diamond(sf::RenderTarget& rt, float cx, float cy, float rx, float ry, sf::Color fill,
    sf::Color outline = sf::Color::Transparent, float ot = 0.f) {
    sf::ConvexShape d(4);
    d.setPoint(0, { cx, cy - ry }); d.setPoint(1, { cx + rx, cy }); d.setPoint(2, { cx, cy + ry }); d.setPoint(3, { cx - rx, cy });
    d.setFillColor(fill); d.setOutlineColor(outline); d.setOutlineThickness(ot);
    rt.draw(d);
}

//  ASSETS
struct Anim { std::vector<std::shared_ptr<sf::Texture>> frames; };

static std::shared_ptr<sf::Texture> loadTex(const std::string& rel) {
    if (!assetExists(rel)) return nullptr;
    auto t = std::make_shared<sf::Texture>();
    if (!t->loadFromFile(asset(rel))) return nullptr;
    t->setSmooth(cfg::SMOOTH_TEXTURES);
    return t;
}

static const char* ANIM_KEYS[] = { "idle", "walk", "dash", "jump", "slash", "airslash", "thrust", "sweep", "barrage",
                                  "spear", "shoot", "block", "parry", "mikiri", "jumpcounter", "hurt",
                                  "deflected", "clash", "broken", "dead", "execute" };

static std::string fallbackKey(const std::string& k) {
    if (k == "parry") return "block";
    if (k == "thrust" || k == "sweep" || k == "barrage" || k == "spear" || k == "shoot" || k == "execute") return "slash";
    if (k == "airslash") return "jump";
    if (k == "dash") return "walk";
    if (k == "jumpcounter") return "jump";
    if (k == "clash") return "deflected";
    if (k == "deflected" || k == "broken" || k == "dead") return "hurt";
    if (k == "idle") return "";
    return "idle";
}

struct CharSet {
    std::map<std::string, Anim> anims;
    float refH = 1.f, fps = 10.f;
    bool any = false;

    bool load(const std::string& dir) {
        anims.clear(); any = false;
        std::ifstream ff(asset(dir + "/fps.txt"));
        float f;
        if (ff >> f && f > 0) fps = f;
        for (const char* key : ANIM_KEYS) {
            Anim a;
            if (auto t = loadTex(dir + "/" + key + ".png")) a.frames.push_back(t);
            else {
                for (int i = 0; i < 100; i++) {
                    auto t2 = loadTex(dir + "/" + key + "_" + std::to_string(i) + ".png");
                    if (t2) a.frames.push_back(t2);
                    else if (i >= 1) break;
                }
            }
            if (!a.frames.empty()) { anims[key] = a; any = true; }
        }
        auto it = anims.find("idle");
        if (it == anims.end() && any) it = anims.begin();
        if (any) refH = (float)it->second.frames[0]->getSize().y;
        return any;
    }
    const Anim* find(std::string key) const {
        for (int i = 0; i < 8 && !key.empty(); i++) {
            auto it = anims.find(key);
            if (it != anims.end() && !it->second.frames.empty()) return &it->second;
            key = fallbackKey(key);
        }
        return nullptr;
    }
};

// seconds of near-silence at the start of a buffer
static float leadSilence(const sf::SoundBuffer& b) {
    const sf::Int16* s = b.getSamples();
    size_t n = b.getSampleCount(), ch = std::max(1u, b.getChannelCount());
    for (size_t i = 0; i < n; i++)
        if (std::abs((int)s[i]) > 1200)
            return std::max(0.f, (float)(i / ch) / (float)b.getSampleRate() - 0.002f);
    return 0.f;
}

struct Assets {
    CharSet players[2], bosses[cfg::LEVELS];
    std::map<std::string, sf::SoundBuffer> sounds;
    std::vector<sf::Sound> pool = std::vector<sf::Sound>(24);
    size_t poolIdx = 0;
    std::shared_ptr<sf::Texture> bgVersus, bgTitle, bgMenu, bgBoss[cfg::LEVELS], bgDefault, danger, tutImg[cfg::LEVELS];
    size_t steelIdx = (size_t)-1;                                      // last parry/clash slot (only one plays at a time)
    std::vector<float> poolLimit = std::vector<float>(24, -1.f), poolBase = std::vector<float>(24, 100.f);                 // pool slot of the last parry/clash sound
    std::map<std::string, float> lead;      // leading silence per sound (seconds)
    void loadAll() {
        g_fontOk = assetExists("font.ttf") && g_font.loadFromFile(asset("font.ttf"));
        if (!g_fontOk) {
            const char* sys[] = { "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                 "/usr/share/fonts/TTF/DejaVuSans.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
                                 "/Library/Fonts/Arial.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf" };
            for (const char* p : sys) { if (!g_fontOk) g_fontOk = g_font.loadFromFile(p); }
            std::printf("[assets] %s not found -> %s\n", asset("font.ttf").c_str(), g_fontOk ? "using a system font" : "NO FONT, text disabled");
        }
        g_titleFontOk = assetExists("font_title.ttf") && g_titleFont.loadFromFile(asset("font_title.ttf"));
        std::printf("[assets] font_title.ttf : %s\n", g_titleFontOk ? "loaded" : "not found (title uses font.ttf)");

        players[0].load("player1");
        if (!players[1].load("player2")) players[1] = players[0];
        CharSet generic;
        bool hasGeneric = generic.load("boss");
        for (int i = 0; i < cfg::LEVELS; i++)
            if (!bosses[i].load("boss" + std::to_string(i + 1)) && hasGeneric) bosses[i] = generic;

        bgDefault = loadTex("background.png");
        bgVersus = loadTex("backgrounds/versus.png");
        bgTitle = loadTex("backgrounds/title.png");
        bgMenu = loadTex("backgrounds/menu.png");
        for (int i = 0; i < cfg::LEVELS; i++) {
            bgBoss[i] = loadTex("backgrounds/bg" + std::to_string(i + 1) + ".png");
            tutImg[i] = loadTex("ui/tutorial_" + std::to_string(i + 1) + ".png");
        }
        danger = loadTex("ui/danger.png");

        const char* names[] = { "swing", "hit", "block", "parry", "mikiri", "jumpcounter", "clash",
                        "deathblow", "jump", "break", "dash", "revive", "gun", "select", "confirm" };
        for (const char* n : names) {
            for (const char* ext : { ".wav", ".ogg" }) {
                std::string rel = std::string("sounds/") + n + ext;
                if (assetExists(rel)) {
                    sf::SoundBuffer b;
                    if (b.loadFromFile(asset(rel))) {
                        lead[n] = leadSilence(b);        // remember the silence at the start of the file
                        sounds[n] = b;
                        break;
                    }
                }
            }
        }
        if (!sounds.count("select"))  sounds["select"] = makeTone(900.f, 1100.f, 0.06f, 0.35f);
        if (!sounds.count("confirm")) sounds["confirm"] = makeTone(560.f, 1000.f, 0.14f, 0.45f);
        for (const char* n : names)
            if (!sounds.count(n)) sounds[n] = synthSfx(n);       // built-in sound whenever no file exists
        std::printf("[assets] sounds: %zu/%zu found\n", sounds.size(), sizeof(names) / sizeof(names[0]));
    }

    void sfx(const std::string& n, float pitch = 1.f, float vol = 100.f) {
        if (n == "hit") return;                       // flesh-hit sound disabled
        auto it = sounds.find(n);
        if (it == sounds.end()) return;
        if (n == "swing" || n == "dash" || n == "jump") vol *= 0.6f;     // keep steel ringing above the whooshes
        size_t i = poolIdx++ % pool.size();
        if (n == "parry" || n == "clash") {                              // only one steel sound at a time
            if (steelIdx < pool.size()) pool[steelIdx].stop();
            steelIdx = i;
        }
        sf::Sound& s = pool[i];
        s.setBuffer(it->second);
        s.setPitch(pitch);
        s.setVolume(vol);
        poolBase[i] = vol;
        poolLimit[i] = (n == "break") ? cfg::BREAK_SOUND_LEN : -1.f;  
        // -1 = play to the end

        float skip = 0.f;
        auto li = lead.find(n);
        if (li != lead.end()) skip = std::min(li->second, 0.25f);                  // trim file silence
        if (n == "hit" || n == "block" || n == "parry" || n == "clash" || n == "deathblow" || n == "mikiri" || n == "jumpcounter")
            skip += cfg::HIT_EXTRA_SKIP;                                           // impact sounds hit a touch earlier
        if (n == "parry") skip += 0.06f;                                           // existing parry trim
        if (skip > 0.f) s.setPlayingOffset(sf::seconds(skip));  // skip the first 30 ms of the file
        s.play();
    }
    void tick(float dt) {                                                // cuts limited sounds with a short fade
        for (size_t i = 0; i < pool.size(); i++) {
            if (poolLimit[i] < 0.f) continue;
            poolLimit[i] -= dt;
            if (poolLimit[i] <= 0.f) { pool[i].stop(); poolLimit[i] = -1.f; }
            else if (poolLimit[i] < 0.08f) pool[i].setVolume(poolBase[i] * poolLimit[i] / 0.08f);
        }
    }
};

// ----------------------------------------------------------------------------
//  MUSIC
// ----------------------------------------------------------------------------
struct Bgm {
    enum { TITLE = 0, BOSS1 = 1, VERSUS = cfg::LEVELS + 1, COUNT = cfg::LEVELS + 2 };
    std::array<std::unique_ptr<sf::Music>, COUNT> trk;
    int cur = -1;
    float fade = 0.f, curVol = 1.f;
    bool muted = false;

    void load() {
        const char* names[COUNT] = { "title", "boss1", "boss2", "boss3", "boss4", "boss5", "boss6", "versus" };
        for (int i = 0; i < COUNT; i++) {
            for (const char* ext : { ".ogg", ".wav", ".flac", ".bgm" }) {
                std::string rel = std::string("music/") + names[i] + ext;
                if (!assetExists(rel)) continue;
                auto m = std::make_unique<sf::Music>();
                if (m->openFromFile(asset(rel))) {
                    m->setLoop(true); m->setVolume(0.f);
                    trk[i] = std::move(m);
                    break;
                }
            }
        }
    }
    void update(int want, float vm, float dt) {
        if (want != cur) {
            if (cur >= 0 && trk[cur]) trk[cur]->stop();
            cur = want; fade = 0.f;
            if (cur >= 0 && trk[cur]) trk[cur]->play();
        }
        if (cur >= 0 && trk[cur]) {
            fade = std::min(1.f, fade + dt / 1.2f);
            curVol += (vm - curVol) * std::min(1.f, dt * 6.f);
            trk[cur]->setVolume(muted ? 0.f : cfg::MUSIC_VOLUME * curVol * fade);
        }
    }
};

//  COMBAT DATA

enum class St {
    Idle, Walk, Dash, Jump, Slash, Thrust, Sweep, Barrage, Block, Mikiri, JumpCounter,
    Hurt, Deflected, Clash, Broken, Dead,
    Spear, Shoot,
    AirSlash, Execute                              // v5: jump slash + deathblow cinematic
};

struct AtkData { float startup, active, recovery, dmg, posture, range; bool perilous; };

static const AtkData& atkData(St s) {
    static const AtkData thrust{ 0.85f, 0.12f, 0.60f, 30, 30, 230, true };
    static const AtkData sweep{ 0.85f, 0.12f, 0.60f, 30, 30, 235, true };
    static const AtkData spear{ 0.32f, 0.12f, 0.36f, 14, 22, 255, false };
    static const AtkData barrage{ 0, 0, 0, 9, 28, 140, false };
    static const AtkData slash{ 0.16f, 0.10f, 0.20f, 12, 18, 125, false };
    static const AtkData shoot{ cfg::GUN_WINDUP, 0, cfg::GUN_TOTAL - cfg::GUN_WINDUP, 0, 0, 520, false };
    static const AtkData air{ 0.12f, 0.16f, 0.16f, 13, 20, 135, false };     // jump slash
    static const AtkData none{ 0, 0, 0, 0, 0, 0, false };
    switch (s) {
    case St::Slash: return slash;
    case St::Thrust: return thrust;
    case St::Sweep: return sweep;
    case St::Barrage: return barrage;
    case St::Spear: return spear;
    case St::Shoot: return shoot;
    case St::AirSlash: return air;
    default: return none;
    }
}
static bool isAttack(St s) { return s == St::Slash || s == St::Thrust || s == St::Sweep || s == St::Spear || s == St::AirSlash; }

// ---- weapon table: ONE source of truth for the drawn blade length and the hit reach ----
// style: 0 ronin katana, 1 katana, 2 odachi, 3 nodachi, 4 twin wakizashi, 5 yari, 6 Kurenai-maru, 7 rival katana
static float styleBladeMul(int style) {
    switch (style) { case 2: return 1.15f; case 3: return 1.55f; case 4: return 0.80f; case 6: return 1.30f; default: return 1.f; }
}
static float styleRange(int style, float heightMul) {
    float base = (style == 5) ? 1.28f : (40.f + 108.f * styleBladeMul(style)) / 148.f;   // arm reach + blade, katana = 1.0
    return base * heightMul;
}

struct Input {
    float move = 0;
    bool jump = false, slash = false, thrust = false, sweep = false, dash = false, barrage = false;
    bool spear = false, shoot = false, blockHeld = false;
    void merge(const Input& b) {
        move = b.move; blockHeld = b.blockHeld;
        jump |= b.jump; slash |= b.slash; thrust |= b.thrust; sweep |= b.sweep; dash |= b.dash; barrage |= b.barrage;
        spear |= b.spear; shoot |= b.shoot;
    }
};

struct Fighter {
    std::string name;
    bool isBoss = false;
    const CharSet* set = nullptr;
    sf::Color color = sf::Color::White;

    float x = 0, y = cfg::GROUND, vx = 0, vy = 0;
    int facing = 1;
    float maxHp = 100, hp = 100, hpGhost = 100, maxPosture = 100, posture = 0;
    int lives = 2, maxLives = 2;
    float heightMul = 1.f, widthMul = 1.f;
    float rangeMul = 1.f;                            // weapon reach (odachi / nodachi / wakizashi ...)
    int combo = 0;
    int style = 0;                                   // 0 = ronin hero, 1..6 = boss gear
    St st = St::Idle;
    float t = 0, animT = 0, sinceHit = 10, blockT = 0, parryWindow = cfg::PARRY_WINDOW, lastBlockPress = -10;
    float invuln = 0, dashDir = 1;
    bool hitDone = false, swung = false, airUsed = false;
    int attackId = 0, barrageCount = 5, strikeIdx = 0, barrageMin = 5, barrageMax = 7;
    float speedMul = 1, dmgMul = 1, postureRegen = 14;
    bool canSpear = false, canGun = false;
    float gunCd = 0.f;
    int seenAtk = -1;
    int deflectKind = 0;                             // 1 = pinned by a mikiri, 2 = stomped by a jump counter

    bool grounded() const { return y >= cfg::GROUND - 0.01f; }
    float spd() const {
        if (st == St::Slash || st == St::Spear || st == St::AirSlash) return speedMul;
        return 1.f + (speedMul - 1.f) * 0.35f;
    }
    float attackTotal() const {
        const AtkData& a = atkData(st);
        return (a.startup + a.active + a.recovery) / spd();
    }
};
static bool finalDead(const Fighter& f) { return f.st == St::Dead && f.lives <= 1; }
static bool deflecting(const Fighter& d) { return d.st == St::Block && d.parryWindow > 0 && d.blockT < d.parryWindow; }
// every hit of a slash combo can be deflected from a fresh guard (like Sekiro), regardless of the anti-spam delay
static bool canDeflect(const Fighter& a, const Fighter& d) {
    if (d.st != St::Block) return false;
    if (d.parryWindow > 0 && d.blockT < d.parryWindow) return true;
    return a.combo > 0 && d.blockT < 0.30f;
}

struct Bullet { sf::Vector2f p; float vx; int owner; float life; };
struct Slice { sf::Vector2f a, b; float life, max; sf::Color c; float th; }; // sword-cut streaks
struct RiseFx { float x, t, dur; sf::Color c; };
struct Shock { sf::Vector2f p; float t, dur, maxR, squash; sf::Color c; };           // expanding ring
struct Ember { sf::Vector2f p, v; float life, max, th; sf::Color c; };               // fast streak spark

//  INPUT: human pad + AI

struct Keys { sf::Keyboard::Key left, right, jump, slash, block, dash, thrust, sweep; };

struct Pad {
    Keys k;
    std::array<bool, 5> prev{};
    Input poll() {
        auto d = [](sf::Keyboard::Key key) { return sf::Keyboard::isKeyPressed(key); };
        Input in;
        in.move = (d(k.right) ? 1.f : 0.f) - (d(k.left) ? 1.f : 0.f);
        in.blockHeld = d(k.block);
        bool now[5] = { d(k.jump), d(k.slash), d(k.dash), d(k.thrust), d(k.sweep) };
        in.jump = now[0] && !prev[0];
        in.slash = now[1] && !prev[1];
        in.dash = now[2] && !prev[2];
        in.thrust = now[3] && !prev[3];
        in.sweep = now[4] && !prev[4];
        in.sweep = false;   // players cannot sweep; only the AI uses it
        for (int i = 0; i < 5; i++) prev[i] = now[i];
        return in;
    }
};

struct AIParams {
    float react, aggr, parry, mikiri, jump, perilous, err;
    bool sweep, thrust;
    float barrage, spear, gun;
    bool uniform = false;
};

struct AI {
    AIParams p{};
    float cd = 1.f, strafeT = 0, strafeDir = 0, blockHold = 0;
    int seen = -1, comboLeft = 0;
    bool hasPlan = false;
    bool counterRolled = false, counterGo = false;
    St planned = St::Slash;
    bool planJump = false;
    struct Pend { bool on = false; int type = 0; float time = 0; } pend;

    void reset() { cd = 1.2f; strafeT = 0; blockHold = 0; seen = -1; comboLeft = 0; hasPlan = false;     counterRolled = false; pend = Pend(); }

    Input think(const Fighter& me, const Fighter& o, float dt) {
        Input in;
        if (me.st == St::Dead || o.st == St::Dead) return in;
        float dist = std::fabs(o.x - me.x);
        float toward = (o.x >= me.x) ? 1.f : -1.f;
        bool free = (me.st == St::Idle || me.st == St::Walk || me.st == St::Block ||
            (me.st == St::Hurt && me.t >= 0.12f) || (me.st == St::Deflected && me.t >= 0.15f));
        if (me.st == St::Slash && comboLeft > 0 && me.combo < 2) {
            const AtkData& sa = atkData(St::Slash);
            float ts = me.t * me.spd();
            if (ts > sa.startup + sa.active && ts < sa.startup + sa.active + 0.12f && dist <= (sa.range + 30) * me.rangeMul) {
                in.slash = true; comboLeft--; return in;
            }
        }
        if (me.st == St::Jump && planJump) {
            // in the air: attack once when over the target
            if (!me.airUsed && std::fabs(o.x - me.x) < 150.f * me.rangeMul) { in.slash = true; planJump = false; }
            else in.move = toward;      // drift over the player
            if (me.grounded()) planJump = false;
            return in;
        }

        if (!free) { pend.on = false; blockHold = 0; return in; }

        if (isAttack(o.st) && o.attackId != seen) {
            seen = o.attackId;
            const AtkData& a = atkData(o.st);
            if (dist <= a.range * o.rangeMul + 110) {
                float tth = (a.startup - o.t * o.spd()) / o.spd();
                float j = rnd(-p.err, p.err);
                pend.on = false;
                if (o.st == St::Slash || o.st == St::Spear || o.st == St::AirSlash) {
                    float r = rnd01();
                    float pc = std::min(0.95f, p.parry + 0.15f);        // skill: chance to parry THIS swing (each combo hit rolls again)
                    if (r < pc) pend = { true, 1, std::max(0.f, tth - 0.09f + j) };   // timing noise = p.err
                    else if (r < p.parry + 0.25f) pend = { true, 4, std::max(p.react * 0.5f, tth - 0.25f) };
                }
                else if (o.st == St::Thrust) {
                    if (rnd01() < p.mikiri) pend = { true, 2, std::max(p.react, tth - 0.14f + j) };
                }
                else if (o.st == St::Sweep) {
                    if (rnd01() < p.jump) pend = { true, 3, std::max(p.react, tth - 0.27f + j) };
                }
            }
        }
        if (pend.on) {
            if (me.st == St::Hurt) return in;    // while staggered the AI may only guard, not attack
            pend.time -= dt;
            if (pend.time <= 0) {
                pend.on = false;
                switch (pend.type) {
                case 1: blockHold = 0.45f; break;
                case 4: blockHold = 0.6f; break;
                case 2: in.dash = true; return in;
                case 3: in.jump = true; return in;
                }
            }
        }
        if (o.st == St::Deflected) blockHold = 0;            // stop holding guard: time to counter
        if (blockHold > 0) { blockHold -= dt; in.blockHeld = true; return in; }
        if (pend.on) return in;

        float slashR = atkData(St::Slash).range * me.rangeMul;
        if (o.st == St::Broken) {
            if (dist < slashR - 10) in.slash = true; else in.move = toward;
            return in;
        }
        if (o.st == St::Deflected) {                         // I just parried him: counterattack, chance scales with aggression
            if (!counterRolled) { counterRolled = true; counterGo = rnd01() < std::min(0.95f, 0.35f + p.aggr * 0.30f); }
            if (counterGo && me.st != St::Hurt && dist <= slashR - 5) {
                in.slash = true; comboLeft = (rnd01() < 0.5f) ? 1 : 0; cd = 0.25f; hasPlan = false;
                return in;
            }
        }
        else counterRolled = false;
        cd -= dt; strafeT -= dt;
        if (!hasPlan && cd <= 0) {
            float r = rnd01();
            planned = St::Slash;
            if (p.uniform) {
                // every move gets the same chance
                std::vector<St> pool = { St::Slash, St::Barrage };
                if (p.thrust) pool.push_back(St::Thrust);
                if (p.sweep)  pool.push_back(St::Sweep);
                if (me.canSpear) pool.push_back(St::Spear);
                if (me.canGun && me.gunCd <= 0.f) pool.push_back(St::Shoot);
                pool.push_back(St::Jump);   // jump attack
                // jump is a movement option handled below
                planned = pool[(size_t)rnd(0.f, (float)pool.size() - 0.001f)];
            }
            else {
                float rg = (me.canGun && me.gunCd <= 0.f) ? p.gun : 0.f;
                if (r < rg) planned = St::Shoot;
                else if (r < rg + p.barrage) planned = St::Barrage;
                else if (r < rg + p.barrage + p.perilous) {
                    if (p.sweep && p.thrust) planned = (rnd01() < 0.5f) ? St::Thrust : St::Sweep;
                    else if (p.sweep) planned = St::Sweep;
                    else if (p.thrust) planned = St::Thrust;
                }
                else if (me.canSpear && rnd01() < p.spear) planned = St::Spear;
            }
            hasPlan = true;
        }
        float want = hasPlan ? (planned == St::Jump ? 260.f : atkData(planned).range * me.rangeMul * 0.85f) : 190.f;
        if (dist > want + 10) {
            in.move = toward * (hasPlan ? 1.f : 0.55f);
        }
        else if (!hasPlan) {
            if (strafeT <= 0) { strafeT = rnd(0.3f, 0.9f); float r = rnd01(); strafeDir = r < 0.3f ? -1.f : (r < 0.6f ? 1.f : 0.f); }
            in.move = strafeDir * toward * 0.4f;
            if (dist < 110) in.move = -toward * 0.7f;
        }
        if (hasPlan && dist <= want + 10) {
            hasPlan = false;
            if (planned == St::Slash) {
                in.slash = true;
                if (comboLeft > 0) { comboLeft--; cd = 0.2f / p.aggr; }
                else { cd = rnd(0.45f, 1.1f) / p.aggr; if (rnd01() < 0.5f) comboLeft = 1 + (rnd01() < 0.4f ? 1 : 0); }
            }
            else if (planned == St::Barrage) { in.barrage = true; comboLeft = 0; cd = rnd(1.3f, 2.0f) / p.aggr; }
            else if (planned == St::Spear) { in.spear = true; comboLeft = 0; cd = rnd(0.6f, 1.2f) / p.aggr; }
            else if (planned == St::Shoot) { in.shoot = true; comboLeft = 0; cd = rnd(0.8f, 1.4f) / p.aggr; }
            else if (planned == St::Jump) {
                in.jump = true; planJump = true; comboLeft = 0; cd = rnd(0.9f, 1.5f) / p.aggr;
            }
            else {
                if (planned == St::Thrust) in.thrust = true; else in.sweep = true;
                comboLeft = 0; cd = rnd(0.9f, 1.6f) / p.aggr;
            }
        }
        return in;
    }
};

//  BOSS TABLE  (level 1..6)  - each boss has his own weapon (range = reach multiplier)

struct BossDef {
    const char* name; float hp, posture, speed, dmg, regen; AIParams ai;
    int barrageMin, barrageMax; const char* newTip;
    float height, width; int lives;
    bool spear, gun;
    float range; const char* weapon;
};
//                                          react aggr parry mik  jump perilous err  sweep  thrust barrage spear gun
static const BossDef BOSSES[cfg::LEVELS] = {
    {"Ashigaru Captain", 80, 90, 0.85f, 0.70f, 8,  {0.35f, 0.7f, 0.15f, 0.0f, 0.0f, 0.00f, 0.10f, false, false, 0.00f, 0.0f, 0.0f}, 0, 0,
     "",
     1.00f, 1.00f, 2, false, false, 1.00f, "Katana"},
    {"Crimson Duelist",  100, 100, 0.95f, 0.85f, 10, {0.28f, 0.9f, 0.30f, 0.0f, 0.30f, 0.30f, 0.08f, true, false, 0.00f, 0.0f, 0.0f}, 0, 0,
     "",
     1.06f, 1.28f, 2, false, false, 1.10f, "Odachi - the Great Sword"},
    {"Shadow Monk",      130, 110, 1.05f, 1.00f, 12, {0.20f, 1.1f, 0.45f, 0.50f, 0.50f, 0.30f, 0.06f, true, true, 0.00f, 0.0f, 0.0f}, 0, 0,
     "",
     1.05f, 0.80f, 2, false, false, 1.28f, "Nodachi - the Long Sword"},
    {"Blade Saint",      160, 125, 1.15f, 1.10f, 14, {0.15f, 1.3f, 0.60f, 0.70f, 0.70f, 0.20f, 0.04f, true, true, 0.38f, 0.0f, 0.0f}, 5, 7,
     "",
     0.88f, 1.05f, 2, false, false, 0.95f, "Twin Wakizashi - Short Swords"},
    {"Matchlock Hayate", 170, 120, 1.35f, 1.05f, 14, {0.12f, 1.5f, 0.50f, 0.60f, 0.60f, 0.00f, 0.04f, false, false, 0.00f, 0.60f, 0.30f}, 0, 0,
     "",
     1.02f, 0.88f, 2, true, true, 1.00f, "Yari Spear + Matchlock Gun"},
    {"Sword Demon",      220, 150, 1.25f, 1.25f, 16, {0.10f, 1.7f, 0.80f, 0.85f, 0.85f, 0.28f, 0.03f, true, true, 0.22f, 0.50f, 0.20f,true}, 7, 10,
     "",
     1.15f, 1.15f, 3, true, true, 1.18f, "Kurenai-maru, the Crimson Blade"},
};
static sf::Color bossColor(int lv) { return sf::Color(200 - lv * 20, 60 + lv * 10, 160 + lv * 15); }


//  STORY + SHORT "NEW THIS LEVEL" TEXT (shown before each duel)

struct LevelInfo { const char* chapter; std::vector<std::string> lore; std::vector<std::string> news; };
static const LevelInfo LEVELINFO[cfg::LEVELS] = {
    {"CHAPTER I  -  THE ASHINA OUTSKIRTS",
     {"Years ago the Sword Demon's clan burned the house of Lord Kuro and carried the young heir away.",
      "Wolf, his last retainer, lost his name, his rank and his master that night. Now he is a ronin,",
      "walking the dusk road to Ashina Castle with one katana and one oath.",
      "At the first gate stands the Ashigaru Captain, a plain soldier who still serves the clan."},
     {"DEFLECT: Tap K just before a hit lands - the attacker's posture fills, not yours.",
      "BLOCK (hold K) costs no health, only posture. Break his posture, then strike for a DEATHBLOW."}},
    {"CHAPTER II  -  THE MOONLIT CASTLE",
     {"Beyond the gate the keep sleeps under a pale moon, its banners the colour of old blood.",
      "Here lives the Crimson Duelist, who has never lost to a man who stood his ground.",
      "His odachi is too heavy to block forever, and too wide to dodge on foot.",
      "Wolf has learned a secret of the road: the sky is the one place a blade cannot follow."},
     {"SWEEP (low, unblockable): JUMP over it for a JUMP COUNTER.",
      "Press J in the air for a JUMP SLASH. You cannot block while airborne."}},
    {"CHAPTER III  -  THE BAMBOO TEMPLE",
     {"The monks who once guarded Lord Kuro's shrine were bought with clan silver.",
      "Their champion, the Shadow Monk, is tall, silent and patient, and he carries a nodachi",
      "so long that every thrust arrives like a spear of steel from out of the mist.",
      "Slip through the blade, say the old masters, and the temple will open."},
     {"THRUST (red warning): Press DASH (L) TOWARD him as it lands = MIKIRI COUNTER.",
      "Too early or too late and you are screwed. His long blade reaches further than a katana."}},
    {"CHAPTER IV  -  THE SNOWY PASS",
     {"Snow closes the mountain road. Only one man keeps the pass: the Blade Saint.",
      "Two short swords, four hundred winters of discipline, and not one wasted motion.",
      "His strikes come like snowfall, one after another, and they do not stop for anything.",
      "Wolf has only one answer to a storm: stay calm, and turn every blade aside."},
     {"BARRAGE: A chain of armored strikes. Blocking breaks you - DEFLECT every strike, one tap each.",
      "He fights with two blades: expect fast cuts, sweeps and thrusts between the barrages."}},
    {"CHAPTER V  -  THE BURNING VILLAGE",
     {"The village below the castle burns, lit by the clan's newest hound, Matchlock Hayate.",
      "He is quick as a hare and fights from a distance: a long yari in one hand,",
      "and a smoking matchlock that needs five slow breaths between shots.",
      "In the glow of the burning roofs, a spear point and a gun barrel follow the same man."},
     {"SPEAR: very long reach, block or deflect it like a slash.  GUN: one bullet every 5 seconds.",
      "Deflect a bullet = it vanishes (no posture). Block = chip damage. Jumping over it works too."}},
    {"CHAPTER VI  -  THE CRIMSON LILY FIELD",
     {"Past the burning village lies a field of red spider lilies under a blood-coloured moon.",
      "Here waits the Sword Demon, wearing Lord Kuro's helm and carrying Kurenai-maru,",
      "a blade said to be forged from the blood of a hundred fallen lords.",
      "He will rise twice after he falls. Wolf has only two lives. It is enough."},
     {"FINAL BOSS: slash, sweep, thrust, barrage, spear and gun - all at once. He has THREE lives.",
     }},
};
static const std::vector<std::string> EPILOGUE = {
    "The Sword Demon falls, and Kurenai-maru shatters among the lilies.",
    "Beneath the horned helm there is only a tired old man, and the clan's curse dies with him.",
    "Dawn climbs over Ashina Castle. Lord Kuro walks free at last.",
    "Wolf sheathes his katana. A ronin no longer - a samurai once more."
};

static const std::vector<std::string> DEATH_LINES = {
    "Hesitation is defeat.",
    "Your posture broke long before your will did.",
    "A closed guard is a closed grave. Deflect, do not flinch.",
    "The Wolf bleeds, but the Wolf does not stay down.",
    "Shadows die twice. So can you - breathe and begin again.",
    "The blade remembers every mistake. Learn its rhythm.",
    "Even the sharpest sword was once broken and reforged."
};
static const std::vector<std::string> VERSUS_LINES = {
    "Steel sang, and only one samurai is left standing.",
    "A duel worthy of a legend.",
    "Posture broken, honour intact.",
    "The lanterns flicker as the loser kneels.",
    "One breath, one cut - and the arena falls silent."
};
static const char* VICTORY_TITLE[cfg::LEVELS] = {
    "THE CAPTAIN FALLS", "THE CRIMSON BLADE BREAKS", "THE MONK'S VIGIL ENDS",
    "THE SAINT KNEELS IN THE SNOW", "THE HOUND IS SILENCED", "THE DEMON IS SLAIN" };
static const std::string VICTORY_SUB[cfg::LEVELS] = {
    "The first gate stands open. Ashina Castle waits beyond it.",
    "The great sword lies silent. The moon watches the ronin walk on.",
    "The mist parts, and the temple bells fall quiet for good.",
    "Snow covers the pass, and four hundred winters of discipline end.",
    "The embers settle. The burning village has lost its hound.",
    "Kurenai-maru is shattered. Only the dawn remains." };

static int loreChars(int lv) {
    int n = 0;
    for (const auto& s : LEVELINFO[std::clamp(lv, 0, cfg::LEVELS - 1)].lore) n += (int)s.size();
    return n;
}

//  HOW TO PLAY (menu) - full pages

struct TutPage { const char* title; std::vector<std::string> lines; };
static const std::vector<TutPage> TUT = {
     {"CONTROLS", {
        "PLAYER 1 (KEYBOARD)",
        "  A / D :  Move                    W :  Jump",
        "  J :  Slash.",
        "  J in the air :  Jump slash (once per jump, and you cannot block while airborne)",
        "  K :  Block (hold). Tap just before a hit to DEFLECT.",
        "  L :  Dash. ",
        "  U :  Thrust (perilous)",
        "",
        "GENERAL",
        "  P :  Pause or go back         R :  Restart round         F2 :  Mute music",
        "  F1 :  Debug view              Enter :  Resurrect when you fall",
        "  Menu:  Up / Down, Enter, Left / Right or 1-6.",
        "  Player 2 keys are shown on the Versus screen."}},
     {"POSTURE", {
        "Every fighter has a POSTURE bar under the health bar.",
        "",
        "It fills when you block a hit, take a hit, or get deflected.",
        "Deflecting fills the ATTACKER's bar. Counters (mikiri, jump) fill it a lot.",
        "",
        "It recovers slowly, starting about one second after the last hit:",
        "   -  Holding block stops all recovery. Release your guard to let it drain.",
        "   -  Low health means slow recovery. The more wounded a fighter is,",
        "       the longer the posture bar stays high. This applies to every boss too.",
        "",
        "BROKEN: a full bar staggers you for about 2.5 seconds, wide open to a DEATHBLOW.",
        "You have two lives. When you fall you may choose:",
        "     ENTER  =  Resurrect with 60% health",
        "     BACKSPACE  =  Accept death",}},
    {"1 - DEFLECTION", {
        "Fight posture, not just health.",
        "",
        "BLOCK (hold K): you lose no health, but your posture bar fills.",
        "DEFLECT (tap K just before the hit lands): you take almost no posture damage",
        "and the ATTACKER's posture fills instead. Deflecting is the best defence.",
        "",
        "Pressing block again too quickly will not deflect, so tap once and then wait.",
        "A full posture bar means BROKEN. Attack a broken enemy for a DEATHBLOW."
        }},
    {"2 - JUMPING & JUMP COUNTER", {
        "Some attacks are PERILOUS. A red warning sign appears over the attacker.",
        "",
        "SWEEP (low sword swing, shown as 'SWEEP -> JUMP'):",
        "  -  It cannot be blocked or deflected.",
        "  -  Press W so that you are in the air when the sword arrives.",
        "",
        "A well-timed jump is a JUMP COUNTER: you stomp the enemy, their posture takes",
        "a huge hit and they are staggered. Jump too early or too late and you are hit.",
        "In the air you cannot block, but J gives a JUMP SLASH (once per jump).",
        }},
    {"3 - MIKIRI COUNTER", {
        "THRUST (a straight stab, shown as 'THRUST -> DASH IN') is also PERILOUS.",
        "It cannot be blocked. You can use it too, with U.",
        "",
        "Press DASH (L) TOWARD the enemy right as the thrust is about to land.",
        "You slip through the blade for a MIKIRI COUNTER: massive posture damage to them.",
        "",
        "Dash too late and you are skewered. Dashing backwards (hold back + L)",
        "only makes distance. It is not a counter.",
        "",
        }},
    {"4 - BARRAGE", {
        "The Blade Saint unleashes a BARRAGE: a relentless chain of strikes.",
        "",
        "While swinging he is ARMORED, so your attacks do nothing to him.",
        "Blocking each strike costs huge posture, and you will break in a few hits.",
        "",
        "The only way out is to DEFLECT every strike: tap K once per swing."
        }},
    {"5 - SPEAR & GUN", {
        "Matchlock Hayate is quick and fights from long range.",
        "",
        "SPEAR: very long reach. It is not perilous, so block or deflect it like a slash,",
        "or step in fast. The edge of its reach is the most dangerous place to stand.",
        "",
        "GUN: a yellow warning appears, then he fires one bullet (once every 5 seconds).",
        "  -  Deflect it (tap K just before it lands) and the bullet vanishes.",
        "      It adds no posture damage to him. Only his sword and spear can break him.",
        "  -  Block it (hold K) and you take small chip damage to your health."}},
    {"6 - THE SWORD DEMON", {
        "The final boss uses everything: slashes, sweeps, thrusts, jump slashes,",
        "barrages, the long spear and the gun.",
        "",
        "THREE LIVES: the Sword Demon rises twice, faster and stronger each time.",
        "A DEATHBLOW drains a whole life bar, so land one on every life.",
        
        "",
        "Remember:  deflect slashes, spear and barrage  |  jump sweeps  |  dash thrusts",
        "           deflect or block bullets (one per 5 seconds).",
        "Good luck."}},
};


//  ARENA THEMES  (one unique look per level, index 6 = versus)

struct Theme { sf::Color top, bot, far, mid, floorC, seam, mist, ring; };
static const Theme THEMES[7] = {
    { {44,30,66}, {232,128,72}, {84,44,78}, {52,32,56}, {44,32,36}, {66,50,52}, {255,200,170,22}, {210,120,70,160} },
    { {8,10,30}, {44,34,76}, {26,24,54}, {16,14,38}, {34,32,46}, {56,52,70}, {170,170,230,18}, {150,150,230,130} },
    { {22,54,52}, {160,198,160}, {44,86,74}, {30,64,54}, {36,48,36}, {54,70,52}, {220,255,230,28}, {90,170,100,130} },
    { {118,138,170}, {226,232,242}, {150,166,192}, {120,136,162}, {204,212,224}, {172,182,200}, {255,255,255,40}, {110,150,210,150} },
    { {30,8,8}, {232,92,32}, {62,18,16}, {36,10,10}, {38,22,20}, {64,38,30}, {90,70,70,40}, {255,120,40,160} },
    { {14,0,10}, {112,10,32}, {52,6,26}, {30,4,16}, {30,12,22}, {58,24,38}, {255,80,100,24}, {255,50,80,160} },
    { {27,20,36}, {74,36,50}, {36,26,52}, {26,20,40}, {28,24,28}, {46,40,48}, {200,200,220,18}, {150,70,70,150} },
};

struct Particle { sf::Vector2f p, v; float life, max, size; sf::Color c; };
struct FloatText { std::string s; sf::Vector2f p; float life, max; sf::Color c; unsigned size; };

enum class Scene { Title, Menu, Tutorial, Intro, Fight, Paused, Revive, RoundEnd, Victory };


//  GAME

class Game {
public:
    void run() {
        sf::RenderWindow win;
        applyWindow(win);
        canvas.create(cfg::W, cfg::H);
        canvas.setSmooth(true);
        as.loadAll();
        bgm.load();
        for (int i = 0; i < SAVE_SLOTS; i++) saves[i] = readSave(i);
        selectSlot(0);
        runs = readRuns();
        pads[0].k = { sf::Keyboard::A, sf::Keyboard::D, sf::Keyboard::W, sf::Keyboard::J,
                     sf::Keyboard::K, sf::Keyboard::L, sf::Keyboard::U, sf::Keyboard::I };
        pads[1].k = { sf::Keyboard::Left, sf::Keyboard::Right, sf::Keyboard::Up, sf::Keyboard::Comma,
                     sf::Keyboard::Period, sf::Keyboard::Slash, sf::Keyboard::Semicolon, sf::Keyboard::Quote };
        sf::Clock clk;
        while (win.isOpen()) {
            float dt = std::min(clk.restart().asSeconds(), 1.f / 30.f);
            sf::Event e;
            while (win.pollEvent(e)) {
                if (e.type == sf::Event::Closed) win.close();
                else if (e.type == sf::Event::KeyPressed) onKey(win, e.key.code);
            }
            update(dt);
            render(canvas);
            canvas.display();
            win.clear(sf::Color::Black);
            sf::Vector2u ws = win.getSize();
            win.setView(sf::View(sf::FloatRect(0.f, 0.f, (float)ws.x, (float)ws.y)));
            float sc = std::min((float)ws.x / cfg::W, (float)ws.y / cfg::H);
            sf::Sprite spr(canvas.getTexture());
            spr.setScale(sc, sc);
            spr.setPosition((ws.x - cfg::W * sc) / 2.f, (ws.y - cfg::H * sc) / 2.f);
            win.draw(spr);
            win.display();
        }
    }

private:
    Assets as;
    Bgm bgm;
    Scene scene = Scene::Title;
    Fighter F[2];
    Fighter menuHero, menuFoe;
    Pad pads[2];
    AI ai;
    Input buf[2];
    bool versus = false, debug = false, matchOver = false, tutFromMenu = false;
    bool challenge = false, versusBrief = false;
    int menuPage = 0, subIdx = 0, pauseIdx = 0;          // menuPage 0 = main list, 1 = campaign submenu
    SaveData save;                       // = data of the currently selected slot
    SaveData saves[SAVE_SLOTS];
    int slot = 0;
    void selectSlot(int s) { slot = (s + SAVE_SLOTS) % SAVE_SLOTS; save = saves[slot]; }
    std::vector<RunRecord> runs;
    bool victBestTime = false, victBestDeaths = false;
    float victPrevBestT = -1.f;
    int victPrevBestD = -1, victRunNo = 0;
    int statsScroll = 0;
    std::string saveMsg; float saveMsgT = 0.f;
    float roundT = 0.f;
    std::string endLine;
    int level = 0, startLevel = 0, menuIdx = 0, tutPage = 0, reviveWho = 0;
    int winner = -1, wins[2] = { 0, 0 };
    int execA = -1;                                   // index of the fighter performing a deathblow (-1 = none)
    int statDeaths = 0, statRezz = 0;
    float statTime = 0, loreT = 0, victoryT = 0;
    float hitStop = 0, shake = 0, introT = 0, endT = 0, flashT = 0, clockT = 0;
    float camX = cfg::WORLD_W / 2.f;
    std::vector<Particle> parts;
    std::vector<FloatText> texts;
    std::vector<Bullet> bullets;
    std::vector<Slice> slices;
    std::vector<RiseFx> rises;
    std::vector<Shock> shocks;
    std::vector<Ember> embers;
    float reviveT = -1.f; bool reviveBoss = false; std::string reviveTitle, reviveSub;
    bool fullscreen = false;
    sf::RenderTexture canvas;
    // ------------------------------------------------------------ setup ---
    void setupFighter(Fighter& f, const std::string& name, const CharSet* set, sf::Color c, float x, int face) {
        f = Fighter();
        f.name = name; f.set = set; f.color = c; f.x = x; f.facing = face;
    }
    void updateCamera(float dt, bool snap = false) {
        float target = std::clamp((F[0].x + F[1].x) * 0.5f, cfg::W / 2.f, cfg::WORLD_W - cfg::W / 2.f);
        camX = snap ? target : camX + (target - camX) * std::min(1.f, dt * 8.f);
    }
    void startFight() {
        parts.clear(); texts.clear(); bullets.clear(); slices.clear(); rises.clear(); shocks.clear(); embers.clear(); reviveT = -1.f;
        hitStop = 0; shake = 0; endT = 0; winner = -1; execA = -1;
        buf[0] = Input(); buf[1] = Input();
        setupFighter(F[0], versus ? "PLAYER 1" : "WOLF", &as.players[0], sf::Color(80, 140, 230), cfg::WORLD_W / 2.f - 260.f, 1);
        if (versus) {
            setupFighter(F[1], "PLAYER 2", &as.players[1], sf::Color(220, 80, 80), cfg::WORLD_W / 2.f + 260.f, -1);
            F[1].style = 7;
        }
        else {
            const BossDef& b = BOSSES[level];
            setupFighter(F[1], b.name, &as.bosses[level], bossColor(level), cfg::WORLD_W / 2.f + 260.f, -1);
            F[1].isBoss = true;
            F[1].maxHp = F[1].hp = b.hp;
            F[1].maxPosture = b.posture;
            F[1].speedMul = b.speed; F[1].dmgMul = b.dmg; F[1].postureRegen = b.regen;
            F[1].barrageMin = b.barrageMin; F[1].barrageMax = b.barrageMax;
            F[1].heightMul = b.height; F[1].widthMul = b.width; F[1].style = level + 1;
            F[1].rangeMul = styleRange(level + 1, b.height);
            F[1].lives = F[1].maxLives = b.lives;
            F[1].canSpear = b.spear; F[1].canGun = b.gun; F[1].gunCd = 2.5f;
            ai.p = b.ai; ai.reset();
        }
        F[0].hpGhost = F[0].hp; F[1].hpGhost = F[1].hp;
        updateCamera(0.f, true);
        scene = Scene::Intro;
        introT = 1.8f;
    }
    void showTutorial() { scene = Scene::Tutorial; tutPage = level; tutFromMenu = false; versusBrief = false; loreT = 0.f; }
    void startMatch() {
        menuPage = 0;
        wins[0] = wins[1] = 0; matchOver = false;
        statDeaths = statRezz = 0; statTime = 0;
        level = challenge ? startLevel : 0;                 // campaign always starts at boss 1 with lore
        versusBrief = false;
        if (versus) { scene = Scene::Tutorial; tutFromMenu = false; versusBrief = true; }
        else if (challenge) startFight();                   // challenge: straight into that one duel
        else showTutorial();
    }
    void applyWindow(sf::RenderWindow& win) {
        if (fullscreen) win.create(sf::VideoMode::getDesktopMode(), "Sekiro 2D", sf::Style::Fullscreen);
        else win.create(sf::VideoMode(cfg::W, cfg::H), "Sekiro 2D", sf::Style::Titlebar | sf::Style::Close | sf::Style::Resize);
        win.setFramerateLimit(60);
        win.setMouseCursorVisible(!fullscreen);
    }
    // ----------------------------------------------------------- events ---

    void loadCampaign() {
        if (!save.valid) return;
        versus = false; challenge = false; matchOver = false; wins[0] = wins[1] = 0;
        level = save.level; statDeaths = save.deaths; statRezz = save.rezz; statTime = save.time;
        menuPage = 0;
        showTutorial();                                   // resumes at that boss's chapter screen
    }
    void pauseConfirm() {
        switch (pauseIdx) {
        case 0: scene = Scene::Fight; break;
        case 1:
            if (!versus && !challenge) {
                SaveData s; s.level = level; s.deaths = statDeaths; s.rezz = statRezz; s.time = statTime; s.valid = true;
                if (writeSave(slot, s)) {
                    saves[slot] = s; save = s;
                    saveMsg = "Saved to slot " + std::to_string(slot + 1) + "  -  Chapter " + std::to_string(level + 1) + ", " + BOSSES[level].name;
                    as.sfx("revive", 1.4f);
                }
                else saveMsg = "Could not write the save file!";
            }
            else saveMsg = "Saving is only available in the campaign";
            saveMsgT = 3.f;
            break;
        case 2: startFight(); break;
        case 3: bgm.muted = !bgm.muted; break;
        case 4: scene = Scene::Menu; menuPage = 0; break;
        }
    }

    // writes the current campaign stats to savegame.sav (campaign only)
    void autoSaveCampaign() {
        if (versus || challenge) return;
        SaveData s;
        s.level = level; s.deaths = statDeaths; s.rezz = statRezz; s.time = statTime; s.valid = true;
        if (writeSave(slot, s)) { saves[slot] = s; save = s; }
    }

    void onKey(sf::RenderWindow& win, sf::Keyboard::Key k) {
        using K = sf::Keyboard;
        if (k == K::F11 || (k == K::Enter && (K::isKeyPressed(K::LAlt) || K::isKeyPressed(K::RAlt)))) {
            fullscreen = !fullscreen; applyWindow(win); return;
        }
        if (k == K::F2) { bgm.muted = !bgm.muted; return; }
        const int L = cfg::LEVELS;
        {
            bool ui = scene == Scene::Title || scene == Scene::Menu || scene == Scene::Tutorial ||
                scene == Scene::Paused || scene == Scene::RoundEnd || scene == Scene::Victory;
            if (ui) {
                if (k == K::Up || k == K::Down || k == K::Left || k == K::Right || (k >= K::Num1 && k <= K::Num6)) as.sfx("select");
                else if (k == K::Enter || k == K::Space) as.sfx("confirm");
                else if (k == K::P || k == K::Escape || k == K::BackSpace) as.sfx("select", 0.75f);
            }
        }
        switch (scene) {
        case Scene::Title:
            if (k == K::Enter || k == K::Space) scene = Scene::Menu;
            break;
        case Scene::Menu:
            if (menuPage == 0) {
                if (k == K::Up) menuIdx = (menuIdx + 4) % 5;
                else if (k == K::Down) menuIdx = (menuIdx + 1) % 5;
                else if (k == K::Left && menuIdx == 2) startLevel = (startLevel + L - 1) % L;
                else if (k == K::Right && menuIdx == 2) startLevel = (startLevel + 1) % L;
                else if (k >= K::Num1 && k <= K::Num6) startLevel = k - K::Num1;
                else if (k == K::Enter || k == K::Space) {
                    if (menuIdx == 0) { menuPage = 1; subIdx = save.valid ? 0 : 1; }
                    else if (menuIdx == 1) { versus = true; challenge = false; startMatch(); }
                    else if (menuIdx == 2) { versus = false; challenge = true; startMatch(); }
                    else if (menuIdx == 3) { scene = Scene::Tutorial; tutPage = 0; tutFromMenu = true; versusBrief = false; }
                    else win.close();
                }
                else if (k == K::P) win.close();
            }
            else if (menuPage == 2) {                                // stats screen
                int maxScroll = std::max(0, (int)runs.size() - 8);
                if (k == K::Up) statsScroll = std::max(0, statsScroll - 1);
                else if (k == K::Down) statsScroll = std::min(maxScroll, statsScroll + 1);
                else if (k == K::P || k == K::Escape || k == K::BackSpace || k == K::Enter || k == K::Space) menuPage = 1;
            }
            else {                                                   // campaign submenu (4 items)
                if (k == K::Left)  selectSlot(slot - 1);
                else if (k == K::Right) selectSlot(slot + 1);
                else if (k == K::Up) subIdx = (subIdx + 3) % 4;
                else if (k == K::Down) subIdx = (subIdx + 1) % 4;
                else if (k == K::Enter || k == K::Space) {
                    if (subIdx == 0) loadCampaign();
                    else if (subIdx == 1) { versus = false; challenge = false; startMatch(); }
                    else if (subIdx == 2) { menuPage = 2; statsScroll = 0; }
                    else menuPage = 0;
                }
                else if (k == K::P || k == K::Escape || k == K::BackSpace) menuPage = 0;
            }
            break;
        case Scene::Tutorial:
            if (k == K::P) scene = Scene::Menu;
            else if (versusBrief) {
                if (k == K::Enter || k == K::Space) { versusBrief = false; startFight(); }
            }
            else if (tutFromMenu) {
                if (k == K::Left) tutPage = std::max(0, tutPage - 1);
                else if (k == K::Right) tutPage = std::min((int)TUT.size() - 1, tutPage + 1);
                else if (k == K::Enter || k == K::Space) {
                    if (tutPage + 1 >= (int)TUT.size()) scene = Scene::Menu; else tutPage++;
                }
            }
            else if (k == K::Enter || k == K::Space) {
                if (loreT * cfg::LORE_CPS < (float)loreChars(level)) loreT = 1e4f;   // first press = finish the text
                else startFight();
            }
            break;
        case Scene::Intro:
        case Scene::Fight:
            if (k == K::P) { scene = Scene::Paused; pauseIdx = 0; }
            else if (k == K::F1) debug = !debug;
            else if (k == K::R) startFight();
            break;
        case Scene::Paused:
            if (k == K::P) scene = Scene::Fight;
            else if (k == K::Up) pauseIdx = (pauseIdx + 4) % 5;
            else if (k == K::Down) pauseIdx = (pauseIdx + 1) % 5;
            else if (k == K::Enter || k == K::Space) pauseConfirm();
            break;
        case Scene::Revive:
            if (k == K::Enter) {                                   // resurrect
                revive(F[reviveWho]); statRezz++;
                buf[0] = Input(); buf[1] = Input();
                scene = Scene::Fight;
            }
            else if (k == K::BackSpace || k == K::Escape) {        // accept death
                F[reviveWho].lives = 1;
                scene = Scene::Fight;
            }
            break;
        case Scene::RoundEnd:
            if (k == K::Enter || k == K::Space) proceed();
            else if (k == K::P) {
                if (!versus && !challenge && winner == 1) autoSaveCampaign();
                scene = Scene::Menu;
            }
            break;
        case Scene::Victory:
            if ((k == K::Enter || k == K::Space || k == K::P) && victoryT > 1.f) scene = Scene::Menu;
            break;
        }
    }
    void proceed() {
        if (versus) {
            if (matchOver) { scene = Scene::Menu; return; }
            startFight();
        }
        else if (challenge && winner == 0) { scene = Scene::Menu; }   // challenge = one duel only
        else if (winner == 0) {
            if (level >= cfg::LEVELS - 1) {
                victPrevBestT = -1.f; victPrevBestD = -1;
                for (const auto& pr : runs) {                         // best of the PREVIOUS runs
                    if (victPrevBestT < 0.f || pr.time < victPrevBestT) victPrevBestT = pr.time;
                    if (victPrevBestD < 0 || pr.deaths < victPrevBestD) victPrevBestD = pr.deaths;
                }
                victBestTime = (victPrevBestT < 0.f) || statTime < victPrevBestT;
                victBestDeaths = (victPrevBestD < 0) || statDeaths < victPrevBestD;
                RunRecord r{ statDeaths, statRezz, statTime };
                appendRun(r); runs.push_back(r);
                victRunNo = (int)runs.size();
                victoryT = 0.f; scene = Scene::Victory; return;
            }
            level++;
            showTutorial();
        }
        else {
            startFight();
        }
    }

    // ------------------------------------------------------------ music ---
    int fightTrack() const { return versus ? (int)Bgm::VERSUS : (int)Bgm::BOSS1 + level; }
    void syncMusic(float dt) {
        int want = -1; float vm = 1.f;
        switch (scene) {
        case Scene::Title: case Scene::Menu: case Scene::Tutorial: case Scene::Victory: want = Bgm::TITLE; break; want = Bgm::TITLE; break;
        case Scene::Intro: case Scene::Fight: want = fightTrack(); break;
        case Scene::Paused:   want = fightTrack(); vm = 0.25f; break;
        case Scene::Revive:   want = fightTrack(); vm = 0.35f; break;
        case Scene::RoundEnd: want = fightTrack(); vm = 0.40f; break;
        }
        bgm.update(want, vm, dt);
    }

    // ------------------------------------------------- deathblow cinematic ---
    bool execActive() const { return execA >= 0 && F[execA].st == St::Execute; }
    // slow motion: crawl until the cut lands, then ramp back to normal speed
    float timeScale() const {
        if (reviveT >= 0.f && reviveT < 0.9f) return lerp(0.25f, 1.f, reviveT / 0.9f);   // slow motion as the fallen rises
        if (!execActive()) return 1.f;
        float et = F[execA].t;
        if (et < cfg::EXEC_STRIKE) return 0.20f;
        return lerp(0.28f, 1.f, (et - cfg::EXEC_STRIKE) / 0.7f);
    }
    float execZoom() const {
        if (!execActive()) return 1.f;
        float et = F[execA].t;
        if (et < 0.5f) return 1.f + 0.3f * std::min(1.f, et / 0.08f);
        return 1.f + 0.3f * std::max(0.f, 1.f - (et - 0.5f) / 0.45f);
    }

    // ----------------------------------------------------------- update ---
    void update(float dt) {
        clockT += dt;
        as.tick(dt);
        if (reviveT >= 0.f) { reviveT += dt; if (reviveT > 2.7f) reviveT = -1.f; }
        saveMsgT = std::max(0.f, saveMsgT - dt);
        updateFX(dt);
        syncMusic(dt);
        if (scene == Scene::Tutorial) loreT += dt;
        if (scene == Scene::Victory) victoryT += dt;
        if (scene == Scene::RoundEnd) roundT += dt;
        // time keeps counting on the "You died" screens and the resurrect prompt
        if (!versus && !challenge &&
            (scene == Scene::Revive || (scene == Scene::RoundEnd && winner == 1)))
            statTime += dt;
        if (scene != Scene::Fight) {
            pads[0].poll(); pads[1].poll();
            if (scene == Scene::Intro) {
                introT -= dt;
                if (introT <= 0) scene = Scene::Fight;
            }
            return;
        }
        if (!versus) statTime += dt;
        dt *= timeScale();

        Input c0 = pads[0].poll();
        Input c1 = versus ? pads[1].poll() : Input();
        if (hitStop > 0) {
            hitStop -= dt;
            buf[0].merge(c0); buf[1].merge(c1);
            return;
        }
        Input i0 = buf[0]; i0.merge(c0); buf[0] = Input();
        Input i1 = buf[1]; i1.merge(c1); buf[1] = Input();
        if (!versus) i1 = ai.think(F[1], F[0], dt);
        if (F[0].st == St::Dead || F[1].st == St::Dead || execActive()) { i0 = Input(); i1 = Input(); }

        checkClash();
        stepFighter(F[0], F[1], i0, dt);
        stepFighter(F[1], F[0], i1, dt);
        updateBullets(dt);
        separate();
        updateCamera(dt);
        if (execA >= 0 && F[execA].st != St::Execute) execA = -1;

        for (auto& f : F) {                                 // health "damage trail"
            if (f.hpGhost < f.hp) f.hpGhost = f.hp;
            else if (f.sinceHit > 0.45f) f.hpGhost = std::max(f.hp, f.hpGhost - f.maxHp * 0.30f * dt);
        }

        for (int i = 0; i < 2; i++) {
            Fighter& f = F[i];
            if (f.st == St::Dead && f.lives > 1 && f.t > 1.2f) {
                if (f.isBoss) revive(f);
                else { reviveWho = i; scene = Scene::Revive; return; }
            }
        }
        if (finalDead(F[0]) || finalDead(F[1])) {
            endT += dt;
            if (endT > 1.8f) endRound();
        }
    }
    void revive(Fighter& f) {
        f.lives--;
        f.hp = f.maxHp * cfg::REVIVE_FRAC; f.hpGhost = f.hp;
        f.posture = 0; f.st = St::Idle; f.t = 0; f.vx = 0; f.vy = 0; f.invuln = 1.5f; f.sinceHit = 10;
        if (f.isBoss) { f.speedMul *= 1.1f; f.dmgMul *= 1.1f; f.gunCd = 2.5f; ai.reset(); }
        rises.push_back({ f.x, 0.f, 2.4f, f.isBoss ? sf::Color(200, 24, 40) : sf::Color(190, 205, 255) });
        reviveT = 0.f; reviveBoss = f.isBoss;
        if (f.isBoss) {
            reviveTitle = (f.lives == f.maxLives - 1) ? "SHADOWS DIE TWICE" : "THE FINAL BREATH";
            reviveSub = f.name + " refuses to fall";
        }
        else {
            reviveTitle = "RESURRECTION";
            reviveSub = versus ? f.name + " rises once more" : std::string("The wolf rises once more");
        }
        shake = 8; as.sfx("revive", 0.8f);
    }
    void endRound() {
        winner = finalDead(F[1]) ? 0 : 1;
        roundT = 0.f;
        auto pick = [](const std::vector<std::string>& v) { return v[(size_t)rnd(0.f, (float)v.size() - 0.01f)]; };
        if (versus) {
            wins[winner]++;
            if (wins[winner] >= 2) matchOver = true;
            endLine = pick(VERSUS_LINES);
        }
        else if (winner == 1) { statDeaths++; endLine = pick(DEATH_LINES); autoSaveCampaign(); }
        else endLine = VICTORY_SUB[level];
        scene = Scene::RoundEnd;
    }
    void separate() {
        Fighter& a = F[0], & b = F[1];
        if (a.grounded() && b.grounded()) {
            float d = b.x - a.x, minD = 62.f * (a.widthMul + b.widthMul) * 0.5f;
            if (std::fabs(d) < minD) {
                float push = (minD - std::fabs(d)) * 0.5f, s = d >= 0 ? 1.f : -1.f;
                a.x -= s * push; b.x += s * push;
            }
        }
        float lo = std::max(60.f, camX - cfg::W / 2.f + 70.f);
        float hi = std::min(cfg::WORLD_W - 60.f, camX + cfg::W / 2.f - 70.f);
        for (auto& f : F) f.x = std::clamp(f.x, lo, hi);
    }

    void checkClash() {
        Fighter& a = F[0], & b = F[1];
        if (a.st != St::Slash || b.st != St::Slash) return;
        const AtkData& d = atkData(St::Slash);
        auto inWin = [&](const Fighter& f) { float ts = f.t * f.spd(); return ts >= d.startup * 0.6f && ts < d.startup + d.active; };
        if (inWin(a) && inWin(b) && std::fabs(a.x - b.x) < d.range * std::max(a.rangeMul, b.rangeMul) + 20) {
            for (int i = 0; i < 2; i++) {
                Fighter& f = F[i];
                f.st = St::Clash; f.t = 0; f.vx = (f.x < F[1 - i].x ? -1.f : 1.f) * 380.f;
                f.hitDone = true;
                addPosture(f, 14);
            }
            spark({ (a.x + b.x) / 2, cfg::GROUND - 110 }, sf::Color(255, 220, 120), 26, 420);
            hitStop = 0.09f; shake = 8;            as.sfx("clash", rnd(0.95f, 1.05f));
        }
    }

    void startAttack(Fighter& f, St s) {
        f.st = s; f.t = 0; f.hitDone = false; f.swung = false; f.vx = 0; f.attackId++; f.combo = 0;
        if (s == St::Barrage) {
            f.barrageCount = (int)rnd((float)f.barrageMin, (float)f.barrageMax + 0.99f);
            f.strikeIdx = 0;
        }
    }

    // ---------------------------------------------------------- gun ------
    void fireBullet(Fighter& f) {
        Bullet b;
        b.owner = (&f == &F[0]) ? 0 : 1;
        b.p = { f.x + f.facing * 85.f * f.heightMul, f.y - 125.f * f.heightMul };
        b.vx = f.facing * cfg::BULLET_SPEED;
        b.life = 2.f;
        bullets.push_back(b);
        f.gunCd = cfg::GUN_COOLDOWN;
        spark(b.p, sf::Color(255, 220, 120), 12, 320);
        shake = std::max(shake, 3.f);
        as.sfx(as.sounds.count("gun") ? "gun" : "deathblow", 1.5f);
    }
    void updateBullets(float dt) {
        for (auto& b : bullets) {
            Fighter& a = F[b.owner];
            Fighter& d = F[1 - b.owner];
            float x0 = b.p.x;
            b.p.x += b.vx * dt;
            b.life -= dt;
            if (d.st == St::Dead || d.invuln > 0.f) continue;
            float lo = std::min(x0, b.p.x), hi = std::max(x0, b.p.x);
            float hw = 34.f * d.widthMul;
            if (hi >= d.x - hw && lo <= d.x + hw && d.y >= cfg::GROUND - 55.f) bulletHit(a, d, b);
        }
        bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) {
            return b.life <= 0.f || b.p.x < -120.f || b.p.x > cfg::WORLD_W + 120.f; }), bullets.end());
    }
    void bulletHit(Fighter& a, Fighter& d, Bullet& b) {
        b.life = 0.f;
        sf::Vector2f hp(d.x - a.facing * 25.f, d.y - 120.f);
        if (d.st == St::Broken) {
            damage(d, cfg::BULLET_DMG * a.dmgMul);
            spark(hp, sf::Color(255, 90, 70), 14, 330); as.sfx("hit");
        }
        else if (deflecting(d)) {
            spark(hp, sf::Color(255, 245, 190), 22, 520);
            hitStop = 0.06f; shake = 4; as.sfx("parry", rnd(1.1f, 1.25f));
        }
        else if (d.st == St::Block) {
            damage(d, cfg::BULLET_CHIP * a.dmgMul);
            d.vx = a.facing * 100.f;
            spark(hp, sf::Color(190, 190, 210), 10, 280);
            hitStop = 0.04f; shake = 3; as.sfx("block");
        }
        else {
            damage(d, cfg::BULLET_DMG * a.dmgMul);
            if (d.st != St::Dead) { d.st = St::Hurt; d.t = 0; d.vx = a.facing * 200.f; }
            addPosture(d, 8);
            spark(hp, sf::Color(255, 90, 70), 18, 380);
            hitStop = 0.07f; shake = 6; as.sfx("hit");
        }
    }

    void stepFighter(Fighter& f, Fighter& o, const Input& in, float dt) {
        f.animT += dt; f.t += dt; f.sinceHit += dt;
        f.invuln = std::max(0.f, f.invuln - dt);
        f.gunCd = std::max(0.f, f.gunCd - dt);
        float walkCap = (f.style == 5) ? 1.3f : 1.1f;
        float walk = cfg::WALK * (f.isBoss ? std::min(f.speedMul, walkCap) : 1.f);

        if (f.st != St::Broken && f.st != St::Dead && f.st != St::Block && f.sinceHit > 1.0f) {
            float hpK = std::clamp(f.hp / f.maxHp, 0.f, 1.f);
            float recover = 0.15f + 0.85f * hpK;                 // full health = full recovery, near death = 15%
            f.posture = std::max(0.f, f.posture - f.postureRegen * recover * dt);
        }

        auto friction = [&]() { f.vx *= std::max(0.f, 1.f - 9.f * dt); };

        switch (f.st) {
        case St::Idle: case St::Walk: case St::Block: {
            if (!f.grounded()) { f.st = St::Jump; break; }
            f.facing = (o.x >= f.x) ? 1 : -1;
            bool newSwing = (o.attackId != f.seenAtk);
            if (newSwing) f.seenAtk = o.attackId;
            bool comboFollow = (o.st == St::Slash && o.combo > 0);
            if (newSwing && comboFollow && f.st == St::Block && in.blockHeld && !f.isBoss) {  // fresh deflect window for every combo swing
                f.blockT = 0; f.parryWindow = cfg::PARRY_WINDOW;
            }
            if (in.blockHeld && f.st != St::Block) {
                f.blockT = 0;
                float guard = (o.st == St::Barrage || comboFollow) ? 0.10f : (isAttack(o.st) ? 0.28f : cfg::PARRY_SPAM_GUARD);
                f.parryWindow = (f.animT - f.lastBlockPress < guard) ? 0.f : cfg::PARRY_WINDOW;
                f.lastBlockPress = f.animT;
            }
            if (in.blockHeld) { f.st = St::Block; f.blockT += dt; f.vx = in.move * walk * 0.35f; }
            else { f.st = (std::fabs(in.move) > 0.1f) ? St::Walk : St::Idle; f.vx = in.move * walk; }

            if (in.slash) startAttack(f, St::Slash);
            else if (in.thrust) startAttack(f, St::Thrust);
            else if (in.sweep) startAttack(f, St::Sweep);
            else if (in.barrage) startAttack(f, St::Barrage);
            else if (in.spear && f.canSpear) startAttack(f, St::Spear);
            else if (in.shoot && f.canGun && f.gunCd <= 0.f) {
                f.st = St::Shoot; f.t = 0; f.hitDone = false; f.vx = 0; f.attackId++;
            }
            else if (in.dash) {
                bool back = in.move * (float)f.facing < -0.1f;
                f.st = St::Dash; f.t = 0; f.dashDir = back ? -(float)f.facing : (float)f.facing;
                as.sfx("dash");
            }
            else if (in.jump) { f.st = St::Jump; f.vy = cfg::JUMP_VEL; f.y -= 1.f; f.t = 0; f.airUsed = false; as.sfx("jump"); }
            break;
        }
        case St::Dash:
            if (f.t < 0.22f) f.vx = f.dashDir * cfg::DASH_SPEED; else friction();
            if (f.t >= cfg::DASH_TOTAL) f.st = St::Idle;
            break;
        case St::Jump:                                   // airborne: NO blocking, but a jump slash is allowed
            f.facing = (o.x >= f.x) ? 1 : -1;            // keep facing the enemy while leaping over him
            f.vx = in.move * walk * cfg::AIR_CONTROL;
            if (in.slash && !f.airUsed) {
                f.airUsed = true;
                float keep = f.vx;
                startAttack(f, St::AirSlash);
                f.vx = keep * 0.6f;
            }
            break;
        case St::AirSlash: {
            const AtkData& a = atkData(f.st);
            float ts = f.t * f.spd();
            f.vx *= std::max(0.f, 1.f - 1.5f * dt);
            if (!f.swung && ts >= a.startup) { f.swung = true; as.sfx("swing", 1.1f); }
            if (!f.hitDone && ts >= a.startup && ts < a.startup + a.active) tryHit(f, o, a);
            if (f.st == St::AirSlash && ts >= a.startup + a.active + a.recovery) f.st = f.grounded() ? St::Idle : St::Jump;
            break;
        }
        case St::JumpCounter:
            friction();
            if (f.t >= 0.45f) f.st = f.grounded() ? St::Idle : St::Jump;
            break;
        case St::Mikiri:
            friction();
            if (f.t >= cfg::MIKIRI_POSE) f.st = St::Idle;
            break;
        case St::Slash: case St::Thrust: case St::Sweep: case St::Spear: {
            const AtkData& a = atkData(f.st);
            float ts = f.t * f.spd();
            f.vx = 0;
            if (f.st == St::Slash && in.slash && f.combo < 2 &&
                ts >= a.startup + a.active * 0.5f && ts < a.startup + a.active + a.recovery * 0.85f) {
                int c = f.combo + 1;
                startAttack(f, St::Slash);
                f.combo = c;
                f.t = 0.f;
                break;
            }
            if (!f.swung && ts >= a.startup) { f.swung = true; as.sfx("swing", f.st == St::Slash ? (f.combo == 2 ? 0.85f : 1.f) : 0.8f); }
            if (ts >= a.startup && ts < a.startup + a.active) {
                if (f.st == St::Slash) f.vx = f.facing * 180.f;
                if (f.st == St::Spear) f.vx = f.facing * 140.f;
                if (!f.hitDone) tryHit(f, o, a);
            }
            if (isAttack(f.st) && ts >= a.startup + a.active + a.recovery) f.st = St::Idle;
            break;
        }
        case St::Shoot: {
            f.vx = 0;
            float gt = f.t * f.spd();
            if (!f.hitDone && gt >= cfg::GUN_WINDUP) { f.hitDone = true; fireBullet(f); }
            if (gt >= cfg::GUN_TOTAL) f.st = St::Idle;
            break;
        }
        case St::Execute: {                              // DEATHBLOW: wind-up, iai dash-through, recover
            f.vx = 0;
            if (!f.hitDone && f.t >= cfg::EXEC_STRIKE) { f.hitDone = true; strikeDeathblow(f, o); }
            if (f.t >= cfg::EXEC_TOTAL) f.st = St::Idle;
            break;
        }
        case St::Barrage: {
            float iv = cfg::BARRAGE_INTERVAL / f.speedMul;
            float dxo = std::fabs(o.x - f.x);
            if (o.grounded()) f.facing = (o.x >= f.x) ? 1 : -1;       // keep tracking the target between strikes
            f.vx = (f.t >= cfg::BARRAGE_FIRST * 0.5f && dxo > 90.f) ? f.facing * 340.f : 0.f;   // faster than walking away
            float ph = f.t - cfg::BARRAGE_FIRST;
            if (ph >= 0) {
                int k = (int)(ph / iv);
                float within = ph - k * iv;
                if (k < f.barrageCount && f.strikeIdx <= k && within >= iv * 0.5f) {
                    f.strikeIdx = k + 1;
                    barrageStrike(f, o);
                }
            }
            if (f.st == St::Barrage && f.t > cfg::BARRAGE_FIRST + f.barrageCount * iv + 0.7f) f.st = St::Idle;
            break;
        }
        case St::Hurt:
            friction();
            if (f.t >= 0.32f || (f.t >= 0.12f && in.blockHeld)) f.st = St::Idle;   // raise guard early to catch the next combo hit
            break;
        case St::Deflected:
            friction();
            if (f.t >= 0.15f) {                          // short stagger, then guard OR strike back
                if (in.blockHeld) {                      // raise the guard with a fresh deflect window
                    f.st = St::Block; f.blockT = 0; f.parryWindow = cfg::PARRY_WINDOW; f.lastBlockPress = f.animT;
                    break;
                }
                if (in.slash) { startAttack(f, St::Slash);  break; }
                if (in.thrust) { startAttack(f, St::Thrust); break; }
                if (in.sweep) { startAttack(f, St::Sweep);  break; }
                if (in.spear && f.canSpear) { startAttack(f, St::Spear); break; }
            }
            if (f.t >= 0.45f) f.st = St::Idle;
            break;
        case St::Clash:
            friction(); if (f.t >= 0.35f) f.st = St::Idle; break;
        case St::Broken:
            friction();
            if (f.t >= 2.6f) { f.st = St::Idle; f.posture = f.maxPosture * 0.35f; }
            break;
        case St::Dead:
            friction(); break;
        }

        f.x += f.vx * dt;
        if (f.y < cfg::GROUND || f.vy < 0) {
            f.vy += cfg::GRAVITY * dt;
            f.y += f.vy * dt;
            if (f.y >= cfg::GROUND) {
                f.y = cfg::GROUND; f.vy = 0;
                if (f.st == St::Jump || f.st == St::AirSlash) f.st = St::Idle;
            }
        }
    }

    // ---------------------------------------------------------- hit logic ---
    void tryHit(Fighter& a, Fighter& d, const AtkData& ad0) {
        AtkData ad = ad0;
        if (a.st == St::Slash) {
            static const float dm[3] = { 1.f, 1.15f, 1.8f }, pm[3] = { 1.f, 1.15f, 1.6f };
            int c = std::clamp(a.combo, 0, 2);
            ad.dmg *= dm[c]; ad.posture *= pm[c];
            if (c == 2) ad.range += 15.f;
        }
        ad.range *= (a.st == St::Spear) ? 1.28f * a.heightMul : a.rangeMul;       // reach follows the DRAWN weapon
        if (d.st == St::Dead || d.invuln > 0) return;
        float dx = (d.x - a.x) * a.facing;
        if (dx < -30 || dx > ad.range + 18.f * d.widthMul) return;                // the blade tip must touch the front of the body
        bool dAir = d.y < cfg::GROUND - 25.f;
        if (a.st != St::Sweep && a.st != St::AirSlash && d.y < cfg::GROUND - 140.f) return;
        resolve(a, d, ad, dAir);
    }

    void addPosture(Fighter& f, float amt) {
        if (f.st == St::Dead) return;
        f.posture += amt; f.sinceHit = 0;
        if (f.posture >= f.maxPosture && f.st != St::Broken) {
            f.posture = f.maxPosture; f.st = St::Broken; f.t = 0; f.vx = 0; f.hitDone = true;
            addText("POSTURE BROKEN", { f.x, f.y - 290 }, sf::Color(255, 190, 60), 30);
            as.sfx("break"); shake = std::max(shake, 8.f);
        }
    }
    void damage(Fighter& f, float amt) {
        if (f.st == St::Dead) return;
        f.hp -= amt; f.sinceHit = 0;
        if (f.hp <= 0) {
            f.hp = 0; f.st = St::Dead; f.t = 0; f.vy = 0;
            hitStop = std::max(hitStop, 0.25f); shake = std::max(shake, 14.f);
        }
    }
    // A deathblow starts the cinematic: the attacker winds up in slow motion, then cuts through.
    void deathblow(Fighter& a, Fighter& d, sf::Vector2f /*hp*/) {
        a.st = St::Execute; a.t = 0; a.hitDone = false; a.vx = 0; a.vy = 0; a.y = cfg::GROUND;
        a.facing = (d.x >= a.x) ? 1 : -1;
        d.t = 0; d.vx = 0;                                // keep the victim staggered until the cut
        execA = (&a == &F[0]) ? 0 : 1;
        as.sfx("swing", 0.6f);
    }
    // the instant the blade lands (called from the Execute state)
    void strikeDeathblow(Fighter& a, Fighter& d) {
        float side = (float)a.facing;
        sf::Vector2f p0(a.x, a.y - 115.f);
        a.x = std::clamp(d.x + side * 105.f, 70.f, cfg::WORLD_W - 70.f);
        sf::Vector2f p1(a.x, a.y - 115.f);
        slices.push_back({ p0, p1, 0.45f, 0.45f, sf::Color(255, 255, 255), 8.f });
        slices.push_back({ p0, p1, 0.45f, 0.45f, sf::Color(255, 70, 60, 150), 22.f });
        sf::Vector2f c1(d.x - side * 85.f, d.y - 225.f), c2(d.x + side * 85.f, d.y - 20.f);
        slices.push_back({ c1, c2, 0.6f, 0.6f, sf::Color(255, 255, 255), 10.f });
        slices.push_back({ c1, c2, 0.6f, 0.6f, sf::Color(255, 50, 50, 160), 26.f });
        damage(d, d.hp);                                  // drains the WHOLE health bar of this life
        d.facing = (int)side;                             // he falls away from the attacker
        hitStop = 0.05f; shake = 18.f;
        blood({ d.x, d.y - 120.f }, 90, side);
        spark({ d.x, d.y - 120.f }, sf::Color(255, 60, 40), 40, 520);
        as.sfx("deathblow");
    }

    void resolve(Fighter& a, Fighter& d, const AtkData& ad, bool dAir) {
        a.hitDone = true;
        sf::Vector2f hp(d.x - a.facing * 25.f, d.y - 120.f);
        const bool blockable = (a.st == St::Slash || a.st == St::Spear || a.st == St::AirSlash);

        if (d.st == St::Barrage) {
            spark(hp, sf::Color(150, 150, 170), 8, 250);
            as.sfx("block");
        }
        else if (d.st == St::Broken) {
            deathblow(a, d, hp);
        }
        else if (a.st == St::Thrust && d.st == St::Dash && d.dashDir == (float)d.facing && d.t < cfg::MIKIRI_WINDOW) {
            a.st = St::Deflected; a.t = 0; a.vx = 0; a.deflectKind = 1;
            d.st = St::Mikiri; d.t = 0; d.vx = 0;
            addPosture(a, 55);
            damage(a, 6.f * d.dmgMul);
            sf::Vector2f sp(d.x + d.facing * 42.f, cfg::GROUND - 4.f);                 // where the foot lands on the blade
            shock(sp, sf::Color(130, 255, 210), 190.f, 0.16f, 0.55f);
            shock(sp, sf::Color(255, 255, 255), 110.f, 0.14f, 0.35f);
            dust(sp, 18);
            sf::Vector2f up = sp + sf::Vector2f(d.facing * 70.f, -190.f);
            slices.push_back({ sp, up, 0.30f, 0.30f, sf::Color(255, 255, 255), 5.f });
            slices.push_back({ sp, up, 0.30f, 0.30f, sf::Color(100, 255, 200, 150), 16.f });
            clashBurst(sp + sf::Vector2f(0.f, -26.f), (float)a.facing, 26, 0.9f);
            addText("MIKIRI", { d.x, d.y - 290 }, sf::Color(120, 255, 210), 34);
            hitStop = 0.17f; shake = 9; as.sfx("mikiri");
        }
        else if (a.st == St::Sweep && dAir) {
            d.st = St::JumpCounter; d.t = 0; d.vy = -620.f; d.vx = 0; d.airUsed = false;
            a.st = St::Deflected; a.t = 0; a.vx = 0; a.deflectKind = 2;
            addPosture(a, 55);
            damage(a, 6.f * d.dmgMul);
            sf::Vector2f head(a.x + a.facing * 8.f, a.y - 165.f * a.heightMul);
            sf::Vector2f g(a.x, cfg::GROUND - 4.f);
            shock(head, sf::Color(255, 240, 140), 150.f, 0.55f, 0.45f);
            shock(head, sf::Color(255, 255, 255), 80.f, 1.f, 0.25f);
            shock(g, sf::Color(255, 230, 150), 170.f, 0.16f, 0.5f);
            dust(g, 14);
            slices.push_back({ { d.x, d.y }, head, 0.22f, 0.22f, sf::Color(255, 255, 255), 5.f });
            slices.push_back({ { d.x, d.y }, head, 0.22f, 0.22f, sf::Color(255, 220, 120, 150), 15.f });
            clashBurst(head, (float)a.facing, 22, 0.8f);
            addText("JUMP COUNTER", { d.x, d.y - 330 }, sf::Color(255, 240, 140), 34);
            hitStop = 0.15f; shake = 10; as.sfx("jumpcounter");
        }
        else if (blockable && canDeflect(a, d)) {
            a.st = St::Deflected; a.t = 0; a.vx = -a.facing * 150.f;      // stay in range for the next exchanges
            addPosture(a, 24);
            addPosture(d, 3);
            spark(hp, sf::Color(255, 245, 190), 24, 520);
            addText("DEFLECT!", { d.x, d.y - 290 }, sf::Color(255, 245, 190), 28);
            hitStop = 0.08f; shake = 6; as.sfx("parry", rnd(0.95f, 1.07f));
        }
        else if (blockable && d.st == St::Block) {
            d.vx = a.facing * 200.f;
            addPosture(d, ad.posture);
            spark(hp, sf::Color(190, 190, 210), 12, 300);
            hitStop = 0.05f; shake = 3; as.sfx("block");
        }
        else {
            damage(d, ad.dmg * a.dmgMul);
            if (d.st != St::Dead) { d.st = St::Hurt; d.t = 0; d.vx = a.facing * 240.f; }
            addPosture(d, ad.posture * 0.6f);
            spark(hp, sf::Color(255, 90, 70), 18, 380);
            hitStop = ad.perilous ? 0.12f : 0.06f; shake = ad.perilous ? 10.f : 5.f; as.sfx("hit");
        }
    }

    void barrageStrike(Fighter& a, Fighter& d) {
        if (d.st == St::Dead || d.invuln > 0) return;
        as.sfx("swing", 1.1f);
        const AtkData& ad = atkData(St::Barrage);
        float dx = (d.x - a.x) * a.facing;
        float reach = std::max(165.f, ad.range * a.rangeMul) + 18.f * d.widthMul;     // barrages always cover close-to-mid range
        if (dx < -30 || dx > reach || d.y < cfg::GROUND - 140.f) return;
        sf::Vector2f hp(d.x - a.facing * 25.f, d.y - 120.f);
        if (d.st == St::Broken) {
            deathblow(a, d, hp);
        }
        else if (deflecting(d)) {
            addPosture(a, cfg::BARRAGE_POSTURE_DEFLECT);
            addPosture(d, 2);
            spark(hp, sf::Color(255, 245, 190), 24, 520);
            hitStop = 0.07f; shake = 5; as.sfx("parry", rnd(0.95f, 1.07f));
        }
        else if (d.st == St::Block) {
            d.vx = a.facing * 120.f;
            addPosture(d, cfg::BARRAGE_POSTURE_BLOCK);
            spark(hp, sf::Color(190, 190, 210), 12, 300);
            hitStop = 0.04f; shake = 3; as.sfx("block");
        }
        else {
            damage(d, ad.dmg * a.dmgMul);
            if (d.st != St::Dead) { d.st = St::Hurt; d.t = 0; d.vx = a.facing * 160.f; }
            addPosture(d, 10);
            spark(hp, sf::Color(255, 90, 70), 14, 330);
            hitStop = 0.05f; shake = 4; as.sfx("hit");
        }
    }

    // ---------------------------------------------------------------- FX ---

    void shock(sf::Vector2f p, sf::Color c, float maxR, float squash, float dur) { shocks.push_back({ p, 0.f, dur, maxR, squash, c }); }
    void dust(sf::Vector2f p, int n) {
        for (int i = 0; i < n; i++) {
            float ang = rnd(-3.0f, -0.15f), sp = rnd(60.f, 300.f), life = rnd(0.3f, 0.7f);
            parts.push_back({ p + sf::Vector2f(rnd(-20.f, 20.f), 0.f), { std::cos(ang) * sp, std::sin(ang) * sp * 0.5f }, life, life, rnd(3.f, 7.f), sf::Color(190, 170, 150) });
        }
    }
    // steel meeting steel: fast streak sparks, a cross flare and a flash ring
    void clashBurst(sf::Vector2f p, float dir, int n, float power) {
        for (int i = 0; i < n; i++) {
            float ang = rnd(-3.0f, -0.15f), sp = rnd(300.f, 1150.f) * power;
            if (i % 4 == 0) ang = (dir > 0 ? rnd(-0.5f, 0.35f) : rnd(2.8f, 3.65f));    // a few shoot sideways along the blades
            float life = rnd(0.25f, 0.65f);
            sf::Color col = mixc(sf::Color(255, 255, 255), sf::Color(255, 150, 40), rnd01());
            embers.push_back({ p, { std::cos(ang) * sp, std::sin(ang) * sp }, life, life, rnd(1.6f, 3.4f), col });
        }
        spark(p, sf::Color(255, 236, 170), 14, 420);
        shock(p, sf::Color(255, 244, 200), 70.f * power + 20.f, 1.f, 0.22f);
        sf::Vector2f d1(0.7f, -0.7f), d2(0.7f, 0.7f);
        float L = 46.f * power + 14.f;
        slices.push_back({ p - d1 * L, p + d1 * L, 0.14f, 0.14f, sf::Color(255, 255, 255), 4.f });
        slices.push_back({ p - d2 * L, p + d2 * L, 0.14f, 0.14f, sf::Color(255, 255, 255), 4.f });
        flashT = 0.09f;
    }


    void spark(sf::Vector2f p, sf::Color c, int n, float speed) {
        for (int i = 0; i < n; i++) {
            float ang = rnd(0.f, 6.2832f), sp = rnd(0.25f, 1.f) * speed;
            float life = rnd(0.2f, 0.45f);
            parts.push_back({ p, {std::cos(ang) * sp, std::sin(ang) * sp}, life, life, rnd(2.f, 5.f), c });
        }
        flashT = 0.05f;
    }
    void blood(sf::Vector2f p, int n, float side) {      // fountain of blood
        for (int i = 0; i < n; i++) {
            float ang = rnd(-2.6f, -0.5f), sp = rnd(150.f, 750.f);
            float life = rnd(0.5f, 1.2f);
            sf::Color c((sf::Uint8)rnd(140.f, 235.f), (sf::Uint8)rnd(0.f, 25.f), (sf::Uint8)rnd(10.f, 35.f));
            parts.push_back({ p, {std::cos(ang) * sp + side * rnd(0.f, 260.f), std::sin(ang) * sp}, life, life, rnd(3.f, 7.f), c });
        }
    }
    void addText(const std::string& s, sf::Vector2f p, sf::Color c, unsigned size) {
        c.a = (sf::Uint8)(c.a * 0.75f);                              // subtle: the feedback is carried by sparks + sound
        texts.push_back({ s, p, 0.7f, 0.7f, c, std::max(14u, (unsigned)(size * 0.6f)) });
    }
    void updateFX(float dt) {
        for (auto& r : rises) {
            r.t += dt;
            if (r.t < 1.0f && rnd01() < 0.9f)
                parts.push_back({ { r.x + rnd(-45.f, 45.f), cfg::GROUND - rnd(0.f, 30.f) }, { rnd(-35.f, 35.f), -rnd(300.f, 650.f) }, 0.9f, 0.9f, rnd(2.f, 4.f), r.c });
        }
        rises.erase(std::remove_if(rises.begin(), rises.end(), [](const RiseFx& r) { return r.t >= r.dur; }), rises.end());
        for (auto& p : parts) { p.life -= dt; p.p += p.v * dt; p.v.y += 900.f * dt; p.v *= 0.98f; }
        parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle& p) { return p.life <= 0; }), parts.end());
        for (auto& t : texts) { t.life -= dt; t.p.y -= 30.f * dt; }
        texts.erase(std::remove_if(texts.begin(), texts.end(), [](const FloatText& t) { return t.life <= 0; }), texts.end());
        for (auto& e : embers) { e.life -= dt; e.p += e.v * dt; e.v.y += 1100.f * dt; e.v *= std::max(0.f, 1.f - 1.8f * dt); }
        embers.erase(std::remove_if(embers.begin(), embers.end(), [](const Ember& e) { return e.life <= 0.f; }), embers.end());
        for (auto& s : shocks) s.t += dt;
        shocks.erase(std::remove_if(shocks.begin(), shocks.end(), [](const Shock& s) { return s.t >= s.dur; }), shocks.end());
        for (auto& s : slices) s.life -= dt;
        slices.erase(std::remove_if(slices.begin(), slices.end(), [](const Slice& s) { return s.life <= 0; }), slices.end());
        shake = std::max(0.f, shake - 40.f * dt);
        flashT = std::max(0.f, flashT - dt);
    }

    // ------------------------------------------------------------ render ---
    void drawTitle(sf::RenderTarget& rt) {
        using V = sf::Vector2f;
        const float W = (float)cfg::W, H = (float)cfg::H, cx = W / 2.f;
        const float fade = std::clamp(clockT / 1.8f, 0.f, 1.f);
        auto A = [](sf::Color c, float m) { c.a = (sf::Uint8)(c.a * std::clamp(m, 0.f, 1.f)); return c; };

        std::shared_ptr<sf::Texture> bg = as.bgTitle ? as.bgTitle : as.bgDefault;
        if (bg) {
            sf::Sprite s(*bg); s.setScale(W / bg->getSize().x, H / bg->getSize().y); rt.draw(s);
        }
        else {                                                   // fallback: panning arena, ronin vs the Sword Demon
            camX = cfg::WORLD_W / 2.f + 160.f * std::sin(clockT * 0.12f);
            float cl = camX - W / 2.f;
            rt.setView(sf::View(sf::FloatRect(cl, 0, W, H)));
            drawArena(rt, 5);
            const BossDef& b = BOSSES[5];
            setupFighter(menuHero, "WOLF", &as.players[0], sf::Color(80, 140, 230), cl + 230.f, 1);
            menuHero.animT = clockT;
            setupFighter(menuFoe, b.name, &as.bosses[5], bossColor(5), cl + 1050.f, -1);
            menuFoe.isBoss = true; menuFoe.heightMul = b.height; menuFoe.widthMul = b.width; menuFoe.style = 6;
            menuFoe.rangeMul = b.range; menuFoe.animT = clockT + 1.7f;
            drawFighter(rt, menuHero); drawFighter(rt, menuFoe);
            rt.setView(rt.getDefaultView());
        }

        // ---- blackish shade: overall dim + heavy edge vignette ----
        dim(rt, 125);
        sf::Color k0(0, 0, 0, 0), kb(0, 0, 0, 235);
        quadV(rt, V(0, 0), V(W, 0), V(W, 340), V(0, 340), kb, kb, k0, k0);
        quadV(rt, V(0, H - 300), V(W, H - 300), V(W, H), V(0, H), k0, k0, kb, kb);
        quadV(rt, V(0, 0), V(280, 0), V(280, H), V(0, H), kb, k0, k0, kb);
        quadV(rt, V(W - 280, 0), V(W, 0), V(W, H), V(W - 280, H), k0, kb, kb, k0);

        // ---- rising embers ----
        for (int i = 0; i < 60; i++) {
            float sp = 26.f + 60.f * hash01(i, 3.f);
            float y = H + 40.f - std::fmod(hash01(i, 4.f) * (H + 80.f) + clockT * sp, H + 80.f);
            float x = hash01(i, 5.f) * W + std::sin(clockT * 0.8f + i) * 20.f;
            float life = std::clamp((H - y) / H, 0.f, 1.f), r = 1.2f + 2.2f * hash01(i, 6.f);
            sf::CircleShape e(r); e.setOrigin(r, r); e.setPosition(x, y);
            e.setFillColor(sf::Color(255, (sf::Uint8)(90 + 90 * hash01(i, 7.f)), 40, (sf::Uint8)(210 * (1.f - life) * fade)));
            rt.draw(e);
        }

        // ---- the title ----
        float flick = 0.94f + 0.06f * std::sin(clockT * 17.f) * std::sin(clockT * 5.3f);
        for (int i = 0; i < 6; i++) {
            float a = clockT * 1.1f + i * 1.047f;
            txt(rt, "SEKIRO", cx + std::cos(a) * 4.f, 36 + std::sin(a) * 4.f, 176, A(sf::Color(210, 30, 40, 60), fade), 1, true);
        }
        txt(rt, "SEKIRO", cx, 36, 176, A(sf::Color(246, 214, 160), fade * flick), 1, true);
        for (int i = 0; i < 5; i++) {
            float a = clockT * 1.3f + i * 1.256f;
            txt(rt, "SHADOWS DIE TWICE", cx + std::cos(a) * 3.f, 236 + std::sin(a) * 3.f, 78, A(sf::Color(255, 40, 40, 55), fade), 1, true);
        }
        txt(rt, "SHADOWS DIE TWICE", cx, 236, 78, A(sf::Color(214, 36, 44), fade * flick), 1, true);

        // blade-line divider with a travelling glint
        const sf::Color gold(222, 188, 108);
        quadV(rt, V(cx - 340, 352), V(cx, 352), V(cx, 354.5f), V(cx - 340, 354.5f), A(k0, 1.f), A(gold, fade), A(gold, fade), A(k0, 1.f));
        quadV(rt, V(cx, 352), V(cx + 340, 352), V(cx + 340, 354.5f), V(cx, 354.5f), A(gold, fade), A(k0, 1.f), A(k0, 1.f), A(gold, fade));
        float gx = cx - 340.f + 680.f * std::fmod(clockT * 0.3f, 1.f);
        quadV(rt, V(gx - 40, 351), V(gx, 351), V(gx, 356), V(gx - 40, 356), k0, A(sf::Color::White, fade), A(sf::Color::White, fade), k0);
        quadV(rt, V(gx, 351), V(gx + 40, 351), V(gx + 40, 356), V(gx, 356), A(sf::Color::White, fade), k0, k0, A(sf::Color::White, fade));
        diamond(rt, cx, 353, 8, 8, A(gold, fade));

        float ta = std::clamp((clockT - 1.2f) / 1.2f, 0.f, 1.f);
        txt(rt, "Hesitation is Defeat!", cx, 372, 34, A(sf::Color(224, 186, 160), ta), 1, true);

        // ---- the tempting prompt ----
        float pa = std::clamp((clockT - 2.2f) / 0.8f, 0.f, 1.f);
        if (pa > 0.f) {
            float pulse = 0.5f + 0.5f * std::sin(clockT * 3.2f);
            float py = 560.f;
            for (int i = 0; i < 4; i++) {
                float a = clockT * 2.f + i * 1.57f;
                txt(rt, "PRESS  ENTER", cx + std::cos(a) * (2.f + 3.f * pulse), py + std::sin(a) * (2.f + 3.f * pulse), 38,
                    A(sf::Color(255, 50, 50, (sf::Uint8)(40 + 60 * pulse)), pa), 1, true);
            }
            txt(rt, "PRESS  ENTER", cx, py, 38, A(sf::Color(255, 255, 255, (sf::Uint8)(150 + 105 * pulse)), pa), 1, true);
            float w = 8.f * (1.f - pulse);                                // blades slide in and out
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                V a(cx + sgn * (300.f - w), py + 22.f), b(cx + sgn * (185.f - w), py + 22.f);
                limb(rt, a, b, 3.f, A(sf::Color(240, 244, 255, (sf::Uint8)(120 + 100 * pulse)), pa));
                diamond(rt, a.x + sgn * 8.f, a.y, 6, 6, A(gold, pa));
            }
            txt(rt, "to draw your blade", cx, py + 52.f, 18, A(sf::Color(190, 170, 160, (sf::Uint8)(110 + 80 * pulse)), pa), 1);
        }

        // fade in from black
        sf::RectangleShape fd({ W, H }); fd.setFillColor(sf::Color(0, 0, 0, (sf::Uint8)(255 * (1.f - fade)))); rt.draw(fd);
    }

    // text with wide letter-spacing (track = extra pixels between letters)
    void trackedText(sf::RenderTarget& rt, const std::string& s, float cx, float y, unsigned size, sf::Color col, float track, bool title = false) {
        const bool useTitle = title && g_titleFontOk;
        if (!g_fontOk && !useTitle) return;
        const sf::Font& ft = useTitle ? g_titleFont : g_font;
        float total = -track;
        for (unsigned char c : s) total += ft.getGlyph(c, size, false).advance + track;
        float x = cx - total / 2.f;
        for (unsigned char c : s) {
            if (c != ' ') txt(rt, std::string(1, (char)c), x, y, size, col, 0, title);
            x += ft.getGlyph(c, size, false).advance + track;
        }
    }
    void drawReviveOverlay(sf::RenderTarget& rt) {
        if (reviveT < 0.f) return;
        using V = sf::Vector2f;
        const float W = (float)cfg::W, H = (float)cfg::H, cx = W / 2.f, t = reviveT;
        float out = 1.f - std::clamp((t - 1.9f) / 0.7f, 0.f, 1.f);
        float k = std::clamp(t / 0.5f, 0.f, 1.f) * out;
        sf::Color tone = reviveBoss ? sf::Color(205, 30, 44) : sf::Color(200, 212, 255);
        auto al = [&](sf::Color c, float m) { c.a = (sf::Uint8)(c.a * std::clamp(m, 0.f, 1.f)); return c; };
        sf::Color k1(0, 0, 0, (sf::Uint8)(215 * k)), k0(0, 0, 0, 0);
        quadV(rt, V(0, 0), V(W, 0), V(W, 190), V(0, 190), k1, k1, k0, k0);
        quadV(rt, V(0, H - 190), V(W, H - 190), V(W, H), V(0, H), k0, k0, k1, k1);
        float ease = 1.f - std::pow(1.f - std::clamp(t / 1.7f, 0.f, 1.f), 3.f);
        trackedText(rt, reviveTitle, cx, 205.f, 46, al(tone, k), lerp(48.f, 12.f, ease), true);
        float half = 40.f + 330.f * ease;
        sf::Color e(tone.r, tone.g, tone.b, 0), m = al(tone, k * 0.9f);
        quadV(rt, V(cx - half, 276), V(cx, 276), V(cx, 277.6f), V(cx - half, 277.6f), e, m, m, e);
        quadV(rt, V(cx, 276), V(cx + half, 276), V(cx + half, 277.6f), V(cx, 277.6f), m, e, e, m);
        float sa = std::clamp((t - 0.6f) / 0.6f, 0.f, 1.f) * out;
        trackedText(rt, reviveSub, cx, 292.f, 19, sf::Color(228, 218, 208, (sf::Uint8)(220 * sa)), 3.f);
    }

    void render(sf::RenderTarget& win) {
        win.clear(sf::Color(10, 10, 14));
        if (scene == Scene::Title) { drawTitle(win);  return; }
        if (scene == Scene::Menu) { drawMenu(win);  return; }
        if (scene == Scene::Tutorial) { drawTutorial(win); return; }
        if (scene == Scene::Victory) { drawVictory(win);  return; }

        const bool ex = execActive();
        const float et = ex ? F[execA].t : 0.f;
        sf::View v(sf::FloatRect(camX - cfg::W / 2.f, 0, (float)cfg::W, (float)cfg::H));
        float zoom = execZoom();
        if (zoom > 1.001f) {                                          // deathblow camera push-in
            float k = (zoom - 1.f) / 0.3f, mid = (F[0].x + F[1].x) * 0.5f;
            v.setSize(cfg::W / zoom, cfg::H / zoom);
            v.setCenter(lerp(camX, mid, k), lerp(cfg::H / 2.f, cfg::GROUND - 150.f, k));
        }
        if (shake > 0.1f) v.move(rnd(-shake, shake), rnd(-shake, shake));
        win.setView(v);
        drawBackground(win);
        for (auto& f : F) drawFighter(win, f);
        for (auto& s : slices) {                                      // sword-cut streaks
            float k = std::clamp(s.life / s.max, 0.f, 1.f);
            sf::Color c = s.c; c.a = (sf::Uint8)(c.a * k);
            limb(win, s.a, s.b, s.th * (0.3f + 0.7f * k), c);
        }
        for (auto& r : rises) {
            float k = std::clamp(r.t / r.dur, 0.f, 1.f), al = std::min(1.f, r.t / 0.2f) * (1.f - k);
            float w = 70.f * (1.f - 0.6f * k), G = cfg::GROUND;
            sf::Color c0 = r.c, c1 = r.c; c0.a = (sf::Uint8)(150 * al); c1.a = 0;
            quadV(win, sf::Vector2f(r.x - w, G - 560), sf::Vector2f(r.x + w, G - 560), sf::Vector2f(r.x + w * 0.6f, G), sf::Vector2f(r.x - w * 0.6f, G), c1, c1, c0, c0);
            for (int i = 0; i < 2; i++) {
                float kk = std::clamp(k * 1.4f - i * 0.25f, 0.f, 1.f);
                if (kk <= 0.f) continue;
                float rad = 40.f + 300.f * kk;
                sf::CircleShape ring(rad, 48); ring.setOrigin(rad, rad); ring.setScale(1.f, 0.12f);
                ring.setPosition(r.x, G + 30.f); ring.setFillColor(sf::Color::Transparent);
                sf::Color rc = r.c; rc.a = (sf::Uint8)(200 * (1.f - kk));
                ring.setOutlineColor(rc); ring.setOutlineThickness(3.f); win.draw(ring);
            }
        }
        for (auto& b : bullets) {
            float sgn = b.vx >= 0 ? 1.f : -1.f;
            limb(win, b.p, b.p - sf::Vector2f(sgn * 70.f, 0), 5.f, sf::Color(255, 190, 80, 130));
            limb(win, b.p, b.p - sf::Vector2f(sgn * 30.f, 0), 3.f, sf::Color(255, 245, 200, 220));
            sf::CircleShape c(7.f); c.setOrigin(7.f, 7.f); c.setPosition(b.p);
            c.setFillColor(sf::Color(255, 245, 190)); c.setOutlineColor(sf::Color(255, 140, 40)); c.setOutlineThickness(2.f);
            win.draw(c);
        }
        for (auto& f : F) drawWarning(win, f);
        for (auto& p : parts) {
            float a = std::clamp(p.life / p.max, 0.f, 1.f);
            sf::RectangleShape r({ p.size, p.size });
            r.setPosition(p.p);
            sf::Color c = p.c; c.a = (sf::Uint8)(255 * a);
            r.setFillColor(c);
            win.draw(r);
        }
        for (auto& s : shocks) {
            float k = std::clamp(s.t / s.dur, 0.f, 1.f), r = std::max(1.f, s.maxR * (1.f - std::pow(1.f - k, 3.f)));
            sf::CircleShape c(r, 40); c.setOrigin(r, r); c.setScale(1.f, s.squash); c.setPosition(s.p);
            c.setFillColor(sf::Color::Transparent);
            sf::Color oc = s.c; oc.a = (sf::Uint8)(oc.a * (1.f - k));
            c.setOutlineColor(oc); c.setOutlineThickness(1.f + 4.f * (1.f - k)); win.draw(c);
        }
        for (auto& e : embers) {
            float k = std::clamp(e.life / e.max, 0.f, 1.f);
            sf::Color c = e.c; c.a = (sf::Uint8)(255 * std::min(1.f, k * 2.f));
            limb(win, e.p, e.p - e.v * 0.028f, e.th * (0.5f + 0.5f * k), c);
        }
        for (auto& t : texts) {
            sf::Color c = t.c; c.a = (sf::Uint8)(255 * std::clamp(t.life / t.max * 2.f, 0.f, 1.f));
            txt(win, t.s, t.p.x, t.p.y, t.size, c, 1);
        }
        win.setView(win.getDefaultView());
        if (ex) drawExecOverlay(win, et); else drawHUD(win);
        drawReviveOverlay(win);
        if (flashT > 0) {
            sf::RectangleShape fl({ (float)cfg::W, (float)cfg::H });
            fl.setFillColor(sf::Color(255, 255, 255, 25));
            win.draw(fl);
        }
        drawOverlay(win);
    }

    // DEATHBLOW screen effects: vignette, letter-box, white flash, screen slash, big title
    void drawExecOverlay(sf::RenderTarget& rt, float et) {
        const float W = (float)cfg::W, H = (float)cfg::H;
        float inA = std::min(1.f, et / 0.10f);
        float outA = 1.f - std::clamp((et - 0.55f) / 0.4f, 0.f, 1.f);
        float k = inA * outA;
        float s = et - cfg::EXEC_STRIKE;
        dim(rt, (sf::Uint8)(120 * k));
        if (s >= 0.f) {
            sf::RectangleShape r({ W, H }); r.setFillColor(sf::Color(160, 0, 10, (sf::Uint8)(70 * k))); rt.draw(r);
        }
        float bh = 72.f * k;
        sf::RectangleShape bar({ W, bh }); bar.setFillColor(sf::Color::Black);
        bar.setPosition(0, 0); rt.draw(bar);
        bar.setPosition(0, H - bh); rt.draw(bar);
        if (s < 0.f) {                                                // focus lines while he gathers himself
            for (int i = 0; i < 14; i++) {
                float a = i * 0.4488f + 0.2f;
                sf::Vector2f c(W / 2.f, H / 2.f), d(std::cos(a), std::sin(a));
                limb(rt, c + d * 380.f, c + d * 700.f, 2.f, sf::Color(255, 255, 255, (sf::Uint8)(70 * inA)));
            }
            return;
        }
        if (s < 0.22f) {
            sf::RectangleShape fl({ W, H }); fl.setFillColor(sf::Color(255, 255, 255, (sf::Uint8)(255 * (1.f - s / 0.22f)))); rt.draw(fl);
        }
        if (s < 0.5f) {                                               // the diagonal screen slash
            float reveal = std::clamp(s / 0.08f, 0.f, 1.f), fade = 1.f - std::clamp((s - 0.15f) / 0.35f, 0.f, 1.f);
            sf::Vector2f p0(W * 0.10f, H * 0.14f), p1 = p0 + sf::Vector2f(W * 0.80f, H * 0.72f) * reveal;
            limb(rt, p0, p1, 26.f * fade + 2.f, sf::Color(255, 40, 40, (sf::Uint8)(170 * fade)));
            limb(rt, p0, p1, 9.f * fade + 1.f, sf::Color(255, 255, 255, (sf::Uint8)(255 * fade)));
        }
        float a = std::clamp(1.f - (et - 0.6f) / 0.35f, 0.f, 1.f);
        float punch = 1.f + 0.5f * std::max(0.f, 1.f - s / 0.15f);
        txt(rt, "DEATHBLOW", W / 2.f, H * 0.30f, (unsigned)(96 * punch), sf::Color(255, 40, 40, (sf::Uint8)(255 * a)), 1, true);
    }

    void drawBackground(sf::RenderTarget& rt) {
        float cl = camX - cfg::W / 2.f;
        std::shared_ptr<sf::Texture> bg = versus ? as.bgVersus : as.bgBoss[level];
        if (bg) {
            const float p = 0.5f;
            sf::Sprite s(*bg);
            float bw = cfg::W + (cfg::WORLD_W - cfg::W) * p;
            s.setScale(bw / bg->getSize().x, (float)cfg::H / bg->getSize().y);
            s.setPosition(cl * (1.f - p), 0);
            rt.draw(s);
            return;
        }
        drawArena(rt, versus ? 6 : level);
    }

    //  UNIQUE PROCEDURAL ARENAS (th = 0..5 = levels 1..6, 6 = versus)

    void drawArena(sf::RenderTarget& rt, int th) {
        const Theme& T = THEMES[th];
        const float G = cfg::GROUND, W = (float)cfg::W, H = (float)cfg::H;
        const float cl = camX - W / 2.f;
        const float DEG = 0.0174533f;

        auto rect = [&](float x, float y, float w, float h, sf::Color c) {
            sf::RectangleShape r({ w, h }); r.setPosition(x, y); r.setFillColor(c); rt.draw(r); };
        auto disc = [&](float x, float y, float r, sf::Color c, std::size_t pts = 24) {
            sf::CircleShape s(r, pts); s.setOrigin(r, r); s.setPosition(x, y); s.setFillColor(c); rt.draw(s); };
        auto poly = [&](std::initializer_list<sf::Vector2f> pts, sf::Color c) {
            sf::ConvexShape s(pts.size()); std::size_t i = 0;
            for (const auto& p : pts) s.setPoint(i++, p);
            s.setFillColor(c); rt.draw(s); };
        auto layer = [&](float p, float step, auto fn) {
            float off = cl * (1.f - p);
            int i0 = (int)std::floor((cl - 160.f - off) / step), i1 = (int)std::ceil((cl + W + 160.f - off) / step);
            for (int i = i0; i <= i1; i++) fn(i, off + i * step);
            };
        auto ridge = [&](float p, float hgt, float step, sf::Color c, float seed) {
            sf::ConvexShape tri(3); tri.setFillColor(c);
            layer(p, step, [&](int i, float lx) {
                float h = hgt * (0.55f + 0.45f * std::fabs(std::sin(i * 1.7f + seed)));
                tri.setPoint(0, sf::Vector2f(lx - step, G));
                tri.setPoint(1, sf::Vector2f(lx, G - h));
                tri.setPoint(2, sf::Vector2f(lx + step, G));
                rt.draw(tri);
                });
            };
        auto drift = [&](int n, float speedY, float sway, float r, sf::Color c, bool rise) {
            for (int i = 0; i < n; i++) {
                float sp = speedY * (0.6f + 0.8f * hash01(i, 7.f));
                float yy = std::fmod(hash01(i, 8.f) * H + clockT * sp, H + 40.f) - 20.f;
                if (rise) yy = H - yy;
                float xx = std::fmod(hash01(i, 9.f) * (W + 80.f) + clockT * 6.f, W + 80.f) - 40.f + std::sin(clockT * 0.9f + i) * sway;
                disc(cl + xx, yy, r * (0.6f + hash01(i, 10.f)), c, 10);
            }
            };

        sf::VertexArray va(sf::Quads, 4);
        va[0] = sf::Vertex(sf::Vector2f(cl - 50, -50), T.top);
        va[1] = sf::Vertex(sf::Vector2f(cl + W + 50, -50), T.top);
        va[2] = sf::Vertex(sf::Vector2f(cl + W + 50, H + 50), T.bot);
        va[3] = sf::Vertex(sf::Vector2f(cl - 50, H + 50), T.bot);
        rt.draw(va);

        float mx = 900.f + cl * 0.9f;
        switch (th) {
        case 0:
            disc(mx, 440, 190, sf::Color(255, 170, 90, 30), 40);
            disc(mx, 440, 120, sf::Color(255, 190, 110, 70), 40);
            disc(mx, 440, 84, sf::Color(255, 214, 140, 235), 40);
            break;
        case 1:
            disc(mx, 120, 110, sf::Color(220, 220, 255, 22), 40);
            disc(mx, 120, 72, sf::Color(238, 235, 214, 235), 40);
            disc(mx - 20, 108, 12, sf::Color(210, 206, 186, 150));
            disc(mx + 18, 136, 8, sf::Color(210, 206, 186, 150));
            break;
        case 2:
            for (int i = 0; i < 5; i++) {
                float sx = cl + 120.f + i * 270.f + std::sin(clockT * 0.3f + i) * 12.f;
                poly({ {sx, -20}, {sx + 70, -20}, {sx + 330, G}, {sx + 170, G} }, sf::Color(255, 255, 210, 20));
            }
            break;
        case 3:
            disc(mx, 130, 90, sf::Color(255, 255, 245, 40), 40);
            disc(mx, 130, 46, sf::Color(255, 255, 245, 190), 40);
            break;
        case 4: {
            float fl = 0.8f + 0.2f * std::sin(clockT * 7.f);
            disc(cl + 640, G - 40, 560, sf::Color(255, 100, 30, (sf::Uint8)(26 * fl)), 48);
            disc(cl + 640, G - 40, 340, sf::Color(255, 150, 50, (sf::Uint8)(30 * fl)), 48);
            break;
        }
        case 5: {
            float pu = 1.f + 0.02f * std::sin(clockT * 1.5f);
            disc(mx - 300, 230, 280 * pu, sf::Color(200, 20, 50, 28), 60);
            disc(mx - 300, 230, 190 * pu, sf::Color(150, 10, 34), 60);
            disc(mx - 300, 230, 170 * pu, sf::Color(205, 28, 52), 60);
            disc(mx - 340, 200, 34, sf::Color(160, 14, 38, 160));
            disc(mx - 250, 270, 22, sf::Color(160, 14, 38, 160));
            break;
        }
        default:
            disc(mx, 140, 70, sf::Color(230, 225, 200, 200), 40);
            break;
        }

        ridge(0.25f, 330, 220, T.far, 1.f + th);
        ridge(0.45f, 220, 180, T.mid, 4.f + th);

        switch (th) {
        case 0: case 6: {
            sf::Color wood = (th == 0) ? sf::Color(128, 34, 30) : sf::Color(80, 22, 28);
            layer(0.3f, 440.f, [&](int, float gx) {
                gx += 160.f;
                rect(gx - 90, G - 300, 18, 300, wood);
                rect(gx + 72, G - 300, 18, 300, wood);
                rect(gx - 115, G - 310, 250, 20, wood);
                rect(gx - 90, G - 262, 200, 12, wood);
                });
            break;
        }
        case 1: {
            sf::Color wall(30, 26, 48), wall2(36, 30, 56), roof(14, 12, 28);
            layer(0.35f, 560.f, [&](int i, float x) {
                if (hash01(i, 1.f) < 0.3f) return;
                rect(x - 120, G - 46, 240, 46, wall);
                for (int t = 0; t < 3; t++) {
                    float bw = 150.f - t * 34.f, by = G - 46.f - t * 74.f;
                    rect(x - bw / 2, by - 60, bw, 60, wall2);
                    rect(x - 8, by - 42, 16, 22, sf::Color(255, 196, 100, 210));
                    poly({ {x - bw / 2 - 40, by - 60}, {x + bw / 2 + 40, by - 60}, {x + bw / 2 + 4, by - 84}, {x - bw / 2 - 4, by - 84} }, roof);
                }
                rect(x - 2, G - 194 - 84 - 26, 4, 26, roof);
                });
            layer(0.6f, 330.f, [&](int i, float x) {
                if (hash01(i, 6.f) < 0.4f) return;
                rect(x - 24, G - 304, 48, 6, sf::Color(70, 40, 30));
                rect(x - 18, G - 298, 36, 118, sf::Color(150, 22, 34));
                poly({ {x - 18, G - 180}, {x + 18, G - 180}, {x, G - 150} }, sf::Color(150, 22, 34));
                disc(x, G - 250, 9, sf::Color(235, 230, 220, 220), 16);
                });
            break;
        }
        case 2: {
            for (int pass = 0; pass < 2; pass++) {
                float p = pass ? 0.85f : 0.5f, step = pass ? 120.f : 64.f, w = pass ? 16.f : 9.f;
                sf::Color c = pass ? sf::Color(38, 98, 62) : sf::Color(28, 68, 50);
                layer(p, step, [&](int i, float x) {
                    x += (hash01(i, 2.f + pass) - 0.5f) * step * 0.7f;
                    float h = 380.f + hash01(i, 3.f + pass) * 300.f;
                    rect(x, G - h, w, h, c);
                    for (float ny = G - 60; ny > G - h; ny -= 70.f) rect(x - 2, ny, w + 4, 4, shade(c, 1.45f));
                    poly({ {x + w, G - h + 30}, {x + w + 44, G - h + 42}, {x + w, G - h + 48} }, shade(c, 1.25f));
                    poly({ {x, G - h + 80}, {x - 40, G - h + 94}, {x, G - h + 98} }, shade(c, 1.25f));
                    });
            }
            break;
        }
        case 3: {
            for (int pass = 0; pass < 2; pass++) {
                float p = pass ? 0.75f : 0.5f, step = pass ? 150.f : 100.f;
                sf::Color pine = pass ? sf::Color(40, 62, 74) : sf::Color(70, 90, 112);
                layer(p, step, [&](int i, float x) {
                    if (hash01(i, 11.f + pass) < 0.2f) return;
                    float h = (pass ? 210.f : 150.f) + hash01(i, 12.f) * 90.f, w = pass ? 110.f : 80.f;
                    rect(x - 4, G - 26, 8, 26, sf::Color(40, 30, 28));
                    for (int t = 0; t < 3; t++) {
                        float by = G - 20.f - t * h * 0.27f, th2 = h * 0.42f, ww = w - t * 24.f;
                        poly({ {x - ww / 2, by}, {x + ww / 2, by}, {x, by - th2} }, pine);
                        poly({ {x - ww * 0.2f, by - th2 * 0.62f}, {x + ww * 0.2f, by - th2 * 0.62f}, {x, by - th2} }, sf::Color(236, 241, 250));
                    }
                    });
            }
            break;
        }
        case 4: {
            layer(0.4f, 280.f, [&](int i, float x) {
                if (hash01(i, 4.f) < 0.25f) return;
                float k = hash01(i, 5.f), hw = 70.f + k * 30.f, hh = 100.f + k * 50.f;
                rect(x - hw, G - hh, hw * 2, hh, sf::Color(30, 14, 12));
                poly({ {x - hw - 24, G - hh}, {x, G - hh - 70}, {x + hw + 24, G - hh}, }, sf::Color(20, 8, 8));
                float fl = 0.75f + 0.25f * std::sin(clockT * 8.f + i);
                rect(x - 14, G - hh + 30, 28, 34, sf::Color(255, 150, 50, (sf::Uint8)(210 * fl)));
                for (int m = 0; m < 4; m++) {
                    float fx = x - hw + m * (2.f * hw / 3.f);
                    float fh = std::max(22.f, 52.f + 34.f * std::sin(clockT * 9.f + i * 1.7f + m * 2.1f));
                    poly({ {fx - 18, G - hh + 4}, {fx + 18, G - hh + 4}, {fx, G - hh - fh} }, sf::Color(255, 120, 30, 210));
                    poly({ {fx - 9, G - hh + 4}, {fx + 9, G - hh + 4}, {fx, G - hh - fh * 0.6f} }, sf::Color(255, 220, 90, 230));
                }
                for (int s = 0; s < 3; s++) {
                    float sy = std::fmod(clockT * 22.f + s * 55.f + i * 17.f, 190.f);
                    disc(x + std::sin(clockT + s + i) * 16.f, G - hh - 90 - sy, 22 + sy * 0.12f, sf::Color(40, 34, 34, (sf::Uint8)(70 - sy * 0.3f)), 14);
                }
                });
            break;
        }
        case 5: {
            sf::Color bark(22, 4, 16);
            layer(0.5f, 420.f, [&](int i, float x) {
                if (hash01(i, 13.f) < 0.3f) return;
                float h = 220.f + hash01(i, 14.f) * 120.f;
                limb(rt, { x, G }, { x + (hash01(i, 15.f) - 0.5f) * 30.f, G - h }, 14.f, bark);
                limb(rt, { x, G - h * 0.6f }, { x - 80, G - h * 0.95f }, 7.f, bark);
                limb(rt, { x, G - h * 0.75f }, { x + 70, G - h * 1.05f }, 6.f, bark);
                limb(rt, { x - 80, G - h * 0.95f }, { x - 110, G - h * 1.12f }, 4.f, bark);
                limb(rt, { x + 70, G - h * 1.05f }, { x + 104, G - h * 1.2f }, 4.f, bark);
                });
            break;
        }
        }

        rect(-200, G, cfg::WORLD_W + 400.f, H - G + 200.f, T.floorC);
        if (th != 3 && th != 5)
            for (float x = 0; x <= cfg::WORLD_W; x += 140.f) rect(x, G, 3.f, H - G + 200.f, T.seam);
        if (th == 3) for (float x = -100; x < cfg::WORLD_W + 100; x += 190.f) {
            sf::CircleShape d(110.f, 30); d.setOrigin(110.f, 110.f); d.setScale(1.f, 0.08f);
            d.setPosition(x, G + 30 + std::fmod(x, 3.f) * 7.f); d.setFillColor(sf::Color(236, 240, 248)); rt.draw(d);
        }
        rect(-200, G + 38, cfg::WORLD_W + 400.f, 2.f, T.seam);
        rect(-200, G, cfg::WORLD_W + 400.f, 3.f, shade(T.seam, 1.5f));
        sf::CircleShape ring(420.f, 60); ring.setOrigin(420.f, 420.f); ring.setScale(1.f, 0.09f);
        ring.setPosition(cfg::WORLD_W / 2.f, G + 30); ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(T.ring); ring.setOutlineThickness(14.f); rt.draw(ring);

        if (th == 0 || th == 1 || th == 6) {
            for (int i = 0; i < (int)(cfg::WORLD_W / 350.f); i++) {
                float lx = 100 + i * 350.f;
                float fl = 0.75f + 0.25f * std::sin(clockT * 9.f + i * 2.f);
                rect(lx - 4, G - 150, 8, 150, sf::Color(40, 34, 40));
                disc(lx, G - 160, 50, sf::Color(255, 170, 80, (sf::Uint8)(45 * fl)));
                sf::RectangleShape box({ 22, 30 }); box.setOrigin(11, 15); box.setPosition(lx, G - 160);
                box.setFillColor(sf::Color(255, 190, 100, (sf::Uint8)(230 * fl))); rt.draw(box);
            }
        }
        if (th == 2) {
            for (int i = 0; i < (int)(cfg::WORLD_W / 540.f); i++) {
                float lx = 260 + i * 540.f, fl = 0.8f + 0.2f * std::sin(clockT * 5.f + i);
                rect(lx - 16, G - 18, 32, 18, sf::Color(70, 80, 70));
                rect(lx - 7, G - 70, 14, 52, sf::Color(80, 92, 80));
                rect(lx - 20, G - 100, 40, 30, sf::Color(70, 80, 70));
                rect(lx - 9, G - 94, 18, 18, sf::Color(255, 220, 130, (sf::Uint8)(220 * fl)));
                poly({ {lx - 30, G - 100}, {lx + 30, G - 100}, {lx, G - 126} }, sf::Color(58, 68, 58));
            }
        }
        if (th == 5) {
            layer(1.0f, 22.f, [&](int i, float x) {
                float x2 = x + hash01(i, 16.f) * 14.f, y0 = G + 6.f + hash01(i, 17.f) * 46.f;
                float ty = y0 - 22.f - hash01(i, 18.f) * 34.f;
                sf::Color red(210, 24, 48);
                rect(x2, ty, 2.f, y0 - ty, sf::Color(80, 24, 34));
                for (int k = 0; k < 5; k++) {
                    float ang = (-90.f + (k - 2) * 32.f) * DEG;
                    limb(rt, { x2 + 1, ty }, { x2 + 1 + std::cos(ang) * 13.f, ty + std::sin(ang) * 13.f }, 2.2f, red);
                }
                });
        }

        for (float px : { 0.f, cfg::WORLD_W - 44.f }) {
            rect(px, G - 430, 44, 430, shade(T.seam, 0.9f));
            rect(px - 8, G - 446, 60, 16, shade(T.seam, 1.3f));
        }

        for (int i = 0; i < 11; i++) {
            float mxp = std::fmod(i * 270.f + clockT * (8.f + i * 3.f), cfg::WORLD_W + 400.f) - 200.f;
            sf::CircleShape m(120); m.setOrigin(120, 120); m.setScale(1.f, 0.18f);
            m.setPosition(mxp, G - 6 + (i % 3) * 8); m.setFillColor(T.mist);
            rt.draw(m);
        }

        if (th == 2) drift(16, 38.f, 30.f, 3.f, sf::Color(120, 190, 100, 200), false);
        if (th == 3) drift(70, 70.f, 24.f, 2.4f, sf::Color(255, 255, 255, 220), false);
        if (th == 4) drift(46, 85.f, 18.f, 2.2f, sf::Color(255, 160, 50, 230), true);
        if (th == 5) drift(36, 36.f, 40.f, 3.0f, sf::Color(220, 30, 60, 210), false);
        if (th == 1) drift(10, 14.f, 12.f, 1.6f, sf::Color(200, 200, 255, 140), true);
    }

    std::string animKey(const Fighter& f) const {
        switch (f.st) {
        case St::Idle: return "idle";       case St::Walk: return "walk";
        case St::Dash: return "dash";       case St::Jump: return "jump";
        case St::Slash: return "slash";     case St::Thrust: return "thrust";
        case St::Sweep: return "sweep";     case St::Barrage: return "barrage";
        case St::Spear: return "spear";     case St::Shoot: return "shoot";
        case St::AirSlash: return "airslash"; case St::Execute: return "execute";
        case St::Block: return deflecting(f) ? "parry" : "block";
        case St::Mikiri: return "mikiri";   case St::JumpCounter: return "jumpcounter";
        case St::Hurt: return "hurt";       case St::Deflected: return "deflected";
        case St::Clash: return "clash";     case St::Broken: return "broken";
        case St::Dead: return "dead";
        }
        return "idle";
    }
    float animProgress(const Fighter& f) const {
        switch (f.st) {
        case St::Slash: case St::Thrust: case St::Sweep: case St::Spear: case St::AirSlash: return f.t / f.attackTotal();
        case St::Shoot: return std::clamp(f.t * f.spd() / cfg::GUN_TOTAL, 0.f, 1.f);
        case St::Execute: return f.t / cfg::EXEC_TOTAL;
        case St::Barrage: {
            float iv = cfg::BARRAGE_INTERVAL / f.speedMul, ph = f.t - cfg::BARRAGE_FIRST;
            if (ph < 0) return 0.f;
            return std::fmod(ph, iv) / iv;
        }
        case St::Dash: return f.t / cfg::DASH_TOTAL;
        case St::Mikiri: return f.t / cfg::MIKIRI_POSE;
        case St::JumpCounter: return f.t / 0.45f;
        case St::Hurt: return f.t / 0.32f;
        case St::Deflected: return f.t / 0.45f;
        case St::Clash: return f.t / 0.35f;
        case St::Dead: return f.t / 0.9f;
        case St::Block: return f.blockT / 0.25f;
        default: return -1.f;
        }
    }

    void drawFighter(sf::RenderTarget& rt, const Fighter& f) {
        sf::CircleShape sh(45); sh.setScale(f.widthMul, 0.22f); sh.setOrigin(45, 45);
        float hAir = std::max(0.f, cfg::GROUND - f.y);
        sh.setPosition(f.x, cfg::GROUND + 4);
        sh.setFillColor(sf::Color(0, 0, 0, (sf::Uint8)std::max(30.f, 110.f - hAir * 0.3f)));
        rt.draw(sh);

        bool blink = f.invuln > 0 && std::fmod(clockT, 0.12f) < 0.06f;
        const Anim* an = f.set ? f.set->find(animKey(f)) : nullptr;
        if (an && !an->frames.empty()) {
            size_t n = an->frames.size(), idx;
            float pr = animProgress(f);
            if (pr >= 0.f) idx = std::min(n - 1, (size_t)(std::clamp(pr, 0.f, 0.999f) * n));
            else idx = (size_t)(f.animT * f.set->fps) % n;
            const sf::Texture& tex = *an->frames[idx];
            sf::Sprite s(tex);
            s.setOrigin(tex.getSize().x / 2.f, (float)tex.getSize().y);
            float sc = cfg::SPRITE_H / f.set->refH;
            bool artRight = f.isBoss ? cfg::BOSS_ART_FACES_RIGHT : cfg::PLAYER_ART_FACES_RIGHT;
            bool flip = (f.facing == 1) != artRight;
            float sx = sc * f.widthMul, sy = sc * f.heightMul;
            s.setScale(flip ? -sx : sx, sy);
            s.setPosition(f.x, f.y);
            if (f.sinceHit < 0.1f && f.st != St::Dead) s.setColor(sf::Color(255, 150, 150));
            else if (f.st == St::Broken && std::fmod(f.t, 0.3f) < 0.15f) s.setColor(sf::Color(255, 230, 120));
            else if (blink) s.setColor(sf::Color(190, 160, 255, 150));
            rt.draw(s);
        }
        else {
            drawPlaceholder(rt, f, blink);
        }
        if (debug) {
            const AtkData& a = atkData(f.st);
            if (a.range > 0) {
                float rg = a.range * f.rangeMul;
                sf::RectangleShape r({ rg, 6 });
                r.setPosition(f.x + (f.facing > 0 ? 0 : -rg), f.y - 100);
                r.setFillColor(sf::Color(255, 0, 0, 120)); rt.draw(r);
            }
            static const char* names[] = { "Idle", "Walk", "Dash", "Jump", "Slash", "Thrust", "Sweep", "Barrage", "Block",
                                          "Mikiri", "JumpCounter", "Hurt", "Deflected", "Clash", "Broken", "Dead",
                                          "Spear", "Shoot", "AirSlash", "Execute" };
            txt(rt, names[(int)f.st], f.x, f.y - 280, 16, sf::Color::Yellow, 1);
            if (f.canGun) txt(rt, "gun cd " + std::to_string((int)(f.gunCd * 10) / 10.f).substr(0, 4), f.x, f.y - 260, 14, sf::Color(255, 210, 120), 1);
        }
    }

    void drawPlaceholder(sf::RenderTarget& rt, const Fighter& f, bool blink) {
        using V = sf::Vector2f;
        const float S = 1.3f, DEG = 0.0174533f;
        const float s = (float)f.facing;
        const float hs = f.heightMul * S;
        const float tw = f.widthMul * S;
        auto rad = [&](float d) { return d * DEG; };

        int weapon = 0;                                             // 0 sword-type, 1 yari, 2 matchlock gun
        if (f.st == St::Spear || (f.style == 5 && f.st != St::Shoot)) weapon = 1;
        if (f.st == St::Shoot) weapon = 2;
        const bool dual = (f.style == 4 && weapon == 0);
        const bool ronin = (f.style == 0);
        const bool rival = (f.style == 7);                          // player 2 uniform

        // ---------------- colours ----------------
        sf::Color skin(228, 198, 170), acc = f.color;
        sf::Color cloth(24, 24, 28), hak(16, 16, 20);               // Wolf: black kimono + black trousers
        sf::Color clothD = shade(cloth, 0.68f), hakD = shade(hak, 0.72f);
        if (f.isBoss) { cloth = mixc(sf::Color(40, 38, 52), f.color, 0.35f); hak = mixc(sf::Color(22, 20, 30), f.color, 0.30f); }
        if (rival) { cloth = sf::Color(160, 36, 44); hak = sf::Color(36, 36, 48); clothD = shade(cloth, 0.68f); hakD = shade(hak, 0.72f); }
        sf::Color obiC = ronin ? sf::Color(44, 40, 46) : (rival ? sf::Color(230, 200, 90) : acc);
        sf::Color armor = mixc(sf::Color(26, 24, 30), acc, 0.35f);
        sf::Color lace = acc, trim(214, 176, 70);                    // armour lacing + metal trim
        if (f.isBoss) {
            switch (f.style) {
            case 1: cloth = sf::Color(44, 58, 98);   hak = sf::Color(30, 32, 46);  armor = sf::Color(96, 68, 46);   lace = sf::Color(176, 38, 36);  trim = sf::Color(150, 152, 164); obiC = sf::Color(176, 38, 36); break;
            case 2: cloth = sf::Color(158, 26, 38);  hak = sf::Color(24, 22, 30);  armor = sf::Color(40, 34, 40);   lace = sf::Color(226, 206, 150); trim = sf::Color(226, 206, 150); obiC = sf::Color(226, 206, 150); acc = sf::Color(236, 230, 218); break;
            case 3: cloth = sf::Color(46, 56, 62);   hak = sf::Color(30, 34, 40);  armor = sf::Color(60, 52, 44);   lace = sf::Color(214, 128, 40);  trim = sf::Color(205, 170, 90);  obiC = sf::Color(96, 62, 38); break;
            case 4: cloth = sf::Color(206, 214, 228); hak = sf::Color(72, 84, 110); armor = sf::Color(160, 172, 192); lace = sf::Color(88, 120, 172);  trim = sf::Color(230, 234, 244); obiC = sf::Color(88, 120, 172); break;
            case 5: cloth = sf::Color(60, 66, 44);   hak = sf::Color(28, 28, 26);  armor = sf::Color(62, 48, 38);   lace = sf::Color(196, 62, 40);   trim = sf::Color(176, 140, 70);  obiC = sf::Color(124, 88, 52); break;
            case 6: cloth = sf::Color(34, 18, 30);   hak = sf::Color(20, 12, 20);  armor = sf::Color(58, 10, 24);   lace = sf::Color(176, 26, 46);   trim = sf::Color(204, 164, 62);  obiC = sf::Color(176, 26, 46); break;
            }
            if (ronin) { armor = sf::Color(30, 28, 32); lace = sf::Color(70, 64, 60); trim = sf::Color(90, 76, 56); }
            clothD = shade(cloth, 0.68f); hakD = shade(hak, 0.72f);
        }
        sf::Color hair(24, 22, 28);
        if (f.style == 6 || rival) hair = sf::Color(226, 226, 238);
        sf::Color sandal = (f.isBoss || rival) ? sf::Color(60, 44, 40) : sf::Color(186, 146, 90);
        float tintK = 0.f; sf::Color tintC = sf::Color::White; sf::Uint8 alpha = 255;
        if (f.sinceHit < 0.1f && f.st != St::Dead) { tintK = 0.55f; tintC = sf::Color(255, 110, 110); }
        else if (f.st == St::Broken && std::fmod(f.t, 0.3f) < 0.15f) { tintK = 0.5f; tintC = sf::Color(255, 220, 90); }
        else if (blink) { tintK = 0.5f; tintC = sf::Color(190, 160, 255); alpha = 170; }
        auto T = [&](sf::Color c) { c = mixc(c, tintC, tintK); c.a = alpha; return c; };

        // ---------------- pose ----------------
        float ts = isAttack(f.st) ? f.t * f.spd() : 0.f;
        const AtkData& a = atkData(f.st);
        float u = 0.f, act = 0.f, rec = 0.f;
        if (isAttack(f.st)) {
            u = std::clamp(ts / a.startup, 0.f, 1.f);
            act = std::clamp((ts - a.startup) / a.active, 0.f, 1.f);
            rec = std::clamp((ts - a.startup - a.active) / a.recovery, 0.f, 1.f);
        }
        auto seg = [&](float v0, float v1, float v2, float v3) {
            if (u < 1.f) return lerp(v0, v1, u);
            if (act < 1.f) return lerp(v1, v2, act);
            return lerp(v2, v3, rec);
            };
        float breathe = std::sin(f.animT * 2.6f);
        float hipY = 82.f + breathe * 1.2f, lean = 4.f, headDX = 3.f, headDY = 0.f;
        V fF(22, 0), fB(-20, 0);
        float swordAng = -50.f + breathe * 2.f, armAng = 28.f, reach = 38.f, blade = 108.f;
        bool twoHand = true, restHand = false; V h2off(-6, 56);
        float fall = 0.f, recoil = 0.f, pivY = 0.f;
        sf::Color bladeCol(215, 222, 236);
        bool trail = false; float trail0 = 0.f, trailFade = 1.f;

        switch (f.st) {
        case St::Idle:
            if (ronin) { swordAng = -14.f + breathe * 1.5f; armAng = 22.f; reach = 42.f; restHand = true;
            }   // katana held low, other hand on the obi
            break;
        case St::Walk: {
            float dirSign = (f.vx * s >= 0.f) ? 1.f : -1.f;
            float ph = f.animT * 10.f * dirSign, sw = std::sin(ph);
            fF = V(22 + sw * 24, -std::max(0.f, std::cos(ph)) * 11);
            fB = V(-20 - sw * 24, -std::max(0.f, -std::cos(ph)) * 11);
            hipY = 80.f - std::fabs(std::cos(ph)) * 3.f;
            lean = (dirSign > 0.f) ? 8.f : 0.f;
            swordAng = -55.f + sw * 3.f;
            if (ronin) { swordAng = -18.f + sw * 3.f; armAng = 22.f; reach = 42.f; restHand = true;
            }
            break;
        }
        case St::Dash: {
            bool fwd = (f.dashDir == s);
            if (fwd) { lean = 30; hipY = 60; fF = V(42, 0); fB = V(-46, -8); swordAng = 168; armAng = 48; reach = 28; }
            else { lean = -14; hipY = 68; fF = V(8, -6); fB = V(-40, 0); swordAng = -35; armAng = 10; reach = 36; }
            break;
        }
        case St::Jump: {
            hipY = 80.f;
            if (f.vy < 0) { fF = V(26, -38); fB = V(-6, -24); swordAng = -105; armAng = -75; reach = 40; lean = 6; }
            else { fF = V(20, -14); fB = V(-14, -8); swordAng = -20; armAng = 10; reach = 40; lean = 10; }
            break;
        }
        case St::AirSlash:                                          // jump slash: overhead arc while tucked in the air
            swordAng = seg(-115, -145, 70, 30); armAng = swordAng * 0.75f; reach = 42; hipY = 80;
            fF = V(26, -22); fB = V(-8, -30); lean = seg(6, -6, 22, 10); trail0 = -145;
            trail = (ts >= a.startup && ts < a.startup + a.active + 0.10f);
            if (ts >= a.startup + a.active) trailFade = 1.f - std::clamp((ts - a.startup - a.active) / 0.10f, 0.f, 1.f);
            break;
        case St::Execute: {                                         // deathblow: crouch, iai dash-through, flick the blade
            float et = f.t;
            bladeCol = sf::Color(255, 250, 235);
            if (et < cfg::EXEC_STRIKE) {
                float k = et / cfg::EXEC_STRIKE;
                hipY = lerp(82.f, 54.f, k); lean = lerp(4.f, -12.f, k);
                fF = V(lerp(22.f, 40.f, k), 0); fB = V(lerp(-20.f, -44.f, k), 0);
                swordAng = lerp(-50.f, -155.f, k); armAng = lerp(28.f, -70.f, k); reach = 40;
            }
            else {
                float k = std::clamp((et - cfg::EXEC_STRIKE) / 0.12f, 0.f, 1.f);
                float rc = std::clamp((et - 0.45f) / 0.45f, 0.f, 1.f);
                hipY = lerp(lerp(54.f, 60.f, k), 82.f, rc); lean = lerp(lerp(-12.f, 30.f, k), 4.f, rc);
                fF = V(lerp(40.f, 52.f, k), 0); fB = V(-40, 0);
                swordAng = lerp(lerp(-155.f, 62.f, k), -40.f, rc); armAng = lerp(lerp(-70.f, 30.f, k), 28.f, rc); reach = 44;
                trail = (et < cfg::EXEC_STRIKE + 0.2f); trail0 = -155.f;
                trailFade = 1.f - std::clamp((et - cfg::EXEC_STRIKE - 0.12f) / 0.08f, 0.f, 1.f);
            }
            break;
        }
        case St::JumpCounter: {                                     // stomp the head, then spring off in a backflip
            float jc = std::clamp(f.t / 0.45f, 0.f, 1.f);
            float stomp = 1.f - std::clamp(jc / 0.20f, 0.f, 1.f);
            float fl = std::clamp((jc - 0.08f) / 0.70f, 0.f, 1.f);
            fall = fl * fl * (3.f - 2.f * fl) * 4.f;                // smooth 360-degree backflip
            pivY = 70.f;
            hipY = 80.f; lean = lerp(-6.f, 16.f, stomp);
            fF = V(lerp(14.f, 38.f, stomp), lerp(-42.f, -4.f, stomp));
            fB = V(lerp(-4.f, -14.f, stomp), lerp(-48.f, -30.f, stomp));
            swordAng = lerp(-135.f, -100.f, stomp); armAng = lerp(-72.f, -60.f, stomp); reach = 40;
            bladeCol = sf::Color(255, 240, 140);
            break;
        }
        case St::Mikiri: {                                          // slide in, stomp the thrusting blade, rising counter-cut
            float m = std::clamp(f.t / cfg::MIKIRI_POSE, 0.f, 1.f);
            float st = std::clamp(m / 0.30f, 0.f, 1.f);              // slide + stomp
            float cut = std::clamp((m - 0.30f) / 0.45f, 0.f, 1.f);   // rising cut
            float rc = std::clamp((m - 0.75f) / 0.25f, 0.f, 1.f);    // recover
            hipY = lerp(lerp(78.f, 42.f, st), 70.f, std::max(cut * 0.6f, rc));
            lean = lerp(lerp(2.f, 38.f, st), 10.f, std::max(cut * 0.7f, rc));
            fF = V(lerp(22.f, 60.f, st), -std::max(0.f, 1.f - st * 3.f) * 26.f);   // front foot lifts, then stamps down
            fB = V(lerp(-20.f, -46.f, st), 0.f);
            swordAng = lerp(lerp(-50.f, 25.f, st), lerp(-150.f, -40.f, rc), cut);
            armAng = lerp(lerp(28.f, 38.f, st), lerp(-60.f, 28.f, rc), cut);
            reach = 46; bladeCol = sf::Color(150, 255, 215);
            trail = (cut > 0.02f && cut < 1.f); trail0 = 28.f; trailFade = 1.f - cut;
            break;
        }
        case St::Block:
            if (deflecting(f)) {
                swordAng = -62; armAng = -2; reach = 42; lean = 8; hipY = 78; bladeCol = sf::Color(255, 250, 210);
            }
            else {
                swordAng = -88; armAng = 8; reach = 34; lean = -2; hipY = 74; fF = V(26, 0); fB = V(-24, 0);
            }
            break;
        case St::Slash: {
            int c = std::clamp(f.combo, 0, 2);
            float L = seg(0.f, -0.3f, 1.f, 0.f);
            float lungeP = std::max(L, 0.f);
            lean = 4.f + 24.f * L; fF.x = 22.f + 24.f * lungeP; fB.x = -20.f - 8.f * lungeP; reach = 40;
            if (c == 0) { swordAng = seg(-50, -135, 55, -50); trail0 = -135; }
            else if (c == 1) { swordAng = seg(-50, 150, -80, -50); trail0 = 150; hipY = 70; }
            else { swordAng = seg(-50, -165, 78, 40); trail0 = -165; hipY = 80.f - 16.f * lungeP; bladeCol = sf::Color(255, 235, 170); }
            armAng = swordAng * 0.75f;
            trail = (ts >= a.startup && ts < a.startup + a.active + 0.10f);
            if (ts >= a.startup + a.active) trailFade = 1.f - std::clamp((ts - a.startup - a.active) / 0.10f, 0.f, 1.f);
            break;
        }
        case St::Thrust:
            reach = seg(38, 14, 52, 38); armAng = seg(28, 40, -4, 28); swordAng = seg(-50, 0, 0, -50);
            lean = seg(4, -10, 24, 4); hipY = seg(82, 66, 60, 82);
            fF.x = seg(22, 18, 46, 22); fB.x = seg(-20, -28, -36, -20);
            blade = seg(108, 108, 112, 108); bladeCol = sf::Color(255, 90, 90);
            break;
        case St::Spear:
            reach = seg(38, 12, 66, 38); armAng = seg(28, 18, -2, 28); swordAng = seg(-34, -2, 0, -34);
            lean = seg(4, -12, 30, 4); hipY = seg(82, 66, 60, 82);
            fF.x = seg(22, 16, 52, 22); fB.x = seg(-20, -34, -42, -20);
            bladeCol = sf::Color(255, 205, 110);
            break;
        case St::Shoot: {
            float gt = f.t * f.spd();
            float aim = std::clamp(gt / cfg::GUN_WINDUP, 0.f, 1.f);
            recoil = (gt >= cfg::GUN_WINDUP && gt < cfg::GUN_WINDUP + 0.12f) ? 1.f - (gt - cfg::GUN_WINDUP) / 0.12f : 0.f;
            swordAng = lerp(-72.f, -2.f, aim); armAng = lerp(-30.f, -2.f, aim);
            reach = 58.f - 8.f * recoil; lean = 8.f - 10.f * recoil; hipY = 76; fF = V(26, 0); fB = V(-24, 0);
            break;
        }
        case St::Sweep:
            hipY = seg(82, 46, 52, 82); lean = seg(4, -14, 28, 4);
            swordAng = seg(-50, -150, 8, -50); armAng = seg(28, -70, 45, 28); reach = seg(38, 40, 50, 38);
            fF.x = seg(22, 30, 38, 22); fB.x = seg(-20, -34, -40, -20);
            blade = 118;  bladeCol = sf::Color(255, 90, 90); trail0 = -150;
            trail = (ts >= a.startup && ts < a.startup + a.active + 0.10f);
            if (ts >= a.startup + a.active) trailFade = 1.f - std::clamp((ts - a.startup - a.active) / 0.10f, 0.f, 1.f);
            break;
        case St::Barrage: {
            float pr = animProgress(f);
            float k = pr < 0.5f ? pr * 2.f : (1.f - pr) * 2.f;
            swordAng = pr < 0.5f ? lerp(-130, 55, pr / 0.5f) : lerp(55, -130, (pr - 0.5f) / 0.5f);
            armAng = swordAng * 0.75f; reach = 46; lean = 8.f + 16.f * k; hipY = 78;
            fF.x = 24.f + 14.f * k; fB.x = -22;
            bladeCol = sf::Color(255, 170, 60);
            trail = pr > 0.25f && pr < 0.62f; trail0 = -130;
            break;
        }
        case St::Hurt:
            lean = -22; headDX = -9; hipY = 74; fF = V(14, 0); fB = V(-30, 0);
            swordAng = 45; armAng = -35; reach = 34; twoHand = false; h2off = V(-34, -10);
            break;
        case St::Deflected:
            if (f.deflectKind == 1) {                               // mikiri: blade pinned, stumbling forward
                float pin = std::clamp(f.t / 0.15f, 0.f, 1.f);
                lean = lerp(4.f, 26.f, pin); headDX = 6; hipY = lerp(82.f, 64.f, pin); fF = V(30, 0); fB = V(-18, 0);
                swordAng = lerp(-50.f, 66.f, pin); armAng = lerp(28.f, 50.f, pin); reach = 40; bladeCol = sf::Color(255, 120, 120);
            }
            else if (f.deflectKind == 2) {                          // jump counter: head driven down
                float pin = std::clamp(f.t / 0.12f, 0.f, 1.f);
                lean = lerp(4.f, 24.f, pin); headDY = 10.f * pin; headDX = 8.f * pin; hipY = lerp(82.f, 52.f, pin);
                fF = V(26, 0); fB = V(-24, 0); swordAng = lerp(-50.f, 30.f, pin); armAng = 30; reach = 36;
            }
            else {
                lean = -16; headDX = 0; hipY = 76; fF = V(10, 0); fB = V(-30, 0);
                swordAng = -118; armAng = -50; reach = 40;
            }
            break;
        case St::Clash:
            lean = 8; hipY = 78; fF = V(26, 0); fB = V(-24, 0); swordAng = -64; armAng = -4; reach = 42;
            break;
        case St::Broken: {
            float wob = std::sin(f.t * 14.f) * 4.f;
            hipY = 42; lean = 18.f + wob; headDY = 8; headDX = 8; fF = V(30, 0); fB = V(-34, -6);
            swordAng = 82; armAng = 50; reach = 30; twoHand = false; h2off = V(8, 40);
            break;
        }
        case St::Dead: {
            float p = std::clamp(f.t / 0.55f, 0.f, 1.f);
            fall = 1.f - (1.f - p) * (1.f - p);
            hipY = 82; lean = -4; swordAng = 80; armAng = 60; reach = 30; twoHand = false; h2off = V(-20, 40);
            break;
        }
        }
        float bladeMul = (weapon == 0) ? styleBladeMul(f.style) : 1.f;
        if (weapon == 1) blade = 170.f;
        if (weapon == 2) blade = 92.f;

        // ---------------- skeleton ----------------
        float tl = std::clamp(lean, -50.f, 50.f);
        V hip(0, -hipY);
        V sh = hip + V(tl, -std::sqrt(62.f * 62.f - tl * tl));
        V head = sh + V(headDX + tl * 0.2f, -24.f + headDY);

        V grip = sh + V(std::cos(rad(armAng)), std::sin(rad(armAng))) * reach;
        V e1 = ik(sh, grip, 37.f, 38.f, 1.f);
        V dir(std::cos(rad(swordAng)), std::sin(rad(swordAng)));
        float bl = blade * bladeMul;
        if (dir.y > 0.05f && grip.y + bl * dir.y > -3.f) bl = std::max(30.f, (-3.f - grip.y) / dir.y);
        V h2;
        if (restHand) h2 = hip + V(12, -8);
        else if (twoHand) h2 = grip - dir * (weapon == 1 ? 34.f : 9.f);
        else h2 = sh + h2off;
        V dir2 = dir; float bl2 = 0.f;
        if (dual) {                                                  // second short sword in the back hand
            float a2 = rad(lerp(swordAng, -45.f, 0.4f) + 18.f), aa = rad(armAng * 0.8f + 14.f);
            dir2 = V(std::cos(a2), std::sin(a2));
            h2 = sh + V(std::cos(aa), std::sin(aa)) * (reach * 0.9f);
            bl2 = 108.f * bladeMul * 0.9f;
            if (dir2.y > 0.05f && h2.y + bl2 * dir2.y > -3.f) bl2 = std::max(24.f, (-3.f - h2.y) / dir2.y);
        }
        V e2 = ik(sh, h2, 37.f, 38.f, 1.f);
        V hF = hip + V(3, 0), hB = hip + V(-3, 0);
        V kF = ik(hF, fF, 43.f, 44.f, -1.f); (void)kF;               // (also clamps the foot to reach)
        V kB = ik(hB, fB, 43.f, 44.f, -1.f);
        V fFd(fF.x * 0.35f + 8.f, fF.y * 0.6f), fBd(fB.x * 0.35f - 8.f, fB.y * 0.6f);   // damped feet: the hem only sways a little

        V dT = sh - hip; float dl = std::sqrt(dT.x * dT.x + dT.y * dT.y); dT /= dl;
        V perp(-dT.y, dT.x);

        float cT = std::cos(rad(fall * 90.f)), sT = std::sin(rad(fall * 90.f));
        auto P = [&](V p) -> V {
            float py = p.y + pivY;                                   // pivot: lets a body flip around its middle
            float x = p.x * cT + py * sT, y = -p.x * sT + py * cT - pivY - 7.f * sT;
            return V(f.x + s * x * hs, f.y + y * hs);
            };
        auto L = [&](V p0, V p1, float th, sf::Color c) { limb(rt, P(p0), P(p1), th * tw, T(c)); };
        auto Lp = [&](V p0, V p1, float px, sf::Color c) { c.a = std::min(c.a, alpha); limb(rt, P(p0), P(p1), px, c); };
        auto dot = [&](V p, float r, sf::Color c) {
            sf::CircleShape d(r); d.setOrigin(r, r); d.setPosition(P(p)); d.setFillColor(c); rt.draw(d);
            };
        // wide kimono sleeve hanging from the arm
        auto sleeve = [&](V s0, V e, sf::Color c) {
            sf::ConvexShape q(4);
            q.setPoint(0, P(s0 + V(-5, -3))); q.setPoint(1, P(e + V(4, -3)));
            q.setPoint(2, P(e + V(11, 30))); q.setPoint(3, P(s0 + V(-13, 26)));
            q.setFillColor(T(c)); rt.draw(q);
            limb(rt, P(s0 + V(-13, 26)), P(e + V(11, 30)), 2.5f, T(shade(c, 0.7f)));
            };

        // ---------------- effects behind the body ----------------
        if (f.style == 6) {
            for (int i = 0; i < 3; i++) {
                float r = (90.f + i * 22.f + std::sin(clockT * 5.f + i) * 6.f) * hs;
                sf::CircleShape c(r); c.setOrigin(r, r); c.setScale(0.7f, 1.f);
                c.setPosition(P(V(0, -95))); c.setFillColor(sf::Color(170, 30, 60, (sf::Uint8)(26 - i * 7)));
                rt.draw(c);
            }
        }
        if (f.st == St::Dash || (f.style == 5 && std::fabs(f.vx) > 200.f)) {
            for (int i = 0; i < 4; i++)
                Lp(V(-18.f - i * 8.f, -24.f - i * 24.f), V(-64.f - i * 16.f, -24.f - i * 24.f), 2.f, sf::Color(255, 255, 255, 90));
        }

        // ---------------- body, back to front ----------------
        if (!ronin) L(hip + V(-4, -2), hip + V(-40, 14), 5.f, sf::Color(30, 22, 24));      // saya (scabbard)
        if (ronin) {                                                 // sheathed katana across the back
            L(sh + V(-26, 36), sh + V(-6, -30), 4.5f, sf::Color(40, 30, 34));
            L(sh + V(-6, -30), sh + V(1, -46), 5.f, sf::Color(26, 22, 28));
            dot(sh + V(-6, -30), 3.6f, sf::Color(205, 172, 90));
        }
        if (f.isBoss) {
            float wv = std::sin(clockT * 3.2f);
            if (f.style == 1) {                                      // sashimono: clan war banner on the captain's back
                V b0 = hip + V(-12, -4), b1 = sh + V(-15, -78);
                L(b0, b1, 2.4f, sf::Color(70, 52, 38));
                sf::ConvexShape bn(4);
                bn.setPoint(0, P(b1 + V(0, 2)));
                bn.setPoint(1, P(b1 + V(-30 - wv * 3.f, 6)));
                bn.setPoint(2, P(b1 + V(-30 - wv * 3.f, 54)));
                bn.setPoint(3, P(b1 + V(0, 50)));
                bn.setFillColor(T(lace)); rt.draw(bn);
                dot(b1 + V(-15 - wv * 1.5f, 28), 7.f, T(sf::Color(236, 230, 218)));
                L(b1 + V(0, 2), b1 + V(-30 - wv * 3.f, 6), 2.f / tw, trim);
            }
            if (f.style == 4) {                                      // long scarf streaming in the wind
                V n0 = sh + V(-4, -6);
                for (int i = 0; i < 2; i++) {
                    V p = n0, q;
                    for (int j = 1; j <= 5; j++) {
                        q = n0 + V(-j * 12.f, 2.f + i * 7.f + std::sin(clockT * 5.f + j * 0.9f + i) * (2.f + j * 1.4f));
                        L(p, q, 6.f - j * 0.7f, i ? sf::Color(170, 186, 214) : sf::Color(236, 240, 248));
                        p = q;
                    }
                }
            }
            if (f.style == 5) {                                      // hinawa: smouldering match-cord at his hip
                V c0 = hip + V(-8, -2), c1 = hip + V(-14, 24 + wv * 2.f);
                L(c0, c1, 2.f / tw, sf::Color(150, 120, 80));
                dot(c1, 2.6f, sf::Color(255, 150, 50, (sf::Uint8)(180 + 70 * std::sin(clockT * 11.f))));
            }
            if (f.style == 6) {                                      // tattered crimson war-cloak
                float sway = wv * 4.f;
                sf::ConvexShape cp(6);
                cp.setPoint(0, P(sh + V(-2, -6)));
                cp.setPoint(1, P(sh + V(6, 0)));
                cp.setPoint(2, P(hip + V(-12 + sway, 58)));
                cp.setPoint(3, P(hip + V(-30 + sway * 1.5f, 66)));
                cp.setPoint(4, P(hip + V(-50 + sway * 2.f, 56)));
                cp.setPoint(5, P(sh + V(-16, -2)));
                cp.setFillColor(T(sf::Color(100, 10, 26))); rt.draw(cp);
                for (int i = 0; i < 3; i++)
                    L(hip + V(-14.f - i * 14.f + sway, 58.f + (i % 2) * 6.f), hip + V(-18.f - i * 14.f + sway * 1.6f, 74.f + i * 3.f), 3.f / tw, sf::Color(70, 6, 18));
            }
        }
        L(hip + V(-40, 14), hip + V(-46, 16), 6.f, sf::Color(205, 172, 90));
        L(fBd + V(4, -3), fBd + V(15, -3), 6.f, sandal);             // back sandal (toe only)

        {   // torso = kimono
            float hw = 10.f * tw, sw2 = 12.5f * tw;
            sf::ConvexShape q(4);
            q.setPoint(0, P(hip + perp * (hw / hs)));
            q.setPoint(1, P(sh + perp * (sw2 / hs)));
            q.setPoint(2, P(sh - perp * (sw2 / hs)));
            q.setPoint(3, P(hip - perp * (hw / hs)));
            q.setFillColor(T(cloth)); rt.draw(q);
            dot(hip, hw, T(cloth)); dot(sh, sw2, T(cloth));
        }
        L(sh + V(-2, -2), hip + dT * 36.f + V(4, 0), 3.f / tw, sf::Color(226, 222, 214));      // white collar
        if (ronin) {                                                 // woven kimono texture
            for (int i = 1; i <= 3; i++) L(sh - dT * (i * 12.f) + V(-6, 0), sh - dT * (i * 12.f) + V(8, 0), 1.5f / tw, shade(cloth, 0.82f));
        }
        if (ronin) {                                                 // cream scarf with a hanging tail
            sf::Color scarf(52, 50, 58), scarfD(36, 34, 42);
            float sw = std::sin(clockT * 3.f) * 2.f;
            L(sh + V(-9, -3), sh + V(11, -1), 11.f, scarf);
            L(sh + V(-4, -6), head + V(3, 12), 9.f, scarf);   // scarf wraps up the neck
            L(sh + V(8, 0), sh + V(12 + sw, 40), 7.f, scarf);
            L(sh + V(12 + sw, 40), sh + V(11 + sw, 48), 6.f, scarfD);
            L(sh + V(-9, -1), sh + V(-28, 6 - sw), 5.f, scarf);
        }
        if (rival) L(hip + dT * 26.f, sh - dT * 6.f, 20.f, mixc(acc, sf::Color::Black, 0.45f));
        if (f.isBoss && f.style != 2 && f.style != 3) {              // do: lamellar cuirass with lacing
            L(hip + dT * 24.f, sh - dT * 5.f, 21.f, armor);
            for (int i = 1; i <= 4; i++) {
                V c = hip + dT * (24.f + (dl - 29.f) * i / 5.f);
                L(c + perp * 9.f, c - perp * 9.f, 1.7f / tw, lace);
            }
            L(sh - dT * 5.f + perp * 10.f, sh - dT * 5.f - perp * 10.f, 2.4f / tw, trim);     // upper rim
            if (f.style == 6) {                                      // demon's gorget + chest boss
                dot(sh + V(2, -5), 9.5f * tw, T(armor));
                L(sh + V(-4, -6), sh + V(8, -6), 2.2f / tw, trim);
                dot(hip + dT * 40.f, 4.2f * tw, T(trim));
            }
        }
        if (f.style == 3) {                                          // kesa: the monk's saffron stole
            L(sh + V(-9, -3), hip + V(11, 4), 10.f, lace);
            L(sh + V(-9, -3), hip + V(11, 4), 1.6f, shade(lace, 0.6f));
            L(sh + V(-12, 0), hip + V(8, 7), 1.4f / tw, trim);
            L(hip + V(11, 4), hip + V(14, 30), 4.f, lace);           // hanging end
        }
        if (f.style == 2) {                                          // crest on the duelist's chest
            dot(sh - dT * 16.f + V(4, 0), 5.5f * tw, T(sf::Color(236, 230, 218)));
            dot(sh - dT * 16.f + V(4, 0), 2.6f * tw, T(cloth));
        }
        if (f.style == 5) {                                          // cartridge bandolier
            L(sh + V(-9, 0), hip + V(10, 2), 4.f, sf::Color(96, 66, 40));
            V b0 = sh + V(-9, 0), b1 = hip + V(10, 2);
            for (int i = 1; i <= 4; i++) dot(b0 + (b1 - b0) * (i / 5.f), 2.6f, T(sf::Color(210, 170, 80)));
        }
        {   // hakama: long wide trousers that hide the leg movement
            float hy = (fF.y + fB.y) * 0.5f - 3.f;
            V wl = hip + V(-13, 0), wr = hip + V(13, 0);
            V bl0 = V(fBd.x - 17.f, hy), br0 = V(fFd.x + 19.f, hy);
            sf::ConvexShape q(4);
            q.setPoint(0, P(wl)); q.setPoint(1, P(wr)); q.setPoint(2, P(br0)); q.setPoint(3, P(bl0));
            q.setFillColor(T(hak)); rt.draw(q);
            for (int i = 1; i <= 4; i++) {
                float k = i / 5.f;
                limb(rt, P(wl + (wr - wl) * k), P(bl0 + (br0 - bl0) * k), 1.8f, T(shade(hak, 0.68f)));
            }
            limb(rt, P(bl0), P(br0), 3.f, T(shade(hak, 0.6f)));       // hem
        }
        if (ronin) {                                                 // open orange coat
            float fl = std::sin(clockT * 4.f) * 2.f;
            sf::Color coat(30, 30, 36), coatD = shade(coat, 0.7f);
            sf::ConvexShape bk(4);
            bk.setPoint(0, P(sh + V(-14, -4))); bk.setPoint(1, P(sh + V(-3, -4)));
            bk.setPoint(2, P(hip + V(-22, 50))); bk.setPoint(3, P(hip + V(-38 - fl, 48)));
            bk.setFillColor(T(coatD)); rt.draw(bk);
            sf::ConvexShape fr(4);
            fr.setPoint(0, P(sh + V(6, -4))); fr.setPoint(1, P(sh + V(15, -4)));
            fr.setPoint(2, P(hip + V(30 + fl, 50))); fr.setPoint(3, P(hip + V(16, 52)));
            fr.setFillColor(T(coat)); rt.draw(fr);
            dot(sh + V(9, -1), 3.6f, T(sf::Color(110, 30, 34)));     // blue shoulder patch
        }

        if (f.style == 2) {                                          // haori: open crimson coat with flaring tails
            float fl = std::sin(clockT * 4.f) * 3.f;
            sf::ConvexShape hq(4);
            hq.setPoint(0, P(hip + V(-13, -6))); hq.setPoint(1, P(hip + V(13, -6)));
            hq.setPoint(2, P(hip + V(26 + fl, 42))); hq.setPoint(3, P(hip + V(-34 - fl, 46)));
            hq.setFillColor(T(cloth)); rt.draw(hq);
            L(hip + V(-34 - fl, 46), hip + V(26 + fl, 42), 3.f / tw, trim);                 // gold hem
            L(hip + V(-13, -6), hip + V(-34 - fl, 46), 1.6f / tw, shade(cloth, 0.7f));
            L(hip + V(13, -6), hip + V(26 + fl, 42), 1.6f / tw, shade(cloth, 0.7f));
        }

        L(hip + perp * (9.f), hip - perp * (9.f), 6.f / tw, obiC);   // obi (sash)
        if (rival) {
            L(hip + V(-2, 2), hip + V(-12, 30), 11.f, mixc(acc, sf::Color::Black, 0.5f));
            L(hip + V(6, 2), hip + V(10, 28), 11.f, mixc(acc, sf::Color::Black, 0.55f));
        }
        if (f.isBoss && f.style != 2 && f.style != 3) {              // kusazuri: hanging skirt plates
            int n = (f.style == 6) ? 5 : 4;
            float drop = (f.style == 6) ? 36.f : 26.f;
            for (int i = 0; i < n; i++) {
                float ox = -18.f + 36.f * i / (n - 1);
                L(hip + V(ox, 2), hip + V(ox * 1.18f - 2.f, drop), 8.5f, armor);
                L(hip + V(ox * 1.18f - 2.f, drop - 2.f), hip + V(ox * 1.18f - 2.f, drop + 1.f), 8.5f, trim);
            }
            L(hip + V(-20, drop * 0.5f), hip + V(20, drop * 0.5f), 1.6f / tw, lace);
        }
        {   // obi tail
            float sw = std::sin(clockT * 6.f) * 3.f;
            L(hip + V(-4, 0), hip + V(-16.f + sw, 22), 4.5f, obiC);
        }
        L(fFd + V(8, -3), fFd + V(21, -3), 6.f, sandal);            // front sandal
        if (ronin) L(fF + V(1, -3), fF + V(5, -3), 2.f / tw, sf::Color(30, 24, 26));   // thong
        if (rival) dot(sh + V(0, 2), 11.5f * tw, T(armor));
        if ((f.isBoss && f.style != 2 && f.style != 3) || ronin) {   // kote: armoured forearm + gauntlet
            float pw = (f.style == 6) ? 17.f : 12.f, ph = (f.style == 6) ? 28.f : 18.f;
            sf::ConvexShape sd(4);
            sd.setPoint(0, P(sh + V(-pw, -6))); sd.setPoint(1, P(sh + V(pw, -6)));
            sd.setPoint(2, P(sh + V(pw - 2.f, ph))); sd.setPoint(3, P(sh + V(-pw + 2.f, ph)));
            sd.setFillColor(T(armor)); rt.draw(sd);
            for (int i = 1; i <= 2; i++)
                L(sh + V(-pw + 1.f, -6.f + (ph + 6.f) * i / 3.f), sh + V(pw - 1.f, -6.f + (ph + 6.f) * i / 3.f), 1.8f / tw, lace);
            L(sh + V(-pw + 2.f, ph), sh + V(pw - 2.f, ph), 2.6f / tw, trim);
        }
        if (f.style == 3) {                                          // prayer beads
            for (int i = 0; i < 6; i++) dot(sh + V(7.f + std::sin(i * 0.55f) * 4.f, 6.f + i * 7.f), 3.2f, T(sf::Color(205, 170, 90)));
        }

        if (restHand && !ronin) {                                              // second sheathed sword at the sash, hand resting on it
            V g0 = hip + V(2, -6), g1 = hip + V(24, -12);
            Lp(g0, g1, 5.5f, sf::Color(38, 28, 34));
            dot(g1 + V(2, 0), 3.5f, T(sf::Color(205, 172, 90)));
        }
        // back arm + sleeve
        L(sh, e2, 11.f, clothD); L(e2, h2, 9.f, clothD); sleeve(sh, e2, clothD); dot(h2, 4.2f * tw, T(skin));
        if (dual) {                                                  // second wakizashi
            Lp(h2 - dir2 * 12.f, h2 + dir2 * 2.f, 4.5f, sf::Color(42, 30, 34));
            V pg = h2 + dir2 * 3.f; Lp(pg - V(-dir2.y, dir2.x) * 5.f, pg + V(-dir2.y, dir2.x) * 5.f, 3.f, sf::Color(205, 172, 90));
            Lp(h2 + dir2 * 4.f, h2 + dir2 * (4.f + bl2), 4.2f, bladeCol);
            Lp(h2 + dir2 * 8.f, h2 + dir2 * (4.f + bl2), 1.3f, sf::Color(255, 255, 255, 200));
        }

        // ---------------- head ----------------
        if (f.style == 2 || rival) {                                         // ronin ponytail
            float wv = std::sin(clockT * 7.f) * 4.f;
            L(head + V(-12, -10), head + V(-26, 0), 6.f, hair);
            L(head + V(-26, 0), head + V(-31, 20.f + wv), 5.f, hair);
        }
        {
            bool helm = (f.style == 4), bald = (f.style == 3);
            if (f.isBoss && (f.style == 1 || f.style == 4 || f.style == 5 || f.style == 6)) {   // shikoro: neck guard
                sf::ConvexShape nk(4);
                nk.setPoint(0, P(head + V(-13, -4))); nk.setPoint(1, P(head + V(-3, 8)));
                nk.setPoint(2, P(head + V(-11, 24))); nk.setPoint(3, P(head + V(-28, 14)));
                nk.setFillColor(T(armor)); rt.draw(nk);
                L(head + V(-24, 15), head + V(-9, 21), 1.6f / tw, lace);
                L(head + V(-26, 12), head + V(-12, 6), 1.6f / tw, lace);
            }

            if (!ronin) {
                sf::Color cap = bald ? skin : helm ? armor : hair;
                dot(head, 15.f * hs, T(cap));
                dot(head + V(5, 3), 11.5f * hs, T(skin));      // face
            }
            else {                                             // ===== WOLF HEAD =====
                sf::Color skinW(206, 172, 142), hairW(18, 16, 20), tie(160, 34, 34);
                float wv = std::sin(clockT * 3.f) * 1.5f;

                // short compact topknot: small tuft close to the skull
                V base = head + V(-3, -12);
                L(base, head + V(-8 + wv * 0.5f, -22), 6.f, hairW);
                L(base, head + V(-1, -25 + wv * 0.3f), 6.5f, hairW);
                L(base, head + V(6 + wv * 0.5f, -21), 5.5f, hairW);
                L(head + V(-8, -13), head + V(2, -13), 3.2f / tw, tie);     // hair tie

                // back of the skull and nape
                dot(head + V(-3, -3), 13.5f * hs, T(hairW));
                L(head + V(-12, 2), head + V(-19, 9), 5.f, hairW);
                dot(head + V(-3, -3), 12.5f * hs, T(hairW));

                // face: angular jaw
                V fp[7] = { V(-4, -11), V(9, -10), V(15, 0), V(14, 8), V(10, 15), V(3, 17), V(-6, 10) };
                sf::ConvexShape fc(7);
                for (int i = 0; i < 7; i++) fc.setPoint(i, P(head + fp[i]));
                fc.setFillColor(T(skinW)); rt.draw(fc);

                // stubble on the jaw
                sf::ConvexShape sb(4);
                sb.setPoint(0, P(head + V(-6, 10))); sb.setPoint(1, P(head + V(3, 17)));
                sb.setPoint(2, P(head + V(10, 15))); sb.setPoint(3, P(head + V(4, 7)));
                sb.setFillColor(T(shade(skinW, 0.60f))); rt.draw(sb);

                // fringe: two jagged bangs over the forehead
                sf::ConvexShape b1(3), b2(3);
                b1.setPoint(0, P(head + V(-8, -14))); b1.setPoint(1, P(head + V(9, -13))); b1.setPoint(2, P(head + V(2, -8)));
                b2.setPoint(0, P(head + V(2, -13)));  b2.setPoint(1, P(head + V(14, -10))); b2.setPoint(2, P(head + V(11, -7)));
                b1.setFillColor(T(hairW)); b2.setFillColor(T(hairW)); rt.draw(b1); rt.draw(b2);

                // stern brow, narrow eye, scar, mouth
                L(head + V(5, -6), head + V(15, -3), 3.2f / tw, hairW);                    // heavy brow angled down
                L(head + V(8, -1.5f), head + V(13.5f, -1.5f), 2.2f / tw, sf::Color(20, 18, 22));
                L(head + V(9, -9), head + V(12.5f, 3), 1.4f / tw, sf::Color(150, 66, 66));  // scar through the brow
                L(head + V(8, 9), head + V(13, 9), 1.5f / tw, shade(skinW, 0.45f));         // set mouth
                L(head + V(11, 2), head + V(14, 4), 1.3f / tw, shade(skinW, 0.7f));         // nose shadow
            }
            if (f.style == 6) {                                      // long white hair flowing from under the helm
                float wv3 = std::sin(clockT * 4.f) * 4.f;
                L(head + V(-12, -2), head + V(-30, 12 + wv3), 6.f, hair);
                L(head + V(-30, 12 + wv3), head + V(-40, 34 - wv3), 4.5f, hair);
            }
            if (bald) L(head + V(-4, -8), head + V(12, -8), 3.f / tw, sf::Color(236, 232, 224));   // white head wrap
        }
        {
            if (!ronin) {
                bool demon = (f.style == 6);

                    sf::Color eye = demon ? sf::Color(255, 60, 60) : sf::Color(25, 25, 30);
                L(head + V(6, -6), head + V(13, -5), 2.6f / tw, hair);
                if (demon) dot(head + V(9, -2), 5.5f, sf::Color(255, 40, 40, 70));
                dot(head + V(9, -2), 2.2f, eye);
            }
        }
        if (f.style == 3) {                                          // sohei cowl: white head-cloth tail
            float wv4 = std::sin(clockT * 5.f) * 3.f;
            L(head + V(-12, -8), head + V(-28, 2 + wv4), 5.f / tw, sf::Color(236, 232, 224));
            L(head + V(-28, 2 + wv4), head + V(-36, 14 - wv4), 4.f / tw, sf::Color(236, 232, 224));
        }
        if (f.style == 5) {                                          // black face-cloth with a trailing tail
            sf::ConvexShape ms(4);
            ms.setPoint(0, P(head + V(-1, 0))); ms.setPoint(1, P(head + V(16, 0)));
            ms.setPoint(2, P(head + V(14, 11))); ms.setPoint(3, P(head + V(1, 12)));
            ms.setFillColor(T(sf::Color(30, 28, 30))); rt.draw(ms);
            float wv2 = std::sin(clockT * 7.f) * 3.f;
            L(head + V(-3, 4), head + V(-22, 8 + wv2), 4.f / tw, sf::Color(30, 28, 30));
            L(head + V(-3, 7), head + V(-20, 15 - wv2), 3.f / tw, sf::Color(30, 28, 30));
        }
        if (f.style == 6) {                                          // demon menpo, horned helm bowl and wings
            sf::ConvexShape ms(4);
            ms.setPoint(0, P(head + V(-1, 0))); ms.setPoint(1, P(head + V(17, 1)));
            ms.setPoint(2, P(head + V(14, 13))); ms.setPoint(3, P(head + V(1, 14)));
            ms.setFillColor(T(sf::Color(96, 12, 30))); rt.draw(ms);
            L(head + V(2, 8), head + V(16, 8), 1.6f / tw, sf::Color(20, 4, 10));
            sf::ConvexShape fg(3);
            fg.setPoint(0, P(head + V(5, 8))); fg.setPoint(1, P(head + V(8, 8))); fg.setPoint(2, P(head + V(6.5f, 2)));
            fg.setFillColor(T(sf::Color(236, 230, 218))); rt.draw(fg);
            fg.setPoint(0, P(head + V(11, 8))); fg.setPoint(1, P(head + V(14, 8))); fg.setPoint(2, P(head + V(12.5f, 2)));
            rt.draw(fg);
            sf::ConvexShape hb(6);
            hb.setPoint(0, P(head + V(-16, -5))); hb.setPoint(1, P(head + V(-15, -14))); hb.setPoint(2, P(head + V(-7, -21)));
            hb.setPoint(3, P(head + V(6, -21)));  hb.setPoint(4, P(head + V(15, -14)));  hb.setPoint(5, P(head + V(18, -6)));
            hb.setFillColor(T(armor)); rt.draw(hb);
            L(head + V(-16, -6), head + V(19, -6), 3.2f / tw, trim);
            sf::ConvexShape wg(3);
            wg.setPoint(0, P(head + V(14, -12))); wg.setPoint(1, P(head + V(30, -22))); wg.setPoint(2, P(head + V(22, -2)));
            wg.setFillColor(T(armor)); rt.draw(wg);
            wg.setPoint(0, P(head + V(-14, -12))); wg.setPoint(1, P(head + V(-30, -22))); wg.setPoint(2, P(head + V(-22, -2)));
            rt.draw(wg);
        }
        if (f.style == 2 || rival) {                                       // hachimaki + tails
            float wv = std::sin(clockT * 8.f) * 3.f;
            L(head + V(-14, -8), head + V(14, -8), 4.f / tw, acc);
            L(head + V(-14, -8), head + V(-30, -4.f + wv), 3.5f / tw, acc);
            L(head + V(-14, -8), head + V(-28, 3.f - wv), 3.f / tw, acc);
        }
    
        if (f.style == 1 || f.style == 5) {                          // jingasa (iron war hat)
            sf::Color hc = (f.style == 1) ? sf::Color(98, 98, 108) : sf::Color(36, 34, 40);
            float hw2 = (f.style == 1) ? 30.f : 36.f;
            sf::ConvexShape hat(3);
            hat.setPoint(0, P(head + V(-hw2, -6))); hat.setPoint(1, P(head + V(0, -38))); hat.setPoint(2, P(head + V(hw2, -6)));
            hat.setFillColor(T(hc)); rt.draw(hat);
            L(head + V(-hw2, -6), head + V(hw2, -6), 3.f / tw, (f.style == 1) ? sf::Color(60, 60, 68) : sf::Color(190, 30, 40));
        }
        if (f.style == 4) {                                          // silver kabuto with a crescent maedate
            L(head + V(-15, -9), head + V(17, -7), 4.2f / tw, trim);
            L(head + V(0, -14), head + V(0, -21), 3.f / tw, trim);
            for (int i = 0; i < 6; i++) {
                float an0 = rad(15.f + i * 25.f), an1 = rad(15.f + (i + 1) * 25.f);
                V c0 = head + V(0, -38) + V(std::cos(an0), std::sin(an0)) * 19.f;
                V c1 = head + V(0, -38) + V(std::cos(an1), std::sin(an1)) * 19.f;
                L(c0, c1, 3.6f, trim);
            }
        }
        if (f.style == 6) {                                          // oni horns
            sf::Color horn(170, 30, 50);
            sf::ConvexShape h1(3), h2c(3);
            h1.setPoint(0, P(head + V(2, -12))); h1.setPoint(1, P(head + V(20, -46))); h1.setPoint(2, P(head + V(11, -10)));
            h2c.setPoint(0, P(head + V(-8, -12))); h2c.setPoint(1, P(head + V(-26, -42))); h2c.setPoint(2, P(head + V(-2, -10)));
            h1.setFillColor(T(horn)); h2c.setFillColor(T(horn)); rt.draw(h1); rt.draw(h2c);
        }

        // ---------------- slash trail ----------------
        if (trail && trailFade > 0.f) {
            const int N = 10;
            sf::VertexArray va(sf::TriangleStrip, 2 * N);
            float rOut = reach + bl, rIn = reach + bl * 0.3f;
            for (int i = 0; i < N; i++) {
                float k = (float)i / (N - 1), ang = rad(lerp(trail0, swordAng, k));
                V d(std::cos(ang), std::sin(ang));
                sf::Color c = bladeCol; c.a = (sf::Uint8)(std::min<float>(alpha, 150.f * k * trailFade));
                va[2 * i] = sf::Vertex(P(sh + d * rOut), c);
                va[2 * i + 1] = sf::Vertex(P(sh + d * rIn), c);
            }
            rt.draw(va);
        }

        // ---------------- front arm, sleeve + weapon ----------------
        L(sh, e1, 11.f, cloth); L(e1, grip, 9.5f, cloth); sleeve(sh, e1, cloth); dot(grip, 4.4f * tw, T(skin));
        if (f.isBoss && f.style != 2 && f.style != 3) {              // kote: armoured forearm + gauntlet
            V k0 = e1 + (grip - e1) * 0.22f, k1 = e1 + (grip - e1) * 0.9f;
            L(k0, k1, 8.4f, armor);
            L(k0, k0 + (k1 - k0) * 0.12f, 9.2f, trim);
            dot(grip, 4.8f * tw, T(armor));
        }
        V pp(-dir.y, dir.x);
        if (weapon == 0) {
            switch (f.style) {
            case 2: {                                                // ODACHI - broad great sword
                sf::Color steel = mixc(bladeCol, sf::Color(150, 160, 182), 0.35f);
                Lp(grip - dir * 26.f, grip + dir * 2.f, 6.f, sf::Color(52, 30, 30));
                V pg = grip + dir * 3.f;
                Lp(pg - pp * 13.f, pg + pp * 13.f, 5.f, sf::Color(205, 172, 90));
                Lp(grip + dir * 4.f, grip + dir * (bl - 14.f), 12.f, steel);
                Lp(grip + dir * (bl - 18.f), grip + dir * bl, 6.f, steel);
                Lp(grip + dir * 10.f, grip + dir * (bl - 16.f), 3.f, shade(steel, 0.5f));
                Lp(grip + dir * 12.f, grip + dir * (bl - 6.f), 1.4f, sf::Color(255, 255, 255, 210));
                break;
            }
            case 3: {                                                // NODACHI - very long thin blade
                Lp(grip - dir * 34.f, grip + dir * 2.f, 5.5f, sf::Color(30, 40, 60));
                V pg = grip + dir * 3.f;
                Lp(pg - pp * 7.f, pg + pp * 7.f, 3.f, sf::Color(205, 172, 90));
                Lp(grip + dir * 4.f, grip + dir * (4.f + bl), 3.6f, bladeCol);
                Lp(grip + dir * 10.f, grip + dir * (4.f + bl), 1.2f, sf::Color(255, 255, 255, 220));
                break;
            }
            case 4: {                                                // TWIN WAKIZASHI - this is the front blade
                Lp(grip - dir * 12.f, grip + dir * 2.f, 4.5f, sf::Color(42, 30, 34));
                V pg = grip + dir * 3.f;
                Lp(pg - pp * 5.f, pg + pp * 5.f, 3.f, sf::Color(205, 172, 90));
                Lp(grip + dir * 4.f, grip + dir * (4.f + bl), 4.2f, bladeCol);
                Lp(grip + dir * 8.f, grip + dir * (4.f + bl), 1.3f, sf::Color(255, 255, 255, 200));
                break;
            }
            case 6: {                                                // KURENAI-MARU - the legendary crimson blade
                float pulse = 0.6f + 0.4f * std::sin(clockT * 6.f);
                V pg = grip + dir * 3.f;
                Lp(grip - dir * 22.f, grip + dir * 2.f, 6.f, sf::Color(150, 20, 40));
                Lp(grip - dir * 22.f, grip - dir * 18.f, 8.f, sf::Color(214, 176, 70));
                Lp(pg - pp * 15.f, pg + pp * 15.f, 5.f, sf::Color(214, 176, 70));
                dot(pg, 8.f, T(sf::Color(214, 176, 70))); dot(pg, 4.f, T(sf::Color(200, 20, 50)));
                Lp(grip + dir * 4.f, grip + dir * (4.f + bl), 22.f, sf::Color(255, 40, 70, (sf::Uint8)(34 + 30 * pulse)));   // aura
                Lp(grip + dir * 4.f, grip + dir * (4.f + bl), 9.f, sf::Color(48, 24, 54));                                  // dark steel
                Lp(grip + dir * 4.f, grip + dir * (4.f + bl), 3.4f, mixc(sf::Color(255, 70, 80), bladeCol, 0.25f));         // glowing edge
                Lp(grip + dir * 8.f, grip + dir * (4.f + bl), 1.4f, sf::Color(255, 235, 220, 230));
                for (int i = 0; i < 5; i++) {                                                                               // drifting embers
                    float k = std::fmod(clockT * 0.9f + i * 0.2f, 1.f);
                    dot(grip + dir * (4.f + bl * k) + pp * (std::sin(clockT * 9.f + i * 2.f) * 7.f), 3.f * (1.f - k) + 1.f, sf::Color(255, 150, 60, (sf::Uint8)(220 * (1.f - k))));
                }
                dot(grip + dir * (4.f + bl), 5.f + 2.f * pulse, sf::Color(255, 120, 90, 170));
                break;
            }
            default: {                                               // KATANA
                Lp(grip - dir * 15.f, grip + dir * 2.f, 5.f, sf::Color(42, 30, 34));
                V pg = grip + dir * 3.f;
                Lp(pg - pp * 6.f, pg + pp * 6.f, 3.f, sf::Color(205, 172, 90));
                Lp(grip + dir * 4.f, grip + dir * (4.f + bl), 4.f, bladeCol);
                Lp(grip + dir * 8.f, grip + dir * (4.f + bl), 1.4f, sf::Color(255, 255, 255, 200));
                break;
            }
            }
        }
        else if (weapon == 1) {                                                              // yari (long spear)
            Lp(grip - dir * 62.f, grip + dir * (bl - 40.f), 4.5f, sf::Color(112, 74, 42));
            Lp(grip - dir * 62.f, grip - dir * 56.f, 6.f, sf::Color(60, 60, 66));
            Lp(grip + dir * (bl - 56.f), grip + dir * (bl - 50.f), 6.f, sf::Color(190, 30, 40));
            Lp(grip + dir * (bl - 42.f), grip + dir * (bl - 8.f), 7.f, bladeCol);
            Lp(grip + dir * (bl - 12.f), grip + dir * (bl + 8.f), 3.f, bladeCol);
            Lp(grip + dir * (bl - 40.f), grip + dir * (bl - 10.f), 1.6f, sf::Color(255, 255, 255, 190));
        }
        else {                                                                               // matchlock gun
            V stockEnd = grip - dir * 46.f + pp * 8.f;
            Lp(grip - dir * 16.f, stockEnd, 9.f, sf::Color(104, 68, 40));
            Lp(grip - dir * 20.f, grip + dir * bl, 5.5f, sf::Color(66, 66, 76));
            Lp(grip + dir * (bl - 6.f), grip + dir * bl, 7.f, sf::Color(40, 40, 48));
            Lp(grip + dir * 4.f, grip + dir * 4.f + pp * 9.f, 3.f, sf::Color(205, 172, 90));
            if (recoil > 0.f) {
                dot(grip + dir * (bl + 12.f), 22.f * recoil + 6.f, sf::Color(255, 140, 40, 150));
                dot(grip + dir * (bl + 10.f), 12.f * recoil + 3.f, sf::Color(255, 240, 150, 230));
            }
            else if (f.t * f.spd() > cfg::GUN_WINDUP * 0.5f)
                dot(grip + dir * 4.f + pp * 9.f, 3.2f, sf::Color(255, 120, 40, 220));
        }
    }

    void drawWarning(sf::RenderTarget& rt, const Fighter& f) {
        const char* msg = nullptr; sf::Color col(220, 20, 20);
        if (f.st == St::Thrust || f.st == St::Sweep) {
            const AtkData& a = atkData(f.st);
            if (f.t * f.spd() > a.startup + a.active) return;
            msg = (f.st == St::Thrust) ? "THRUST  -> DASH IN" : "SWEEP  -> JUMP";
        }
        else if (f.st == St::Barrage) {
            if (f.t > cfg::BARRAGE_FIRST + 0.5f) return;
            msg = "BARRAGE  -> DEFLECT ONLY"; col = sf::Color(255, 140, 20);
        }
        else if (f.st == St::Shoot) {
            if (f.t * f.spd() > cfg::GUN_WINDUP) return;
            msg = "GUNSHOT  -> DEFLECT / BLOCK / JUMP"; col = sf::Color(235, 190, 40);
        }
        else return;
        float pulse = 1.f + 0.15f * std::sin(clockT * 30.f);
        sf::Vector2f p(f.x, f.y - cfg::SPRITE_H * f.heightMul - 40);
        if (as.danger) {
            sf::Sprite s(*as.danger);
            s.setOrigin(as.danger->getSize().x / 2.f, as.danger->getSize().y / 2.f);
            s.setPosition(p); s.setScale(pulse, pulse); rt.draw(s);
        }
        else {
            sf::CircleShape c(24 * pulse, 3); c.setOrigin(24 * pulse, 24 * pulse);
            c.setPosition(p); c.setFillColor(col); c.setOutlineColor(sf::Color::White); c.setOutlineThickness(3);
            rt.draw(c);
            txt(rt, "!", p.x, p.y - 14, 30, sf::Color::White, 1);
        }
        txt(rt, msg, p.x, p.y - 62, 18, sf::Color(255, 150, 120), 1);
    }

    //  FANCY HUD
   
    void fancyBar(sf::RenderTarget& rt, float x, float y, float w, float h, float frac, float ghost,
        sf::Color top, sf::Color bot, bool rtl, float slant, bool ticks) {
        using V = sf::Vector2f;
        const float sl = rtl ? -slant : slant;
        auto seg = [&](float a, float b, sf::Color ct, sf::Color cb, float y0f = 0.f, float y1f = 1.f) {
            if (b <= a) return;
            float bx0 = rtl ? x + w * (1.f - b) : x + w * a, bx1 = rtl ? x + w * (1.f - a) : x + w * b;
            float shT = sl * (1.f - y0f), shB = sl * (1.f - y1f), yT = y + h * y0f, yB = y + h * y1f;
            quadV(rt, V(bx0 + shT, yT), V(bx1 + shT, yT), V(bx1 + shB, yB), V(bx0 + shB, yB), ct, ct, cb, cb);
            };
        frac = std::clamp(frac, 0.f, 1.f); ghost = std::clamp(ghost, 0.f, 1.f);
        seg(0.f, 1.f, sf::Color(10, 6, 8, 235), sf::Color(34, 18, 22, 235));
        if (ghost > frac) seg(frac, ghost, sf::Color(255, 238, 205, 235), sf::Color(255, 190, 120, 235));
        seg(0.f, frac, top, bot);
        seg(0.f, frac, sf::Color(255, 255, 255, 95), sf::Color(255, 255, 255, 0), 0.f, 0.5f);      // gloss
        if (ticks) for (int i = 1; i < 10; i++) {
            float px = rtl ? x + w - w * i / 10.f : x + w * i / 10.f;
            limb(rt, V(px + sl, y), V(px, y + h), 1.4f, sf::Color(0, 0, 0, 110));
        }
        sf::ConvexShape fr(4);
        fr.setPoint(0, V(x + sl, y)); fr.setPoint(1, V(x + w + sl, y)); fr.setPoint(2, V(x + w, y + h)); fr.setPoint(3, V(x, y + h));
        fr.setFillColor(sf::Color::Transparent); fr.setOutlineColor(sf::Color(222, 188, 108)); fr.setOutlineThickness(2.f);
        rt.draw(fr);
        float ex = rtl ? x + w + sl * 0.5f + 8.f : x + sl * 0.5f - 8.f;                           // ornament at the outer end
        diamond(rt, ex, y + h * 0.5f, h * 0.55f, h * 0.8f, sf::Color(222, 188, 108), sf::Color(60, 40, 20), 1.5f);
    }
    void lantern(sf::RenderTarget& rt, float cx, float cy, bool full, int idx) {
        if (full) {
            float p = 0.6f + 0.4f * std::sin(clockT * 3.f + idx * 1.3f);
            sf::CircleShape g(15.f); g.setOrigin(15.f, 15.f); g.setPosition(cx, cy);
            g.setFillColor(sf::Color(170, 110, 255, (sf::Uint8)(40 + 40 * p))); rt.draw(g);
            diamond(rt, cx, cy, 9.f, 12.f, sf::Color(150, 96, 235), sf::Color(222, 188, 108), 2.f);
            diamond(rt, cx, cy - 1.f, 4.f, 6.f, sf::Color(235, 220, 255, (sf::Uint8)(150 + 90 * p)));
        }
        else diamond(rt, cx, cy, 9.f, 12.f, sf::Color(26, 18, 40), sf::Color(110, 100, 130), 2.f);
    }

    void drawHUD(sf::RenderTarget& rt) {
        using V = sf::Vector2f;
        const float bw = 520, W = (float)cfg::W, cx = W / 2.f;
        for (int i = 0; i < 2; i++) {
            const Fighter& f = F[i];
            bool rtl = (i == 1);
            float x = rtl ? W - 40 - bw : 40;
            txt(rt, f.name, rtl ? x + bw : x, 12, 26, sf::Color(250, 240, 220), rtl ? 2 : 0, true);
            // calligraphic underline fading away from the screen edge
            float ux = rtl ? x + bw - 300 : x;
            sf::Color g1(222, 188, 108, 255), g0(222, 188, 108, 0);
            quadV(rt, V(ux, 46), V(ux + 300, 46), V(ux + 300, 48.5f), V(ux, 48.5f), rtl ? g0 : g1, rtl ? g1 : g0, rtl ? g1 : g0, rtl ? g0 : g1);
            // health
            float hf = f.hp / f.maxHp, gf = f.hpGhost / f.maxHp;
            float lowP = (hf < 0.3f) ? 0.5f + 0.5f * std::sin(clockT * 9.f) : 0.f;
            sf::Color ht = mixc(sf::Color(226, 56, 60), sf::Color(255, 130, 120), lowP), hb = mixc(sf::Color(120, 14, 28), sf::Color(190, 30, 40), lowP);
            fancyBar(rt, x, 54, bw, 24, hf, gf, ht, hb, rtl, 16.f, true);
            // posture (turns from gold to red; flashes when broken)
            float pf = f.posture / f.maxPosture;
            bool broken = (f.st == St::Broken);
            float heat = std::clamp((pf - 0.5f) * 2.f, 0.f, 1.f);
            sf::Color pt = mixc(sf::Color(246, 222, 130), sf::Color(255, 96, 50), heat);
            sf::Color pb = mixc(sf::Color(196, 142, 54), sf::Color(190, 30, 22), heat);
            if (broken && std::fmod(clockT, 0.2f) < 0.1f) { pt = sf::Color::White; pb = sf::Color(255, 230, 160); }
            fancyBar(rt, rtl ? x + 14 : x, 84, bw - 14, 11, broken ? 1.f : pf, broken ? 1.f : pf, pt, pb, rtl, 8.f, false);
            // life orbs (lanterns)
            for (int j = 0; j < f.maxLives; j++)
                lantern(rt, rtl ? x + bw - 14 - j * 30 : x + 14 + j * 30, 118, f.lives > j, j);
            if (f.canGun) {                                       // gun reload gauge
                float ready = 1.f - std::clamp(f.gunCd / cfg::GUN_COOLDOWN, 0.f, 1.f);
                float gx = rtl ? x + bw - 200 : x + 100;
                fancyBar(rt, gx, 111, 100, 9, ready, ready, ready >= 1.f ? sf::Color(255, 232, 120) : sf::Color(176, 150, 90),
                    ready >= 1.f ? sf::Color(214, 160, 40) : sf::Color(110, 90, 50), rtl, 5.f, false);
                txt(rt, ready >= 1.f ? "GUN READY" : "RELOAD", rtl ? gx - 12 : gx + 112, 105, 13, sf::Color(255, 224, 150), rtl ? 2 : 0);
            }
        }
        if (versus) {
            diamond(rt, cx, 58, 62, 44, sf::Color(14, 10, 16, 220), sf::Color(222, 188, 108), 2.f);
            txt(rt, "ROUND " + std::to_string(wins[0] + wins[1] + 1), cx, 22, 15, sf::Color(220, 220, 220), 1);
            txt(rt, std::to_string(wins[0]) + " - " + std::to_string(wins[1]), cx, 38, 32, sf::Color(255, 230, 140), 1);
            txt(rt, "P1: A/D W (J in air = jump slash) | J slash  K block  L dash  U thrust     P2: Arrows | , slash  . block  / dash  ; thrust",
                cx, cfg::H - 26, 13, sf::Color(200, 200, 200, 190), 1);
        }
        else {
            diamond(rt, cx, 58, 62, 44, sf::Color(14, 10, 16, 220), sf::Color(222, 188, 108), 2.f);
            txt(rt, "BOSS", cx, 22, 15, sf::Color(255, 200, 120), 1);
            txt(rt, std::to_string(level + 1) + " / " + std::to_string(cfg::LEVELS), cx, 38, 30, sf::Color(255, 230, 170), 1);
            txt(rt, "A/D move  W jump (J in air = jump slash, no block)  J slash  K block (tap = deflect)  L dash  U thrust | P pause",
                cx, cfg::H - 26, 14, sf::Color(200, 200, 200, 190), 1);
        }
    }

    void dim(sf::RenderTarget& rt, sf::Uint8 a) {
        sf::RectangleShape r({ (float)cfg::W, (float)cfg::H }); r.setFillColor(sf::Color(0, 0, 0, a)); rt.draw(r);
    }

    void drawPause(sf::RenderTarget& rt) {
        using V = sf::Vector2f;
        const float W = (float)cfg::W, cx = W / 2.f;
        const sf::Color gold(222, 188, 108), k0(0, 0, 0, 0);
        dim(rt, 165);
        sf::RectangleShape p({ 520, 560 }); p.setPosition(cx - 260, 80);
        p.setFillColor(sf::Color(8, 5, 12, 228)); p.setOutlineColor(gold); p.setOutlineThickness(3); rt.draw(p);
        sf::RectangleShape in({ 500, 540 }); in.setPosition(cx - 250, 90);
        in.setFillColor(sf::Color::Transparent); in.setOutlineColor(sf::Color(222, 188, 108, 70)); in.setOutlineThickness(1); rt.draw(in);

        for (int i = 0; i < 5; i++) {                                         // blood-red glow
            float a = clockT * 1.3f + i * 1.256f;
            txt(rt, "PAUSED", cx + std::cos(a) * 3.f, 98 + std::sin(a) * 3.f, 62, sf::Color(210, 40, 40, 60), 1, true);
        }
        txt(rt, "PAUSED", cx, 98, 62, sf::Color(244, 214, 170), 1, true);
        quadV(rt, V(cx - 230, 178), V(cx, 178), V(cx, 180.5f), V(cx - 230, 180.5f), k0, gold, gold, k0);
        quadV(rt, V(cx, 178), V(cx + 230, 178), V(cx + 230, 180.5f), V(cx, 180.5f), gold, k0, k0, gold);
        diamond(rt, cx, 179, 8, 8, gold);

        std::string c1, c2;
        const bool camp = !versus && !challenge;
        if (versus) { c1 = "VERSUS  -  ROUND " + std::to_string(wins[0] + wins[1] + 1); c2 = "P1  " + std::to_string(wins[0]) + "  -  " + std::to_string(wins[1]) + "  P2"; }
        else if (challenge) { c1 = "SAMURAI CHALLENGE"; c2 = BOSSES[level].name; }
        else { c1 = "CAMPAIGN  -  CHAPTER " + std::to_string(level + 1) + " OF " + std::to_string(cfg::LEVELS); c2 = std::string(BOSSES[level].name) + "  -  " + BOSSES[level].weapon; }
        txt(rt, c1, cx, 192, 19, sf::Color(255, 190, 120), 1);
        txt(rt, c2, cx, 218, 21, sf::Color(255, 236, 200), 1);

        std::string it[5] = { "Resume", "Save Game", "Restart Duel", std::string("Music:  ") + (bgm.muted ? "OFF" : "ON"), "Quit to Main Menu" };
        for (int i = 0; i < 5; i++) {
            bool sel = (i == pauseIdx), off = (i == 1 && !camp);
            float iy = 262.f + i * 58.f;
            if (sel) {
                sf::Color r1(190, 30, 40, 185), r0(190, 30, 40, 15);
                quadV(rt, V(cx - 232 + 12, iy - 2), V(cx + 232 + 12, iy - 2), V(cx + 232, iy + 44), V(cx - 232, iy + 44), r1, r0, r0, r1);
                drawKatanaIcon(rt, cx - 246.f + std::sin(clockT * 3.f) * 3.f, iy + 21.f, 62.f, clockT);
            }
            sf::Color c = off ? sf::Color(95, 95, 95) : (sel ? sf::Color(255, 232, 150) : sf::Color(210, 210, 210));
            txt(rt, it[i], cx, iy + 4, 29, c, 1);
            if (off) txt(rt, "campaign only", cx + 225, iy + 14, 14, sf::Color(110, 110, 110), 2);
        }
        if (camp) {
            txt(rt, "Time " + fmtTime(statTime) + "      Deaths " + std::to_string(statDeaths) + "      Resurrections " + std::to_string(statRezz),
                cx, 556, 16, sf::Color(200, 200, 200), 1);
            if (saveMsgT > 0.f)
                txt(rt, saveMsg, cx, 584, 19, sf::Color(130, 235, 160, (sf::Uint8)(255 * std::min(1.f, saveMsgT))), 1);
            else
                txt(rt, save.valid
                    ? "Slot " + std::to_string(slot + 1) + ": Chapter " + std::to_string(save.level + 1) + " - " + BOSSES[save.level].name
                    : "Slot " + std::to_string(slot + 1) + ": no save yet",
                    cx, 586, 15, sf::Color(150, 150, 150), 1);
        }
        else if (saveMsgT > 0.f)
            txt(rt, saveMsg, cx, 584, 18, sf::Color(235, 170, 130, (sf::Uint8)(255 * std::min(1.f, saveMsgT))), 1);
        txt(rt, "Up/Down select    Enter confirm    P resume", cx, 612, 15, sf::Color(170, 170, 170), 1);
    }

    void drawOverlay(sf::RenderTarget& rt) {
        float cx = cfg::W / 2.f;
        if (scene == Scene::Intro) {
            std::string s = versus ? "ROUND " + std::to_string(wins[0] + wins[1] + 1) : std::string(BOSSES[level].name);
            txt(rt, s, cx, 220, 64, sf::Color(255, 230, 190), 1, true);
            if (!versus) {
                txt(rt, "Boss " + std::to_string(level + 1) + " of " + std::to_string(cfg::LEVELS) + "   -   " + BOSSES[level].weapon, cx, 300, 26, sf::Color(220, 180, 140), 1);
                txt(rt, BOSSES[level].newTip, cx, 346, 21, sf::Color(255, 190, 120), 1);
            }
            if (introT < 0.5f) txt(rt, "FIGHT!", cx, 400, 48, sf::Color(255, 90, 70), 1);
        }
        else if (scene == Scene::Paused) drawPause(rt);
        else if (scene == Scene::Revive) {
            dim(rt, 170);
            std::string who = versus ? F[reviveWho].name : "YOU";
            txt(rt, who + (versus ? " HAS FALLEN" : " HAVE FALLEN"), cx, 220, 60, sf::Color(190, 20, 20), 1, true);
            txt(rt, "Two lives... will you rise again?", cx, 300, 26, sf::Color(220, 200, 230), 1);
            txt(rt, "[ENTER]  Resurrect  ", cx, 380, 34, sf::Color(190, 150, 255), 1);
            txt(rt, "[BACKSPACE]  Accept death", cx, 440, 34, sf::Color(230, 230, 230), 1);
        }
        else if (scene == Scene::RoundEnd) drawRoundEnd(rt);
    }

    void drawRoundEnd(sf::RenderTarget& rt) {
        using V = sf::Vector2f;
        const float W = (float)cfg::W, cx = W / 2.f;
        const float k = std::clamp(roundT / 0.6f, 0.f, 1.f);
        const bool lost = !versus && winner == 1;
        dim(rt, (sf::Uint8)(175 * k));

        sf::Color e(0, 0, 0, 0);
        sf::Color m = lost ? sf::Color(60, 0, 6, (sf::Uint8)(215 * k)) : sf::Color(20, 10, 4, (sf::Uint8)(215 * k));
        quadV(rt, V(0, 150), V(W, 150), V(W, 290), V(0, 290), e, e, m, m);
        quadV(rt, V(0, 290), V(W, 290), V(W, 430), V(0, 430), m, m, e, e);

        std::string title, sub2, prompt;
        if (versus) {
            std::string w = winner == 0 ? "PLAYER 1" : "PLAYER 2";
            title = matchOver ? w + " WINS THE MATCH" : w + " TAKES THE ROUND";
            sub2 = "Score  " + std::to_string(wins[0]) + " - " + std::to_string(wins[1]) +
                (matchOver ? "" : "        Next: Round " + std::to_string(wins[0] + wins[1] + 1));
            prompt = matchOver ? "ENTER - Back to the menu" : "ENTER - Draw your blades for the next round";
        }
        else if (lost) {
            title = "YOU DIED";
            sub2 = std::string(BOSSES[level].name) + " stands over your fallen blade.";
            prompt = "ENTER - Rise again          P - menu";
        }
        else {
            title = VICTORY_TITLE[level];
            if (challenge) { sub2 = "Challenge complete."; prompt = "ENTER - Back to the menu"; }
            else if (level >= cfg::LEVELS - 1) { sub2 = "The long night is almost over..."; prompt = "ENTER - Face the dawn"; }
            else {
                sub2 = std::string("Next foe: ") + BOSSES[level + 1].name + "  -  " + BOSSES[level + 1].weapon;
                prompt = "ENTER - Walk on to the next duel";
            }
        }
        sf::Color tc = lost ? sf::Color(200, 24, 28) : sf::Color(255, 226, 140);
        float punch = 1.f + 0.35f * std::max(0.f, 1.f - roundT / 0.25f);
        unsigned size = (unsigned)((lost ? 100 : 60) * punch);
        float ty = lost ? 160.f : 185.f;
        for (int i = 0; i < 4; i++) {                                    // glow
            float a = clockT * 1.4f + i * 1.57f;
            sf::Color g = tc; g.a = (sf::Uint8)(55 * k);
            txt(rt, title, cx + std::cos(a) * 3.f, ty + std::sin(a) * 3.f, size, g, 1, true);
        }
        sf::Color t2 = tc; t2.a = (sf::Uint8)(255 * k);
        txt(rt, title, cx, ty, size, t2, 1, true);

        float half = 380.f * k;                                          // divider grows from the centre
        sf::Color d1(222, 188, 108, 255), d0(222, 188, 108, 0);
        quadV(rt, V(cx - half, 292), V(cx, 292), V(cx, 294.5f), V(cx - half, 294.5f), d0, d1, d1, d0);
        quadV(rt, V(cx, 292), V(cx + half, 292), V(cx + half, 294.5f), V(cx, 294.5f), d1, d0, d0, d1);
        diamond(rt, cx, 293, 7, 7, sf::Color(222, 188, 108, (sf::Uint8)(255 * k)));

        if (roundT > 0.4f) {
            float a2 = std::clamp((roundT - 0.4f) / 0.5f, 0.f, 1.f);
            txt(rt, endLine, cx, 308, 26, sf::Color(255, 238, 215, (sf::Uint8)(255 * a2)), 1);
            txt(rt, sub2, cx, 352, 22, sf::Color(255, 190, 130, (sf::Uint8)(255 * a2)), 1);
        }
        if (roundT > 1.0f)
            txt(rt, prompt, cx, 456, 24, sf::Color(255, 255, 255, (sf::Uint8)(170 + 85 * std::sin(clockT * 4.f))), 1);
    }

    // falling sakura petals in screen space
    void petals(sf::RenderTarget& rt, int n, sf::Color c, float speed) {
        const float W = (float)cfg::W, H = (float)cfg::H;
        for (int i = 0; i < n; i++) {
            float sp = speed * (0.6f + 0.8f * hash01(i, 7.f));
            float yy = std::fmod(hash01(i, 8.f) * (H + 60.f) + clockT * sp, H + 60.f) - 30.f;
            float xx = std::fmod(hash01(i, 9.f) * (W + 120.f) + clockT * 40.f * (0.5f + hash01(i, 3.f)), W + 120.f) - 60.f + std::sin(clockT * 1.3f + i) * 24.f;
            float r = 3.f + hash01(i, 10.f) * 3.f;
            sf::CircleShape p(r); p.setOrigin(r, r); p.setScale(1.f, 0.55f);
            p.setRotation(clockT * 70.f + i * 37.f); p.setPosition(xx, yy); p.setFillColor(c);
            rt.draw(p);
        }
    }

    //  PRE-LEVEL CHAPTER SCREEN (story + short "new this level" box)  /  HOW TO PLAY
   
    void drawTutorial(sf::RenderTarget& rt) {
        if (versusBrief) { drawVersusBrief(rt); return; }
        if (!tutFromMenu) { drawChapter(rt); return; }
        sf::VertexArray va(sf::Quads, 4);
        sf::Color top(12, 12, 24), bot(40, 20, 34);
        va[0] = sf::Vertex(sf::Vector2f(0, 0), top);
        va[1] = sf::Vertex(sf::Vector2f((float)cfg::W, 0), top);
        va[2] = sf::Vertex(sf::Vector2f((float)cfg::W, (float)cfg::H), bot);
        va[3] = sf::Vertex(sf::Vector2f(0, (float)cfg::H), bot);
        rt.draw(va);
        const TutPage& pg = TUT[std::clamp(tutPage, 0, (int)TUT.size() - 1)];
        sf::RectangleShape panel({ 1160, 560 }); panel.setPosition(60, 80);
        panel.setFillColor(sf::Color(0, 0, 0, 140)); panel.setOutlineColor(sf::Color(200, 170, 140, 180)); panel.setOutlineThickness(2);
        rt.draw(panel);
        txt(rt, "HOW TO PLAY", cfg::W / 2.f, 20, 30, sf::Color(200, 170, 150), 1);
        txt(rt, pg.title, 90, 100, 48, sf::Color(255, 220, 160), 0, true);
        bool hasImg = tutPage >= 2 && tutPage - 2 < cfg::LEVELS && as.tutImg[tutPage - 2];
        if (hasImg) {
            const sf::Texture& tx = *as.tutImg[tutPage-2];
            sf::Sprite s(tx);
            float k = std::min(340.f / tx.getSize().x, 300.f / tx.getSize().y);
            s.setScale(k, k); s.setPosition(860, 170);
            rt.draw(s);
        }
        float y = 175;
        for (const auto& line : pg.lines) { txt(rt, line, 90, y, 21, sf::Color(235, 235, 235)); y += 32; }
        txt(rt, "Left/Right: change page    Enter: next    P: back   (" + std::to_string(tutPage + 1) + "/" + std::to_string(TUT.size()) + ")",
            cfg::W / 2.f, 660, 20, sf::Color(200, 200, 200), 1);
    }

    void drawChapter(sf::RenderTarget& rt) {
        const float W = (float)cfg::W, H = (float)cfg::H, cx = W / 2.f;
        const LevelInfo& li = LEVELINFO[level];
        const BossDef& b = BOSSES[level];
        if (as.bgBoss[level]) {
            sf::Sprite s(*as.bgBoss[level]);
            s.setScale(W / as.bgBoss[level]->getSize().x, H / as.bgBoss[level]->getSize().y);
            rt.draw(s);
        }
        else {                                                   // the level's own arena slowly panning behind the text
            camX = W / 2.f + 160.f + 120.f * std::sin(clockT * 0.15f);
            sf::View v(sf::FloatRect(camX - W / 2.f, 0, W, H));
            rt.setView(v);
            drawArena(rt, level);
            rt.setView(rt.getDefaultView());
        }
        dim(rt, 165);
        sf::RectangleShape panel({ 1160, 620 }); panel.setPosition(60, 50);
        panel.setFillColor(sf::Color(6, 4, 10, 175)); panel.setOutlineColor(sf::Color(222, 188, 108, 200)); panel.setOutlineThickness(3);
        rt.draw(panel);
        sf::RectangleShape inner({ 1140, 600 }); inner.setPosition(70, 60);
        inner.setFillColor(sf::Color::Transparent); inner.setOutlineColor(sf::Color(222, 188, 108, 70)); inner.setOutlineThickness(1);
        rt.draw(inner);

        txt(rt, li.chapter, cx, 76, 40, sf::Color(255, 224, 168), 1, true);
        quadV(rt, { 110, 132 }, { cx, 132 }, { cx, 134.5f }, { 110, 134.5f }, sf::Color(222, 188, 108, 0), sf::Color(222, 188, 108, 255), sf::Color(222, 188, 108, 255), sf::Color(222, 188, 108, 0));
        quadV(rt, { cx, 132 }, { W - 110, 132 }, { W - 110, 134.5f }, { cx, 134.5f }, sf::Color(222, 188, 108, 255), sf::Color(222, 188, 108, 0), sf::Color(222, 188, 108, 0), sf::Color(222, 188, 108, 255));
        diamond(rt, cx, 133, 9, 9, sf::Color(222, 188, 108));

        int shown = (int)(loreT * cfg::LORE_CPS);                // typewriter
        float y = 156;
        for (const auto& line : li.lore) {
            int n = std::clamp(shown, 0, (int)line.size());
            if (n > 0) txt(rt, line.substr(0, (size_t)n), 108, y, 20, sf::Color(238, 228, 212));
            shown -= (int)line.size();
            y += 36;
        }
        bool done = loreT * cfg::LORE_CPS >= (float)loreChars(level);

        // short box: what is new this level
        sf::RectangleShape box({ 1040, 214 }); box.setPosition(100, 340);
        box.setFillColor(sf::Color(0, 0, 0, 120)); box.setOutlineColor(bossColor(level)); box.setOutlineThickness(2);
        rt.draw(box);
        txt(rt, "YOUR FOE", 120, 352, 18, sf::Color(255, 170, 110));
        txt(rt, std::string(b.name) + "   -   " + b.weapon, 120, 374, 28, sf::Color(255, 236, 200), 0, true);
        txt(rt, "NEW THIS LEVEL", 120, 424, 18, sf::Color(255, 170, 110));
        float ty = 450;
        for (const auto& line : li.news) { txt(rt, line, 120, ty, 18, sf::Color(240, 240, 240)); ty += 32; }
        if (as.tutImg[level]) {
            const sf::Texture& tx = *as.tutImg[level];
            sf::Sprite s(tx);
            float k = std::min(150.f / tx.getSize().x, 120.f / tx.getSize().y);
            s.setScale(k, k); s.setPosition(980, 420);
            rt.draw(s);
        }
        std::string hint = done ? "ENTER - Begin the duel          P - menu" : "ENTER - Skip the text          P - menu";
        txt(rt, hint, cx, 600, 22, sf::Color(255, 255, 255, done ? (sf::Uint8)(170 + 85 * std::sin(clockT * 4.f)) : (sf::Uint8)200), 1);
    }

    void drawVersusBrief(sf::RenderTarget& rt) {
        using V = sf::Vector2f;
        using Rows = std::vector<std::pair<std::string, std::string>>;
        const float W = (float)cfg::W, H = (float)cfg::H, cx = W / 2.f;
        sf::Color top(12, 12, 28), bot(44, 18, 34);
        quadV(rt, V(0, 0), V(W, 0), V(W, H), V(0, H), top, top, bot, bot);
        petals(rt, 24, sf::Color(255, 170, 190, 160), 36.f);
        txt(rt, "2 PLAYER - VERSUS", cx, 22, 52, sf::Color(255, 220, 160), 1, true);
        txt(rt, "Break your rival's posture, then land the DEATHBLOW. First to win 2 rounds takes the match.",
            cx, 90, 19, sf::Color(220, 220, 220), 1);

        auto panel = [&](float px, const std::string& head, sf::Color col, const Rows& rows) {
            sf::RectangleShape r({ 450, 400 }); r.setPosition(px, 130);
            r.setFillColor(sf::Color(0, 0, 0, 150)); r.setOutlineColor(col); r.setOutlineThickness(2); rt.draw(r);
            txt(rt, head, px + 225, 142, 30, col, 1, true);
            float y = 196;
            for (const auto& row : rows) {
                txt(rt, row.first, px + 22, y, 20, sf::Color(255, 224, 150));
                txt(rt, row.second, px + 165, y, 19, sf::Color(235, 235, 235));
                y += 38;
            }
            };
        panel(40, "PLAYER 1", sf::Color(110, 160, 240), {
     {"A / D", "Move"}, {"W", "Jump (leap over the foe)"}, {"J", "Slash (tap again: combo)"},
     {"J in the air", "Jump slash"}, {"K", "Block (tap = DEFLECT)"},
     {"L", "Dash (at a thrust = mikiri)"}, {"U", "Thrust (perilous)"} });
        panel(790, "PLAYER 2", sf::Color(230, 100, 100), {
            {"Left / Right", "Move"}, {"Up", "Jump (leap over the foe)"}, {", (comma)", "Slash (tap again: combo)"},
            {", in the air", "Jump slash"}, {". (period)", "Block (tap = DEFLECT)"},
            {"/", "Dash (at a thrust = mikiri)"}, {";", "Thrust (perilous)"} });

        Fighter a, b;                                                    // preview of both uniforms
        setupFighter(a, "PLAYER 1", &as.players[0], sf::Color(80, 140, 230), 565.f, 1);
        setupFighter(b, "PLAYER 2", &as.players[1], sf::Color(220, 80, 80), 715.f, -1);
        b.style = 7;
        a.animT = clockT; b.animT = clockT + 1.3f;
        drawFighter(rt, a); drawFighter(rt, b);
        txt(rt, "ENTER - start the duel          P - menu", cx, 678, 22,
            sf::Color(255, 255, 255, (sf::Uint8)(170 + 85 * std::sin(clockT * 4.f))), 1);
    }

    // sleek katana icon, pointing right. (x,y) = pommel end.
    void drawKatanaIcon(sf::RenderTarget& rt, float x, float y, float len, float t) {
        using V = sf::Vector2f;
        const float hl = len * 0.27f, bx = x + hl + 6.f, tx = x + len;
        const sf::Color gold(196, 160, 82);
        limb(rt, V(bx, y + 5.f), V(tx - 4.f, y + 5.f), 2.f, sf::Color(0, 0, 0, 90));          // soft shadow
        sf::RectangleShape h({ hl, 6.f }); h.setOrigin(0.f, 3.f); h.setPosition(x, y);          // tsuka (handle)
        h.setFillColor(sf::Color(22, 18, 24)); rt.draw(h);
        for (int i = 0; i < 5; i++) {                                                           // diamond ito wrap
            float px = x + hl * (i + 0.5f) / 5.f;
            limb(rt, V(px - 3.f, y - 2.6f), V(px + 3.f, y + 2.6f), 1.1f, sf::Color(120, 24, 32));
            limb(rt, V(px - 3.f, y + 2.6f), V(px + 3.f, y - 2.6f), 1.1f, sf::Color(120, 24, 32));
        }
        sf::RectangleShape ka({ 4.f, 8.f }); ka.setOrigin(2.f, 4.f); ka.setPosition(x, y);      // kashira (pommel cap)
        ka.setFillColor(gold); rt.draw(ka);
        sf::CircleShape ts(10.f, 8); ts.setOrigin(10.f, 10.f); ts.setScale(0.32f, 1.f);         // tsuba (guard)
        ts.setPosition(x + hl + 1.f, y); ts.setFillColor(sf::Color(28, 24, 30));
        ts.setOutlineColor(gold); ts.setOutlineThickness(1.4f); rt.draw(ts);
        sf::RectangleShape hb({ 5.f, 6.f }); hb.setOrigin(0.f, 3.f); hb.setPosition(x + hl + 4.f, y);   // habaki (collar)
        hb.setFillColor(gold); rt.draw(hb);
        sf::ConvexShape sp(4);                                                                  // blade: dark spine half
        sp.setPoint(0, V(bx, y - 3.4f)); sp.setPoint(1, V(tx - 10.f, y - 3.4f));
        sp.setPoint(2, V(tx, y - 0.2f));  sp.setPoint(3, V(bx, y - 0.2f));
        sp.setFillColor(sf::Color(118, 126, 142)); rt.draw(sp);
        sf::ConvexShape ed(4);                                                                  // bright edge half
        ed.setPoint(0, V(bx, y - 0.2f)); ed.setPoint(1, V(tx, y - 0.2f));
        ed.setPoint(2, V(tx - 12.f, y + 3.f)); ed.setPoint(3, V(bx, y + 3.f));
        ed.setFillColor(sf::Color(226, 232, 242)); rt.draw(ed);
        limb(rt, V(bx + 4.f, y + 1.5f), V(tx - 16.f, y + 1.5f), 1.f, sf::Color(255, 255, 255, 120));   // hamon line
        float g = std::fmod(t * 0.6f, 1.6f);                                                    // slow glint
        if (g < 1.f) {
            float gx = bx + (tx - bx - 8.f) * g, a = std::sin(3.1416f * g);
            limb(rt, V(gx - 5.f, y + 2.f), V(gx + 5.f, y - 2.8f), 1.6f, sf::Color(255, 255, 255, (sf::Uint8)(210 * a)));
        }
    }

 
    //  MAIN MENU: live arena + the selected boss in his gear facing the ronin

    void drawStats(sf::RenderTarget& rt, float MX, float MR, float MC) {
        using V = sf::Vector2f;
        const sf::Color gold(222, 188, 108);
        txt(rt, "CAMPAIGN STATS", MX, 160, 30, gold, 0, true);
        if (runs.empty()) {
            txt(rt, "No completed runs yet.", MC, 300, 24, sf::Color(200, 200, 200), 1);
            txt(rt, "Defeat the Sword Demon to record your first run.", MC, 340, 18, sf::Color(150, 150, 150), 1);
            txt(rt, "Enter / Esc back", MC, 676, 15, sf::Color(170, 170, 170), 1);
            return;
        }
        int n = (int)runs.size(), bestT = 0, bestD = 0;
        for (int i = 1; i < n; i++) {
            if (runs[i].time < runs[bestT].time) bestT = i;
            if (runs[i].deaths < runs[bestD].deaths) bestD = i;
        }
        txt(rt, "Runs: " + std::to_string(n) + "     Best time: " + fmtTime(runs[bestT].time) +
            "     Fewest deaths: " + std::to_string(runs[bestD].deaths), MX, 208, 17, sf::Color(255, 190, 130));

        const float cNo = MX, cTime = MX + 70, cDeath = MX + 200, cRez = MX + 340;
        txt(rt, "RUN", cNo, 250, 16, sf::Color(255, 170, 110));
        txt(rt, "TIME", cTime, 250, 16, sf::Color(255, 170, 110));
        txt(rt, "DEATHS", cDeath, 250, 16, sf::Color(255, 170, 110));
        txt(rt, "RESURRECTIONS", cRez, 250, 16, sf::Color(255, 170, 110));
        quadV(rt, V(MX, 274), V(MR, 274), V(MR, 276), V(MX, 276), sf::Color(222, 188, 108, 200), sf::Color(222, 188, 108, 0), sf::Color(222, 188, 108, 0), sf::Color(222, 188, 108, 200));

        for (int j = 0; j < 8; j++) {
            int idx = n - 1 - statsScroll - j;                    // newest first
            if (idx < 0) break;
            float y = 290.f + j * 40.f;
            const RunRecord& r = runs[idx];
            sf::Color c(230, 230, 230);
            if (idx == bestT) c = sf::Color(255, 226, 120);       // gold = best time
            txt(rt, "#" + std::to_string(idx + 1), cNo, y, 22, c);
            txt(rt, fmtTime(r.time) + (idx == bestT ? "  *" : ""), cTime, y, 22, c);
            txt(rt, std::to_string(r.deaths) + (idx == bestD ? "  *" : ""), cDeath, y, 22, idx == bestD ? sf::Color(130, 235, 160) : c);
            txt(rt, std::to_string(r.rezz), cRez, y, 22, c);
        }
        txt(rt, "* = personal best     Up/Down scroll   |   Enter / Esc back", MC, 676, 15, sf::Color(170, 170, 170), 1);

    }
    void drawMenu(sf::RenderTarget& rt) {
        using V = sf::Vector2f;
        const float W = (float)cfg::W, H = (float)cfg::H;
        const float VW = 800.f;                                   // video area width
        const float MX = 780.f, MR = 1250.f, MC = (MX + MR) / 2.f;  // menu left / right / centre
        const sf::Color gold(222, 188, 108);

        // ---- left: video (frame sequence) or live arena fallback ----
        if (as.bgMenu) {
            const sf::Texture& tx = *as.bgMenu;
            float tw = (float)tx.getSize().x, th = (float)tx.getSize().y;
            float asp = VW / H, cw = std::min(tw, th * asp), ch = cw / asp;   // centre-crop to fill the left area
            sf::Sprite s(tx);
            s.setTextureRect(sf::IntRect((int)((tw - cw) / 2), (int)((th - ch) / 2), (int)cw, (int)ch));
            s.setScale(VW / cw, H / ch);
            rt.draw(s);
        }
        else {
            camX = W / 2.f + 160.f + 160.f * std::sin(clockT * 0.12f);
            float cl = camX - W / 2.f;
            sf::View v(sf::FloatRect(cl, 0, VW, H));
            v.setViewport(sf::FloatRect(0, 0, VW / W, 1.f));
            rt.setView(v);
            drawArena(rt, startLevel);
            const BossDef& b = BOSSES[startLevel];
            setupFighter(menuHero, "WOLF", &as.players[0], sf::Color(80, 140, 230), cl + 210.f, 1);
            menuHero.animT = clockT;
            setupFighter(menuFoe, b.name, &as.bosses[startLevel], bossColor(startLevel), cl + 600.f, -1);
            menuFoe.isBoss = true; menuFoe.heightMul = b.height; menuFoe.widthMul = b.width; menuFoe.style = startLevel + 1;
            menuFoe.rangeMul = b.range; menuFoe.animT = clockT + 1.7f;
            drawFighter(rt, menuHero); drawFighter(rt, menuFoe);
            rt.setView(rt.getDefaultView());
        }
        petals(rt, 14, sf::Color(255, 170, 190, 170), 38.f);

        // ---- black that melts into the video ----
        sf::Color k0(0, 0, 0, 0), k1(0, 0, 0, 255);
        quadV(rt, V(VW - 320, 0), V(VW, 0), V(VW, H), V(VW - 320, H), k0, k1, k1, k0);
        sf::RectangleShape blk({ W - VW, H }); blk.setPosition(VW, 0); blk.setFillColor(sf::Color::Black); rt.draw(blk);
        quadV(rt, V(0, 0), V(VW, 0), V(VW, 90), V(0, 90), sf::Color(0, 0, 0, 150), sf::Color(0, 0, 0, 150), k0, k0);
        quadV(rt, V(0, H - 90), V(VW, H - 90), V(VW, H), V(0, H), k0, k0, sf::Color(0, 0, 0, 150), sf::Color(0, 0, 0, 150));
        quadV(rt, V(VW, H * 0.45f), V(W, H * 0.45f), V(W, H), V(VW, H), k0, k0, sf::Color(70, 6, 14, 150), sf::Color(70, 6, 14, 150));

        // small logo, top right
        txt(rt, "SEKIRO", MC, 40, 50, sf::Color(244, 214, 170), 1, true);
        txt(rt, "Shadows Die Twice", MC, 100, 24, sf::Color(214, 176, 150), 1, true);
        quadV(rt, V(MX, 140), V(MC, 140), V(MC, 142.f), V(MX, 142.f), k0, gold, gold, k0);
        quadV(rt, V(MC, 140), V(MR, 140), V(MR, 142.f), V(MC, 142.f), gold, k0, k0, gold);

        auto row = [&](float iy, float h, bool sel) {
            if (!sel) return;
            sf::Color r1(190, 30, 40, 175), r0(190, 30, 40, 10);
            quadV(rt, V(MX - 30 + 12, iy - 2), V(MR + 12, iy - 2), V(MR, iy + h), V(MX - 30, iy + h), r1, r0, r0, r1);
            float wob = std::sin(clockT * 3.f) * 3.f, cy = iy + h * 0.5f - 2.f;
            drawKatanaIcon(rt, MX - 100.f + wob, cy, 66.f, clockT);
            };

        if (menuPage == 0) {
            const char* labels[5] = { "Campaign", "Versus", "Samurai Challenge", "How to Play", "Exit" };
            for (int i = 0; i < 5; i++) {
                bool sel = (i == menuIdx);
                float iy = 392.f + i * 50.f;
                row(iy, 44.f, sel);
                txt(rt, labels[i], MX, iy + 4, 27, sel ? sf::Color(255, 232, 150) : sf::Color(206, 206, 206));
                if (i == 2)
                    txt(rt, "< " + std::to_string(startLevel + 1) + " - " + BOSSES[startLevel].name + " >", MR, iy + 13, 18,
                        sel ? sf::Color(255, 190, 130) : sf::Color(150, 150, 150), 2);
            }
            txt(rt, "Up/Down  Enter   |   Left/Right or 1-6: pick samurai", MC, 676, 15, sf::Color(170, 170, 170), 1);
        }
        else if (menuPage == 1) {
            txt(rt, "CAMPAIGN  -  SAVE SLOT  <  " + std::to_string(slot + 1) + " / " + std::to_string(SAVE_SLOTS) + "  >", MX, 362, 20, gold);
            const char* labels[4] = { "Load Campaign", "Start New Campaign", "Stats", "Back" };
            for (int i = 0; i < 4; i++) {
                bool sel = (i == subIdx), off = (i == 0 && !save.valid);
                float iy = 392.f + i * 64.f;
                row(iy, 56.f, sel);
                sf::Color c = off ? sf::Color(95, 95, 95) : (sel ? sf::Color(255, 232, 150) : sf::Color(206, 206, 206));
                txt(rt, labels[i], MX, iy + 4, 27, c);
                if (i == 0) {
                    std::string info = save.valid
                        ? "Chapter " + std::to_string(save.level + 1) + " - " + BOSSES[save.level].name + "  |  " + fmtTime(save.time) + "  |  " + std::to_string(save.deaths) + " deaths"
                        : "Slot " + std::to_string(slot + 1) + " is empty";
                    txt(rt, info, MX, iy + 34, 15, off ? sf::Color(95, 95, 95) : sf::Color(255, 190, 130));
                }
                if (i == 1 && save.valid)
                    txt(rt, "overwrites slot " + std::to_string(slot + 1), MR, iy + 13, 16, sf::Color(235, 150, 120), 2);
                if (i == 2) txt(rt, std::to_string(runs.size()) + " run(s)", MR, iy + 13, 18, sf::Color(150, 150, 150), 2);
            }
            txt(rt, "Left/Right slot   Enter confirm   |   Esc / P back", MC, 676, 15, sf::Color(170, 170, 170), 1);
        }
        else drawStats(rt, MX, MR, MC);
    }

    // ENDING
    void drawVictory(sf::RenderTarget& rt) {
        using V = sf::Vector2f;
        const float W = (float)cfg::W, H = (float)cfg::H, G = cfg::GROUND, t = victoryT;
        const float rise = 1.f - std::pow(1.f - std::clamp(t / 8.f, 0.f, 1.f), 3.f);
        sf::Color topC = mixc(sf::Color(10, 8, 30), sf::Color(74, 112, 196), rise);
        sf::Color botC = mixc(sf::Color(70, 22, 44), sf::Color(255, 196, 120), rise);
        quadV(rt, V(0, 0), V(W, 0), V(W, H), V(0, H), topC, topC, botC, botC);
        for (int i = 0; i < 70; i++) {                           // stars fade out at dawn
            float tw = 0.5f + 0.5f * std::sin(clockT * 2.f + i);
            sf::RectangleShape st({ 2.f, 2.f }); st.setPosition(hash01(i, 21.f) * W, hash01(i, 22.f) * H * 0.55f);
            st.setFillColor(sf::Color(255, 255, 240, (sf::Uint8)(210 * (1.f - rise) * tw))); rt.draw(st);
        }
        float sx = W * 0.64f, sy = lerp(G + 150.f, 250.f, rise);
        for (int k = 0; k < 18; k++) {                           // rotating sun rays
            float ang = clockT * 0.06f + k * (6.2832f / 18.f);
            V d(std::cos(ang), std::sin(ang));
            limb(rt, V(sx, sy) + d * 110.f, V(sx, sy) + d * 900.f, 16.f, sf::Color(255, 232, 170, (sf::Uint8)(20 * rise)));
        }
        for (int i = 0; i < 3; i++) {
            float r = 200.f - i * 55.f;
            sf::CircleShape c(r, 48); c.setOrigin(r, r); c.setPosition(sx, sy);
            c.setFillColor(sf::Color(255, 214, 140, (sf::Uint8)(40 + i * 60))); rt.draw(c);
        }
        sf::CircleShape sun(80.f, 48); sun.setOrigin(80.f, 80.f); sun.setPosition(sx, sy); sun.setFillColor(sf::Color(255, 244, 214)); rt.draw(sun);
        auto ridgeS = [&](float step, float hgt, sf::Color c, float seed) {
            sf::ConvexShape tri(3); tri.setFillColor(c);
            for (float x = -step; x < W + step; x += step) {
                float h = hgt * (0.55f + 0.45f * std::fabs(std::sin(x / step * 1.7f + seed)));
                tri.setPoint(0, V(x - step, G)); tri.setPoint(1, V(x, G - h)); tri.setPoint(2, V(x + step, G));
                rt.draw(tri);
            }
            };
        ridgeS(230.f, 300.f, mixc(sf::Color(40, 24, 58), sf::Color(110, 100, 150), rise), 2.f);
        ridgeS(170.f, 190.f, mixc(sf::Color(24, 14, 36), sf::Color(70, 64, 100), rise), 5.f);
        sf::RectangleShape ground({ W, H - G + 4 }); ground.setPosition(0, G);
        ground.setFillColor(mixc(sf::Color(16, 10, 18), sf::Color(52, 40, 44), rise)); rt.draw(ground);
        quadV(rt, V(0, G), V(W, G), V(W, G + 5), V(0, G + 5), sf::Color(255, 220, 160, (sf::Uint8)(120 * rise)), sf::Color(255, 220, 160, (sf::Uint8)(120 * rise)), sf::Color(255, 220, 160, 0), sf::Color(255, 220, 160, 0));

        Fighter vh;                                              // the ronin, standing in the dawn
        setupFighter(vh, "WOLF", &as.players[0], sf::Color(80, 140, 230), 240.f, 1);
        vh.animT = clockT;
        drawFighter(rt, vh);
        petals(rt, 40, sf::Color(255, 176, 196, 210), 50.f);

        float a = std::clamp(t / 1.2f, 0.f, 1.f);
        const float tx = 800.f;
        for (int i = 0; i < 6; i++) {
            float an = clockT * 1.5f + i * 1.047f;
            txt(rt, "VICTORY", tx + std::cos(an) * 3.f, 40 + std::sin(an) * 3.f, 108, sf::Color(255, 190, 60, (sf::Uint8)(70 * a)), 1, true);
        }
        txt(rt, "VICTORY", tx, 40, 108, sf::Color(255, 226, 140, (sf::Uint8)(255 * a)), 1, true);
        txt(rt, "THE LONG NIGHT ENDS", tx, 164, 26, sf::Color(255, 236, 200, (sf::Uint8)(255 * a)), 1, true);

        int shown = (int)(std::max(0.f, t - 1.2f) * cfg::LORE_CPS);   // typewriter epilogue
        float y = 218;
        for (const auto& line : EPILOGUE) {
            int n = std::clamp(shown, 0, (int)line.size());
            if (n > 0) txt(rt, line.substr(0, (size_t)n), tx, y, 20, sf::Color(255, 246, 228), 1);
            shown -= (int)line.size();
            y += 34;
        }
        if (t > 6.f) {                                           // stats panel
            float k = std::clamp((t - 6.f) / 1.0f, 0.f, 1.f);
            auto A = [&](sf::Color c) { c.a = (sf::Uint8)(c.a * k); return c; };
            std::string labels[3] = { "TIME", "DEATHS", "RESURRECTIONS" };
            std::string vals[3] = { fmtTime(statTime), std::to_string(statDeaths), std::to_string(statRezz) };
            std::string prev[3] = {
                victPrevBestT < 0.f ? "First clear!" : "Best: " + fmtTime(victPrevBestT),
                victPrevBestD < 0 ? "First clear!" : "Best: " + std::to_string(victPrevBestD),
                "" };
            bool isBest[3] = { victBestTime, victBestDeaths, false };
            for (int i = 0; i < 3; i++) {
                float bx = tx - 330.f + i * 230.f;
                sf::RectangleShape r({ 210, 100 }); r.setPosition(bx, 372);
                r.setFillColor(A(sf::Color(8, 4, 12, 150)));
                r.setOutlineColor(A(isBest[i] ? sf::Color(130, 235, 160, 255) : sf::Color(222, 188, 108, 220)));
                r.setOutlineThickness(2); rt.draw(r);
                txt(rt, labels[i], bx + 105, 380, 15, A(sf::Color(255, 190, 120)), 1);
                // NOTE: regular font (title=false) - the title font has no digits
                txt(rt, vals[i], bx + 105, 402, 38, A(isBest[i] ? sf::Color(170, 255, 190) : sf::Color(255, 240, 210)), 1, false);
                if (isBest[i] && victPrevBestT >= 0.f)
                    txt(rt, "NEW BEST!", bx + 105, 446, 14, A(sf::Color(130, 235, 160)), 1);
                else if (!prev[i].empty())
                    txt(rt, prev[i], bx + 105, 446, 14, A(sf::Color(170, 170, 170)), 1);
            }
            txt(rt, "Run #" + std::to_string(victRunNo) + " saved to Stats", tx, 478, 16, A(sf::Color(200, 200, 200)), 1);
            const char* rank = (statDeaths == 0 && statRezz == 0) ? "FLAWLESS DEMON-SLAYER" : (statDeaths <= 2 ? "MASTER SWORDSMAN" : (statDeaths <= 6 ? "SEASONED SAMURAI" : "STUBBORN RONIN"));
            txt(rt, "RANK:", tx - 20.f, 508, 30, A(sf::Color(255, 222, 120)), 2, false);
            txt(rt, rank, tx - 6.f, 508, 32, A(sf::Color(255, 222, 120)), 0, true);
        }
        if (t > 7.5f) txt(rt, "Press ENTER to return to the menu", tx, 590, 22, sf::Color(255, 255, 255, (sf::Uint8)(150 + 105 * std::sin(clockT * 4.f))), 1);
    }
};

int main() {
    Game g;
    g.run();
    return 0;
}