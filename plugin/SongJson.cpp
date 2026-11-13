#include "SongJson.h"

#include <cmath>

namespace hocket::json {

namespace {

juce::String hex(std::uint64_t x) { return juce::String::toHexString(static_cast<juce::int64>(x)).paddedLeft('0', 16); }
std::uint64_t unhex(const juce::var& v, std::uint64_t fallback) {
    return v.isString() ? static_cast<std::uint64_t>(v.toString().getHexValue64()) : fallback;
}
int num(const juce::var& o, const char* key, int fallback) {
    const juce::var v = o[key];
    return v.isVoid() ? fallback : static_cast<int>(v);
}
bool flag(const juce::var& o, const char* key, bool fallback) {
    const juce::var v = o[key];
    return v.isVoid() ? fallback : static_cast<bool>(v);
}

juce::var overrides(const Overrides& o) {
    auto* d = new juce::DynamicObject();
    auto put = [&](const char* k, const std::optional<int>& x) {
        if (x) d->setProperty(k, *x);
    };
    put("range", o.range);
    put("wander", o.wander);
    put("accent", o.accent);
    put("gatePercent", o.gatePercent);
    put("octaveLow", o.octaveLow);
    put("octaveHigh", o.octaveHigh);
    return juce::var(d);
}
Overrides overridesFrom(const juce::var& v) {
    Overrides o;
    auto get = [&](const char* k) -> std::optional<int> {
        const juce::var x = v[k];
        if (x.isVoid()) return std::nullopt;
        return static_cast<int>(x);
    };
    o.range = get("range");
    o.wander = get("wander");
    o.accent = get("accent");
    o.gatePercent = get("gatePercent");
    o.octaveLow = get("octaveLow");
    o.octaveHigh = get("octaveHigh");
    return o;
}

// §16.9: a slot's field. Nothing for a slot that has never been set, so a song that uses only the defaults is written
// as before. What this build couldn't read (a newer build's) went into `extra`, and comes back out here.
juce::var genSlot(const GenSlot& g) {
    if (g.use.empty() && g.params.empty() && g.extra.empty() && g.sources.empty()) return {};
    const juce::var extra = g.extra.empty() ? juce::var() : juce::JSON::parse(juce::String(g.extra));
    auto* o = new juce::DynamicObject();
    if (auto* e = extra.getDynamicObject())
        for (const auto& kv : e->getProperties())
            if (kv.name.toString() != "params") o->setProperty(kv.name, kv.value);
    if (!g.use.empty()) o->setProperty("use", juce::String(g.use));
    auto* params = new juce::DynamicObject();
    if (auto* e = extra["params"].getDynamicObject())
        for (const auto& kv : e->getProperties()) params->setProperty(kv.name, kv.value.clone());
    for (const auto& [name, values] : g.params) {
        juce::var gen = params->getProperty(juce::Identifier(name));
        if (!gen.getDynamicObject()) gen = juce::var(new juce::DynamicObject());
        for (const auto& [key, x] : values) gen.getDynamicObject()->setProperty(juce::Identifier(key), x);
        params->setProperty(juce::Identifier(name), gen);
    }
    // §16.6: a derived generator's source, as an id (16 hex digits) under its generator's parameters.
    for (const auto& [name, id] : g.sources) {
        if (id == 0) continue;
        juce::var gen = params->getProperty(juce::Identifier(name));
        if (!gen.getDynamicObject()) gen = juce::var(new juce::DynamicObject());
        gen.getDynamicObject()->setProperty("source", hex(id));
        params->setProperty(juce::Identifier(name), gen);
    }
    if (!params->getProperties().isEmpty()) o->setProperty("params", juce::var(params));
    else delete params;
    return juce::var(o);
}
bool whole(const juce::var& x) {
    if (x.isInt() || x.isInt64()) return true;
    if (!x.isDouble()) return false;
    const double d = static_cast<double>(x);
    return std::abs(d - std::round(d)) < 1e-9 && std::abs(d) < 2e9;  // a whole number written as 3.0
}
GenSlot genSlotFrom(const juce::var& v) {
    GenSlot g;
    auto* o = v.getDynamicObject();
    if (!o) return g;
    auto* extra = new juce::DynamicObject();
    juce::var extraVar(extra);
    for (const auto& kv : o->getProperties()) {
        const juce::String k = kv.name.toString();
        if (k == "use" && kv.value.isString()) {
            g.use = kv.value.toString().toStdString();
        } else if (k == "params" && kv.value.getDynamicObject()) {
            auto* rest = new juce::DynamicObject();
            juce::var restVar(rest);
            for (const auto& gen : kv.value.getDynamicObject()->getProperties()) {
                auto* values = gen.value.getDynamicObject();
                if (!values) {
                    rest->setProperty(gen.name, gen.value);
                    continue;
                }
                auto* odd = new juce::DynamicObject();
                juce::var oddVar(odd);
                for (const auto& x : values->getProperties()) {
                    if (x.name.toString() == "source" && x.value.isString())  // §16.6: an id
                        g.sources[gen.name.toString().toStdString()] = unhex(x.value, 0);
                    else if (whole(x.value)) g.params[gen.name.toString().toStdString()][x.name.toString().toStdString()] = static_cast<int>(x.value);
                    else odd->setProperty(x.name, x.value);
                }
                if (!odd->getProperties().isEmpty()) rest->setProperty(gen.name, oddVar);
            }
            if (!rest->getProperties().isEmpty()) extra->setProperty("params", restVar);
        } else {
            extra->setProperty(kv.name, kv.value);
        }
    }
    if (!extra->getProperties().isEmpty()) g.extra = juce::JSON::toString(extraVar, true).toStdString();
    return g;
}


// ---- §17.7: mod ------------------------------------------------------------------------------------------------

juce::var parseExtra(const std::string& text) { return text.empty() ? juce::var() : juce::JSON::parse(juce::String(text)); }
std::string extraText(juce::DynamicObject* o) {
    if (o->getProperties().isEmpty()) return {};
    return juce::JSON::toString(juce::var(o), true).toStdString();
}
bool wholeArray(const juce::var& v) {
    const auto* a = v.getArray();
    if (!a) return false;
    for (const juce::var& x : *a)
        if (!whole(x)) return false;
    return true;
}
bool objectArray(const juce::var& v) {
    const auto* a = v.getArray();
    if (!a || a->isEmpty()) return false;
    for (const juce::var& x : *a) {
        auto* o = x.getDynamicObject();
        if (!o) return false;
        for (const auto& kv : o->getProperties())
            if (!whole(kv.value)) return false;
    }
    return true;
}

juce::var source(const ModSource& src) {
    auto* o = new juce::DynamicObject();
    juce::var out(o);
    const juce::var extra = parseExtra(src.extra);
    if (auto* e = extra.getDynamicObject())
        for (const auto& kv : e->getProperties())
            if (kv.name.toString() != "params") o->setProperty(kv.name, kv.value);
    o->setProperty("id", hex(src.id));
    o->setProperty("kind", juce::String(src.kind));
    o->setProperty("name", juce::String::fromUTF8(src.name.c_str()));
    auto* params = new juce::DynamicObject();
    if (auto* e = extra["params"].getDynamicObject())
        for (const auto& kv : e->getProperties()) params->setProperty(kv.name, kv.value.clone());
    for (const auto& [k, x] : src.params) params->setProperty(juce::Identifier(k), x);
    for (const auto& [k, set] : src.sets) {
        juce::Array<juce::var> a;
        for (int x : set) a.add(x);
        params->setProperty(juce::Identifier(k), a);
    }
    for (const auto& [k, list] : src.lists) {
        juce::Array<juce::var> a;
        for (const auto& item : list) {
            auto* io = new juce::DynamicObject();
            for (const auto& [ik, x] : item) io->setProperty(juce::Identifier(ik), x);
            a.add(juce::var(io));
        }
        params->setProperty(juce::Identifier(k), a);
    }
    o->setProperty("params", juce::var(params));
    return out;
}

ModSource sourceFrom(const juce::var& v) {
    ModSource src;
    auto* extra = new juce::DynamicObject();
    juce::var extraVar(extra);
    if (auto* o = v.getDynamicObject())
        for (const auto& kv : o->getProperties()) {
            const juce::String k = kv.name.toString();
            if (k == "id") src.id = unhex(kv.value, 0);
            else if (k == "kind" && kv.value.isString()) src.kind = kv.value.toString().toStdString();
            else if (k == "name" && kv.value.isString()) src.name = kv.value.toString().toStdString();
            else if (k == "params" && kv.value.getDynamicObject()) {
                auto* rest = new juce::DynamicObject();
                juce::var restVar(rest);
                for (const auto& p : kv.value.getDynamicObject()->getProperties()) {
                    const std::string pk = p.name.toString().toStdString();
                    if (whole(p.value)) src.params[pk] = static_cast<int>(p.value);
                    else if (wholeArray(p.value)) {
                        auto& set = src.sets[pk];
                        for (const juce::var& x : *p.value.getArray()) set.push_back(static_cast<int>(x));
                    } else if (objectArray(p.value)) {
                        auto& list = src.lists[pk];
                        for (const juce::var& x : *p.value.getArray()) {
                            std::map<std::string, int> item;
                            for (const auto& f : x.getDynamicObject()->getProperties()) item[f.name.toString().toStdString()] = static_cast<int>(f.value);
                            list.push_back(item);
                        }
                    } else rest->setProperty(p.name, p.value);
                }
                if (!rest->getProperties().isEmpty()) extra->setProperty("params", restVar);
            } else extra->setProperty(kv.name, kv.value);
        }
    src.extra = extraText(extra);
    return src;
}

// §17.5: a condition, {"kind": "AB", "a": 1, "b": 4, "not": true}. One this build doesn't know is written as read.
juce::var condition(const Condition& c) {
    using K = Condition::Kind;
    if (c.kind == K::None) return {};
    if (c.kind == K::Unknown) return parseExtra(c.extra);
    auto* o = new juce::DynamicObject();
    static const char* names[] = {"", "AB", "first", "pre", "nei", "fill", "gong", "kenong"};
    o->setProperty("kind", names[static_cast<int>(c.kind)]);
    if (c.kind == K::AB) o->setProperty("a", c.a), o->setProperty("b", c.b);
    if (c.kind == K::Nei) o->setProperty("item", hex(c.item));
    if (c.negate) o->setProperty("not", true);
    return juce::var(o);
}
Condition conditionFrom(const juce::var& v) {
    using K = Condition::Kind;
    Condition c;
    if (!v.getDynamicObject()) return c;
    const juce::String kind = v["kind"].toString();
    c.negate = static_cast<bool>(v.getProperty("not", false));
    if (kind == "AB") c.kind = K::AB, c.a = num(v, "a", 1), c.b = num(v, "b", 1);
    else if (kind == "first") c.kind = K::First;
    else if (kind == "pre") c.kind = K::Pre;
    else if (kind == "nei") c.kind = K::Nei, c.item = unhex(v["item"], 0);
    else if (kind == "fill") c.kind = K::Fill;
    else if (kind == "gong") c.kind = K::Gong;  // §18.2
    else if (kind == "kenong") c.kind = K::Kenong;
    else c.kind = K::Unknown, c.extra = juce::JSON::toString(v, true).toStdString();
    return c;
}

juce::var cord(const Cord& c) {
    auto* o = new juce::DynamicObject();
    juce::var out(o);
    const juce::var extra = parseExtra(c.extra);
    if (auto* e = extra.getDynamicObject())
        for (const auto& kv : e->getProperties()) o->setProperty(kv.name, kv.value);
    o->setProperty("id", hex(c.id));
    o->setProperty("source", hex(c.source));
    o->setProperty("output", juce::String(c.output));
    auto* d = new juce::DynamicObject();
    d->setProperty("scope", static_cast<int>(c.dest.scope));
    d->setProperty("target", hex(c.dest.target));
    d->setProperty("key", juce::String(c.dest.key));
    o->setProperty("dest", juce::var(d));
    // §17.7: a missing field is its default (§17.3), so only what differs is written.
    const Cord def;
    if (c.move != def.move) o->setProperty("move", c.move);
    if (c.scale != def.scale) o->setProperty("scale", c.scale);
    if (c.maxShift != def.maxShift) o->setProperty("maxShift", c.maxShift);
    if (c.min) o->setProperty("min", *c.min);
    if (c.max) o->setProperty("max", *c.max);
    if (c.lasts != def.lasts) o->setProperty("lasts", c.lasts);
    if (!c.allowed.empty()) {
        juce::Array<juce::var> a;
        for (int x : c.allowed) a.add(x);
        o->setProperty("allowed", a);
    }
    if (c.fixedSeed != def.fixedSeed) o->setProperty("fixedSeed", c.fixedSeed);
    if (const juce::var cv = condition(c.cond); !cv.isVoid()) o->setProperty("cond", cv);
    if (c.chance != def.chance) o->setProperty("chance", c.chance);
    if (c.depth != def.depth) o->setProperty("depth", c.depth);
    if (c.threshold != def.threshold) o->setProperty("threshold", c.threshold);
    if (c.invert != def.invert) o->setProperty("invert", c.invert);
    if (c.on != def.on) o->setProperty("on", c.on);
    return out;
}

Cord cordFrom(const juce::var& v) {
    Cord c;
    auto* extra = new juce::DynamicObject();
    juce::var extraVar(extra);
    if (auto* o = v.getDynamicObject())
        for (const auto& kv : o->getProperties()) {
            const juce::String k = kv.name.toString();
            const juce::var& x = kv.value;
            if (k == "id") c.id = unhex(x, 0);
            else if (k == "source") c.source = unhex(x, 0);
            else if (k == "output" && x.isString()) c.output = x.toString().toStdString();
            else if (k == "dest" && x.getDynamicObject()) {
                c.dest.scope = static_cast<Scope>(num(x, "scope", 0));
                c.dest.target = unhex(x["target"], 0);
                c.dest.key = x["key"].toString().toStdString();
            } else if (k == "move" && whole(x)) c.move = static_cast<int>(x);
            else if (k == "scale" && whole(x)) c.scale = static_cast<int>(x);
            else if (k == "maxShift" && whole(x)) c.maxShift = static_cast<int>(x);
            else if (k == "min" && whole(x)) c.min = static_cast<int>(x);
            else if (k == "max" && whole(x)) c.max = static_cast<int>(x);
            else if (k == "lasts" && whole(x)) c.lasts = static_cast<int>(x);
            else if (k == "allowed" && wholeArray(x)) {
                for (const juce::var& a : *x.getArray()) c.allowed.push_back(static_cast<int>(a));
            } else if (k == "fixedSeed" && x.isBool()) c.fixedSeed = static_cast<bool>(x);
            else if (k == "cond" && x.getDynamicObject()) c.cond = conditionFrom(x);
            else if (k == "chance" && whole(x)) c.chance = static_cast<int>(x);
            else if (k == "depth" && whole(x)) c.depth = static_cast<int>(x);
            else if (k == "threshold" && whole(x)) c.threshold = static_cast<int>(x);
            else if (k == "invert" && x.isBool()) c.invert = static_cast<bool>(x);
            else if (k == "on" && x.isBool()) c.on = static_cast<bool>(x);
            else extra->setProperty(kv.name, x);
        }
    c.extra = extraText(extra);
    return c;
}

juce::var mod(const Mod& m) {
    if (m.sources.empty() && m.cords.empty() && m.extra.empty()) return {};  // a song without it is written as before
    auto* o = new juce::DynamicObject();
    juce::var out(o);
    const juce::var extra = parseExtra(m.extra);
    if (auto* e = extra.getDynamicObject())
        for (const auto& kv : e->getProperties()) o->setProperty(kv.name, kv.value);
    juce::Array<juce::var> sources, cords;
    for (const ModSource& s : m.sources) sources.add(source(s));
    for (const Cord& c : m.cords) cords.add(cord(c));
    o->setProperty("sources", sources);
    o->setProperty("cords", cords);
    return out;
}

Mod modFrom(const juce::var& v) {
    Mod m;
    auto* extra = new juce::DynamicObject();
    juce::var extraVar(extra);
    if (auto* o = v.getDynamicObject())
        for (const auto& kv : o->getProperties()) {
            const juce::String k = kv.name.toString();
            if (k == "sources" && kv.value.getArray())
                for (const juce::var& x : *kv.value.getArray()) m.sources.push_back(sourceFrom(x));
            else if (k == "cords" && kv.value.getArray())
                for (const juce::var& x : *kv.value.getArray()) m.cords.push_back(cordFrom(x));
            else extra->setProperty(kv.name, kv.value);
        }
    m.extra = extraText(extra);
    return m;
}

// ---- §18.1: form -----------------------------------------------------------------------------------------------

juce::var form(const Form& f) {
    if (!f.on && f.text.empty() && f.letters.empty() && f.extra.empty()) return {};  // a song without one is written as before
    auto* o = new juce::DynamicObject();
    juce::var out(o);
    const juce::var extra = parseExtra(f.extra);
    if (auto* e = extra.getDynamicObject())
        for (const auto& kv : e->getProperties()) o->setProperty(kv.name, kv.value);
    o->setProperty("on", f.on);
    o->setProperty("text", juce::String::fromUTF8(f.text.c_str()));
    o->setProperty("section", f.section);
    o->setProperty("end", f.end);
    o->setProperty("prime", f.prime);
    juce::Array<juce::var> letters;
    for (const FormLetter& l : f.letters) {
        auto* x = new juce::DynamicObject();
        juce::var xv(x);
        const juce::var lextra = parseExtra(l.extra);
        if (auto* e = lextra.getDynamicObject())
            for (const auto& kv : e->getProperties()) x->setProperty(kv.name, kv.value);
        x->setProperty("name", juce::String::charToString(static_cast<juce::juce_wchar>(l.name)));
        x->setProperty("salt", hex(l.salt));
        juce::Array<juce::var> rolls;
        for (const auto& [name, on] : {std::pair<const char*, bool>{"notes", l.notes}, {"rhythm", l.rhythm}, {"harmony", l.harmony},
                                       {"deal", l.deal}, {"matrix", l.matrix}})
            if (on) rolls.add(juce::String(name));
        x->setProperty("rolls", rolls);
        juce::Array<juce::var> set;
        for (const auto& [d, value] : l.params) {
            auto* p = new juce::DynamicObject();
            p->setProperty("scope", static_cast<int>(d.scope));
            p->setProperty("target", hex(d.target));
            p->setProperty("key", juce::String(d.key));
            p->setProperty("value", value);
            set.add(juce::var(p));
        }
        x->setProperty("set", set);
        letters.add(xv);
    }
    o->setProperty("letters", letters);
    return out;
}

Form formFrom(const juce::var& v) {
    Form f;
    auto* extra = new juce::DynamicObject();
    juce::var extraVar(extra);
    auto* o = v.getDynamicObject();
    if (!o) return f;
    for (const auto& kv : o->getProperties()) {
        const juce::String k = kv.name.toString();
        const juce::var& x = kv.value;
        if (k == "on" && x.isBool()) f.on = static_cast<bool>(x);
        else if (k == "text" && x.isString()) f.text = x.toString().toStdString();
        else if (k == "section" && whole(x)) f.section = static_cast<int>(x);
        else if (k == "end" && whole(x)) f.end = static_cast<int>(x);
        else if (k == "prime" && whole(x)) f.prime = static_cast<int>(x);
        else if (k == "letters" && x.getArray()) {
            for (const juce::var& lv : *x.getArray()) {
                auto* lo = lv.getDynamicObject();
                if (!lo) continue;
                FormLetter l;
                l.notes = l.rhythm = false;
                auto* lextra = new juce::DynamicObject();
                juce::var lextraVar(lextra);
                for (const auto& p : lo->getProperties()) {
                    const juce::String pk = p.name.toString();
                    if (pk == "name" && p.value.isString() && p.value.toString().length() == 1) l.name = static_cast<char>(p.value.toString()[0]);
                    else if (pk == "salt" && p.value.isString()) l.salt = unhex(p.value, 0);
                    else if (pk == "rolls" && p.value.getArray()) {
                        for (const juce::var& r : *p.value.getArray()) {
                            const juce::String n = r.toString();
                            l.notes |= n == "notes", l.rhythm |= n == "rhythm", l.harmony |= n == "harmony", l.deal |= n == "deal",
                                l.matrix |= n == "matrix";
                        }
                    } else if (pk == "set" && p.value.getArray()) {
                        for (const juce::var& d : *p.value.getArray())
                            if (d.getDynamicObject() && d["key"].isString() && whole(d["value"]))
                                l.params.push_back({Dest{static_cast<Scope>(num(d, "scope", 0)), unhex(d["target"], 0), d["key"].toString().toStdString()},
                                                    static_cast<int>(d["value"])});
                    } else {
                        lextra->setProperty(p.name, p.value);
                    }
                }
                l.extra = extraText(lextra);
                f.letters.push_back(l);
            }
        } else {
            extra->setProperty(kv.name, x);
        }
    }
    f.extra = extraText(extra);
    return f;
}

}  // namespace

juce::var toVar(const Song& s) {
    auto* root = new juce::DynamicObject();
    root->setProperty("formatVersion", 1);
    root->setProperty("engineVersion", kEngineVersion);
    root->setProperty("masterSeed", hex(s.masterSeed));
    root->setProperty("matrixSeed", hex(s.matrixSeed));
    root->setProperty("matrixLocked", s.matrixLocked);
    root->setProperty("octaveJumps", s.octaveJumps);
    root->setProperty("fill", s.fill);
    root->setProperty("modeMoves", s.modeMoves);
    root->setProperty("generateCount", hex(s.generateCount));
    root->setProperty("idCounter", hex(s.idCounter));
    root->setProperty("barLength", s.barLength);
    root->setProperty("beatUnit", s.beatUnit);
    root->setProperty("phraseLength", s.phraseLength);
    root->setProperty("root", s.root);
    root->setProperty("scale", s.scale);
    root->setProperty("range", s.range);
    root->setProperty("wander", s.wander);
    root->setProperty("octaveLow", s.octaveLow);
    root->setProperty("octaveHigh", s.octaveHigh);
    root->setProperty("accent", s.accent);
    root->setProperty("colour", s.colour);
    root->setProperty("gatePercent", s.gatePercent);

    auto* h = new juce::DynamicObject();
    h->setProperty("seed", hex(s.harmony.seed));
    h->setProperty("changesEvery", s.harmony.changesEvery);
    h->setProperty("length", s.harmony.length);
    h->setProperty("range", s.harmony.range);
    h->setProperty("wander", s.harmony.wander);
    h->setProperty("locked", s.harmony.locked);
    root->setProperty("harmony", juce::var(h));

    juce::Array<juce::var> groups;
    for (const Group& g : s.groups) {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", hex(g.id));
        o->setProperty("name", juce::String(g.name));
        o->setProperty("rhythmSeed", hex(g.rhythmSeed));
        o->setProperty("dealSeed", hex(g.dealSeed));
        o->setProperty("density", g.density);
        o->setProperty("steps", g.steps);
        o->setProperty("beatDivision", g.beatDivision);
        o->setProperty("offset", g.offset);
        o->setProperty("direction", static_cast<int>(g.direction));
        o->setProperty("swing", g.swing);
        o->setProperty("jitter", g.jitter);
        o->setProperty("deal", static_cast<int>(g.deal));
        o->setProperty("overlap", static_cast<int>(g.overlap));
        o->setProperty("soloMember", g.soloMember);
        o->setProperty("dealSpan", g.dealSpan);
        if (g.handoff != 10) o->setProperty("handoff", g.handoff);  // §5.5: written only when it isn't the default
        if (g.cleanHandoffs) o->setProperty("cleanHandoffs", true);  // §5.7: likewise
        o->setProperty("probability", g.probability);
        o->setProperty("ratchet", g.ratchet);
        o->setProperty("repeats", g.repeats);
        o->setProperty("ramp", g.ramp);
        o->setProperty("fillEvery", g.fillEvery);
        o->setProperty("fillBars", g.fillBars);
        o->setProperty("fillAmount", g.fillAmount);
        o->setProperty("fillNow", g.fillNow);
        if (g.irama) o->setProperty("irama", g.irama);  // §18.3: optional
        if (g.tihai || g.tihaiUnit || g.tihaiLength != 5 || g.tihaiGap != 1 || g.tihaiNow) {  // §18.4: optional
            auto* t = new juce::DynamicObject();
            t->setProperty("every", g.tihai);
            t->setProperty("unit", g.tihaiUnit);
            t->setProperty("length", g.tihaiLength);
            t->setProperty("gap", g.tihaiGap);
            t->setProperty("now", g.tihaiNow);
            o->setProperty("tihai", juce::var(t));
        }
        o->setProperty("mute", g.mute);
        o->setProperty("lockRhythm", g.lockRhythm);
        o->setProperty("lockDeal", g.lockDeal);
        if (g.densityFromVoices) o->setProperty("densityFromVoices", true);  // §5.8: written only when it is on
        o->setProperty("slot", g.slot);  // §12.2
        o->setProperty("overrides", overrides(g.overrides));
        if (const juce::var gen = genSlot(g.rhythmGen); !gen.isVoid()) o->setProperty("rhythmGen", gen);
        juce::Array<juce::var> voices;
        for (const Voice& v : g.voices) {
            auto* vo = new juce::DynamicObject();
            vo->setProperty("id", hex(v.id));
            vo->setProperty("type", static_cast<int>(v.type));
            vo->setProperty("channel", v.channel);
            vo->setProperty("octaveTranspose", v.octaveTranspose);
            vo->setProperty("weight", v.weight);
            vo->setProperty("mute", v.mute);
            vo->setProperty("pitchSeed", hex(v.pitchSeed));
            vo->setProperty("chordSize", v.chordSize);
            vo->setProperty("voicing", v.voicing);
            vo->setProperty("articulation", static_cast<int>(v.articulation));
            vo->setProperty("nearest", v.nearest);
            vo->setProperty("humanVelocity", v.humanVelocity);
            vo->setProperty("humanTiming", v.humanTiming);
            vo->setProperty("tie", v.tie);
            vo->setProperty("lockNotes", v.lockNotes);
            if (v.density != 8) vo->setProperty("density", v.density);  // §5.8: written only when it isn't the default
            vo->setProperty("slot", v.slot);  // §12.2
            vo->setProperty("overrides", overrides(v.overrides));
            if (const juce::var gen = genSlot(v.notesGen); !gen.isVoid()) vo->setProperty("notesGen", gen);
            voices.add(juce::var(vo));
        }
        o->setProperty("voices", voices);
        groups.add(juce::var(o));
    }
    root->setProperty("groups", groups);

    juce::Array<juce::var> rows;
    for (const Row& r : s.matrix) {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", hex(r.id));
        o->setProperty("scope", static_cast<int>(r.scope));
        o->setProperty("target", hex(r.target));
        // §17.2: a destination with no number goes by its key alone.
        if (r.key.empty()) o->setProperty("param", static_cast<int>(r.param));
        else o->setProperty("key", juce::String(r.key));
        o->setProperty("every", r.every);
        o->setProperty("inPhrases", r.inPhrases);
        if (r.clock == 2 || r.clock == 3) o->setProperty("clock", r.clock == 2 ? "gong" : "kenong");  // §18.2: optional
        o->setProperty("offset", r.offset);
        o->setProperty("chance", r.chance);
        o->setProperty("maxShift", r.maxShift);
        if (r.min) o->setProperty("min", *r.min);
        if (r.max) o->setProperty("max", *r.max);
        o->setProperty("lasts", r.lasts);
        o->setProperty("fixedSeed", r.fixedSeed);
        o->setProperty("on", r.on);
        juce::Array<juce::var> allowed;
        for (int x : r.allowed) allowed.add(x);
        o->setProperty("allowed", allowed);
        if (const juce::var cv = condition(r.cond); !cv.isVoid()) o->setProperty("cond", cv);  // §17.5: optional
        rows.add(juce::var(o));
    }
    root->setProperty("matrix", rows);
    if (const juce::var m = mod(s.mod); !m.isVoid()) root->setProperty("mod", m);
    if (const juce::var f = form(s.form); !f.isVoid()) root->setProperty("form", f);  // §18.1: optional
    if (s.gongCycle != 4 || s.kenongs != 4) {  // §18.2: optional, written only when it isn't the default
        auto* g = new juce::DynamicObject();
        g->setProperty("cycle", s.gongCycle);
        g->setProperty("kenongs", s.kenongs);
        root->setProperty("gong", juce::var(g));
    }
    return juce::var(root);
}

Song fromVar(const juce::var& v) {
    const std::uint64_t master = unhex(v["masterSeed"], 0);
    Song s;  // a song with nothing in it; every field read, or left at its default
    s.masterSeed = master;
    s.generateCount = unhex(v["generateCount"], 0);
    s.idCounter = unhex(v["idCounter"], 0);
    s.barLength = num(v, "barLength", s.barLength);
    s.beatUnit = num(v, "beatUnit", s.beatUnit);
    s.phraseLength = num(v, "phraseLength", s.phraseLength);
    s.root = num(v, "root", s.root);
    s.scale = num(v, "scale", s.scale);
    s.range = num(v, "range", s.range);
    s.wander = num(v, "wander", s.wander);
    s.octaveLow = num(v, "octaveLow", s.octaveLow);
    s.octaveHigh = num(v, "octaveHigh", s.octaveHigh);
    s.accent = num(v, "accent", s.accent);
    s.colour = num(v, "colour", 0);
    // Songs from before these fields: the matrix on the master seed, octave lanes as they were.
    s.matrixSeed = unhex(v["matrixSeed"], master);
    s.matrixLocked = static_cast<bool>(v.getProperty("matrixLocked", false));
    s.octaveJumps = num(v, "octaveJumps", 16);  // a song from before Colour: the plain ranking it was made with
    s.fill = num(v, "fill", 0);                  // §4.6: songs from before it have every knob off
    s.modeMoves = num(v, "modeMoves", 0);        // §3.8: and Mode moves off, so their harmony is as it was
    s.gatePercent = num(v, "gatePercent", s.gatePercent);
    const juce::var h = v["harmony"];
    s.harmony.seed = unhex(h["seed"], s.harmony.seed);
    s.harmony.changesEvery = num(h, "changesEvery", s.harmony.changesEvery);
    s.harmony.length = num(h, "length", s.harmony.length);
    s.harmony.range = num(h, "range", s.harmony.range);
    s.harmony.wander = num(h, "wander", s.harmony.wander);
    s.harmony.locked = flag(h, "locked", false);

    if (const auto* groups = v["groups"].getArray())
        for (const juce::var& o : *groups) {
            Group g;
            g.id = unhex(o["id"], 0);
            g.name = o["name"].toString().toStdString();
            g.rhythmSeed = unhex(o["rhythmSeed"], 0);
            g.dealSeed = unhex(o["dealSeed"], 0);
            g.density = num(o, "density", g.density);
            g.steps = num(o, "steps", g.steps);
            g.beatDivision = num(o, "beatDivision", g.beatDivision);
            g.offset = num(o, "offset", g.offset);
            g.direction = static_cast<Direction>(num(o, "direction", 0));
            g.swing = num(o, "swing", 0);
            g.jitter = num(o, "jitter", 0);
            g.deal = static_cast<DealMode>(num(o, "deal", 0));
            g.overlap = static_cast<Overlap>(num(o, "overlap", 0));
            g.soloMember = num(o, "soloMember", 0);
            g.dealSpan = num(o, "dealSpan", 2);
            g.handoff = num(o, "handoff", 10);
            g.cleanHandoffs = flag(o, "cleanHandoffs", false);
            g.probability = num(o, "probability", 100);
            g.ratchet = num(o, "ratchet", 0);
            g.repeats = num(o, "repeats", 2);
            g.ramp = num(o, "ramp", 0);
            g.fillEvery = num(o, "fillEvery", 0);
            g.fillBars = num(o, "fillBars", 1);
            g.fillAmount = num(o, "fillAmount", 8);
            g.fillNow = flag(o, "fillNow", false);
            g.irama = num(o, "irama", 0);
            const juce::var t = o["tihai"];
            g.tihai = num(t, "every", 0), g.tihaiUnit = num(t, "unit", 0), g.tihaiLength = num(t, "length", 5), g.tihaiGap = num(t, "gap", 1),
            g.tihaiNow = num(t, "now", 0);
            g.mute = flag(o, "mute", false);
            g.lockRhythm = flag(o, "lockRhythm", false);
            g.lockDeal = flag(o, "lockDeal", false);
            g.densityFromVoices = flag(o, "densityFromVoices", false);
            g.slot = num(o, "slot", -1);  // §12.2: a song from before slots takes the first free ones, in order (below)
            g.overrides = overridesFrom(o["overrides"]);
            g.rhythmGen = genSlotFrom(o["rhythmGen"]);  // §16.9: missing, the default
            if (const auto* voices = o["voices"].getArray())
                for (const juce::var& vo : *voices) {
                    Voice x;
                    x.id = unhex(vo["id"], 0);
                    x.type = static_cast<VoiceType>(num(vo, "type", 0));
                    x.channel = num(vo, "channel", 1);
                    x.octaveTranspose = num(vo, "octaveTranspose", 0);
                    x.weight = num(vo, "weight", 8);
                    x.mute = flag(vo, "mute", false);
                    x.pitchSeed = unhex(vo["pitchSeed"], 0);
                    x.chordSize = num(vo, "chordSize", 4);
                    x.voicing = num(vo, "voicing", 1);
                    x.articulation = static_cast<Articulation>(num(vo, "articulation", 0));
                    x.nearest = flag(vo, "nearest", false);  // §3.4a: a song from before it, the fixed Voicing
                    x.humanVelocity = num(vo, "humanVelocity", 0);  // §4.7: and no humanize
                    x.humanTiming = num(vo, "humanTiming", 0);
                    x.tie = flag(vo, "tie", false);
                    x.lockNotes = flag(vo, "lockNotes", false);
                    x.density = num(vo, "density", 8);
                    x.slot = num(vo, "slot", -1);
                    x.overrides = overridesFrom(vo["overrides"]);
                    x.notesGen = genSlotFrom(vo["notesGen"]);
                    // §16.5: v0.5's Pedal switch is the Pedal generator, unless the slot names one (then the flag is dropped).
                    if (flag(vo, "pedal", false) && x.notesGen.use.empty()) x.notesGen.use = "pedal";
                    g.voices.push_back(x);
                }
            s.groups.push_back(g);
        }

    if (const auto* rows = v["matrix"].getArray())
        for (const juce::var& o : *rows) {
            Row r;
            r.id = unhex(o["id"], 0);
            r.scope = static_cast<Scope>(num(o, "scope", 0));
            r.target = unhex(o["target"], 0);
            r.param = static_cast<Param>(num(o, "param", 0));
            if (o["key"].isString()) r.key = o["key"].toString().toStdString();  // sanitize makes a numbered one its param
            r.every = num(o, "every", 4);
            r.inPhrases = flag(o, "inPhrases", false);
            r.clock = o["clock"].toString() == "gong" ? 2 : o["clock"].toString() == "kenong" ? 3 : 0;
            r.offset = num(o, "offset", 0);
            r.chance = num(o, "chance", 100);
            r.maxShift = num(o, "maxShift", 1);
            if (!o["min"].isVoid()) r.min = static_cast<int>(o["min"]);
            if (!o["max"].isVoid()) r.max = static_cast<int>(o["max"]);
            r.lasts = num(o, "lasts", 0);
            r.fixedSeed = flag(o, "fixedSeed", true);
            r.on = flag(o, "on", true);
            if (const auto* allowed = o["allowed"].getArray())
                for (const juce::var& x : *allowed) r.allowed.push_back(static_cast<int>(x));
            r.cond = conditionFrom(o["cond"]);
            s.matrix.push_back(r);
        }
    s.mod = modFrom(v["mod"]);  // §17.7: missing, no other sources
    s.form = formFrom(v["form"]);  // §18.1: missing, no form
    s.gongCycle = num(v["gong"], "cycle", 4), s.kenongs = num(v["gong"], "kenongs", 4);  // §18.2
    sanitize(s);  // a song from outside: every value in range before the engine sees it
    assignSlots(s);  // §12.2: a song from before automation slots takes the first free ones, in order
    return s;
}

}  // namespace hocket::json
