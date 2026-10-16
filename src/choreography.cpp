// choreography.cpp - see choreography.h.
#include "choreography.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace {

float wrapAngle(float a) {  // to (-pi, pi]
    while (a > kPi) a -= 2.0f * kPi;
    while (a <= -kPi) a += 2.0f * kPi;
    return a;
}

std::string number(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", v);
    std::string s(text);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

int degrees(float yaw) {
    int d = static_cast<int>(std::lround(yaw * 180.0f / kPi)) % 360;
    if (d < 0) d += 360;
    return d;
}

// A small reader over the text form.
struct Reader {
    const std::string& s;
    size_t i = 0;
    bool done() const { return i >= s.size(); }
    bool take(char c) {
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    bool integer(int* out) {
        const char* start = s.c_str() + i;
        char* end = nullptr;
        const long v = std::strtol(start, &end, 10);
        if (end == start) return false;
        i += static_cast<size_t>(end - start);
        *out = static_cast<int>(v);
        return true;
    }
    bool real(double* out) {
        const char* start = s.c_str() + i;
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start) return false;
        i += static_cast<size_t>(end - start);
        *out = v;
        return true;
    }
    // "(x,z,facing)"
    bool spot(float* x, float* z, float* yaw) {
        double a = 0.0, b = 0.0, c = 0.0;
        if (!take('(') || !real(&a) || !take(',') || !real(&b) || !take(',') || !real(&c) || !take(')')) return false;
        *x = static_cast<float>(a);
        *z = static_cast<float>(b);
        *yaw = wrapAngle(static_cast<float>(c) * kPi / 180.0f);
        return true;
    }
};

}  // namespace

bool Choreography::hasKeys(int juggler) const {
    return juggler >= 0 && juggler < static_cast<int>(keys.size()) && !keys[static_cast<size_t>(juggler)].empty();
}

WalkLink Choreography::link(int juggler) const {
    if (juggler < 0 || juggler >= static_cast<int>(links.size())) return WalkLink();
    return links[static_cast<size_t>(juggler)];
}

bool Choreography::walks(int juggler) const {
    for (int depth = 0; depth <= 64 && juggler >= 0; ++depth) {
        const WalkLink l = link(juggler);
        if (!l.active()) return hasKeys(juggler);
        juggler = l.leader;
    }
    return false;  // (a loop of links: shouldn't happen)
}

bool Choreography::dependsOn(int juggler, int other) const {
    for (int depth = 0; depth <= 64 && juggler >= 0; ++depth) {
        if (juggler == other) return true;
        juggler = link(juggler).leader;
    }
    return false;
}

namespace {
int gcd(int a, int b) {
    a = std::abs(a);
    b = std::abs(b);
    while (b != 0) {
        const int t = a % b;
        a = b;
        b = t;
    }
    return a;
}
// A turn's degrees (rounded to a tenth): "120", "72", "51.4".
std::string turnDegreesText(const Turn& t) {
    const double degrees = std::fabs(360.0 * t.num / t.den);
    char text[24];
    if (std::fabs(degrees - std::round(degrees)) < 0.05)
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::lround(degrees)));
    else
        std::snprintf(text, sizeof(text), "%.1f", degrees);
    return text;
}
}  // namespace

int Turn::repeats() const {
    if (none()) return 1;
    const int r = num % den;
    return den / gcd(r, den);
}

Turn Turn::times(int k) const {
    Turn t;
    t.den = den;
    if (den <= 0) return t;
    t.num = static_cast<int>(static_cast<long long>(num) * k % den);  // (the same way round, under a circle)
    return t;
}

std::string turnText(const Turn& t) {
    if (t.none()) return "none";
    const std::string fraction = std::to_string(std::abs(t.num)) + "/" + std::to_string(t.den);
    if (t.half()) return fraction + " (" + turnDegreesText(t) + "\xC2\xB0)";
    return fraction + (t.num > 0 ? " CCW (" : " CW (") + turnDegreesText(t) + "\xC2\xB0)";
}

