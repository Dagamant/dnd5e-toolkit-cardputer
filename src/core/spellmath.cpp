#include "spellmath.h"

namespace spellmath {

static bool parseDie(const String& s, int& n, int& d) {
    int k = s.indexOf('d');
    if (k < 0) return false;
    n = k ? s.substring(0, k).toInt() : 1;
    d = s.substring(k + 1).toInt();
    for (size_t i = k + 1; i < s.length(); i++) if (!isdigit((unsigned char)s[i])) return false;
    return n > 0 && d > 0;
}

String addDice(const String& base, const String& per, int times) {
    if (times <= 0 || !per.length()) return base;
    int pn, pd;
    if (!parseDie(per, pn, pd)) return base;
    // merge with the first term of base if it is the same die
    int plus = base.indexOf('+');
    String first = plus < 0 ? base : base.substring(0, plus);
    String rest = plus < 0 ? "" : base.substring(plus);
    int bn, bd;
    if (parseDie(first, bn, bd) && bd == pd) return String(bn + pn * times) + "d" + bd + rest;
    return base + "+" + String(pn * times) + "d" + pd;
}

String upcastDice(const String& higher) {
    int k = higher.indexOf("increases by ");
    while (k >= 0) {
        int s = k + 13, e = s;
        while (e < (int)higher.length() && (isdigit((unsigned char)higher[e]) || higher[e] == 'd')) e++;
        String d = higher.substring(s, e);
        if (d.indexOf('d') > 0 && higher.indexOf("for each spell slot level", e) >= 0) return d;
        k = higher.indexOf("increases by ", e);
    }
    return "";
}

String cantripDice(const String& base, const String& higher, int lvl) {
    if (!higher.startsWith("Cantrip Upgrade")) return base;
    int tier = lvl >= 17 ? 3 : lvl >= 11 ? 2 : lvl >= 5 ? 1 : 0;
    if (!tier) return base;
    const char* marks[] = {"", "5 (", "11 (", "17 ("};
    int k = higher.indexOf(marks[tier]);
    if (k < 0) return base;
    int s = k + strlen(marks[tier]);
    int e = higher.indexOf(')', s);
    if (e < 0) return base;
    String d = higher.substring(s, e);
    // keep any flat bonus from the base ("1d10" vs "1d6+MOD")
    int plus = base.indexOf('+');
    return plus < 0 ? d : d + base.substring(plus);
}

String castExpr(const String& baseIn, const String& higher, int level, int slot, int charLevel, int mod) {
    String e = baseIn;
    if (level == 0) e = cantripDice(e, higher, charLevel);
    else if (slot > level) e = addDice(e, upcastDice(higher), slot - level);
    if (e.indexOf("+MOD") >= 0) {
        e.replace("+MOD", mod ? (mod > 0 ? String("+") + mod : String(mod)) : String(""));
    }
    return e;
}

}  // namespace spellmath
