// siteswap.cpp - see siteswap.h.
#include "siteswap.h"

#include <cctype>

int Siteswap::throwAt(int beat) const {
    int n = period();
    if (n == 0) return 0;
    int i = beat % n;
    if (i < 0) i += n;
    return throws[static_cast<size_t>(i)];
}

JugglingLoop soloLoop(const std::vector<int>& values) {
    JugglingLoop loop;
    loop.jugglers = 1;
    loop.period = static_cast<int>(values.size());
    for (const int v : values) loop.throws.push_back({v, 0});
    return loop;
}

int loopPropCount(const JugglingLoop& loop) {
    if (loop.period == 0) return 0;
    int sum = 0;
    for (const LoopThrow& t : loop.throws) {
        if (t.value == kOpenThrow) return 0;  // a sketch: not known yet
        sum += t.value;
    }
    return sum / loop.period;
}

namespace {

// "J1" etc. (jugglers are numbered from 1 for people).
std::string jugglerName(int juggler) { return "J" + std::to_string(juggler + 1); }

// Throw value of a siteswap character, or -1 if it isn't one.
int throwValue(unsigned char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (std::isalpha(ch) && std::tolower(ch) != 'x' && std::tolower(ch) != 'p') return std::tolower(ch) - 'a' + 10;
    return -1;
}

// Checks a loop: the props must divide evenly over the period, and every throw must land in a
// different (juggler, beat) slot. Fills in s->valid / ballCount / error.
void validate(Siteswap* s) {
    const JugglingLoop& loop = s->loop;
    const int n = loop.period;
    int sum = 0;
    int open = 0;
    for (const LoopThrow& t : loop.throws) {
        if (t.value == kOpenThrow) ++open;
        else sum += t.value;
    }
    if (open == 0 && sum % n != 0) {
        if (loop.jugglers == 1)
            s->error = "Invalid: the average throw value (" + std::to_string(sum) + "/" +
                       std::to_string(n) + ") isn't a whole number.";
        else
            s->error = "Invalid: the throws add up to " + std::to_string(sum) + ", which isn't a "
                       "multiple of the period (" + std::to_string(n) + "), so they can't make a "
                       "whole number of props.";
        return;
    }
    // Each (juggler, beat) slot must receive exactly one landing.
    std::vector<int> landedFrom(static_cast<size_t>(loop.jugglers * n), -1);
    for (int j = 0; j < loop.jugglers; ++j) {
        for (int b = 0; b < n; ++b) {
            const LoopThrow& t = loop.at(j, b);
            if (t.value == kOpenThrow) continue;  // a sketch's open throw lands nowhere yet
            const int slot = t.dest * n + (b + t.value) % n;
            const int previous = landedFrom[static_cast<size_t>(slot)];
            if (previous >= 0) {
                const int pj = previous / n, pb = previous % n;
                if (loop.jugglers == 1)
                    s->error = "Invalid: throws on beats " + std::to_string(pb + 1) + " and " +
                               std::to_string(b + 1) + " land on the same beat.";
                else
                    s->error = "Invalid: " + jugglerName(pj) + "'s throw on beat " + std::to_string(pb + 1) +
                               " and " + jugglerName(j) + "'s throw on beat " + std::to_string(b + 1) +
                               " land in " + jugglerName(t.dest) + "'s hand at the same time.";
                s->problemThrows.push_back({pj, pb});
                s->problemThrows.push_back({j, b});
                return;
            }
            landedFrom[static_cast<size_t>(slot)] = j * n + b;
        }
    }
    if (open > 0) {
        s->sketch = true;
        s->openThrows = open;
        return;
    }
    s->ballCount = sum / n;
    s->valid = true;
}

// The not-supported messages shared by both forms.
bool unsupported(unsigned char ch, Siteswap* s) {
    if (ch == '(' || ch == ')' || ch == ',' || ch == '*') {
        s->error = "Synchronous siteswap isn't supported yet.";
        return true;
    }
    if (ch == '[' || ch == ']') {
        s->error = "Multiplex siteswap isn't supported yet.";
        return true;
    }
    if (std::tolower(ch) == 'x') {
        s->error = "'x' only applies to synchronous siteswap, which isn't supported yet.";
        return true;
    }
    return false;
}

Siteswap parseVanilla(const std::string& text) {
    Siteswap s;
    for (const char raw : text) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isspace(ch)) continue;
        const int v = throwValue(ch);
        if (v >= 0) {
            s.throws.push_back(v);
        } else if (ch == '?') {
            s.throws.push_back(kOpenThrow);
        } else if (unsupported(ch, &s)) {
            s.throws.clear();
            return s;
        } else if (ch == '<' || ch == '>' || ch == '|' || std::tolower(ch) == 'p') {
            s.error = "Passing patterns are written like <3p 3|3p 3>: one part per juggler, between < and >.";
            s.throws.clear();
            return s;
        } else {
            s.error = std::string("Unexpected character '") + raw + "'.";
            s.throws.clear();
            return s;
        }
    }
    if (s.throws.empty()) return s;  // blank input: not valid, but no error to show
    s.loop = soloLoop(s.throws);
    validate(&s);
    return s;
}

