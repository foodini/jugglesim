// siteswap_edit.cpp - see siteswap_edit.h.
#include "siteswap_edit.h"
#include "siteswap.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

constexpr int kOpen = -1;  // a "?" (matches kOpenThrow)

constexpr int kNoRelative = -100;

// One throw as written: its characters, value (kOpen for "?"), and pass marker: "p", with a
// target juggler number for 3+ jugglers ("3p2", or -1 for none) or a relative target ("3p+1",
// "3p-1": steps along; kNoRelative for none).
struct Token {
    int start = 0, end = 0;
    int value = 0;
    bool pass = false;
    int target = -1;
    int relative = kNoRelative;
    bool sameThrow(const Token& o) const {
        return value == o.value && pass == o.pass && target == o.target && relative == o.relative;
    }
};

// The text read loosely as siteswap: each juggler's throws (one part for a solo pattern).
struct Parsed {
    bool ok = false;       // only throws, "?", "p", spaces, and (for passing) "<", "|", ">"
    bool passing = false;  // starts with "<"
    bool closed = false;   // has its ">"
    std::vector<std::vector<Token>> parts;
    std::vector<int> partStarts;  // character where each part starts (after "<" or "|")
    // Per part: a link instead of throws ("@2+3", as written without spaces), and options after
    // commas (",LRswap"), both kept as they are when the text is rewritten.
    std::vector<std::string> links, options;
    std::vector<int> linkStart, linkEnd;  // the link's characters, if any
};

int valueOf(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    const int lower = std::tolower(c);
    if (lower >= 'a' && lower <= 'z' && lower != 'p' && lower != 'x') return lower - 'a' + 10;
    return -2;  // not a throw
}

char charOf(int value) {
    if (value == kOpen) return '?';
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + value - 10);
}

bool writable(int value) { return value >= 0 && value <= 35 && value != 25 && value != 33; }

Parsed parse(const std::string& text) {
    Parsed p;
    size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    p.passing = i < text.size() && text[i] == '<';
    if (p.passing) ++i;
    auto newPart = [&]() {
        p.parts.emplace_back();
        p.partStarts.push_back(static_cast<int>(i));
        p.links.emplace_back();
        p.options.emplace_back();
        p.linkStart.push_back(-1);
        p.linkEnd.push_back(-1);
    };
    newPart();
    // A digit right after a "p" is read as its target (3p2), whatever the number of parts: with
    // three or more jugglers it is one, and while the third juggler's part hasn't been typed
    // yet it soon will be, so autofill mustn't pull "3p2" apart. (With exactly two jugglers,
    // siteswap.cpp reads "3p2" as a pass then a 2; spacing it out is left to the writer.) A
    // relative target (3p+1) can follow any "p".
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isspace(c)) {
            ++i;
        } else if (p.passing && c == '|' && !p.closed) {
            ++i;
            newPart();
        } else if (p.passing && c == '>' && !p.closed) {
            p.closed = true;
            ++i;
        } else if (!p.closed && c == ',') {
            // Options, up to the end of the part.
            const size_t stop = p.passing ? text.find_first_of("|>", i) : std::string::npos;
            const std::string raw = text.substr(i, stop == std::string::npos ? std::string::npos : stop - i);
            std::string normalized;
            size_t from = 1;
            while (from <= raw.size()) {
                size_t next = raw.find(',', from);
                if (next == std::string::npos) next = raw.size();
                std::string option = raw.substr(from, next - from);
                while (!option.empty() && std::isspace(static_cast<unsigned char>(option.back()))) option.pop_back();
                while (!option.empty() && std::isspace(static_cast<unsigned char>(option.front()))) option.erase(0, 1);
                std::string lower;
                for (const char ch : option) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lower == "lrswap") option = "LRswap";  // (written the standard way)
                normalized += "," + option;
                from = next + 1;
            }
            p.options.back() += normalized;
            i = stop == std::string::npos ? text.size() : stop;
        } else if (p.passing && !p.closed && c == '@' && p.parts.back().empty() && p.links.back().empty() &&
                   p.options.back().empty()) {
            // A link: "@2", "@2+3", "@2 - 1" (written back without the spaces).
            p.linkStart.back() = static_cast<int>(i);
            std::string link = "@";
            ++i;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '+' ||
                                       text[i] == '-' || text[i] == ' ')) {
                if (text[i] != ' ') link += text[i];
                ++i;
            }
            p.links.back() = link;
            p.linkEnd.back() = static_cast<int>(i);
        } else if (!p.closed && !p.links.back().empty()) {
            return p;  // throws after a link: not ok
        } else if (!p.closed && (c == '?' || valueOf(c) >= 0)) {
            Token t;
            t.start = static_cast<int>(i);
            t.value = c == '?' ? kOpen : valueOf(c);
            ++i;
            if (p.passing && i < text.size() && std::tolower(static_cast<unsigned char>(text[i])) == 'p') {
                t.pass = true;
                ++i;
                if (i + 1 < text.size() && (text[i] == '+' || text[i] == '-') &&
                    std::isdigit(static_cast<unsigned char>(text[i + 1]))) {
                    t.relative = (text[i + 1] - '0') * (text[i] == '-' ? -1 : 1);
                    i += 2;
                } else if (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    t.target = text[i] - '0';
                    ++i;
                }
            }
            t.end = static_cast<int>(i);
            p.parts.back().push_back(t);
        } else {
            return p;  // something else: not ok
        }
    }
    p.ok = true;
    return p;
}

