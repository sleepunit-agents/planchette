#!/usr/bin/env python3
"""Reference arithmetic for SPEC.md: §2.6 ported, and the worked examples of §16 and §17.

Plain Python 3, no dependencies. `python3 tools/spec_arith.py` prints every worked value the spec quotes, section by
section; each spec example says which section of this output it comes from. The first lines reproduce the golden
file's header (tests/golden), which is how this port is checked against the engine.

This is a reference for the spec's arithmetic, not the engine: the engine is the C++ in engine/.
"""

M64 = (1 << 64) - 1


# ---- §2.6 ---------------------------------------------------------------------------------------------------------

def mix(z):
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
    return z ^ (z >> 31)


def fnv1a(s):
    h = 0xCBF29CE484222325
    for b in s.encode("utf-8"):
        h = ((h ^ b) * 0x100000001B3) & M64
    return h


def enc(c):
    return (c & M64, 1) if isinstance(c, int) else (fnv1a(c), 2)


def hsh(seed, *path):
    h = mix(seed)
    for c in path:
        e, k = enc(c)
        h = mix((mix(h ^ e) + k) & M64)
    return h


class Stream:
    def __init__(self, h):
        self.state = h

    def next(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & M64
        return mix(self.state)

    def below(self, n):
        assert n >= 1
        x = self.next(); m = x * n; lo = m & M64
        if lo < n:
            t = ((1 << 64) - n) % n
            while lo < t:
                x = self.next(); m = x * n; lo = m & M64
        return m >> 64


def roll(h):
    return Stream(h).below(100)


def rdiv(a, b):
    assert a >= 0 and b > 0
    return (2 * a + b) // (2 * b)


# Python's // and % are §2.6's signed div and mod (floor, mod in 0 … b−1).
def ceil_div(a, b):
    return -((-a) // b)


def rsdiv(a, b):
    return (1 if a >= 0 else -1) * rdiv(abs(a), b)


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def reflect(v, lo, hi):
    if hi <= lo:
        return lo
    p = 2 * (hi - lo); x = (v - lo) % p
    return lo + (x if x <= hi - lo else p - x)


def priority(h, L=64):
    """§2.5: the seeded order of the 64 slots, keeping slots < L."""
    a = list(range(64)); s = Stream(h)
    for i in range(63, 0, -1):
        j = s.below(i + 1); a[i], a[j] = a[j], a[i]
    return [x for x in a if x < L]


# ---- §16.4 rhythm generators --------------------------------------------------------------------------------------

def euclid(L, pulses, rotation):
    k = clamp(pulses, 1, L); rot = rotation % L
    return [((((s - rot) % L) * k) % L) < k for s in range(L)]


def prime_factors(n):
    out, p = [], 2
    while n > 1:
        while n % p == 0:
            out.append(p); n //= p
        p += 1
    return out


_U = {1: [0], 2: [1, 0]}


def upper(p):
    """U(p), a prime's own order (§16.4)."""
    if p not in _U:
        base = psi(tuple(sorted(prime_factors(p - 1), reverse=True)))
        k = p // 4
        t = [v + 1 if v >= k else v for v in base]
        t.insert(p - 2, k)
        _U[p] = t
    return _U[p]


def psi(strata):
    """Barlow's indispensability over the strata, coarsest first: a list, slot 0 … L−1."""
    L = 1
    for q in strata:
        L *= q
    out = []
    for s in range(L):
        x = (s - 1) % L; total = 0
        for j, p in enumerate(strata):
            below = 1
            for q in strata[j + 1:]:
                below *= q
            above = 1
            for q in strata[:j]:
                above *= q
            d = (x // below) % p
            total += above * upper(p)[(d + 1) % p]
        out.append(total)
    return out


def strata(L, beat):
    from math import gcd
    B = gcd(beat, L)
    return tuple(sorted(prime_factors(L // B)) + sorted(prime_factors(B), reverse=True))


# ---- §16.7 the Register -------------------------------------------------------------------------------------------

class Register:
    """One seed's register: b(t) for t ≥ −16 (§16.7). Lock 17 is the inverted end: every returning bit flips."""

    def __init__(self, seed):
        self.seed = seed
        h0 = hsh(seed, "register", "init")
        self.b = {-1 - i: (h0 >> i) & 1 for i in range(16)}
        self.t = 0

    def flip(self, t, lock):
        if lock >= 17:
            return 1
        return int(Stream(hsh(self.seed, "register", t)).below(512) < (16 - lock) ** 2)

    def step(self, n, lock):
        t = self.t
        self.b[t] = self.b[t - n] ^ self.flip(t, lock)
        self.t += 1

    def run(self, T, n, lock):
        while self.t < T:
            self.step(n, lock)
        return self

    def vary(self, s, target_id, depth, n):
        """§16.7's Vary: flip k of the n bits before the next step, chosen by a seeded priority order."""
        k = min(depth, n)
        order = priority(hsh(s, "register", target_id), n)
        flipped = []
        for i in range(k):
            j = order[i]
            self.b[self.t - 1 - j] ^= 1
            flipped.append(j)
        return flipped

    def value(self, t, w):
        return sum(self.b[t - i] << (w - 1 - i) for i in range(w))


def bits(r, a, z):
    return "".join(str(r.b[t]) for t in range(a, z))


def grouped(s, k=8):
    return " ".join(s[i:i + k] for i in range(0, len(s), k))


# ---- §16.5 1/f Wander ---------------------------------------------------------------------------------------------

def wander_and_pink(seed, L, pool, wander, dice):
    roots = L if pool == 1 else max(1, rdiv(L * (17 - wander), 16))
    m = pool - 1
    P = priority(hsh(seed, "notes"), L)
    w = [0] * L; p = [0] * L
    for j in range(L):
        sl = P[j]
        if j < roots:
            continue
        w[sl] = 1 + (L - 1 - j) % m
        d = [Stream(hsh(seed, "pink", i, sl >> i)).below(m) for i in range(dice)]
        p[sl] = 1 + rdiv(sum(d), dice)
    return roots, w, p


# ---- §16.6 derived generators -------------------------------------------------------------------------------------

PREF = [0, 7, 4, 3, 10, 11, 9, 2, 5, 8, 6, 1]
SCALES = {"Minor": [0, 2, 3, 5, 7, 8, 10], "Major": [0, 2, 4, 5, 7, 9, 11]}


def ranked(r, root, scale):
    """§3.1: the scale's pitch classes ranked by PREF from r (Colour 0)."""
    return [(r + i) % 12 for i in PREF if (r + i - root) % 12 in SCALES[scale]]


def pool_size(n, rng):
    return clamp(rdiv((n - 1) * (rng - 1), 15) + 1, 1, n)


KOTEKAN = ["nyog cag", "telu", "empat", "nyok cok"]


def kotekan(mode, pool, c, x, anticipate=False, next_root=None):
    """§16.6: the sangsih's MIDI note from the polos's note x, over the pool (pitch classes) around the core c."""
    S = set(pool)
    height = sorted(pool, key=lambda pc: (pc - c) % 12)
    base = x
    while base % 12 not in S:                   # the highest pool note at or below x
        base -= 1
    d = height.index(base % 12)                  # its Height position, counted from the core in its octave

    def up(n, k):                                # k pool notes above n, folded to within an octave of base
        y = n
        for _ in range(k):
            y += 1
            while y % 12 not in S:
                y += 1
        while y - base > 12:
            y -= 12
        return y

    if mode == "nyog cag":
        return base
    if mode == "telu":
        return up(base, 2) if d % 2 == 0 else base
    if mode == "empat":
        return up(base, 3) if d % 2 == 0 else up(base, 1)
    if anticipate:                               # nyok cok: the next core tone, the nearest one (a tie: above)
        lo = base - (base - next_root) % 12
        return lo if base - lo < lo + 12 - base else lo + 12
    return up(base, 1)


def resultant(notes, low, high, transpose=0):
    """§16.6 Resultant: the source's notes (step, MIDI) inside the band [low, high], moved by the voice's transpose."""
    return [(t, n + 12 * transpose) for t, n in notes if low <= n <= high]


def ladder(x, root, scale):
    """§5.5: a note's place on the scale's ladder from Root (an off-scale note counts as the scale note below it)."""
    iv = SCALES[scale]; K = len(iv)
    o, pc = divmod(x - root, 12)
    return o * K + sum(1 for i in iv if i <= pc) - 1


def partition(x, root, scale, n, mode, span=2):
    l = ladder(x, root, scale)
    return l % n if mode == "alternate" else (l // span) % n


def redraw(pitch_seed, slot, pool_size, avoid_pc, pitch_at, tries=3):
    """§5.7: a handoff note on the pitch class of the note before it: up to three seeded draws of another pool
    position, the first whose pitch class differs; none: the note as it was (None)."""
    st = Stream(hsh(pitch_seed, "handoff", slot))
    for _ in range(tries):
        y = pitch_at(st.below(pool_size))
        if y % 12 != avoid_pc:
            return y
    return None


def clean_chain(steps, pool, centre, base):
    """§5.7 over a run of handoffs: steps are (pitchSeed, slot, position) in order, each one a handoff from the one before.
    pitch_at(c) puts pool[c] in the octave above `base`."""
    at = lambda c: base + (pool[c] - centre) % 12
    out = []
    for i, (seed, t, pos) in enumerate(steps):
        x = at(pos)
        if out and x % 12 == out[-1] % 12:
            y = redraw(seed, t, len(pool), out[-1] % 12, at)
            x = y if y is not None else x
        out.append(x)
    return out


def interlock(src_hits, src_st, st, L, P_hits, slots):
    """§16.6 Interlock on one grid from 0: this group's step a is a hit when no source hit is in [a·st, (a+1)·st)
    and its slot is one of Random's hits. src_hits(a') → bool at the source's step a'."""
    out = []
    for a in range(L):
        t0, t1 = a * st, (a + 1) * st
        busy = any(src_hits(b) for b in range((t0 + src_st - 1) // src_st, (t1 + src_st - 1) // src_st))
        out.append(not busy and slots[a] in P_hits)
    return out


# ---- §17.9 sources ------------------------------------------------------------------------------------------------

def lfo(b, P, shape, phase=0):
    x = (b + phase) % P; q = rdiv(2000 * x, P)
    if shape == "triangle":
        return 2 * q - 1000 if q < 1000 else 3000 - 2 * q
    if shape == "saw up":
        return q - 1000
    if shape == "saw down":
        return 1000 - q
    return 1000 if q < 1000 else -1000


def accumulator(b, period, offset, step, S, start, edge, reset=0):
    c = sum(1 for bb in range(1, b + 1) if bb % period == offset % period)
    if reset:
        c %= reset
    raw = start + c * step
    if edge == "wrap":
        return raw % S
    if edge == "pendulum":
        return reflect(raw, 0, S - 1)
    return clamp(raw, 0, S - 1)


def dice_sum(ms, sid, c, D):
    return sum(Stream(hsh(ms, "mod", sid, "die", i, c >> i)).below(16) for i in range(D))


ARC = [(0, 0), (250, 150), (600, 600), (650, 250), (900, 1000), (960, 1000), (1000, 0)]


def arc(b, N, loop=True):
    x = b % N if loop else min(b, N)
    pos = rdiv(1000 * x, N)
    for j in range(1, 7):
        x0, y0 = ARC[j - 1]; x1, y1 = ARC[j]
        if pos < x1 or j == 6:
            return y0 + rsdiv((pos - x0) * (y1 - y0), x1 - x0), j, pos


# ---- §9.2 Freeze --------------------------------------------------------------------------------------------------

def frozen_bar(b, F, R):
    """The bar whose state bar b plays, Freeze held from bar line F and let go at bar line R (§9.2)."""
    return F - 1 if F <= b < R else b


# ---- §18.1 the form -----------------------------------------------------------------------------------------------

def parse_form(text):
    """The form string: letters A–H, each optionally primed (' or ′); anything else skipped; at most 64."""
    out = []
    for c in text:
        u = c.upper()
        if "A" <= u <= "H" and len(out) < 64:
            out.append([u, False])
        elif c in "'\u2032" and out:
            out[-1][1] = True
    return [(l, p) for l, p in out]


def section_at(entries, S, bar, hold=False):
    """(index, position, start, bars) of the section bar is in; bars −1: the last letter held."""
    n = len(entries)
    k = max(0, bar) // S
    if hold and k >= n - 1:
        return (n - 1, n - 1, (n - 1) * S, -1)
    return (k, k % n, k * S, S)


# ---- §18.2 the colotomic clock -------------------------------------------------------------------------------------

def gong_clock(N, K, b):
    """(gong?, kenong?, kenong number 1…K with K at the gong or 0, strokes up to and including b)."""
    K = min(K, N)
    p = b % N
    kenong = (p * K) % N < K
    j = p * K // N
    return (p == 0, kenong, (K if j == 0 else j) if kenong else 0, (b // N) * K + j + 1)


# ---- §18.3 irama --------------------------------------------------------------------------------------------------

def irama_step(change_ticks, level, division_ticks=240):
    """A group's step at an irama level: its Beat division at 0; else the change in 2^(level−1), halved as it divides."""
    if level <= 0:
        return division_ticks
    n = 1 << (level - 1)
    while n > 1 and change_ticks % n:
        n //= 2
    return change_ticks // n


def irama_latched(levels, gong_bars, b):
    """The level bar b plays: the state's at the last gong boundary at or before b (bar 0 counts as one)."""
    return levels[b - b % gong_bars]


# ---- §18.4 tihai ---------------------------------------------------------------------------------------------------

def tihai(B, bar_ticks, anchor, T, N, g, P):
    """The tihai landing on the sam at bar line B: (sam step, first step, roles by step), or None if it doesn't fit.
    start + 3N + 2g = sam. Roles: ('phrase', the first statement's step), ('gap',), ('land',)."""
    sam = ceil_div(B * bar_ticks - anchor, T)
    a0 = sam - (3 * N + 2 * g)
    if a0 < 0 or anchor + a0 * T < (B - P) * bar_ticks:
        return None
    roles = {}
    for a in range(a0, sam):
        k = (a - a0) % (N + g)
        roles[a] = ("phrase", a0 + k) if k < N else ("gap",)
    roles[sam] = ("land",)
    return sam, a0, roles


def tihai_line(r):
    sam, a0, roles = r
    out = []
    for a in range(a0, sam + 1):
        x = roles[a]
        out.append("L" if x[0] == "land" else "." if x[0] == "gap" else "123456789abcdefg"[x[1] - a0])
    return "".join(out)


# ---- the worked values --------------------------------------------------------------------------------------------

def main():
    seed = 0x0123456789ABCDEF
    print("== golden header (tests/golden/default-0123456789abcdef.txt, line 2)")
    print('hash(seed, "hits") %016x; stream(0) %016x; below(10) x3 %d %d %d' % (
        hsh(seed, "hits"), Stream(0).next(),
        Stream(seed).below(10), Stream(seed + 1).below(10), Stream(seed + 2).below(10)))

    print("\n== §16.4 Euclidean")
    show = lambda h: "".join("x" if v else "." for v in h)
    for L, k, r in [(8, 3, 0), (16, 5, 0), (16, 5, 2)]:
        print(f"L {L}, Pulses {k}, Rotation {r}: {show(euclid(L, k, r))}")

    print("\n== §16.4 Metric")
    for p in (3, 5, 7, 11, 13):
        print(f"U({p}) =", " ".join(map(str, upper(p))))
    for st in [(2, 2, 2, 2), (3, 2, 2), (2, 3, 2), (2, 2, 3), (3, 5)]:
        print("psi", "·".join(map(str, st)), "=", " ".join(map(str, psi(st))))
    ok = all(sorted(psi(strata(L, B))) == list(range(L)) for L in range(2, 65) for B in range(1, 17))
    print("a permutation for every L 2…64, Beat 1…16:", ok)

    print("\n== §16.5 1/f Wander (pitchSeed 0123456789abcdef, L 16, pool 4, Wander 8, Dice 4, Riff)")
    roots, w, p = wander_and_pink(seed, 16, 4, 8, 4)
    print("roots", roots)
    print("Wander", " ".join(map(str, w)))
    print("1/f   ", " ".join(map(str, p)))


    print("\n== §16.6 Kotekan (A minor, chord root A, Range 8: pool A E C G; Height A C E G)")
    pool = ranked(9, 9, "Minor")[:pool_size(7, 8)]
    print("pool", pool, "height", sorted(pool, key=lambda pc: (pc - 9) % 12))
    A = [57, 60, 62, 64, 67, 69, 72, 71]
    print("A        ", " ".join(map(str, A)))
    for m in KOTEKAN:
        print(f"{m:9s}", " ".join(str(kotekan(m, pool, 9, x, anticipate=(i >= 6), next_root=2)) for i, x in enumerate(A)))
    print("   (nyok cok: the last two steps anticipate a change to D)")
    one = ranked(9, 9, "Minor")[:1]
    print("pool of 1 (A), A 57: telu", kotekan("telu", one, 9, 57), "empat", kotekan("empat", one, 9, 57), "nyok cok", kotekan("nyok cok", one, 9, 57))
    pool5 = ranked(0, 9, "Minor")[:pool_size(7, 8)]
    print("chord root C, Range 8: pool", pool5, "height", sorted(pool5, key=lambda pc: (pc - 0) % 12))
    print("the same A, re-targeted:", " ".join(str(kotekan("telu", pool5, 0, x)) for x in A), "(telu)")

    print("\n== §16.6 Resultant (the composite of the Kotekan example: A on even steps, B on telu on odd, Round robin)")
    telu = [kotekan("telu", pool, 9, x) for x in A]
    comp = [A[i] if i % 2 == 0 else telu[i] for i in range(len(A))]
    print("composite", " ".join(map(str, comp)))
    for lo, hi in ((64, 72), (57, 63)):
        got = dict(resultant(list(enumerate(comp)), lo, hi))
        print(f"band {lo}-{hi}:", " ".join(str(got[i]) if i in got else "." for i in range(len(comp))))

    print("\n== §5.5 Alternate and Bells (A minor, Root A: pitch class 9, so MIDI 9 is place 0; two members, then three)")
    tune = [57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 61]
    print("tune       ", " ".join(f"{x:3d}" for x in tune))
    print("ladder     ", " ".join(f"{ladder(x, 9, 'Minor'):3d}" for x in tune), "  (61, C#, is off the scale: C's place)")
    for name, n, sp in (("alternate", 2, 2), ("bells", 2, 2), ("alternate", 3, 2), ("bells", 3, 2), ("bells", 2, 3)):
        print(f"{name:9s} N {n} Span {sp}", " ".join(f"{partition(x, 9, 'Minor', n, name, sp):3d}" for x in tune))
    print("handoff 10 %, 1/16 steps (240): next note at 480 → this one lasts to", 480 + 10 * 240 // 100)

    print("\n== §5.7 Clean handoffs (pool A E C G, Ranked, in the octave from A3 = 57; X and Y alternate, a handoff each step)")
    SX, SY = 0x0123456789ABCDEF, 0xFEDCBA9876543210
    pos = [0, 0, 1, 1, 0, 0, 2, 3]
    steps = [((SX if i % 2 == 0 else SY), i, p) for i, p in enumerate(pos)]   # slot i
    at = lambda c: 57 + ([9, 4, 0, 7][c] - 9) % 12
    print("as drawn ", " ".join(str(at(p)) for p in pos))
    print("draws    ", " ".join(str(Stream(hsh(sd, "handoff", t)).below(4)) for sd, t, _ in steps), " (each slot's first draw)")
    print("clean    ", " ".join(map(str, clean_chain(steps, [9, 4, 0, 7], 9, 57))))
    print("pool of 1:", redraw(SY, 1, 1, 9, lambda c: 57))

    print("\n== §16.6 Interlock (source: L 16, Pulses 5 Euclidean; this group L 16, every slot its own, Density 16)")
    src = euclid(16, 5, 0)
    il = interlock(lambda b: src[b % 16], 240, 240, 16, set(range(16)), list(range(16)))
    print("source   ", show(src))
    print("interlock", show(il))
    il8 = interlock(lambda b: src[b % 16], 240, 480, 8, set(range(8)), list(range(8)))
    print("this group on 1/8 (8 steps):", show(il8))

    print("\n== §16.7 Register (seed 0123456789abcdef, n 8)")
    h0 = hsh(seed, "register", "init")
    print("h0 = %016x; b(-1 … -16) = %s" % (h0, "".join(str((h0 >> i) & 1) for i in range(16))))
    for lock, T in ((16, 32), (8, 48), (12, 80), (0, 32), (17, 32)):
        r = Register(seed).run(T, 8, lock)
        print(f"Lock {lock:2d}, t 0…{T - 1}: {grouped(bits(r, 0, T))}")
    r = Register(seed).run(32, 8, 16)
    thr = rdiv(256 * 8, 16)
    print("Lock 16 hits, w 8, Density 8:", "".join("x" if r.value(t, 8) < thr else "." for t in range(32)))
    print("Lock 16 positions, w 8, pool 4:", " ".join(str((r.value(t, 8) * 4) >> 8) for t in range(16)))
    print("Lock 16 positions, w 3, pool 4:", " ".join(str((r.value(t, 3) * 4) >> 3) for t in range(16)))
    for lock in (16, 12, 8, 0, 17):
        rr = Register(seed)
        print(f"flips over 4096 steps, Lock {lock}:", sum(rr.flip(t, lock) for t in range(4096)))
    # Vary: depth 2 at t = 16 (Lock 16), row seed s = hash(seed, "example vary"), target id 0x42
    rv = Register(seed).run(16, 8, 16)
    flipped = rv.vary(hsh(seed, "example vary"), 0x42, 2, 8)
    rv.run(32, 8, 16)
    print("Vary depth 2 at t 16, s = hash(seed, \"example vary\"), target 42: flips the bits",
          ", ".join(f"b({16 - 1 - j})" for j in flipped), "->", grouped(bits(rv, 0, 32)))

    print("\n== §9.2 Freeze (LFO 17 bars, Triangle, Depth 4; pressed during bar 8, let go during bar 12: F 9, R 13)")
    bars = range(7, 15)
    print("bar              ", " ".join("%5d" % b for b in bars))
    print("replay's level   ", " ".join("%5d" % lfo(b, 17, "triangle") for b in bars))
    print("level heard      ", " ".join("%5d" % lfo(frozen_bar(b, 9, 13), 17, "triangle") for b in bars))
    print("Density moved by ", " ".join("%+5d" % rsdiv(lfo(frozen_bar(b, 9, 13), 17, "triangle") * 4, 1000) for b in bars))

    print("\n== §18.1 the form (A B A′ C, Section 2 phrases, phrase length 4: S = 8 bars)")
    e = parse_form("A B A' C")
    print("parsed:", " ".join(l + ("′" if p else "") for l, p in e))
    for bar in (0, 7, 8, 16, 24, 31, 32, 40, 100):
        k, pos, start, n = section_at(e, 8, bar)
        kh, posh, starth, nh = section_at(e, 8, bar, hold=True)
        print(f"bar {bar:3d}: Loop section {k:2d}, {e[pos][0]}{'′' if e[pos][1] else ''} from bar {start:3d};"
              f"  Hold section {kh}, {e[posh][0]}{'′' if e[posh][1] else ''} from bar {starth}{' (held)' if nh < 0 else ''}")
    salt = 0x00000000000000BB
    print("letter B's seed for A's 0123456789abcdef, salt 00000000000000bb: %016x" % hsh(seed, "form", salt))
    print("prime row id, matrixSeed 0123456789abcdef, place 2, group 0000000000000001: %016x" % hsh(seed, "form", "prime", 2, 1))
    print("its fixed seed s = hash(matrixSeed, \"matrix\", id): %016x" % hsh(seed, "matrix", hsh(seed, "form", "prime", 2, 1)))

    print("\n== §18.2 the colotomic clock")
    for N, K in ((16, 4), (12, 8), (3, 4)):
        bars = [b for b in range(N) if gong_clock(N, K, b)[1]]
        print(f"N {N}, K {K}: kenong boundaries at p", bars, " numbers", [gong_clock(N, K, b)[2] for b in bars])
    print("rows, N 16 K 4: every 1 gong ->", [b for b in range(1, 70) if b % 16 == 0],
          "; every 2 kenongs, offset 1 ->", [b for b in range(1, 70) if gong_clock(16, 4, b)[1] and (gong_clock(16, 4, b)[3] - 1) % 2 == 1],
          "; every 1 gong, offset -1 ->", [b for b in range(1, 70) if b % 16 == 15])
    print("position level, N 16, b 0 4 8 15 16:", [rdiv(1000 * (b % 16), 16) for b in (0, 4, 8, 15, 16)])

    print("\n== §18.3 irama (notes per core note 1 2 4 8 16 = levels 1…5)")
    for name, ct in (("4/4, chords every bar", 3840), ("4/4, every 1/4 bar", 960), ("4/4, every 2 bars", 7680), ("7/8, every 1/4 bar", 840)):
        print(f"{name:24s} (change {ct:5d} ticks): steps", [irama_step(ct, l) for l in range(1, 6)])
    levels = [2] * 5 + [4] * 59   # a row moves the Leads from 2 to 4 notes a chord at bar 5 (stays); gong cycle 16 bars
    print("moved at bar 5, gong 16: bars 0 4 5 15 16 17 32 play levels", [irama_latched(levels, 16, b) for b in (0, 4, 5, 15, 16, 17, 32)])

    print("\n== §18.4 tihai (start + 3N + 2g = sam; the phrase's steps 1…9 a…g, . a gap, L the landing)")
    for name, args in (("1/16, sam bar 4, N 5, g 1", (4, 3840, 0, 240, 5, 1, 4)),
                       ("1/16, sam bar 4, N 3, g 2", (4, 3840, 0, 240, 3, 2, 4)),
                       ("irama 4 (quarters), sam bar 4, N 3, g 0", (4, 3840, 0, 960, 3, 0, 4)),
                       ("3/16 steps (720), sam bar 4, N 4, g 1", (4, 3840, 0, 720, 4, 1, 4)),
                       ("1/16, N 16, g 8, sam bar 4 (64 steps: the whole phrase)", (4, 3840, 0, 240, 16, 8, 4)),
                       ("1/16 anchored at bar 3, sam bar 4, N 5, g 1", (4, 3840, 3 * 3840, 240, 5, 1, 4))):
        r = tihai(*args)
        if r is None:
            print(f"{name}: doesn't fit, no tihai")
        else:
            sam, a0, _ = r
            print(f"{name}: sam step {sam} (tick {args[2] + sam * args[3]}), start {a0} = {sam} - (3·{args[4]} + 2·{args[5]})")
            print("   ", tihai_line(r))

    print("\n== §17.10 sources (matrixSeed 0123456789abcdef, source id 00000000000000aa, phrase length 4)")
    sid = 0xAA
    print("LFO 17 triangle:", [(b, lfo(b, 17, "triangle")) for b in (0, 1, 4, 8, 9, 13, 16, 17)])
    print("LFO 19 saw up:  ", [(b, lfo(b, 19, "saw up")) for b in (0, 1, 9, 10, 18, 19)])
    print("seeded phase, i 0, P 17:", Stream(hsh(seed, "mod", sid, "phase", 0)).below(17))
    print("converge 17 and 19 bars:", next(b for b in range(1, 10000) if b % 17 == 0 and b % 19 == 0))
    print("depth 4 at b 4, 8:", rsdiv(lfo(4, 17, "triangle") * 4, 1000), rsdiv(lfo(8, 17, "triangle") * 4, 1000))
    for edge in ("wrap", "pendulum", "hold"):
        print(f"accumulator {edge}:", [accumulator(b, 4, 0, 3, 8, 0, edge) for b in range(0, 29, 4)])
    print("accumulator pendulum level:", [rdiv(1000 * accumulator(b, 4, 0, 3, 8, 0, "pendulum"), 7) for b in range(0, 29, 4)])
    print("accumulator step -3 wrap:", [accumulator(b, 4, 0, -3, 8, 0, "wrap") for b in range(0, 29, 4)])
    print("dice sums b 0…15:", [dice_sum(seed, sid, c, 5) for c in range(16)])
    print("dice levels b 0…7:", [rdiv(2000 * dice_sum(seed, sid, c, 5), 75) - 1000 for c in range(8)])
    for b in (0, 32, 64, 128, 153, 160, 166, 200, 230, 245, 255, 256):
        lvl, j, pos = arc(b, 256)
        print(f"arc b {b}: pos {pos}, level {lvl}, segment {j}")
    prev, fires = arc(0, 256)[1], []
    for b in range(1, 300):
        j = arc(b, 256)[1]
        if j != prev:
            fires.append((b, j))
        prev = j
    print("arc section fires (bar, amount):", fires)
    print("A:B 1:4, a row every bar, passes at:", [b for b in range(1, 14) if (b - 1) % 4 == 0])


if __name__ == "__main__":
    main()