std::string turnShort(const Turn& t) {
    if (t.none()) return std::string();
    const std::string fraction = std::to_string(std::abs(t.num)) + "/" + std::to_string(t.den);
    if (t.half()) return fraction;
    return (t.num > 0 ? "+" : "-") + fraction;
}

namespace {
// A spot turned about the middle of the floor (counterclockwise seen from above when positive).
void turnSpot(float turn, float* x, float* z, float* yaw) {
    if (turn == 0.0f) return;
    const float ct = std::cos(turn), st = std::sin(turn);
    const float nx = *x * ct + *z * st, nz = -*x * st + *z * ct;
    *x = nx;
    *z = nz;
    *yaw = wrapAngle(*yaw + turn);
}
}  // namespace

void setWalkLink(Choreography* c, int juggler, const WalkLink& link) {
    if (juggler < 0) return;
    if (static_cast<int>(c->links.size()) <= juggler) c->links.resize(static_cast<size_t>(juggler) + 1);
    c->links[static_cast<size_t>(juggler)] = link;
    if (link.active() && juggler < static_cast<int>(c->keys.size())) c->keys[static_cast<size_t>(juggler)].clear();
}

// Two turns added, as a fraction of a circle in [0, 1) (reduced).
static Turn addTurns(const Turn& a, const Turn& b) {
    const long long ad = a.den > 0 ? a.den : 1, bd = b.den > 0 ? b.den : 1;
    long long num = static_cast<long long>(a.num) * bd + static_cast<long long>(b.num) * ad, den = ad * bd;
    num %= den;
    if (num < 0) num += den;
    long long g = std::max(1LL, std::llabs(num));
    for (long long x = den; x != 0;) {
        const long long t = g % x;
        g = x;
        x = t;
    }
    if (num == 0) g = den;
    Turn t;
    t.num = static_cast<int>(num / g);
    t.den = static_cast<int>(den / g);
    return t;
}

std::string continuationText(const Continuation& n, int juggler) {
    if (n.isDefault(juggler)) return std::string();
    std::string out = n.roleFor(juggler) == juggler ? std::string() : std::to_string(n.roleFor(juggler) + 1);
    if (!n.turn.none()) out += "r" + turnShort(n.turn);
    return out.empty() ? std::to_string(juggler + 1) : out;
}

bool readContinuation(const std::string& text, Continuation* n) {
    Continuation k;
    size_t i = 0;
    while (i < text.size() && text[i] == ' ') ++i;
    int role = 0;
    bool any = false;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
        role = role * 10 + (text[i] - '0');
        any = true;
        ++i;
    }
    if (any) {
        if (role < 1 || role > 64) return false;
        k.role = role - 1;
    }
    if (i < text.size() && (text[i] == 'r' || text[i] == 'R')) {
        ++i;
        int sign = 1;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) sign = text[i++] == '-' ? -1 : 1;
        int num = 0, den = 0;
        bool digits = false;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            num = num * 10 + (text[i++] - '0');
            digits = true;
        }
        if (!digits || i >= text.size() || text[i] != '/') return false;
        ++i;
        digits = false;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            den = den * 10 + (text[i++] - '0');
            digits = true;
        }
        if (!digits || den < 1 || den > kMaxTurnDen || num > 1000) return false;
        k.turn.num = sign * num;
        k.turn.den = den;
    }
    while (i < text.size() && text[i] == ' ') ++i;
    if (i != text.size()) return false;
    *n = k;
    return true;
}

Continuation Choreography::continuation(int juggler) const {
    if (juggler >= 0 && juggler < static_cast<int>(next.size())) return next[static_cast<size_t>(juggler)];
    Continuation n;
    n.turn = repeat;
    return n;
}

int Choreography::repeats() const {
    int n = static_cast<int>(next.size());
    for (const Continuation& k : next) n = std::max(n, k.role + 1);
    return walkLoop(*this, n).phases;
}