std::string tokenText(const Token& t) {
    std::string s(1, charOf(t.value));
    if (t.pass) {
        s += 'p';
        if (t.relative != kNoRelative)
            s += (t.relative < 0 ? "-" : "+") + std::to_string(t.relative < 0 ? -t.relative : t.relative);
        else if (t.target >= 0)
            s += std::to_string(t.target);
    }
    return s;
}

Token openToken() {
    Token t;
    t.value = kOpen;
    return t;
}

// Standard form. For each part, partStart receives where it begins, and tokenEnds where each of
// its throws ends, in the result.
std::string format(const Parsed& p, std::vector<int>* partStart = nullptr,
                   std::vector<std::vector<int>>* tokenEnds = nullptr) {
    std::string out;
    if (partStart) partStart->clear();
    if (tokenEnds) tokenEnds->assign(p.parts.size(), std::vector<int>());
    if (p.passing) out += '<';
    for (size_t f = 0; f < p.parts.size(); ++f) {
        if (f > 0) out += '|';
        if (partStart) partStart->push_back(static_cast<int>(out.size()));
        if (f < p.links.size() && !p.links[f].empty()) out += p.links[f];
        for (size_t k = 0; k < p.parts[f].size(); ++k) {
            if (k > 0 && p.passing) out += ' ';
            out += tokenText(p.parts[f][k]);
            if (tokenEnds) (*tokenEnds)[f].push_back(static_cast<int>(out.size()));
        }
        if (f < p.options.size()) out += p.options[f];
    }
    if (p.passing) out += '>';
    return out;
}

// If `longer` is `shorter` with one throw inserted, returns where; otherwise -1.
int insertedAt(const std::vector<Token>& shorter, const std::vector<Token>& longer) {
    if (longer.size() != shorter.size() + 1) return -1;
    size_t k = 0;
    while (k < shorter.size() && shorter[k].sameThrow(longer[k])) ++k;
    for (size_t m = k; m < shorter.size(); ++m)
        if (!shorter[m].sameThrow(longer[m + 1])) return -1;
    return static_cast<int>(k);
}

bool samePart(const std::vector<Token>& a, const std::vector<Token>& b) {
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); ++k)
        if (!a[k].sameThrow(b[k])) return false;
    return true;
}

// Where the cursor goes in the rewritten text: after the same throw (counting within its part),
// or at the start of its part.
int mapCursor(const Parsed& after, int cursor, const std::vector<int>& partStart,
              const std::vector<std::vector<int>>& tokenEnds) {
    size_t part = 0;
    for (size_t f = 1; f < after.partStarts.size(); ++f)
        if (cursor >= after.partStarts[f]) part = f;
    if (part >= partStart.size()) return partStart.empty() ? 0 : partStart.back();
    int before = 0;
    for (const Token& t : after.parts[part])
        if (t.end <= cursor) ++before;
    if (before == 0) return partStart[part];
    const std::vector<int>& ends = tokenEnds[part];
    return ends[static_cast<size_t>(std::min<int>(before, static_cast<int>(ends.size())) - 1)];
}

// The throw at a cursor: the one it's in or just after (else the one starting there).
bool tokenAtCursor(const Parsed& p, int cursor, size_t* part, size_t* index) {
    for (size_t f = 0; f < p.parts.size(); ++f)
        for (size_t k = 0; k < p.parts[f].size(); ++k)
            if (p.parts[f][k].start < cursor && cursor <= p.parts[f][k].end) {
                *part = f;
                *index = k;
                return true;
            }
    for (size_t f = 0; f < p.parts.size(); ++f)
        for (size_t k = 0; k < p.parts[f].size(); ++k)
            if (p.parts[f][k].start == cursor) {
                *part = f;
                *index = k;
                return true;
            }
    return false;
}