// <field|field|...>: each field is a juggler's throws, each a value optionally followed by
// 'p' (a pass). With 2 jugglers 'p' passes to the other one; with more, Juggling Lab writes the
// target's number after the 'p' (3p2), which is recognized but not supported yet.
Siteswap parsePassing(const std::string& text) {
    Siteswap s;
    std::string body;
    for (const char raw : text)
        if (!std::isspace(static_cast<unsigned char>(raw))) body += raw;
    if (body.size() < 2 || body.front() != '<' || body.back() != '>') {
        s.error = "A passing pattern needs to end with '>'.";
        return s;
    }
    body = body.substr(1, body.size() - 2);
    std::vector<std::string> fields(1);
    for (const char c : body) {
        if (c == '|') fields.emplace_back();
        else fields.back() += c;
    }
    const int jugglers = static_cast<int>(fields.size());
    if (jugglers == 1) {
        s.error = "A passing pattern needs at least two jugglers, separated by '|'.";
        return s;
    }
    if (jugglers > 2) {
        s.error = "Passing patterns with " + std::to_string(jugglers) + " jugglers aren't supported yet (2 for now).";
        return s;
    }
    std::vector<std::vector<LoopThrow>> perJuggler(static_cast<size_t>(jugglers));
    for (int j = 0; j < jugglers; ++j) {
        const std::string& f = fields[static_cast<size_t>(j)];
        for (size_t i = 0; i < f.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(f[i]);
            const int v = throwValue(ch);
            if (v >= 0) {
                perJuggler[static_cast<size_t>(j)].push_back({v, j});
            } else if (ch == '?') {
                perJuggler[static_cast<size_t>(j)].push_back({kOpenThrow, j});
            } else if (std::tolower(ch) == 'p') {
                if (perJuggler[static_cast<size_t>(j)].empty()) {
                    s.error = "In " + jugglerName(j) + "'s part, 'p' has to follow a throw value (3p).";
                    return s;
                }
                LoopThrow& t = perJuggler[static_cast<size_t>(j)].back();
                if (t.value == 0) {
                    s.error = "In " + jugglerName(j) + "'s part, a 0 (an empty hand) can't be a pass.";
                    return s;
                }
                if (t.value == kOpenThrow) {
                    s.error = "In " + jugglerName(j) + "'s part, a ? (a throw not decided yet) can't be a pass.";
                    return s;
                }
                if (t.dest != j) {
                    s.error = "In " + jugglerName(j) + "'s part, a throw has two 'p's.";
                    return s;
                }
                t.dest = 1 - j;
            } else if (unsupported(ch, &s)) {
                return s;
            } else if (ch == '<' || ch == '>') {
                s.error = "Unexpected '" + std::string(1, static_cast<char>(ch)) + "' inside a passing pattern.";
                return s;
            } else {
                s.error = std::string("Unexpected character '") + static_cast<char>(ch) + "'.";
                return s;
            }
        }
    }
    const size_t period = perJuggler[0].size();
    for (int j = 0; j < jugglers; ++j) {
        if (perJuggler[static_cast<size_t>(j)].empty()) {
            s.error = jugglerName(j) + " has no throws.";
            return s;
        }
        if (perJuggler[static_cast<size_t>(j)].size() != period) {
            s.error = "Each juggler needs the same number of throws (J1 has " + std::to_string(period) + ", " +
                      jugglerName(j) + " has " + std::to_string(perJuggler[static_cast<size_t>(j)].size()) + ").";
            return s;
        }
    }
    s.loop.jugglers = jugglers;
    s.loop.period = static_cast<int>(period);
    for (const std::vector<LoopThrow>& f : perJuggler) s.loop.throws.insert(s.loop.throws.end(), f.begin(), f.end());
    for (const LoopThrow& t : perJuggler[0]) s.throws.push_back(t.value);
    validate(&s);
    return s;
}

}  // namespace

Siteswap parseSiteswap(const std::string& text) {
    for (const char raw : text) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isspace(ch)) continue;
        return ch == '<' ? parsePassing(text) : parseVanilla(text);
    }
    return Siteswap();  // blank
}