WalkLoop walkLoop(const Choreography& c, int jugglers) {
    WalkLoop w;
    const int n = std::max(jugglers, 1);
    auto identity = [&]() {
        w.phases = 1;
        w.role.assign(1, std::vector<int>(static_cast<size_t>(n)));
        for (int j = 0; j < n; ++j) w.role[0][static_cast<size_t>(j)] = j;
        w.turn.assign(1, std::vector<Turn>(static_cast<size_t>(n)));
    };
    identity();
    // Each part taken by exactly one juggler.
    std::vector<int> takes(static_cast<size_t>(n));
    std::vector<std::vector<int>> takers(static_cast<size_t>(n));
    std::vector<Turn> step(static_cast<size_t>(n));
    std::string problem;
    for (int j = 0; j < n; ++j) {
        const Continuation k = c.continuation(j);
        const int r = k.roleFor(j);
        step[static_cast<size_t>(j)] = k.turn;
        takes[static_cast<size_t>(j)] = r;
        if (r < 0 || r >= n) {
            problem += (problem.empty() ? "" : "; ") + std::string("C") + std::to_string(j + 1) + " goes on to C" +
                       std::to_string(r + 1) + ", and there's no C" + std::to_string(r + 1);
            w.bad.push_back(j);
            continue;
        }
        takers[static_cast<size_t>(r)].push_back(j);
    }
    for (int r = 0; r < n; ++r) {
        const std::vector<int>& t = takers[static_cast<size_t>(r)];
        if (t.size() == 1) continue;
        if (t.empty()) {
            problem += (problem.empty() ? "" : "; ") + std::string("nothing goes on to C") + std::to_string(r + 1);
            continue;
        }
        std::string who;
        for (size_t i = 0; i < t.size(); ++i) {
            who += (i == 0 ? "C" : (i + 1 == t.size() ? " and C" : ", C")) + std::to_string(t[i] + 1);
            w.bad.push_back(t[i]);
        }
        problem += (problem.empty() ? "" : "; ") + who + (t.size() == 2 ? " both" : " all") + " go on to C" +
                   std::to_string(r + 1);
    }
    if (!problem.empty()) {
        w.valid = false;
        w.problem = problem;
        return w;
    }
    // Round and round till everyone's back on their own part, unturned.
    for (int p = 0; p < WalkLoop::kMaxPhases; ++p) {
        std::vector<int> roles(static_cast<size_t>(n));
        std::vector<Turn> turns(static_cast<size_t>(n));
        bool home = true;
        for (int j = 0; j < n; ++j) {
            const int k = takes[static_cast<size_t>(j)];
            roles[static_cast<size_t>(j)] = w.role[static_cast<size_t>(p)][static_cast<size_t>(k)];
            turns[static_cast<size_t>(j)] = addTurns(w.turn[static_cast<size_t>(p)][static_cast<size_t>(k)], step[static_cast<size_t>(j)]);
            home = home && roles[static_cast<size_t>(j)] == j && turns[static_cast<size_t>(j)].none();
        }
        if (home) {
            w.phases = p + 1;
            return w;
        }
        w.role.push_back(roles);
        w.turn.push_back(turns);
    }
    identity();
    w.valid = false;
    w.problem = "it doesn't come back to the start within " + std::to_string(WalkLoop::kMaxPhases) + " times round";
    return w;
}