// Replaces tokens in the text in place (keeping everything else as the user wrote it).
std::string replaceTokens(const std::string& text, std::vector<std::pair<Token, Token>> changes) {
    // Right to left, so earlier positions stay valid.
    std::string out = text;
    for (size_t a = 0; a < changes.size(); ++a)
        for (size_t b = a + 1; b < changes.size(); ++b)
            if (changes[b].first.start > changes[a].first.start) std::swap(changes[a], changes[b]);
    for (const std::pair<Token, Token>& c : changes)
        out.replace(static_cast<size_t>(c.first.start), static_cast<size_t>(c.first.end - c.first.start),
                    tokenText(c.second));
    return out;
}

}  // namespace

namespace {
bool autofillCore(const std::string& before, const std::string& after, int cursor, SiteswapEdit* result,
                  SiteswapAutofillMemory* memory);

// The fewest jugglers a passing pattern's relative targets need: "3p+2" (or "3p-2") needs 3.
// (Not absolute ones: with two jugglers "3p3" is a pass and then a 3, so a digit after a "p"
// says nothing for sure about how many jugglers there are.)
size_t jugglersNeeded(const Parsed& p) {
    size_t needed = 0;
    for (const std::vector<Token>& part : p.parts)
        for (const Token& t : part)
            if (t.pass && t.relative != kNoRelative)
                needed = std::max(needed, static_cast<size_t>(std::abs(t.relative)) + 1);
    return needed;
}
}  // namespace

bool autofillSiteswap(const std::string& before, const std::string& after, int cursor, SiteswapEdit* result,
                      SiteswapAutofillMemory* memory) {
    SiteswapEdit filled;
    const bool changed = autofillCore(before, after, cursor, &filled, memory);
    if (!changed) {
        filled.text = after;
        filled.cursor = cursor;
    }
    // A pass to a juggler the pattern doesn't have yet ("<3p+2" with two parts): add jugglers,
    // all "?", to make room (up to kMaxJugglers).
    Parsed p = parse(filled.text);
    const size_t needed = p.ok && p.passing ? std::min<size_t>(jugglersNeeded(p), kMaxJugglers) : 0;
    if (needed > p.parts.size()) {
        size_t longest = 0;
        for (size_t f = 0; f < p.parts.size(); ++f)
            if (p.links[f].empty()) longest = std::max(longest, p.parts[f].size());
        const Parsed was = p;
        while (p.parts.size() < needed) {
            p.parts.emplace_back(longest, openToken());
            p.links.emplace_back();
            p.options.emplace_back();
        }
        p.closed = true;
        std::vector<int> partStart;
        std::vector<std::vector<int>> tokenEnds;
        result->text = format(p, &partStart, &tokenEnds);
        result->cursor = mapCursor(was, filled.cursor, partStart, tokenEnds);
        if (memory && memory->armed) {
            memory->text = result->text;
            memory->cursor = result->cursor;
        }
        return result->text != after;
    }
    if (changed) *result = filled;
    return changed;
}

