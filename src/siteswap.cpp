// siteswap.cpp - see siteswap.h.
#include "siteswap.h"

#include <algorithm>
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

int modPositive(int a, int m) {
    const int r = a % m;
    return r < 0 ? r + m : r;
}

std::string trimmed(const std::string& text) {
    size_t a = 0, b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) --b;
    return text.substr(a, b - a);
}

// Splits "body,opt,opt" at its first comma. Options are read into *lrSwap (each "LRswap", in
// any case, toggles it). False, with s->error, for an empty or unknown option.
bool splitOptions(const std::string& text, const std::string& where, std::string* body, bool* lrSwap, Siteswap* s) {
    const size_t comma = text.find(',');
    *body = text.substr(0, comma);
    *lrSwap = false;
    if (comma == std::string::npos) return true;
    size_t start = comma + 1;
    while (true) {
        const size_t next = text.find(',', start);
        const std::string option = trimmed(text.substr(start, next == std::string::npos ? std::string::npos : next - start));
        std::string lower;
        for (const char c : option) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "lrswap") {
            *lrSwap = !*lrSwap;
        } else if (option.empty()) {
            s->error = "An empty option after a comma in " + where + ".";
            return false;
        } else {
            s->error = "Unknown option '" + option + "' in " + where + " (LRswap is the only one so far).";
            return false;
        }
        if (next == std::string::npos) return true;
        start = next + 1;
    }
}

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
    if (ch == '(' || ch == ')' || ch == '*') {
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

Siteswap parseVanilla(const std::string& all) {
    Siteswap s;
    std::string text;
    bool lrSwap = false;
    if (!splitOptions(all, "the pattern", &text, &lrSwap, &s)) return s;
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
    if (lrSwap) s.loop.swapHands.assign(1, 1);
    s.form.jugglers = 1;
    s.form.period = s.loop.period;
    s.form.original = s.loop;
    s.form.throws.assign(s.throws.size(), ThrowForm());
    s.form.links.assign(1, PartLink());
    validate(&s);
    return s;
}

// <field|field|...>: each field is a juggler's throws, each a value optionally followed by
// 'p' (a pass) and its target: none with two jugglers (the other one), else the target's
// number (3p2) or a relative one (3p+1, 3p-1; also allowed with two jugglers). A field can
// instead be a link, "@2+3" (the same as J2, 3 beats later). Any field can end in options
// after commas (",LRswap").
Siteswap parsePassing(const std::string& text) {
    Siteswap s;
    // Spaces separate throws but are otherwise ignored; a pass's target has to follow its 'p'
    // directly ("3p2 3" is a pass to J2 then a 3; "3p 2" is a pass with no target, then a 2).
    const std::string all = trimmed(text);
    if (all.size() < 2 || all.front() != '<' || all.back() != '>') {
        s.error = "A passing pattern needs to end with '>'.";
        return s;
    }
    const std::string body = all.substr(1, all.size() - 2);
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
    if (jugglers > kMaxJugglers) {
        s.error = "Patterns can have up to " + std::to_string(kMaxJugglers) + " jugglers (this one has " +
                  std::to_string(jugglers) + ").";
        return s;
    }
    std::vector<std::vector<LoopThrow>> perJuggler(static_cast<size_t>(jugglers));
    std::vector<std::vector<ThrowForm>> forms(static_cast<size_t>(jugglers));
    std::vector<PartLink> links(static_cast<size_t>(jugglers));
    std::vector<char> ownSwap(static_cast<size_t>(jugglers), 0);
    for (int j = 0; j < jugglers; ++j) {
        std::string f;
        bool lrSwap = false;
        if (!splitOptions(fields[static_cast<size_t>(j)], jugglerName(j) + "'s part", &f, &lrSwap, &s)) return s;
        ownSwap[static_cast<size_t>(j)] = lrSwap ? 1 : 0;
        const std::string head = trimmed(f);
        if (!head.empty() && head[0] == '@') {
            // A link: "@2", "@2+3", "@2-1".
            size_t i = 1;
            int to = 0;
            bool anyDigit = false;
            while (i < head.size() && std::isdigit(static_cast<unsigned char>(head[i]))) {
                to = to * 10 + (head[i] - '0');
                anyDigit = true;
                ++i;
            }
            while (i < head.size() && std::isspace(static_cast<unsigned char>(head[i]))) ++i;
            int offset = 0;
            bool ok = anyDigit;
            if (ok && i < head.size()) {
                const int sign = head[i] == '-' ? -1 : (head[i] == '+' ? 1 : 0);
                ++i;
                while (i < head.size() && std::isspace(static_cast<unsigned char>(head[i]))) ++i;
                bool anyOffset = false;
                while (i < head.size() && std::isdigit(static_cast<unsigned char>(head[i]))) {
                    offset = offset * 10 + (head[i] - '0');
                    anyOffset = true;
                    ++i;
                }
                ok = sign != 0 && anyOffset && i == head.size();
                offset *= sign;
            }
            if (!ok) {
                s.error = "In " + jugglerName(j) + "'s part, a link is written @2 or @2+3 (the same as J2, 3 beats later).";
                return s;
            }
            if (to < 1 || to > jugglers) {
                s.error = jugglerName(j) + " is a link to J" + std::to_string(to) + ", but there are only " +
                          std::to_string(jugglers) + " jugglers.";
                return s;
            }
            if (to - 1 == j) {
                s.error = jugglerName(j) + " is a link to itself.";
                return s;
            }
            links[static_cast<size_t>(j)].to = to - 1;
            links[static_cast<size_t>(j)].offset = offset;
            links[static_cast<size_t>(j)].lrSwap = lrSwap;
            continue;
        }
        std::vector<LoopThrow>& throws = perJuggler[static_cast<size_t>(j)];
        std::vector<ThrowForm>& throwForms = forms[static_cast<size_t>(j)];
        bool lastHasP = false;
        for (size_t i = 0; i < f.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(f[i]);
            if (std::isspace(ch)) continue;
            const int v = throwValue(ch);
            if (v >= 0) {
                throws.push_back({v, j});
                throwForms.push_back(ThrowForm());
                lastHasP = false;
            } else if (ch == '?') {
                throws.push_back({kOpenThrow, j});
                throwForms.push_back(ThrowForm());
                lastHasP = false;
            } else if (std::tolower(ch) == 'p') {
                if (throws.empty()) {
                    s.error = "In " + jugglerName(j) + "'s part, 'p' has to follow a throw value (3p).";
                    return s;
                }
                if (lastHasP) {
                    s.error = "In " + jugglerName(j) + "'s part, a throw has two 'p's.";
                    return s;
                }
                lastHasP = true;
                LoopThrow& t = throws.back();
                ThrowForm& form = throwForms.back();
                if (t.value == kOpenThrow) {
                    s.error = "In " + jugglerName(j) + "'s part, a ? (a throw not decided yet) can't be a pass.";
                    return s;
                }
                // The target: relative (+1, -1), absolute (a juggler's number, 3+ jugglers), or
                // none (two jugglers: the other one).
                const char next = i + 1 < f.size() ? f[i + 1] : '\0';
                if ((next == '+' || next == '-') && i + 2 < f.size() &&
                    std::isdigit(static_cast<unsigned char>(f[i + 2]))) {
                    const int steps = (f[i + 2] - '0') * (next == '-' ? -1 : 1);
                    form.kind = ThrowForm::Kind::Relative;
                    form.value = steps;
                    i += 2;
                } else if (next == '+' || next == '-') {
                    s.error = "In " + jugglerName(j) + "'s part, '" + std::string(1, next) +
                              "' after a 'p' needs a number of jugglers along (3p+1).";
                    return s;
                } else if (jugglers > 2) {
                    if (!std::isdigit(static_cast<unsigned char>(next))) {
                        s.error = "With " + std::to_string(jugglers) + " jugglers, a pass needs its target: in " +
                                  jugglerName(j) + "'s part, 3p2 passes to J2, 3p+1 to the next juggler.";
                        return s;
                    }
                    const int target = next - '0';
                    if (target < 1 || target > jugglers) {
                        s.error = "In " + jugglerName(j) + "'s part, a pass goes to J" + std::to_string(target) +
                                  ", but there are only " + std::to_string(jugglers) + " jugglers.";
                        return s;
                    }
                    form.kind = ThrowForm::Kind::Absolute;
                    form.value = target - 1;
                    ++i;
                } else {
                    form.kind = ThrowForm::Kind::BareP;
                }
                t = carryThrow(form, t, j, j, jugglers);
                if (t.dest != j && t.value == 0) {
                    s.error = "In " + jugglerName(j) + "'s part, a 0 (an empty hand) can't be a pass.";
                    return s;
                }
            } else if (ch == '+' || ch == '-') {
                s.error = "In " + jugglerName(j) + "'s part, '" + std::string(1, static_cast<char>(ch)) +
                          "' only follows a 'p' (3p+1).";
                return s;
            } else if (ch == '@') {
                s.error = "In " + jugglerName(j) + "'s part, a link (@2+3) takes the whole part.";
                return s;
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
    // Every part written out has the same number of throws; links take it from what they copy.
    int period = -1, firstWritten = -1;
    for (int j = 0; j < jugglers; ++j) {
        if (links[static_cast<size_t>(j)].to >= 0) continue;
        const int count = static_cast<int>(perJuggler[static_cast<size_t>(j)].size());
        if (count == 0) {
            s.error = jugglerName(j) + " has no throws.";
            return s;
        }
        if (period < 0) {
            period = count;
            firstWritten = j;
        } else if (count != period) {
            s.error = "Each juggler needs the same number of throws (" + jugglerName(firstWritten) + " has " +
                      std::to_string(period) + ", " + jugglerName(j) + " has " + std::to_string(count) + ").";
            return s;
        }
    }
    if (period < 0) {
        s.error = "Every juggler is a link to another: at least one needs their throws written out.";
        return s;
    }
    // Links that go round in a loop never reach a part written out.
    for (int j = 0; j < jugglers; ++j) {
        int k = j;
        for (int steps = 0; links[static_cast<size_t>(k)].to >= 0; ++steps) {
            if (steps > jugglers) {
                std::string who;
                int m = j;
                for (int i = 0; i < jugglers; ++i) {
                    who += (i ? ", " : "") + jugglerName(m);
                    m = links[static_cast<size_t>(m)].to;
                    if (m == j) break;
                }
                s.error = "These jugglers are links to each other in a loop: " + who +
                          ". One of them needs their throws written out.";
                return s;
            }
            k = links[static_cast<size_t>(k)].to;
        }
    }
    // The loop: parts written out as they are, then each link from what it copies (sources
    // first, for chains).
    s.loop.jugglers = jugglers;
    s.loop.period = period;
    s.loop.throws.assign(static_cast<size_t>(jugglers * period), LoopThrow());
    s.loop.swapHands.assign(static_cast<size_t>(jugglers), 0);
    s.form.throws.assign(static_cast<size_t>(jugglers * period), ThrowForm());
    std::vector<char> done(static_cast<size_t>(jugglers), 0);
    for (int j = 0; j < jugglers; ++j) {
        if (links[static_cast<size_t>(j)].to >= 0) continue;
        for (int b = 0; b < period; ++b) {
            s.loop.throws[static_cast<size_t>(j * period + b)] = perJuggler[static_cast<size_t>(j)][static_cast<size_t>(b)];
            s.form.throws[static_cast<size_t>(j * period + b)] = forms[static_cast<size_t>(j)][static_cast<size_t>(b)];
        }
        s.loop.swapHands[static_cast<size_t>(j)] = ownSwap[static_cast<size_t>(j)];
        done[static_cast<size_t>(j)] = 1;
    }
    for (int pass = 0; pass < jugglers; ++pass) {
        for (int j = 0; j < jugglers; ++j) {
            const PartLink& link = links[static_cast<size_t>(j)];
            if (done[static_cast<size_t>(j)] || !done[static_cast<size_t>(link.to)]) continue;
            for (int b = 0; b < period; ++b) {
                const int from = modPositive(b - link.offset, period);
                const ThrowForm& form = s.form.throws[static_cast<size_t>(link.to * period + from)];
                s.loop.throws[static_cast<size_t>(j * period + b)] =
                    carryThrow(form, s.loop.at(link.to, from), link.to, j, jugglers);
                s.form.throws[static_cast<size_t>(j * period + b)] = form;
            }
            s.loop.swapHands[static_cast<size_t>(j)] = static_cast<char>(ownSwap[static_cast<size_t>(j)] ^ s.loop.swapHands[static_cast<size_t>(link.to)]);
            done[static_cast<size_t>(j)] = 1;
        }
    }
    s.form.jugglers = jugglers;
    s.form.period = period;
    s.form.original = s.loop;
    s.form.links = links;
    int relatives = 0, negatives = 0;
    for (const ThrowForm& form : s.form.throws) {
        if (form.kind != ThrowForm::Kind::Relative) continue;
        ++relatives;
        if (form.value < 0) ++negatives;
    }
    s.form.relative = relatives > 0;
    s.form.negative = relatives > 0 && negatives == relatives;
    for (int b = 0; b < period; ++b) s.throws.push_back(s.loop.at(0, b).value);
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

LoopThrow carryThrow(const ThrowForm& form, const LoopThrow& t, int from, int to, int jugglers) {
    LoopThrow out = t;
    if (t.value == kOpenThrow) {
        out.dest = to;
        return out;
    }
    switch (form.kind) {
    case ThrowForm::Kind::Plain:
        out.dest = to;
        break;
    case ThrowForm::Kind::BareP:
        out.dest = jugglers == 2 ? 1 - to : to;
        break;
    case ThrowForm::Kind::Relative:
        out.dest = modPositive(to + form.value, std::max(1, jugglers));
        break;
    case ThrowForm::Kind::Absolute:
        out.dest = form.value;
        break;
    }
    (void)from;
    return out;
}

bool PatternForm::hasLinks() const {
    for (const PartLink& link : links)
        if (link.to >= 0) return true;
    return false;
}

bool PatternForm::root(int juggler, int* rootJuggler, int* offset) const {
    int k = juggler, total = 0;
    for (int steps = 0; linked(k); ++steps) {
        if (steps > jugglers) return false;
        total += links[static_cast<size_t>(k)].offset;
        k = links[static_cast<size_t>(k)].to;
    }
    *rootJuggler = k;
    *offset = total;
    return true;
}

ThrowForm PatternForm::formFor(int juggler, int beat, const LoopThrow& t) const {
    if (period > 0 && juggler >= 0 && juggler < jugglers && !original.empty() && original.at(juggler, beat) == t)
        return throws[static_cast<size_t>(juggler * period + modPositive(beat, period))];
    ThrowForm f;
    if (t.value == kOpenThrow || t.dest == juggler || jugglers < 2) return f;  // a self
    if (relative) {
        int k = modPositive(t.dest - juggler, jugglers);
        if (negative) k -= jugglers;
        f.kind = ThrowForm::Kind::Relative;
        f.value = k;
    } else if (jugglers == 2) {
        f.kind = ThrowForm::Kind::BareP;
    } else {
        f.kind = ThrowForm::Kind::Absolute;
        f.value = t.dest;
    }
    return f;
}

std::string loopProblem(const JugglingLoop& loop) {
    if (loop.empty()) return "No throws.";
    Siteswap s;
    s.loop = loop;
    validate(&s);
    return (s.valid || s.sketch) ? std::string() : s.error;
}