namespace {
// The keyframes of juggler `j` over the whole loop (see effectiveKeyframes). `depth` guards
// against links and loops that go round in a circle.
std::vector<Keyframe> keyframesOver(const Choreography& c, int juggler, int cycle, const WalkLoop& w, int depth) {
    std::vector<Keyframe> out;
    if (depth > 12 || cycle <= 0 || juggler < 0) return out;
    const int full = cycle * w.phases;
    // A part's keyframes in the beats written out (a part with none: standing on its own mark).
    // (A linked part's are its leader's part's, moved round by the link within the written beats,
    // so this only follows links, never the loop: following the loop here went round and round,
    // which took seconds once a loop handed linked parts about.)
    std::function<std::vector<Keyframe>(int, int)> partKeys = [&](int r, int linkDepth) {
        std::vector<Keyframe> keys;
        const WalkLink pl = c.link(r);
        if (pl.active() && linkDepth < 12 && !c.dependsOn(pl.leader, r)) {
            for (const Keyframe& k : partKeys(pl.leader, linkDepth + 1)) {
                Keyframe m = k;
                m.beat = ((k.beat - pl.offset) % cycle + cycle) % cycle;
                if (!pl.turn.none()) {
                    keyframeSpot(c, k, &m.x, &m.z, &m.yaw);
                    turnSpot(pl.turnRadians(), &m.x, &m.z, &m.yaw);
                    m.mark = -1;
                }
                keys.push_back(m);
            }
            std::sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.beat < b.beat; });
        } else if (c.hasKeys(r)) {
            for (const Keyframe& k : c.keys[static_cast<size_t>(r)])
                if (k.beat < cycle) keys.push_back(k);
        }
        if (keys.empty() && r < static_cast<int>(c.marks.size())) {
            Keyframe k;
            k.mark = r;
            keys.push_back(k);
        }
        return keys;
    };
    const WalkLink l = c.link(juggler);
    if (l.active() && !c.dependsOn(l.leader, juggler)) {
        std::vector<Keyframe> lead = keyframesOver(c, l.leader, cycle, w, depth + 1);
        if (lead.empty()) lead = partKeys(l.leader, 0);
        for (const Keyframe& k : lead) {
            Keyframe m = k;
            m.beat = ((k.beat - l.offset) % full + full) % full;
            if (!l.turn.none()) {
                keyframeSpot(c, k, &m.x, &m.z, &m.yaw);
                turnSpot(l.turnRadians(), &m.x, &m.z, &m.yaw);
                m.mark = -1;
            }
            out.push_back(m);
        }
    } else if (w.phases <= 1) {
        if (c.hasKeys(juggler)) out = c.keys[static_cast<size_t>(juggler)];
        return out;
    } else {
        for (int p = 0; p < w.phases; ++p) {
            const int r = juggler < static_cast<int>(w.role[static_cast<size_t>(p)].size())
                              ? w.role[static_cast<size_t>(p)][static_cast<size_t>(juggler)]
                              : juggler;
            const Turn t = juggler < static_cast<int>(w.turn[static_cast<size_t>(p)].size())
                               ? w.turn[static_cast<size_t>(p)][static_cast<size_t>(juggler)]
                               : Turn();
            for (const Keyframe& k : partKeys(r, 0)) {
                Keyframe m = k;
                m.beat = k.beat + p * cycle;
                if (!t.none()) {
                    keyframeSpot(c, k, &m.x, &m.z, &m.yaw);
                    turnSpot(t.radians(), &m.x, &m.z, &m.yaw);
                    m.mark = -1;
                }
                out.push_back(m);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Keyframe& a, const Keyframe& b) { return a.beat < b.beat; });
    return out;
}
}  // namespace

std::vector<Keyframe> effectiveKeyframes(const Choreography& c, int juggler, int cycle) {
    int n = std::max(static_cast<int>(c.next.size()), juggler + 1);
    n = std::max(n, static_cast<int>(std::max(c.keys.size(), c.links.size())));
    for (const Continuation& k : c.next) n = std::max(n, k.role + 1);
    return keyframesOver(c, juggler, cycle, walkLoop(c, n), 0);
}

bool Choreography::operator==(const Choreography& o) const {
    return choreographyToText(*this) == choreographyToText(o);
}

void keyframeSpot(const Choreography& c, const Keyframe& k, float* x, float* z, float* yaw) {
    if (k.mark >= 0 && k.mark < static_cast<int>(c.marks.size())) {
        const SpikeMark& m = c.marks[static_cast<size_t>(k.mark)];
        *x = m.x;
        *z = m.z;
        *yaw = m.yaw;
    } else {
        *x = k.x;
        *z = k.z;
        *yaw = k.yaw;
    }
}