namespace {
bool autofillCore(const std::string& before, const std::string& after, int cursor, SiteswapEdit* result,
                  SiteswapAutofillMemory* memory) {
    // Any edit uses up what the last one remembered.
    SiteswapAutofillMemory remembered;
    if (memory) {
        remembered = *memory;
        memory->forget();
    }
    const Parsed b = parse(before);
    const Parsed a = parse(after);

    // The edit right after a column deletion types a throw where the deleted one was: it was a
    // replacement, so the deleted column comes back with the new throw in it.
    if (remembered.armed && before == remembered.text && b.ok && a.ok && a.parts.size() == b.parts.size()) {
        const size_t part = static_cast<size_t>(remembered.part);
        bool othersSame = part < a.parts.size();
        for (size_t f = 0; f < a.parts.size() && othersSame; ++f)
            if (f != part && !samePart(a.parts[f], b.parts[f])) othersSame = false;
        const Parsed restored = parse(remembered.beforeDelete);
        if (othersSame && insertedAt(b.parts[part], a.parts[part]) == remembered.column && restored.ok &&
            restored.parts.size() == a.parts.size() &&
            static_cast<size_t>(remembered.column) < restored.parts[part].size()) {
            Parsed out = restored;
            out.closed = true;
            out.parts[part][static_cast<size_t>(remembered.column)] = a.parts[part][static_cast<size_t>(remembered.column)];
            std::vector<int> partStart;
            std::vector<std::vector<int>> tokenEnds;
            result->text = format(out, &partStart, &tokenEnds);
            result->cursor = tokenEnds[part][static_cast<size_t>(remembered.column)];
            return true;
        }
    }

    // A "|" or ">" typed just in front of the same character (one autofill already put there)
    // steps over it, as editors do with closing brackets, so typing a whole pattern works.
    if (b.ok && b.passing && after.size() == before.size() + 1 && cursor >= 1 &&
        static_cast<size_t>(cursor) <= before.size()) {
        const char typed = after[static_cast<size_t>(cursor - 1)];
        if ((typed == '|' || typed == '>') && static_cast<size_t>(cursor - 1) < before.size() &&
            before[static_cast<size_t>(cursor - 1)] == typed &&
            after.compare(0, static_cast<size_t>(cursor - 1), before, 0, static_cast<size_t>(cursor - 1)) == 0 &&
            after.compare(static_cast<size_t>(cursor), std::string::npos, before, static_cast<size_t>(cursor - 1),
                          std::string::npos) == 0) {
            result->text = before;
            result->cursor = cursor;
            return true;
        }
    }

    // "@" typed at the start of a part that autofill filled with "?": it starts a link there,
    // replacing the "?"s (as a throw typed in front of a "?" fills it in).
    if (b.ok && b.passing && after.size() == before.size() + 1 && cursor >= 1 &&
        after[static_cast<size_t>(cursor - 1)] == '@' &&
        after.compare(0, static_cast<size_t>(cursor - 1), before, 0, static_cast<size_t>(cursor - 1)) == 0) {
        for (size_t f = 0; f < b.parts.size(); ++f) {
            if (b.partStarts[f] != cursor - 1 || !b.links[f].empty() || b.parts[f].empty()) continue;
            bool allOpen = true;
            for (const Token& t : b.parts[f]) allOpen = allOpen && t.value == kOpen;
            if (!allOpen) break;
            Parsed made = b;
            made.closed = true;
            made.parts[f].clear();
            made.links[f] = "@";
            std::vector<int> partStart;
            result->text = format(made, &partStart);
            result->cursor = partStart[f] + 1;
            return true;
        }
    }

    // "<" typed into an empty box, or in front of a solo pattern.
    if (b.ok && !b.passing && after.size() == before.size() + 1 && !after.empty() && after[0] == '<' &&
        after.substr(1) == before) {
        Parsed made;
        made.ok = made.passing = made.closed = true;
        made.parts.push_back(b.parts[0]);
        made.parts.emplace_back(b.parts[0].size(), openToken());
        made.links = {"", ""};
        made.options = {b.options.empty() ? std::string() : b.options[0], ""};
        result->text = format(made);
        result->cursor = 1;
        return true;
    }
    // A passing pattern started without a "|" ("<3"): the second juggler is still to decide.
    if (a.ok && a.passing && a.parts.size() == 1 && !a.parts[0].empty() && (!b.ok || !b.passing || b.parts.size() == 1)) {
        Parsed made = a;
        made.closed = true;
        made.parts.emplace_back(a.parts[0].size(), openToken());
        std::vector<int> partStart;
        std::vector<std::vector<int>> tokenEnds;
        result->text = format(made, &partStart, &tokenEnds);
        result->cursor = mapCursor(a, cursor, partStart, tokenEnds);
        return result->text != after;
    }
    if (!b.ok || !a.ok || !b.passing || !a.passing) return false;
    // Parts compared whole: throws, link and options. A link part has no throws of its own and
    // takes no part in lining up columns.
    auto same = [](const Parsed& x, size_t fx, const Parsed& y, size_t fy) {
        return samePart(x.parts[fx], y.parts[fy]) && x.links[fx] == y.links[fy] && x.options[fx] == y.options[fy];
    };
    auto isLink = [](const Parsed& x, size_t f) { return f < x.links.size() && !x.links[f].empty(); };

    Parsed out = a;
    out.closed = true;
    bool changed = false;
    bool columnDeleted = false;  // (remembered, in case the next keystroke was meant to replace it)
    int deletedPart = 0, deletedColumn = 0;
    if (a.parts.size() == b.parts.size() + 1) {
        // A "|" typed: if it made a new, empty part (the others unchanged), fill it with "?".
        size_t added = a.parts.size();
        for (size_t f = 0, g = 0; f < a.parts.size(); ++f) {
            if (added == a.parts.size() && a.parts[f].empty() && !isLink(a, f) && a.options[f].empty() &&
                (g >= b.parts.size() || !same(a, f, b, g))) {
                added = f;
                continue;
            }
            if (g >= b.parts.size() || !same(a, f, b, g)) return false;
            ++g;
        }
        if (added == a.parts.size()) return false;
        size_t longest = 0;
        for (size_t f = 0; f < b.parts.size(); ++f)
            if (!isLink(b, f)) longest = std::max(longest, b.parts[f].size());
        out.parts[added].assign(longest, openToken());
        changed = longest > 0;
    } else if (a.parts.size() == b.parts.size()) {
        size_t edited = a.parts.size();
        for (size_t f = 0; f < a.parts.size(); ++f) {
            if (a.links[f] != b.links[f] || a.options[f] != b.options[f]) return false;  // a link or option typed
            if (samePart(a.parts[f], b.parts[f])) continue;
            if (edited != a.parts.size()) return false;  // more than one part changed (a paste?)
            edited = f;
        }
        if (edited == a.parts.size()) {
            // Nothing changed in the throws (a space, say); just add a missing ">".
            if (!a.closed && b.closed) {
                result->text = after + ">";
                result->cursor = cursor;
                return true;
            }
            return false;
        }
        const std::vector<Token>& was = b.parts[edited];
        const std::vector<Token>& now = a.parts[edited];
        const int inserted = insertedAt(was, now);
        const int deleted = insertedAt(now, was);
        const bool fillsOpen = inserted >= 0 && static_cast<size_t>(inserted) + 1 < now.size() &&
                               now[static_cast<size_t>(inserted) + 1].value == kOpen && now[static_cast<size_t>(inserted)].value != kOpen;
        if (fillsOpen) {
            // Typed just in front of a "?": it fills that "?" in (like a placeholder), so typing
            // into a part autofill made doesn't push the other parts along.
            out.parts[edited].erase(out.parts[edited].begin() + inserted + 1);
            changed = true;
        } else if (inserted >= 0) {
            changed = true;  // (spaced out in the standard style, at least)
            for (size_t g = 0; g < out.parts.size(); ++g) {
                if (g == edited || isLink(out, g)) continue;
                std::vector<Token>& part = out.parts[g];
                if (part.size() == was.size()) {
                    part.insert(part.begin() + inserted, openToken());
                    changed = true;
                } else {
                    while (part.size() < now.size()) {
                        part.push_back(openToken());
                        changed = true;
                    }
                }
            }
        } else if (deleted >= 0) {
            const Token& gone = was[static_cast<size_t>(deleted)];
            changed = true;  // (tidy up the spaces the deleted throw leaves, at least)
            for (size_t g = 0; g < out.parts.size(); ++g) {
                if (g == edited || isLink(out, g)) continue;
                std::vector<Token>& part = out.parts[g];
                if (part.size() != was.size()) continue;
                // Deleting a real throw takes the "?"s across from it with it. Deleting a "?" only
                // deletes that "?": other jugglers' parts, and their real throws above all, are
                // never touched by it.
                if (gone.value != kOpen && part[static_cast<size_t>(deleted)].value == kOpen) {
                    part.erase(part.begin() + deleted);
                    changed = true;
                    columnDeleted = true;
                    deletedPart = static_cast<int>(edited);
                    deletedColumn = deleted;
                }
            }
        } else {
            return false;  // a throw changed in place: nothing to fill in
        }
    } else {
        return false;
    }
    if (!changed && a.closed) return false;
    std::vector<int> partStart;
    std::vector<std::vector<int>> tokenEnds;
    result->text = format(out, &partStart, &tokenEnds);
    result->cursor = mapCursor(a, cursor, partStart, tokenEnds);
    if (columnDeleted && memory) {
        memory->armed = true;
        memory->text = result->text;
        memory->cursor = result->cursor;
        memory->beforeDelete = before;
        memory->part = deletedPart;
        memory->column = deletedColumn;
    }
    return result->text != after;
}

}  // namespace

