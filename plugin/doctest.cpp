// planchette_doctest: a song carrying every kind of edit (groups, voices, overrides, locks, matrix rows of each kind,
// two Generates) goes to JSON and back. The copy must play the same notes and write the same JSON.
#include <algorithm>
#include <cstdio>
#include <functional>

#include "Clip.h"
#include "Library.h"
#include "SongJson.h"
#include "hocket/engine.hpp"
#include "hocket/generators.hpp"
#include "hocket/form.hpp"
#include "hocket/golden.hpp"

int main() {
    using namespace hocket;
    Song s = defaultSong(0xC0FFEE);
    s.root = 4, s.scale = 2, s.range = 11, s.harmony.length = 6, s.harmony.locked = true;
    Group& arp = addGroup(s, "Arp \"quoted\" ünïcode");
    arp.steps = 13, arp.density = 12, arp.direction = Direction::Pendulum, arp.swing = 40, arp.jitter = 10;
    arp.overlap = Overlap::Cut, arp.overrides.range = 14, arp.lockDeal = true;
    Voice& a = addVoice(s, s.groups.back(), VoiceType::Lead, 5);
    a.tie = true, a.weight = 3, a.overrides.octaveHigh = 1, a.lockNotes = true;
    a.notesGen.use = "pedal";
    arp.rhythmGen.use = "euclid";  // §16.9: a slot's choice and its knobs, and a row on one of them by key
    arp.rhythmGen.params["euclid"] = {{"pulses", 5}, {"rotation", 3}};
    arp.deal = DealMode::Tendency, arp.dealSpan = 5;
    arp.probability = 65, arp.ratchet = 40, arp.repeats = 3, arp.ramp = -35, arp.fillEvery = 2, arp.fillBars = 3, arp.fillAmount = 11;
    s.groups[1].fillNow = true;
    s.fill = 1;
    s.modeMoves = 1;
    {
        Voice& k = addVoice(s, s.groups.back(), VoiceType::Bass, 6);  // §16.6: a Kotekan on the Arp's Lead, by id
        k.notesGen.use = "kotekan";
        k.notesGen.sources["kotekan"] = s.groups.back().voices[0].id;
        k.notesGen.params["kotekan"] = {{"mode", 3}, {"lead", 4}};
    }
    s.groups[0].deal = DealMode::Bells, s.groups[0].handoff = 33;  // §5.5, §5.6
    s.groups[2].deal = DealMode::Tutti;
    s.groups[0].cleanHandoffs = true;  // §5.7
    s.groups[1].rhythmGen.use = "interlock";  // and the Bass interlocking with the Leads
    s.groups[1].rhythmGen.sources["interlock"] = s.groups[0].id;
    s.groups[2].voices[0].articulation = Articulation::RestrikeAccents;
    s.groups[2].voices[0].voicing = 11;
    s.groups[2].voices[0].nearest = true;
    s.groups[2].voices[0].stagger = 9, s.groups[2].voices[0].staggerOrder = StaggerOrder::Seeded;  // §3.4b
    s.groups[2].voices[0].staggerLean = StaggerLean::Around, s.groups[2].voices[0].top = 7;
    s.groups[0].voices[0].humanVelocity = 12, s.groups[0].voices[0].humanTiming = 70;
    Row& r1 = addRow(s);
    r1.scope = Scope::Global, r1.param = Param::Range, r1.every = 2, r1.maxShift = 3, r1.min = 4, r1.max = 12, r1.chance = 70;
    Row& r2 = addRow(s);
    r2.scope = Scope::Group, r2.target = s.groups[3].id, r2.param = Param::DealMode, r2.allowed = {0, 2}, r2.every = 4;
    Row& r3 = addRow(s);
    r3.scope = Scope::Harmony, r3.param = Param::VaryPitch, r3.every = 1, r3.inPhrases = true, r3.offset = -1, r3.lasts = 1, r3.fixedSeed = false;
    Row& r4 = addRow(s);
    r4.scope = Scope::Global, r4.param = Param::Root, r4.on = false;
    Row& r5 = addRow(s);  // §4.6's targets: they're numbers past the old last one, so they must come back as themselves
    r5.scope = Scope::Group, r5.target = s.groups[0].id, r5.param = Param::Fill, r5.every = 4, r5.offset = -1, r5.lasts = 1;
    Row& r6 = addRow(s);
    r6.scope = Scope::Group, r6.target = s.groups[1].id, r6.param = Param::Ratchet, r6.maxShift = 20, r6.min = 0, r6.max = 60;
    Row& r7 = addRow(s);  // §3.8's target, the newest number
    r7.scope = Scope::Global, r7.param = Param::ModeMoves, r7.every = 2, r7.inPhrases = true, r7.lasts = 4;
    Row& r8 = addRow(s);  // §3.4a's target
    r8.scope = Scope::Voice, r8.target = s.groups[2].voices[0].id, r8.param = Param::VoiceLeading, r8.every = 4;
    Row& r9 = addRow(s);  // §4.7's targets
    r9.scope = Scope::Voice, r9.target = s.groups[0].voices[0].id, r9.param = Param::HumanTiming, r9.maxShift = 20, r9.min = 0, r9.max = 80;
    Row& r10 = addRow(s);
    r10.scope = Scope::Group, r10.target = arp.id, r10.key = "rhythm.euclid.pulses", r10.every = 2, r10.maxShift = 2, r10.min = 3;
    generate(s);
    generate(s);

    const juce::String text = json::toString(s);
    const Song back = json::fromString(text);
    const std::int64_t end = barTicks(s) * 32;
    const bool sameNotes = notes(s, 0, end) == notes(back, 0, end);
    const bool sameText = json::toString(back) == text && back.matrix[5].param == Param::Fill && back.matrix[6].param == Param::Ratchet &&
                          back.groups[3].ratchet == 40 && back.groups[1].fillNow && back.fill == 1 &&
                          back.modeMoves == 1 && back.matrix[7].param == Param::ModeMoves &&
                          back.matrix[8].param == Param::VoiceLeading && back.groups[2].voices[0].nearest &&
                          back.matrix[9].param == Param::HumanTiming && back.groups[0].voices[0].humanTiming == 70 &&
                          back.groups[0].voices[0].humanVelocity == 12 && back.matrix[10].key == "rhythm.euclid.pulses" &&
                          back.groups[3].rhythmGen.use == "euclid" && back.groups[3].rhythmGen.params.at("euclid").at("rotation") == 3 &&
                          back.groups[3].voices[1].notesGen.sources.at("kotekan") == back.groups[3].voices[0].id &&
                          back.groups[3].voices[1].notesGen.params.at("kotekan").at("mode") == 3 &&
                          back.groups[1].rhythmGen.sources.at("interlock") == back.groups[0].id && kotekanSource(back, 3, 1).has_value() &&
                          back.groups[0].deal == DealMode::Bells && back.groups[0].handoff == 33 && back.groups[2].deal == DealMode::Tutti &&
                          back.groups[1].handoff == 10 && back.groups[0].cleanHandoffs && !back.groups[1].cleanHandoffs &&
                          back.groups[2].voices[0].stagger == 9 && back.groups[2].voices[0].staggerOrder == StaggerOrder::Seeded &&
                          back.groups[2].voices[0].staggerLean == StaggerLean::Around && back.groups[2].voices[0].top == 7 &&
                          back.groups[1].voices[0].stagger == 0 && back.groups[1].voices[0].top == 0;
    std::printf("%zu notes over 32 bars; round trip: notes %s, JSON %s (%d bytes)\n", notes(s, 0, end).size(),
                sameNotes ? "identical" : "DIFFER", sameText ? "identical" : "DIFFERS", text.length());

    // A clip of bars 1–8 holds exactly what the engine plays there. With chords changing every 2 bars, one from bar 2
    // starts with the chord that began in bar 1 and is still ringing, and stops every note by its end.
    const Song d = defaultSong(0x0123456789ABCDEFull);
    const std::int64_t bar = barTicks(d);
    std::vector<std::int64_t> want, got;
    for (const Note& n : notes(d, 0, 8 * bar)) want.push_back(n.start);
    const juce::MidiFile all = clip::render(d, 0, 8, 120.0);
    for (int t = 0; t < all.getNumTracks(); ++t)
        for (const auto* ev : *all.getTrack(t))
            if (ev->message.isNoteOn()) got.push_back(static_cast<std::int64_t>(ev->message.getTimeStamp()));
    std::sort(want.begin(), want.end());
    std::sort(got.begin(), got.end());
    const bool sameClip = want == got;
    Song slow = d;
    slow.harmony.changesEvery = 3;  // 2 bars
    const juce::MidiFile later = clip::render(slow, 1, 4, 120.0);
    bool chordAtZero = false, endsInside = true;
    for (int t = 0; t < later.getNumTracks(); ++t)
        for (const auto* ev : *later.getTrack(t)) {
            const auto& msg = ev->message;
            if (msg.isNoteOn() && msg.getChannel() == 3 && msg.getTimeStamp() == 0.0) chordAtZero = true;
            if (msg.isNoteOnOrOff() && msg.getTimeStamp() > 4.0 * bar) endsInside = false;
        }
    std::printf("clip of bars 1-8: %zu notes, %s the engine's; from bar 2: chord %s at the start, notes %s\n", got.size(),
                sameClip ? "the same as" : "DIFFERENT FROM", chordAtZero ? "held" : "MISSING", endsInside ? "end inside" : "RUN PAST THE END");

    // A real file saved by ced1476, not reconstructed by the new serializer.
    const juce::File fixture(juce::String(HOCKET_GOLDEN_DIR) + "/../fixtures/old-default.hocket");
    const auto legacy = library::load(fixture);
    const bool legacyPlays = legacy && golden(*legacy, 64) == juce::File(juce::String(HOCKET_GOLDEN_DIR) +
                                                  "/default-0123456789abcdef.txt").loadFileAsString().toStdString();
    const auto editedLegacy = library::load(juce::File(juce::String(HOCKET_GOLDEN_DIR) + "/../fixtures/old-edited.hocket"));
    const auto editedExpected = juce::File(juce::String(HOCKET_GOLDEN_DIR) + "/legacy-edited.txt").loadFileAsString().toStdString();
    const bool editedPlays = editedLegacy && !editedExpected.empty() && golden(*editedLegacy, 64) == editedExpected &&
                             golden(defaultSong(editedLegacy->masterSeed), 64) != editedExpected;
    // Read the actual production path selectors without writing anything to user directories.
    const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const auto applicationData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    const bool defaultPaths = library::folder() == documents.getChildFile("Planchette/Songs") &&
                              clip::folder() == documents.getChildFile("Planchette/Clips") &&
                              clip::stagingFolder() == applicationData.getChildFile("Planchette/Outgoing");
    const auto bench = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("planchette-library", "", false);
    const auto newer = bench.getChildFile("Planchette/Songs"), older = bench.getChildFile("Hocket/Songs");
    older.createDirectory();
    const auto oldFile = older.getChildFile("Old song.hocket");
    bool libraryOK = fixture.copyFileTo(oldFile);
    auto entries = library::list(newer);  // new directory absent
    libraryOK = libraryOK && entries.size() == static_cast<std::size_t>(library::numStarters() + 1) &&
                entries.back().file == oldFile && library::load(entries.back()).has_value();
    if (legacy) {
        const auto saved = library::save(newer, "Old song", *legacy);
        libraryOK = libraryOK && saved == newer.getChildFile("Old song.planchette") && saved.existsAsFile() &&
                    oldFile.loadFileAsString() == fixture.loadFileAsString();
        entries = library::list(newer);  // both directories exist; same names remain reachable
        libraryOK = libraryOK && entries.size() == static_cast<std::size_t>(library::numStarters() + 2);
        libraryOK = libraryOK && fixture.copyFileTo(newer.getChildFile("Imported.hocket")) &&
                    library::list(newer).size() == static_cast<std::size_t>(library::numStarters() + 3);
        Song changed = *legacy;
        changed.root = (changed.root + 1) % 12;
        library::save(newer, "Old song", changed);  // existing save-by-name overwrite semantics
        const auto reloaded = library::load(saved);
        libraryOK = libraryOK && reloaded && reloaded->root == changed.root &&
                    oldFile.loadFileAsString() == fixture.loadFileAsString();
    }
    bench.deleteRecursively();  // only this test's unique directory
    juce::MemoryOutputStream clipBytes;
    clip::render(defaultSong(0x0123456789ABCDEFull), 0, 8, 120).writeTo(clipBytes);
    juce::MemoryBlock expectedClip;
    const bool clipGolden = juce::File(juce::String(HOCKET_GOLDEN_DIR) + "/clip-0123456789abcdef.mid").loadFileAsData(expectedClip) &&
                            expectedClip == clipBytes.getMemoryBlock();
    std::printf("old-build fixture: %s; both libraries/extensions and Save: %s; clip bytes: %s\n",
                legacyPlays ? "64 bars identical" : "FAILED", libraryOK ? "preserved" : "FAILED", clipGolden ? "golden" : "FAILED");

    std::printf("edited old-build fixture: %s; production output directories: %s\n",
                editedPlays ? "64 bars identical, differs from default" : "FAILED", defaultPaths ? "Planchette" : "FAILED");

    // §16.1: the default song with every slot set to its default by name, saved and read back, renders the golden file.
    juce::String goldenText;
    {
        const juce::File f(juce::String(HOCKET_GOLDEN_DIR) + "/default-0123456789abcdef.txt");
        goldenText = f.loadFileAsString();
    }
    Song named = defaultSong(0x0123456789ABCDEFull);
    for (Group& g : named.groups) {
        g.rhythmGen.use = "random";
        for (Voice& v : g.voices)
            if (v.type != VoiceType::Chord) v.notesGen.use = "wander";
    }
    const juce::String namedText = json::toString(named);
    const Song namedBack = json::fromString(namedText);
    const bool namedGolden = goldenText.isNotEmpty() && juce::String(golden(namedBack, 64)) == goldenText &&
                             namedText.contains("\"rhythmGen\"") && namedBack.groups[0].rhythmGen.use == "random" &&
                             namedBack.groups[0].voices[1].notesGen.use == "wander";
    // And a song that has never had a slot set writes no slot fields at all: it is a song from before them (§16.9).
    const bool noFields = !json::toString(defaultSong(5)).contains("Gen\"");
    std::printf("explicit default slots, to JSON and back: %s the golden render; an unset slot writes %s\n",
                namedGolden ? "exactly" : "NOT", noFields ? "nothing" : "A FIELD");

    // §16.9, §17.7: what this build doesn't know (a newer build's generator, parameters it doesn't have, values that
    // aren't numbers, a row's key) plays the default or does nothing, and is written back as it was read.
    const juce::String future = R"({"masterSeed": "0000000000000007", "groups": [ { "id": "00000000000000a1", "rhythmSeed": "0000000000000011",
        "rhythmGen": { "use": "additive", "flavour": [1, 2],
                       "params": { "additive": { "every": 3, "way": 1, "curve": "soft" }, "random": { "spice": 2 }, "odd": 4 } },
        "voices": [ { "id": "00000000000000b1", "pitchSeed": "0000000000000021",
                      "notesGen": { "use": "kotekan", "params": { "kotekan": { "source": "00000000000000b2" } } } } ] } ],
      "matrix": [ { "id": "00000000000000c1", "scope": 2, "target": "00000000000000a1", "key": "rhythm.additive.every", "every": 1 },
                  { "id": "00000000000000c2", "scope": 2, "target": "00000000000000a1", "key": "density", "every": 1 } ] })";
    const Song fut = json::fromString(future);
    const Song futBack = json::fromString(json::toString(fut));
    const juce::var fa = json::toVar(fut), fb = json::toVar(futBack);
    const juce::var g0 = juce::JSON::parse(future)["groups"][0];
    auto same = [](const juce::var& x, const juce::var& y) { return juce::JSON::toString(x, true) == juce::JSON::toString(y, true); };
    Song futPlain = fut;
    futPlain.groups[0].rhythmGen = {}, futPlain.groups[0].voices[0].notesGen = {}, futPlain.matrix.erase(futPlain.matrix.begin());
    const bool keptFuture = same(fa["groups"][0]["rhythmGen"]["flavour"], g0["rhythmGen"]["flavour"]) &&
                            same(fa["groups"][0]["rhythmGen"]["params"]["additive"]["curve"], g0["rhythmGen"]["params"]["additive"]["curve"]) &&
                            static_cast<int>(fa["groups"][0]["rhythmGen"]["params"]["additive"]["every"]) == 3 &&
                            static_cast<int>(fa["groups"][0]["rhythmGen"]["params"]["random"]["spice"]) == 2 &&
                            static_cast<int>(fa["groups"][0]["rhythmGen"]["params"]["odd"]) == 4 &&
                            fa["groups"][0]["rhythmGen"]["use"].toString() == "additive" &&
                            fa["groups"][0]["voices"][0]["notesGen"]["params"]["kotekan"]["source"].toString() == "00000000000000b2" &&
                            fa["matrix"][0]["key"].toString() == "rhythm.additive.every" && fa["matrix"][0]["param"].isVoid() &&
                            static_cast<int>(fa["matrix"][1]["param"]) == static_cast<int>(Param::Density) && fa["matrix"][1]["key"].isVoid() &&
                            same(fa, fb);
    const bool futurePlays = notes(fut, 0, barTicks(fut) * 8) == notes(futPlain, 0, barTicks(fut) * 8);
    std::printf("a newer build's generator, parameters and key: %s, and %s\n", keptFuture ? "written back as read" : "NOT KEPT",
                futurePlays ? "played as the defaults" : "PLAYED SOMETHING ELSE");
    // §16.5: a v0.5 song's Pedal switch reads as the Pedal generator and plays as it did; a slot that names another
    // generator drops the old flag. Written back with notesGen and no pedal flag.
    const juce::String v05 = R"({"masterSeed": "0000000000000007", "groups": [ { "id": "00000000000000a1", "rhythmSeed": "0000000000000011",
        "voices": [ { "id": "00000000000000b1", "pitchSeed": "0000000000000021", "pedal": true },
                    { "id": "00000000000000b2", "pitchSeed": "0000000000000022", "pedal": true, "notesGen": { "use": "pink" } } ] } ] })";
    const Song old5 = json::fromString(v05);
    const juce::String old5Text = json::toString(old5);
    const bool pedalRead = old5.groups[0].voices[0].notesGen.use == "pedal" && old5.groups[0].voices[1].notesGen.use == "pink" &&
                           !old5Text.contains("\"pedal\": true") && old5Text.contains("\"use\": \"pedal\"");
    std::printf("a v0.5 Pedal switch: %s\n", pedalRead ? "reads as the Pedal generator" : "LOST");
    // §17.7: mod. The spec's own example, read, played and written back; a song without mod writes none; what this build
    // doesn't know (a kind, an output, a key, a cord's field, a source's parameter, a condition from a later build) does
    // nothing and is written back as it was read.
    const juce::String modText = R"({"masterSeed": "0123456789abcdef", "groups": [ { "id": "00000000000000a1", "rhythmSeed": "0000000000000011",
        "density": 8, "voices": [ { "id": "00000000000000b1", "pitchSeed": "0000000000000021" } ] } ],
      "mod": { "sources": [ { "id": "00000000000000aa", "kind": "lfo", "name": "Tides",
                              "params": { "count": 2,
                                          "lfos": [ { "period": 17, "unit": 0, "shape": 0, "phase": 0 },
                                                    { "period": 19, "unit": 0, "shape": 0, "phase": 0 } ],
                                          "converge": [0, 1], "wobble": "soft" } },
                            { "id": "00000000000000ab", "kind": "ca", "name": "Life", "params": { "type": 1, "rule": 90 }, "grid": [0, 1] } ],
               "cords":   [ { "id": "00000000000000c1", "source": "00000000000000aa", "output": "lfo1",
                              "dest": { "scope": 2, "target": "00000000000000a1", "key": "density" }, "depth": 4 },
                            { "id": "00000000000000c2", "source": "00000000000000aa", "output": "converge",
                              "dest": { "scope": 2, "target": "00000000000000a1", "key": "reroll.rhythm" },
                              "lasts": 4, "fixedSeed": false, "cond": { "kind": "AB", "a": 1, "b": 2 }, "glide": 3 },
                            { "id": "00000000000000c3", "source": "00000000000000ab", "output": "births",
                              "dest": { "scope": 2, "target": "00000000000000a1", "key": "harmony.grammar.depth" } } ],
               "macros": [] } })";
    // Equal as JSON values, whatever order an object's fields come in.
    std::function<bool(const juce::var&, const juce::var&)> alike = [&](const juce::var& x, const juce::var& y) {
        if (auto* ox = x.getDynamicObject()) {
            auto* oy = y.getDynamicObject();
            if (!oy || ox->getProperties().size() != oy->getProperties().size()) return false;
            for (const auto& kv : ox->getProperties())
                if (!oy->hasProperty(kv.name) || !alike(kv.value, oy->getProperty(kv.name))) return false;
            return true;
        }
        if (const auto* ax = x.getArray()) {
            const auto* ay = y.getArray();
            if (!ay || ax->size() != ay->size()) return false;
            for (int i = 0; i < ax->size(); ++i)
                if (!alike((*ax)[i], (*ay)[i])) return false;
            return true;
        }
        return juce::JSON::toString(x, true) == juce::JSON::toString(y, true);
    };
    const Song ms = json::fromString(modText);
    const juce::var mv = json::toVar(ms), orig = juce::JSON::parse(modText);
    const bool modRead = ms.mod.sources.size() == 2 && ms.mod.cords.size() == 3 && ms.mod.sources[0].name == "Tides" &&
                         ms.mod.sources[0].lists.at("lfos").size() == 2 && ms.mod.sources[0].sets.at("converge") == std::vector<int>{0, 1} &&
                         ms.mod.cords[0].depth == 4 && ms.mod.cords[0].dest.key == "density" && ms.mod.cords[1].lasts == 4 &&
                         !ms.mod.cords[1].fixedSeed && ms.mod.cords[1].move == 1 && ms.mod.cords[1].chance == 100 &&
                         stateAt(ms, 8).groups[0].density == 12 && stateAt(ms, 0).groups[0].density == 4;
    const bool modKept = alike(mv["mod"]["sources"], orig["mod"]["sources"]) && alike(mv["mod"]["cords"][1], orig["mod"]["cords"][1]) &&
                         alike(mv["mod"]["cords"][2], orig["mod"]["cords"][2]) && mv["mod"]["cords"][0]["depth"].isVoid() &&  // 4 is the default
                         mv["mod"]["macros"].isArray() && same(json::toVar(json::fromString(json::toString(ms))), mv) &&
                         !json::toString(defaultSong(5)).contains("\"mod\"");
    std::printf("mod (§17.7): the spec's example %s; a later build's kind, fields and keys %s\n", modRead ? "reads and plays" : "MISREAD",
                modKept ? "written back as read" : "NOT KEPT");
    // §17.5: a row's and a cord's conditions, each kind and its negation, to JSON and back; one from a later build kept.
    Song cs = defaultSong(0xC0DE);
    using K = Condition::Kind;
    const K kinds[] = {K::AB, K::First, K::Pre, K::Nei, K::Fill};
    for (K k : kinds) {
        Row& r = addRow(cs);
        r.cond.kind = k, r.cond.negate = k == K::Pre;
        if (k == K::AB) r.cond.a = 3, r.cond.b = 5;  // only A:B has A and B, only NEI an item
        if (k == K::Nei) r.cond.item = cs.matrix[0].id;
    }
    ModSource& tide = addSource(cs, "lfo");
    Cord& cc = addCord(cs, tide.id, "converge");
    cc.dest = {Scope::Group, cs.groups[0].id, "reroll.rhythm"}, cc.cond.kind = K::First, cc.cond.negate = true;
    const Song csBack = json::fromString(json::toString(cs));
    bool condsKept = csBack.matrix.size() == cs.matrix.size() && csBack.mod.cords.size() == 1 && csBack.mod.cords[0].cond == cc.cond;
    for (std::size_t i = 0; i < cs.matrix.size(); ++i) condsKept = condsKept && csBack.matrix[i].cond == cs.matrix[i].cond;
    const juce::var laterCond = juce::JSON::parse(R"({"kind": "prob", "p": 30, "not": true})");
    const Song laterSong = json::fromString(R"({"masterSeed": "0000000000000007", "matrix": [ { "id": "00000000000000c1", "every": 1,
        "cond": {"kind": "prob", "p": 30, "not": true} } ] })");
    condsKept = condsKept && laterSong.matrix[0].cond.kind == K::Unknown && alike(json::toVar(laterSong)["matrix"][0]["cond"], laterCond) &&
                !json::toString(defaultSong(5)).contains("\"cond\"");
    std::printf("conditions (§17.5): %s\n", condsKept ? "every kind and its negation round-trips; a later build's kept" : "NOT KEPT");
    // §18.1: a form and its letters to JSON and back; a song without one writes no "form"; a later build's fields kept.
    Song fs = defaultSong(0xF0F0);
    fs.form.on = true, fs.form.text = "A B A' C", fs.form.section = 3, fs.form.end = 1, fs.form.prime = 6;
    FormLetter* fl = addLetter(fs);
    fl->harmony = true, fl->deal = true;
    fl->params.push_back({Dest{Scope::Group, fs.groups[0].id, "density"}, 13});
    fl->params.push_back({Dest{Scope::Global, 0, "root"}, 4});
    FormLetter* fc = addLetter(fs);
    fc->rhythm = false, fc->matrix = true;
    const Song fsBack = json::fromString(json::toString(fs));
    const Song laterForm = json::fromString(R"({"masterSeed": "0000000000000007", "form": { "on": true, "text": "A B", "tempo": 3,
        "letters": [ { "name": "B", "salt": "00000000000000bb", "rolls": ["notes"], "colour": "red" } ] } })");
    const bool formKept = json::toString(fsBack) == json::toString(fs) && fsBack.form.letters.size() == 2 && fsBack.form.letters[0].harmony &&
                          fsBack.form.letters[0].params.size() == 2 && fsBack.form.letters[0].params[0].second == 13 && fsBack.form.letters[1].matrix &&
                          !fsBack.form.letters[1].rhythm && fsBack.form.end == 1 && fsBack.form.prime == 6 &&
                          notes(fs, 0, barTicks(fs) * 48) == notes(fsBack, 0, barTicks(fs) * 48) &&
                          !json::toString(defaultSong(5)).contains("\"form\"") && laterForm.form.letters.size() == 1 &&
                          laterForm.form.letters[0].salt == 0xBB && json::toString(laterForm).contains("\"tempo\"") &&
                          json::toString(laterForm).contains("\"colour\"");
    std::printf("form (§18.1): %s\n", formKept ? "a form and its letters round-trip and play the same; a later build's fields kept" : "NOT KEPT");
    // §18.2: the gong clock, a row on it, the GONG and KENONG conditions; a default song writes no "gong".
    Song gs = defaultSong(0x60);
    gs.gongCycle = 3, gs.kenongs = 8;
    Row& gr = addRow(gs);
    gr.clock = 3, gr.every = 2, gr.offset = 1, gr.cond.kind = Condition::Kind::Gong, gr.cond.negate = true;
    const Condition grc = gr.cond;
    Row& gr2 = addRow(gs);
    gr2.clock = 2, gr2.cond.kind = Condition::Kind::Kenong;
    const Song gsBack = json::fromString(json::toString(gs));
    bool gongKept = json::toString(gsBack) == json::toString(gs) && gsBack.gongCycle == 3 && gsBack.kenongs == 8 &&
                          gsBack.matrix.back().clock == 2 && gsBack.matrix[gsBack.matrix.size() - 2].clock == 3 &&
                          gsBack.matrix[gsBack.matrix.size() - 2].cond == grc && !json::toString(defaultSong(5)).contains("\"gong\"");
    std::printf("gong (§18.2): %s\n", gongKept ? "the clock, its rows and conditions round-trip" : "NOT KEPT");
    // §18.3: a group's irama, and a row on it by key.
    Song is = defaultSong(0x1A);
    is.groups[0].irama = 4;
    Row& ir = addRow(is);
    ir.scope = Scope::Group, ir.target = is.groups[0].id, ir.key = "irama", ir.clock = 2, ir.allowed = {3, 5};
    const Song isBack = json::fromString(json::toString(is));
    gongKept = gongKept && isBack.groups[0].irama == 4 && isBack.matrix.back().key == "irama" && json::toString(isBack) == json::toString(is) &&
               notes(is, 0, barTicks(is) * 40) == notes(isBack, 0, barTicks(is) * 40) && !json::toString(defaultSong(5)).contains("\"irama\"");
    std::printf("irama (§18.3): %s\n", gongKept ? "a group's level and a row on it round-trip and play the same" : "NOT KEPT");
    // §18.4: a group's tihai, and a row on "tihai now".
    Song ts = defaultSong(0x71);
    ts.groups[0].tihai = 2, ts.groups[0].tihaiUnit = 1, ts.groups[0].tihaiLength = 7, ts.groups[0].tihaiGap = 0, ts.groups[1].tihaiNow = 1;
    Row& tr = addRow(ts);
    tr.scope = Scope::Group, tr.target = ts.groups[1].id, tr.key = "tihai", tr.every = 8, tr.offset = 7, tr.allowed = {1}, tr.lasts = 1;
    const Song tsBack = json::fromString(json::toString(ts));
    const bool tihaiKept = tsBack.groups[0].tihai == 2 && tsBack.groups[0].tihaiUnit == 1 && tsBack.groups[0].tihaiLength == 7 &&
                           tsBack.groups[0].tihaiGap == 0 && tsBack.groups[1].tihaiNow == 1 && json::toString(tsBack) == json::toString(ts) &&
                           notes(ts, 0, barTicks(ts) * 40) == notes(tsBack, 0, barTicks(ts) * 40) && !json::toString(defaultSong(5)).contains("\"tihai\"");
    std::printf("tihai (§18.4): %s\n", tihaiKept ? "a group's tihai and a row on it round-trip and play the same" : "NOT KEPT");
    gongKept = gongKept && tihaiKept;
    // §5.8, §4.3, §12.2: Density from voices and a voice's Density, its own Accent and Gate %, and the automation slots.
    Song vs = defaultSong(0x58);
    vs.groups[0].densityFromVoices = true, vs.groups[0].voices[0].density = 3, vs.groups[0].voices[1].density = 0;
    vs.groups[0].voices[1].overrides.accent = 90, vs.groups[0].voices[1].overrides.gatePercent = 40;
    vs.groups[1].slot = 0, vs.groups[2].voices[0].slot = 9;  // one with no slot, one moved to another
    const Song vsBack = json::fromString(json::toString(vs));
    // A song from before any of it: no setting written, its groups and voices in the first free slots, in order.
    juce::var oldSong = juce::JSON::parse(json::toString(defaultSong(5)));
    for (juce::var& gv : *oldSong["groups"].getArray()) {
        gv.getDynamicObject()->removeProperty("slot");
        for (juce::var& vv : *gv["voices"].getArray()) vv.getDynamicObject()->removeProperty("slot");
    }
    const Song fromOld = json::fromString(juce::JSON::toString(oldSong));
    const bool voicesKept =
        vsBack.groups[0].densityFromVoices && vsBack.groups[0].voices[0].density == 3 && vsBack.groups[0].voices[1].density == 0 &&
        vsBack.groups[0].voices[1].overrides.accent == std::optional<int>(90) && vsBack.groups[0].voices[1].overrides.gatePercent == std::optional<int>(40) &&
        vsBack.groups[0].slot == 1 && vsBack.groups[1].slot == 0 && vsBack.groups[2].slot == 3 && vsBack.groups[2].voices[0].slot == 9 &&
        json::toString(vsBack) == json::toString(vs) && notes(vs, 0, barTicks(vs) * 16) == notes(vsBack, 0, barTicks(vs) * 16) &&
        !json::toString(defaultSong(5)).contains("densityFromVoices") && !json::toString(defaultSong(5)).contains("\"density\":8,\"slot\"") &&
        fromOld.groups[0].slot == 1 && fromOld.groups[1].slot == 2 && fromOld.groups[0].voices[1].slot == 2 && fromOld.groups[2].voices[0].slot == 4 &&
        notes(fromOld, 0, barTicks(fromOld) * 16) == notes(defaultSong(5), 0, barTicks(fromOld) * 16);
    std::printf("voices (§5.8, §12.2): %s\n", voicesKept ? "Density from voices, a voice's own values and the automation slots round-trip; a song from before them takes the first free slots" : "NOT KEPT");
    gongKept = gongKept && voicesKept;
    return defaultPaths && editedPlays && legacyPlays && libraryOK && clipGolden && gongKept && formKept && condsKept && modRead && modKept && pedalRead && sameNotes && sameText && sameClip && chordAtZero && endsInside && namedGolden && noFields && keptFuture && futurePlays ? 0 : 1;
}