bool placementFromKeyframes(const Choreography& c, const std::vector<Keyframe>& keys, int full, double beat, float scale,
                            Vec3* position, float* yaw) {
    if (keys.empty() || full <= 0) return false;
    const double t = beat - std::floor(beat / full) * full;  // in [0, full)
    // The keyframes either side of t (round the whole loop).
    size_t next = 0;
    while (next < keys.size() && keys[next].beat <= t) ++next;
    const Keyframe& k1 = keys[next % keys.size()];
    const Keyframe& k0 = keys[(next + keys.size() - 1) % keys.size()];
    double b0 = k0.beat, b1 = k1.beat;
    if (b0 > t) b0 -= full;   // the last keyframe, a whole loop back
    if (b1 <= t) b1 += full;  // the first keyframe, a whole loop on
    float x0 = 0.0f, z0 = 0.0f, y0 = 0.0f, x1 = 0.0f, z1 = 0.0f, y1 = 0.0f;
    keyframeSpot(c, k0, &x0, &z0, &y0);
    keyframeSpot(c, k1, &x1, &z1, &y1);
    float u = 0.0f;
    if (keys.size() > 1 && b1 > b0) {
        u = static_cast<float>((t - b0) / (b1 - b0));
        u = u * u * (3.0f - 2.0f * u);  // ease in and out
    }
    float turn = wrapAngle(y1 - y0);
    if (k1.longTurn && keys.size() > 1) turn = turn > 0.0f ? turn - 2.0f * kPi : turn + 2.0f * kPi;
    *position = Vec3((x0 + (x1 - x0) * u) * scale, 0.0f, (z0 + (z1 - z0) * u) * scale);
    *yaw = wrapAngle(y0 + turn * u);
    return true;
}

bool choreographyPlacement(const Choreography& c, int juggler, double beat, int cycle, float scale, Vec3* position,
                           float* yaw) {
    if (cycle <= 0) return false;
    return placementFromKeyframes(c, effectiveKeyframes(c, juggler, cycle), choreographyFullCycle(c, cycle), beat, scale,
                                  position, yaw);
}

bool sameKeyframeSpot(const Choreography& c, const Keyframe& a, const Keyframe& b) {
    float ax = 0.0f, az = 0.0f, ay = 0.0f, bx = 0.0f, bz = 0.0f, by = 0.0f;
    keyframeSpot(c, a, &ax, &az, &ay);
    keyframeSpot(c, b, &bx, &bz, &by);
    return std::hypot(ax - bx, az - bz) < 0.001f && std::fabs(wrapAngle(ay - by)) < 0.01f;
}

void setKeyframe(Choreography* c, int juggler, const Keyframe& k) {
    if (juggler < 0) return;
    if (static_cast<int>(c->keys.size()) <= juggler) c->keys.resize(static_cast<size_t>(juggler) + 1);
    std::vector<Keyframe>& keys = c->keys[static_cast<size_t>(juggler)];
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&k](const Keyframe& e) { return e.beat == k.beat; }),
               keys.end());
    keys.push_back(k);
    std::sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.beat < b.beat; });
}

bool removeKeyframe(Choreography* c, int juggler, int beat) {
    if (!c->hasKeys(juggler)) return false;
    std::vector<Keyframe>& keys = c->keys[static_cast<size_t>(juggler)];
    const size_t before = keys.size();
    keys.erase(std::remove_if(keys.begin(), keys.end(), [beat](const Keyframe& e) { return e.beat == beat; }),
               keys.end());
    return keys.size() != before;
}

bool validMarkName(const std::string& name) {
    if (name.size() != 2) return false;
    for (const char ch : name)
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z'))) return false;
    return true;
}