bool bumpThrowAtCursor(const std::string& text, int cursor, int delta, SiteswapEdit* result) {
    const Parsed p = parse(text);
    if (!p.ok) return false;
    size_t part = 0, index = 0;
    if (!tokenAtCursor(p, cursor, &part, &index)) return false;
    const Token& t = p.parts[part][index];
    Token changed = t;
    if (t.value == kOpen) {
        if (delta < 0) return false;
        changed.value = 0;
    } else {
        int v = t.value + delta;
        while (v >= 0 && v <= 35 && !writable(v)) v += delta;
        if (v < 0) {
            changed.value = kOpen;
        } else if (v > 35) {
            return false;
        } else {
            changed.value = v;
        }
    }
    if (changed.value == 0 || changed.value == kOpen) {
        changed.pass = false;  // an empty hand (or an undecided throw) isn't a pass
        changed.target = -1;
        changed.relative = kNoRelative;
    }
    result->text = replaceTokens(text, {{t, changed}});
    result->cursor = t.start + static_cast<int>(tokenText(changed).size());
    return true;
}

// Which juggler a token's throw goes to (thrown by `juggler`, in a pattern of `jugglers`).
int destinationOf(const Token& t, int juggler, int jugglers) {
    if (!t.pass) return juggler;
    if (t.relative != kNoRelative) return ((juggler + t.relative) % jugglers + jugglers) % jugglers;
    if (t.target >= 1 && jugglers > 2 && t.target <= jugglers) return t.target - 1;
    return jugglers == 2 ? 1 - juggler : juggler;  // (a bare "p" with 3+ jugglers is incomplete)
}

