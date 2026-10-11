// choreography.cpp - see choreography.h.
#include "choreography.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

void setWalkLink(Choreography* c, int juggler, const WalkLink& link) {
    if (juggler < 0) return;
    if (static_cast<int>(c->links.size()) <= juggler) c->links.resize(static_cast<size_t>(juggler) + 1);
    c->links[static_cast<size_t>(juggler)] = link;
    if (link.active() && juggler < static_cast<int>(c->keys.size())) c->keys[static_cast<size_t>(juggler)].clear();
}

std::vector<Keyframe> effectiveKeyframes(const Choreography& c, int juggler, int cycle) {
    const WalkLink l = c.link(juggler);
    if (!l.active() || cycle <= 0 || c.dependsOn(l.leader, juggler)) {
        return juggler >= 0 && juggler < static_cast<int>(c.keys.size()) ? c.keys[static_cast<size_t>(juggler)]
                                                                         : std::vector<Keyframe>();
    }
    const float turn = l.turnRadians();
    const float ct = std::cos(turn), st = std::sin(turn);
    std::vector<Keyframe> out;
    for (const Keyframe& k : effectiveKeyframes(c, l.leader, cycle)) {
        Keyframe m = k;
        m.beat = ((k.beat - l.offset) % cycle + cycle) % cycle;
        if (l.turnNum % std::max(1, l.turnDen) != 0) {
            float x = 0.0f, z = 0.0f, yaw = 0.0f;
            keyframeSpot(c, k, &x, &z, &yaw);
            m.mark = -1;
            m.x = x * ct + z * st;
            m.z = -x * st + z * ct;
            m.yaw = wrapAngle(yaw + turn);
        }
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const Keyframe& a, const Keyframe& b) { return a.beat < b.beat; });
    return out;
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

bool choreographyPlacement(const Choreography& c, int juggler, double beat, int cycle, float scale, Vec3* position,
                           float* yaw) {
    // A walk link: where the leader is, offset beats on, turned about the middle of the floor.
    const WalkLink l = c.link(juggler);
    if (l.active() && cycle > 0 && !c.dependsOn(l.leader, juggler)) {
        Vec3 p;
        float y = 0.0f;
        if (!choreographyPlacement(c, l.leader, beat + l.offset, cycle, scale, &p, &y)) {
            // A leader with no keyframes stands on their own mark (as the jugglers do).
            if (l.leader >= static_cast<int>(c.marks.size())) return false;
            const SpikeMark& m = c.marks[static_cast<size_t>(l.leader)];
            p = Vec3(m.x * scale, 0.0f, m.z * scale);
            y = m.yaw;
        }
        const float turn = l.turnRadians();
        const float ct = std::cos(turn), st = std::sin(turn);
        *position = Vec3(p.x * ct + p.z * st, p.y, -p.x * st + p.z * ct);
        *yaw = wrapAngle(y + turn);
        return true;
    }
    if (!c.hasKeys(juggler) || cycle <= 0) return false;
    const std::vector<Keyframe>& keys = c.keys[static_cast<size_t>(juggler)];
    const double t = beat - std::floor(beat / cycle) * cycle;  // in [0, cycle)
    // The keyframes either side of t (round the cycle).
    size_t next = 0;
    while (next < keys.size() && keys[next].beat <= t) ++next;
    const Keyframe& k1 = keys[next % keys.size()];
    const Keyframe& k0 = keys[(next + keys.size() - 1) % keys.size()];
    double b0 = k0.beat, b1 = k1.beat;
    if (b0 > t) b0 -= cycle;  // the last keyframe, a cycle back
    if (b1 <= t) b1 += cycle;  // the first keyframe, a cycle on
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

void removeSpikeMark(Choreography* c, int mark) {
    if (mark < 0 || mark >= static_cast<int>(c->marks.size())) return;
    const SpikeMark gone = c->marks[static_cast<size_t>(mark)];
    for (std::vector<Keyframe>& keys : c->keys) {
        for (Keyframe& k : keys) {
            if (k.mark == mark) {
                k.mark = -1;
                k.x = gone.x;
                k.z = gone.z;
                k.yaw = gone.yaw;
            } else if (k.mark > mark) {
                --k.mark;
            }
        }
    }
    c->marks.erase(c->marks.begin() + mark);
}

std::string choreographyToText(const Choreography& c) {
    std::string out = "D" + number(c.reference);
    if (c.cycle > 0) out += "/C" + std::to_string(c.cycle);
    for (size_t m = 0; m < c.marks.size(); ++m) {
        const SpikeMark& mark = c.marks[m];
        out += "/M" + std::to_string(m + 1) + "(" + number(mark.x) + "," + number(mark.z) + "," +
               std::to_string(degrees(mark.yaw)) + ")";
    }
    for (size_t j = 0; j < c.links.size(); ++j) {
        const WalkLink& l = c.links[j];
        if (!l.active()) continue;
        out += "/J" + std::to_string(j + 1) + "(=J" + std::to_string(l.leader + 1) + (l.offset >= 0 ? "+" : "") +
               std::to_string(l.offset);
        if (l.turnNum != 0)
            out += std::string(l.turnNum > 0 ? "ccw" : "cw") + std::to_string(std::abs(l.turnNum)) + "/" +
                   std::to_string(l.turnDen);
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

bool choreographyFromText(const std::string& text, Choreography* result) {
    Choreography c;
    Reader r{text};
    while (!r.done()) {
        if (r.take('D')) {
            if (!r.real(&c.reference) || c.reference < 0.0) return false;
        } else if (r.take('C')) {
            if (!r.integer(&c.cycle) || c.cycle < 0) return false;
        } else if (r.take('M')) {
            int index = 0;
            SpikeMark m;
            if (!r.integer(&index) || index != static_cast<int>(c.marks.size()) + 1 || !r.spot(&m.x, &m.z, &m.yaw))
                return false;
            c.marks.push_back(m);
        } else if (r.take('J')) {
            int juggler = 0;
            if (!r.integer(&juggler) || juggler < 1 || juggler > 64 || !r.take('(')) return false;
            if (r.take('=')) {  // a walk link: =J<leader>+<offset>, then maybe ccw<n>/<d> or cw<n>/<d>
                WalkLink l;
                int leader = 0;
                if (!r.take('J') || !r.integer(&leader) || leader < 1 || leader > 64 || leader == juggler ||
                    !r.integer(&l.offset))
                    return false;
                l.leader = leader - 1;
                int sign = 0;
                if (r.take('c')) {
                    if (r.take('c')) {
                        if (!r.take('w')) return false;
                        sign = 1;
                    } else if (r.take('w')) {
                        sign = -1;
                    } else {
                        return false;
                    }
                }
                if (sign != 0) {
                    if (!r.integer(&l.turnNum) || !r.take('/') || !r.integer(&l.turnDen) || l.turnNum < 0 || l.turnDen < 1)
                        return false;
                    l.turnNum *= sign;
                }
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
    *result = c;
    return true;
}