std::string markNameFor(int n) {
    const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    n = std::clamp(n, 0, 36 * 36 - 1);
    return std::string(1, digits[n / 36]) + digits[n % 36];
}

std::string nextMarkName(const Choreography& c) {
    for (int n = 1; n < 36 * 36; ++n) {
        const std::string name = markNameFor(n);
        if (findMarkNamed(c, name) < 0) return name;
    }
    return "zz";
}

int findMarkNamed(const Choreography& c, const std::string& name) {
    std::string lower = name;
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (size_t m = 0; m < c.marks.size(); ++m)
        if (c.marks[m].name == lower) return static_cast<int>(m);
    return -1;
}

void nameMarks(Choreography* c) {
    for (size_t m = 0; m < c->marks.size(); ++m) {
        SpikeMark& mark = c->marks[m];
        bool taken = false;
        for (size_t e = 0; e < m; ++e) taken = taken || c->marks[e].name == mark.name;
        if (!validMarkName(mark.name) || taken) {
            mark.name.clear();
            mark.name = nextMarkName(*c);
        }
    }
}

int addSpikeMark(Choreography* c, SpikeMark mark) {
    if (!validMarkName(mark.name) || findMarkNamed(*c, mark.name) >= 0) mark.name = nextMarkName(*c);
    c->marks.push_back(mark);
    return static_cast<int>(c->marks.size()) - 1;
}

int markForSpot(Choreography* c, float x, float z, float yaw) {
    for (size_t m = 0; m < c->marks.size(); ++m) {
        const SpikeMark& mark = c->marks[m];
        if (std::hypot(mark.x - x, mark.z - z) < 0.005f && std::fabs(wrapAngle(mark.yaw - yaw)) < 0.01f)
            return static_cast<int>(m);
    }
    SpikeMark mark;
    mark.x = x;
    mark.z = z;
    mark.yaw = yaw;
    return addSpikeMark(c, mark);
}

void keyframesOntoMarks(Choreography* c) {
    for (std::vector<Keyframe>& keys : c->keys)
        for (Keyframe& k : keys)
            if (k.mark < 0 || k.mark >= static_cast<int>(c->marks.size())) k.mark = markForSpot(c, k.x, k.z, k.yaw);
}

bool removeSpikeMark(Choreography* c, int mark, int jugglers, std::string* why) {
    if (mark < 0 || mark >= static_cast<int>(c->marks.size())) return false;
    // Who stands on it: keyframes of their own on it, or (no keyframes, no walk link) it's theirs.
    std::string users;
    for (size_t j = 0; j < c->keys.size(); ++j) {
        std::string beats;
        for (const Keyframe& k : c->keys[j])
            if (k.mark == mark) beats += (beats.empty() ? "" : ", ") + std::to_string(k.beat + 1);
        if (!beats.empty())
            users += (users.empty() ? "" : "; ") + std::string("J") + std::to_string(j + 1) + " on beat" +
                     (beats.find(',') != std::string::npos ? "s " : " ") + beats;
    }
    if (mark < jugglers && !c->hasKeys(mark) && !c->link(mark).active())
        users += (users.empty() ? "" : "; ") + std::string("J") + std::to_string(mark + 1) + " (it's theirs, and they have no keyframes)";
    if (!users.empty()) {
        if (why)
            *why = "Can't delete mark " + c->marks[static_cast<size_t>(mark)].name + ": it's in use (" + users +
                   "). Move them to another mark first.";
        return false;
    }
    // Jugglers on their own mark (no keyframes, no walk link) after this one: pinned there.
    for (int j = mark + 1; j < std::min(jugglers, static_cast<int>(c->marks.size())); ++j) {
        if (c->hasKeys(j) || c->link(j).active()) continue;
        Keyframe k;
        k.beat = 0;
        k.mark = j;
        setKeyframe(c, j, k);
    }
    for (std::vector<Keyframe>& keys : c->keys)
        for (Keyframe& k : keys)
            if (k.mark > mark) --k.mark;
    c->marks.erase(c->marks.begin() + mark);
    return true;
}