bool stepPassTarget(const std::string& text, int cursor, int delta, SiteswapEdit* result, std::string* why) {
    const Parsed p = parse(text);
    if (!p.ok || !p.passing || p.parts.size() < 2) {
        if (why) *why = "Only a passing pattern's throws can go to another juggler.";
        return false;
    }
    size_t part = 0, index = 0;
    if (!tokenAtCursor(p, cursor, &part, &index)) return false;
    const Token& t = p.parts[part][index];
    if (t.value == kOpen || t.value == 0) {
        if (why) *why = t.value == 0 ? "A 0 (an empty hand) can't be a pass." : "Decide the throw (?) first.";
        return false;
    }
    const int jugglers = static_cast<int>(p.parts.size());
    const int juggler = static_cast<int>(part);
    bool relativeStyle = false;
    for (const std::vector<Token>& f : p.parts)
        for (const Token& k : f)
            if (k.relative != kNoRelative) relativeStyle = true;
    const int along = ((destinationOf(t, juggler, jugglers) - juggler) % jugglers + jugglers) % jugglers;
    const int next = ((along + delta) % jugglers + jugglers) % jugglers;
    Token changed = t;
    changed.target = -1;
    changed.relative = kNoRelative;
    changed.pass = next != 0;
    if (changed.pass) {
        if (relativeStyle) changed.relative = next;
        else if (jugglers > 2) changed.target = (juggler + next) % jugglers + 1;
    }
    result->text = replaceTokens(text, {{t, changed}});
    result->cursor = t.start + static_cast<int>(tokenText(changed).size());
    return true;
}

bool swapThrows(const std::string& text, int selectionStart, int selectionEnd, int cursor, SiteswapEdit* result,
                std::string* why) {
    const Parsed p = parse(text);
    if (!p.ok) {
        if (why) *why = "That isn't siteswap yet.";
        return false;
    }
    if (selectionStart > selectionEnd) std::swap(selectionStart, selectionEnd);
    size_t part = 0, i = 0, j = 0;
    int d = 0;  // how many beats after throw i throw j is made
    bool found = false;
    if (selectionStart != selectionEnd) {
        // The throws the selection touches: exactly two, in one juggler's part.
        int count = 0;
        for (size_t f = 0; f < p.parts.size(); ++f)
            for (size_t k = 0; k < p.parts[f].size(); ++k) {
                const Token& t = p.parts[f][k];
                if (t.end <= selectionStart || t.start >= selectionEnd) continue;
                if (count == 0) {
                    part = f;
                    i = k;
                } else if (f != part) {
                    if (why) *why = "Select two throws in the same juggler's part.";
                    return false;
                } else {
                    j = k;
                }
                ++count;
            }
        if (count != 2) {
            if (why) *why = "Select two throws to swap (or put the cursor just after them).";
            return false;
        }
        d = static_cast<int>(j - i);
        found = true;
    } else {
        // The two throws just before the cursor, in its part.
        for (size_t f = 0; f < p.parts.size() && !found; ++f) {
            int last = -1;
            for (size_t k = 0; k < p.parts[f].size(); ++k)
                if (p.parts[f][k].end <= cursor && (f + 1 >= p.partStarts.size() || cursor <= p.partStarts[f + 1]))
                    last = static_cast<int>(k);
            if (cursor < p.partStarts[f]) continue;
            if (f + 1 < p.partStarts.size() && cursor >= p.partStarts[f + 1]) continue;
            // The pattern repeats, so before the part's first throw come the last throws of the
            // repeat before: with the cursor after the first throw, it swaps with the last one
            // (531 becomes 036), and at the start of the part, the last two swap.
            const int n = static_cast<int>(p.parts[f].size());
            if (n < 2) continue;
            part = f;
            if (last >= 1) {
                i = static_cast<size_t>(last - 1);
                j = static_cast<size_t>(last);
            } else if (last == 0) {
                i = static_cast<size_t>(n - 1);  // (beat -1)
                j = 0;
            } else {
                i = static_cast<size_t>(n - 2);
                j = static_cast<size_t>(n - 1);
            }
            d = 1;
            found = true;
        }
        if (!found) {
            if (why) *why = "Put the cursor just after two throws (or select two) to swap them.";
            return false;
        }
    }
    const Token& first = p.parts[part][i];
    const Token& second = p.parts[part][j];
    if (first.value == kOpen || second.value == kOpen) {
        if (why) *why = "A throw that isn't decided yet (?) can't be swapped.";
        return false;
    }
    Token newFirst = second;
    newFirst.value = second.value + d;
    Token newSecond = first;
    newSecond.value = first.value - d;
    if (newSecond.value < 0) {
        if (why)
            *why = "Can't swap: the second throw would have to land before it's thrown (" +
                   std::to_string(first.value) + " - " + std::to_string(d) + " < 0).";
        return false;
    }
    for (const Token* t : {&newFirst, &newSecond}) {
        if (!writable(t->value)) {
            if (why)
                *why = "Can't swap: a throw would become a " + std::to_string(t->value) +
                       (t->value > 35 ? ", higher than siteswap can write (35)."
                                      : ", which siteswap can't write (its letter means passing or sync).");
            return false;
        }
        if (t->value == 0 && t->pass && destinationOf(*t, static_cast<int>(part), static_cast<int>(p.parts.size())) != static_cast<int>(part)) {
            if (why) *why = "Can't swap: a pass would become a 0 (an empty hand can't be a pass).";
            return false;
        }
    }
    result->text = replaceTokens(text, {{first, newFirst}, {second, newSecond}});
    // The cursor stays put among the throws, moving with any that change length before it.
    auto growthBeforeCursor = [cursor](const Token& was, const Token& now) {
        return was.end <= cursor ? static_cast<int>(tokenText(now).size()) - (was.end - was.start) : 0;
    };
    result->cursor = cursor + growthBeforeCursor(first, newFirst) + growthBeforeCursor(second, newSecond);
    return true;
}

bool rotateThrows(const std::string& text, int steps, int cursor, SiteswapEdit* result, std::string* why) {
    const Parsed p = parse(text);
    if (!p.ok) {
        if (why) *why = "That isn't siteswap yet.";
        return false;
    }
    Parsed out = p;
    if (out.passing) out.closed = true;
    bool any = false;
    for (std::vector<Token>& part : out.parts) {
        const int n = static_cast<int>(part.size());
        if (n < 2) continue;
        const int shift = ((steps % n) + n) % n;  // to the right
        std::rotate(part.begin(), part.begin() + (n - shift), part.end());
        any = any || shift != 0;
    }
    if (!any) {
        if (why) *why = "Nothing to rotate: a pattern needs two or more throws.";
        return false;
    }
    std::vector<int> partStart;
    std::vector<std::vector<int>> tokenEnds;
    result->text = format(out, &partStart, &tokenEnds);
    result->cursor = mapCursor(p, cursor, partStart, tokenEnds);
    return true;
}

std::string tidySiteswap(const std::string& text) {
    Parsed p = parse(text);
    if (!p.ok || (p.parts.size() == 1 && p.parts[0].empty())) return text;
    p.closed = true;
    // With two jugglers a digit right after a "p" is the next throw ("<3p3|3p3>" is 3p, 3), so
    // the tidy text spaces it out.
    if (p.passing && p.parts.size() == 2) {
        for (std::vector<Token>& part : p.parts) {
            for (size_t k = 0; k < part.size(); ++k) {
                if (part[k].target < 0) continue;
                Token next;
                next.value = part[k].target;
                part[k].target = -1;
                part.insert(part.begin() + static_cast<std::ptrdiff_t>(k) + 1, next);
            }
        }
    }
    return format(p);
}

bool throwAtCharacter(const std::string& text, int index, int* juggler, int* beat) {
    const Parsed p = parse(text);
    if (!p.ok) return false;
    for (size_t f = 0; f < p.parts.size(); ++f)
        for (size_t k = 0; k < p.parts[f].size(); ++k)
            if (p.parts[f][k].start <= index && index < p.parts[f][k].end) {
                *juggler = static_cast<int>(f);
                *beat = static_cast<int>(k);
                return true;
            }
    return false;
}

namespace {

// "@2+3" -> juggler 1 (0-based), offset 3. False if it isn't one.
bool readLink(const std::string& link, int* to, int* offset) {
    if (link.size() < 2 || link[0] != '@') return false;
    size_t i = 1;
    int number = 0;
    bool any = false;
    while (i < link.size() && std::isdigit(static_cast<unsigned char>(link[i]))) {
        number = number * 10 + (link[i] - '0');
        any = true;
        ++i;
    }
    if (!any) return false;
    int off = 0;
    if (i < link.size()) {
        const int sign = link[i] == '-' ? -1 : (link[i] == '+' ? 1 : 0);
        if (sign == 0) return false;
        ++i;
        bool digits = false;
        while (i < link.size() && std::isdigit(static_cast<unsigned char>(link[i]))) {
            off = off * 10 + (link[i] - '0');
            digits = true;
            ++i;
        }
        if (!digits || i != link.size()) return false;
        off *= sign;
    }
    *to = number - 1;
    *offset = off;
    return true;
}

bool hasLrSwap(const std::string& options) {
    std::string lower;
    for (const char c : options) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("lrswap") != std::string::npos;
}

}  // namespace