std::string choreographyToText(const Choreography& c) {
    std::string out = "D" + number(c.reference);
    if (c.cycle > 0) out += "/C" + std::to_string(c.cycle);
    if (!c.repeat.none()) out += "r" + turnShort(c.repeat);  // rotated repeats ("r+1/3", "r-2/5", "r1/2")
    if (!c.grid.empty()) out += "/G" + c.grid;                // the floor grid ("/Gradial,0.5,4,8,0,1")
    // The walking loop: each juggler's continuation, J1's first ("/L2r+1/4,3,,1"; blank: their own
    // part, unturned), up to the last that isn't blank.
    {
        size_t last = 0;
        for (size_t j = 0; j < c.next.size(); ++j)
            if (!c.next[j].isDefault(static_cast<int>(j))) last = j + 1;
        if (last > 0) {
            out += "/L";
            for (size_t j = 0; j < last; ++j) {
                if (j > 0) out += ",";
                const Continuation& k = c.next[j];
                // (Written out in full here, so "their own part, turned" isn't mistaken for blank.)
                if (!k.isDefault(static_cast<int>(j)))
                    out += std::to_string(k.roleFor(static_cast<int>(j)) + 1) + (k.turn.none() ? "" : "r" + turnShort(k.turn));
            }
        }
    }
    for (size_t m = 0; m < c.marks.size(); ++m) {
        const SpikeMark& mark = c.marks[m];
        out += "/M" + std::to_string(m + 1) + "=" + mark.name + "(" + number(mark.x) + "," + number(mark.z) + "," +
               std::to_string(degrees(mark.yaw)) + ")";
    }
    for (size_t j = 0; j < c.links.size(); ++j) {
        const WalkLink& l = c.links[j];
        if (!l.active()) continue;
        out += "/J" + std::to_string(j + 1) + "(=J" + std::to_string(l.leader + 1) + (l.offset >= 0 ? "+" : "") +
               std::to_string(l.offset);
        if (!l.turn.none()) out += "r" + turnShort(l.turn);  // turned: "r+1/4", "r-3/9", "r1/2"
        out += ")";
    }
    for (size_t j = 0; j < c.keys.size(); ++j) {
        if (c.keys[j].empty()) continue;
        out += "/J" + std::to_string(j + 1) + "(";
        for (size_t i = 0; i < c.keys[j].size(); ++i) {
            const Keyframe& k = c.keys[j][i];
            if (i > 0) out += ",";
            out += std::to_string(k.beat) + ":";
            if (k.mark >= 0)
                out += "M" + std::to_string(k.mark + 1);
            else
                out += "(" + number(k.x) + "," + number(k.z) + "," + std::to_string(degrees(k.yaw)) + ")";
            if (k.longTurn) out += "~";
        }
        out += ")";
    }
    return out;
}

// A turn after its "r": a fraction ("+3/9", "-2/5", "1/2"), or (older files) degrees ("+90").
static bool readTurn(Reader& r, Turn* t) {
    int num = 0;
    if (!r.integer(&num)) return false;
    // A fraction's "/" has a digit after it (else it's the "/" before the next part: "r180/M1").
    if (r.i + 1 < r.s.size() && r.s[r.i] == '/' && std::isdigit(static_cast<unsigned char>(r.s[r.i + 1]))) {
        ++r.i;
        int den = 0;
        if (!r.integer(&den) || den < 1 || den > kMaxTurnDen || std::abs(num) > 1000) return false;
        t->num = num;
        t->den = den;
        return true;
    }
    num %= 360;  // degrees: the fraction they are (90: 1/4)
    const int g = gcd(num, 360);
    t->num = g == 0 ? 0 : num / g;
    t->den = g == 0 ? 1 : 360 / g;
    if (t->den > kMaxTurnDen) return false;
    return true;
}