bool linkAtCharacter(const std::string& text, int index, int* juggler, int* to, int* offset, bool* lrSwap) {
    const Parsed p = parse(text);
    if (!p.ok) return false;
    for (size_t f = 0; f < p.parts.size(); ++f) {
        if (p.links[f].empty() || index < p.linkStart[f] || index >= p.linkEnd[f]) continue;
        if (!readLink(p.links[f], to, offset)) return false;
        *juggler = static_cast<int>(f);
        *lrSwap = hasLrSwap(p.options[f]);
        return true;
    }
    return false;
}

bool charactersOfThrow(const std::string& text, int juggler, int beat, int* start, int* end) {
    Parsed p = parse(text);
    if (!p.ok || juggler < 0 || juggler >= static_cast<int>(p.parts.size())) return false;
    // A linked juggler's throw is written where the throw it copies is.
    for (size_t steps = 0; !p.links[static_cast<size_t>(juggler)].empty(); ++steps) {
        int to = 0, offset = 0;
        if (steps > p.parts.size() || !readLink(p.links[static_cast<size_t>(juggler)], &to, &offset) || to < 0 ||
            to >= static_cast<int>(p.parts.size()))
            return false;
        juggler = to;
        beat -= offset;
    }
    const std::vector<Token>& part = p.parts[static_cast<size_t>(juggler)];
    if (!part.empty()) beat = ((beat % static_cast<int>(part.size())) + static_cast<int>(part.size())) % static_cast<int>(part.size());
    if (beat < 0 || beat >= static_cast<int>(part.size())) return false;
    *start = part[static_cast<size_t>(beat)].start;
    *end = part[static_cast<size_t>(beat)].end;
    return true;
}

bool throwAtCursor(const std::string& text, int cursor, int* juggler, int* beat) {
    const Parsed p = parse(text);
    if (!p.ok) return false;
    size_t part = 0, index = 0;
    if (!tokenAtCursor(p, cursor, &part, &index)) return false;
    *juggler = static_cast<int>(part);
    *beat = static_cast<int>(index);
    return true;
}

bool linkToClickedThrow(const std::string& text, int cursor, int clickIndex, SiteswapEdit* result, std::string* why) {
    const Parsed p = parse(text);
    if (!p.ok) {
        *why = "The pattern can't be read as it is, so it can't be linked";
        return false;
    }
    if (!p.passing || p.parts.size() < 2) {
        *why = "Links are for passing patterns";
        return false;
    }
    // The part the cursor is in (just after a "|" is the part that follows it).
    size_t mine = 0;
    for (size_t f = 1; f < p.parts.size(); ++f)
        if (cursor >= p.partStarts[f]) mine = f;
    // The throw clicked.
    int other = -1, position = -1;
    for (size_t f = 0; f < p.parts.size() && other < 0; ++f)
        for (size_t k = 0; k < p.parts[f].size(); ++k)
            if (p.parts[f][k].start <= clickIndex && clickIndex < p.parts[f][k].end) {
                other = static_cast<int>(f);
                position = static_cast<int>(k);
                break;
            }
    const std::string me = "J" + std::to_string(mine + 1);
    if (other < 0) {
        *why = "Ctrl+click a throw in another juggler's part: " + me + " copies them, starting on that beat";
        return false;
    }
    if (other == static_cast<int>(mine)) {
        *why = "That's " + me + "'s own part: Ctrl+click a throw in another juggler's part";
        return false;
    }
    // Written the way the text's other links are: negative if any of them is.
    bool negative = false;
    for (const std::string& link : p.links) {
        int to = 0, off = 0;
        if (!link.empty() && readLink(link, &to, &off) && off < 0) negative = true;
    }
    const int period = static_cast<int>(p.parts[static_cast<size_t>(other)].size());
    int offset = position;
    if (negative && offset > 0) offset -= period;
    std::string link = "@" + std::to_string(other + 1);
    if (offset != 0) link += (offset < 0 ? "-" : "+") + std::to_string(std::abs(offset));
    // The part's characters: from just after its "<" or "|" up to the next "|" or ">".
    const size_t start = static_cast<size_t>(p.partStarts[mine]);
    size_t end = text.size();
    if (mine + 1 < p.parts.size()) {
        end = static_cast<size_t>(p.partStarts[mine + 1] - 1);
    } else {
        const size_t close = text.find('>', start);
        if (close != std::string::npos) end = close;
    }
    result->text = text.substr(0, start) + link + p.options[mine] + text.substr(end);
    result->cursor = static_cast<int>(start + link.size());
    return true;
}