bool choreographyFromText(const std::string& text, Choreography* result) {
    Choreography c;
    Reader r{text};
    while (!r.done()) {
        if (r.take('D')) {
            if (!r.real(&c.reference) || c.reference < 0.0) return false;
        } else if (r.take('C')) {
            if (!r.integer(&c.cycle) || c.cycle < 0) return false;
            if (r.take('r')) {  // rotated repeats: "r+1/3", "r-2/5", "r1/2"
                if (!readTurn(r, &c.repeat) || c.repeat.none()) return false;
            }
        } else if (r.take('L')) {  // the walking loop: continuations, J1's first ("L2r+1/4,3,,1")
            // (The field runs to a "/" that isn't a turn's: one not followed by a digit.)
            size_t end = r.i;
            while (end < r.s.size() && !(r.s[end] == '/' && (end + 1 >= r.s.size() || !std::isdigit(static_cast<unsigned char>(r.s[end + 1])))))
                ++end;
            const std::string field = r.s.substr(r.i, end - r.i);
            r.i = end;
            c.next.clear();
            size_t start = 0;
            while (true) {
                const size_t comma = field.find(',', start);
                const std::string entry = field.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                Continuation k;
                if (!readContinuation(entry, &k)) return false;
                c.next.push_back(k);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (r.take('G')) {  // the floor grid, kept as it is (up to the next "/")
            const size_t end = r.s.find('/', r.i);
            c.grid = r.s.substr(r.i, end == std::string::npos ? std::string::npos : end - r.i);
            r.i = end == std::string::npos ? r.s.size() : end;
        } else if (r.take('M')) {
            int index = 0;
            SpikeMark m;
            if (!r.integer(&index) || index != static_cast<int>(c.marks.size()) + 1) return false;
            if (r.take('=')) {  // its name: two characters ("M3=0a")
                if (r.i + 2 > r.s.size()) return false;
                m.name = r.s.substr(r.i, 2);
                r.i += 2;
            }
            if (!r.spot(&m.x, &m.z, &m.yaw)) return false;
            c.marks.push_back(m);
        } else if (r.take('J')) {
            int juggler = 0;
            if (!r.integer(&juggler) || juggler < 1 || juggler > 64 || !r.take('(')) return false;
            if (r.take('=')) {  // a walk link: =J<leader>+<offset>, then maybe r<degrees>
                WalkLink l;
                int leader = 0;
                if (!r.take('J') || !r.integer(&leader) || leader < 1 || leader > 64 || leader == juggler ||
                    !r.integer(&l.offset))
                    return false;
                l.leader = leader - 1;
                if (r.take('r') && !readTurn(r, &l.turn)) return false;  // turned: "r+1/4", "r-3/9", "r1/2"
                if (!r.take(')')) return false;
                if (static_cast<int>(c.links.size()) < juggler) c.links.resize(static_cast<size_t>(juggler));
                c.links[static_cast<size_t>(juggler - 1)] = l;
                if (!r.done() && !r.take('/')) return false;
                continue;
            }
            while (true) {
                Keyframe k;
                if (!r.integer(&k.beat) || k.beat < 0 || !r.take(':')) return false;
                if (r.take('M')) {
                    int mark = 0;
                    if (!r.integer(&mark) || mark < 1) return false;
                    k.mark = mark - 1;
                } else if (!r.spot(&k.x, &k.z, &k.yaw)) {
                    return false;
                }
                k.longTurn = r.take('~');
                setKeyframe(&c, juggler - 1, k);
                if (r.take(')')) break;
                if (!r.take(',')) return false;
            }
        } else {
            return false;
        }
        if (!r.done() && !r.take('/')) return false;
    }
    for (const std::vector<Keyframe>& keys : c.keys)
        for (const Keyframe& k : keys)
            if (k.mark >= static_cast<int>(c.marks.size())) return false;
    // Marks without names (older files) are named; keyframes not on marks are put on them.
    nameMarks(&c);
    keyframesOntoMarks(&c);
    *result = c;
    return true;
}
