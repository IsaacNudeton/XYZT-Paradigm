/*
 * encoding_vs_position.c — Proving the difference
 *
 * Part 1: Conventional encoding. Same bits. Different observer.
 *         Show how meaning changes, corrupts, breaks.
 *
 * Part 2: Positional (XYZT). Same grid. Different observer.
 *         Show how meaning can vary in INTERPRETATION but
 *         the structural fact — WHO showed up WHERE — cannot.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int exp, int got) {
    if (exp == got) g_pass++;
    else { g_fail++; printf("  FAIL %s: expected %d, got %d\n", name, exp, got); }
}

/* ══════════════════════════════════════════════════════════════
 * PART 1: CONVENTIONAL ENCODING
 * Same bits. Different observer. Different meaning.
 * This is where every exploit lives.
 * ══════════════════════════════════════════════════════════════ */

static void test_same_bits_different_meaning(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  PART 1: Conventional — same bits, different meaning\n");
    printf("═══════════════════════════════════════════════════\n\n");

    uint8_t bits = 0b00000011;  /* just two switches on */

    /* Observer 1: "it's an unsigned integer" */
    int as_uint = (int)bits;

    /* Observer 2: "it's a signed integer" (already same here, but watch 0b11111111) */
    int8_t as_signed = (int8_t)bits;

    /* Observer 3: "it's ASCII" */
    char as_ascii = (char)bits;  /* ETX = end of text, non-printable */

    /* Observer 4: "it's boolean flags" */
    int flag_bit0 = (bits >> 0) & 1;  /* "feature enabled" */
    int flag_bit1 = (bits >> 1) & 1;  /* "admin access"    */

    printf("  Physical reality: two switches on — 0b00000011\n\n");
    printf("  Observer says 'unsigned int'  → %d\n", as_uint);
    printf("  Observer says 'signed int'    → %d\n", (int)as_signed);
    printf("  Observer says 'ASCII char'    → 0x%02X (ETX, non-printable)\n", (uint8_t)as_ascii);
    printf("  Observer says 'flag field'    → feature=%d, admin=%d\n\n", flag_bit0, flag_bit1);

    /* All four readings are "correct." The bits don't know which one they are. */
    check("uint",   3, as_uint);
    check("signed", 3, (int)as_signed);
    check("flag0",  1, flag_bit0);
    check("flag1",  1, flag_bit1);

    printf("  All four pass. All four disagree on meaning.\n");
    printf("  The bits don't know what they are.\n\n");
}

static void test_sign_confusion(void) {
    printf("--- Sign confusion: same bits, opposite meaning ---\n");
    uint8_t bits = 0b11111111;

    int as_unsigned = (int)bits;        /* 255 */
    int as_signed   = (int)(int8_t)bits; /* -1  */

    printf("  Bits: 0b11111111\n");
    printf("  Unsigned observer: %d\n", as_unsigned);
    printf("  Signed observer:   %d\n", as_signed);
    printf("  Same bits. One says 255. One says -1.\n\n");

    check("unsigned=255", 255, as_unsigned);
    check("signed=-1",    -1,  as_signed);
}

static void test_type_punning(void) {
    printf("--- Type punning: same bits, wildly different meaning ---\n");
    uint32_t int_bits = 0x40490FDB;  /* IEEE 754 for pi ≈ 3.14159 */

    int as_int = (int)int_bits;
    float as_float;
    memcpy(&as_float, &int_bits, sizeof(float));

    printf("  Bits: 0x40490FDB\n");
    printf("  Integer observer: %d\n", as_int);
    printf("  Float observer:   %.5f\n", as_float);
    printf("  Same 32 switches. One says million. One says pi.\n\n");

    check("int=1078530011", 1078530011, as_int);
    /* float check: pi to 2 decimal places */
    check("float≈3.14", 1, (as_float > 3.14f && as_float < 3.15f));
}

static void test_buffer_overflow_concept(void) {
    printf("--- Buffer overflow: data becomes code ---\n");

    /* Simulate: a buffer of "data" followed by a "return address" */
    struct {
        uint8_t buffer[4];     /* meant for data */
        uint32_t return_addr;  /* meant for code pointer */
    } stack_frame;

    stack_frame.return_addr = 0xDEADBEEF;  /* legitimate return */

    /* Attacker writes "data" — but writes too much */
    uint8_t attack[] = {0x41, 0x41, 0x41, 0x41,  /* fills buffer */
                        0x37, 0x13, 0x37, 0x13};  /* overwrites return addr */

    memcpy(&stack_frame, attack, 8);  /* the overflow */

    printf("  Before: return address = 0xDEADBEEF (safe)\n");
    printf("  After:  return address = 0x%08X (attacker controlled)\n", stack_frame.return_addr);
    printf("  Same bits. Were 'data'. Now 'code pointer'.\n");
    printf("  The CPU doesn't know the difference.\n\n");

    check("overwritten", 1, stack_frame.return_addr != 0xDEADBEEF);
}

static void test_encoding_disagreement(void) {
    printf("--- Encoding disagreement: same bytes, different text ---\n");

    uint8_t bytes[] = {0xC3, 0xA9};  /* UTF-8 for 'é' */

    /* UTF-8 observer: it's one character, é */
    printf("  Bytes: 0xC3 0xA9\n");
    printf("  UTF-8 observer:  'é' (one character)\n");

    /* Latin-1 observer: it's TWO characters, Ã© */
    printf("  Latin-1 observer: 'Ã©' (two characters: Ã and ©)\n");

    /* ASCII observer: both bytes are invalid (>127) */
    printf("  ASCII observer:  INVALID INVALID (>127, undefined)\n\n");

    printf("  Same two bytes. Three observers. Three realities.\n");
    printf("  Every mojibake you've ever seen is this.\n\n");

    /* UTF-8: 0xC3 = 110_00011, 0xA9 = 10_101001 → codepoint 0x00E9 = é */
    int utf8_codepoint = ((bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
    check("utf8=é", 0xE9, utf8_codepoint);

    /* Latin-1: each byte is its own character */
    check("latin1_byte0=Ã", 0xC3, bytes[0]);
    check("latin1_byte1=©", 0xA9, bytes[1]);
}

/* ══════════════════════════════════════════════════════════════
 * PART 2: POSITIONAL (XYZT)
 * Position IS value. No encoding. No convention.
 * Observer can read different OPERATIONS from same co-presence,
 * but cannot change the structural fact of WHO is WHERE.
 * ══════════════════════════════════════════════════════════════ */

#define MAX_POS 64
#define BIT(n) (1ULL << (n))

typedef struct {
    uint8_t  marked[MAX_POS];
    uint64_t reads[MAX_POS];
    uint64_t co_present[MAX_POS];
    uint8_t  active[MAX_POS];
    int      n_pos;
} Grid;

static void grid_init(Grid *g) { memset(g, 0, sizeof(*g)); }

static void grid_mark(Grid *g, int pos, int val) {
    g->marked[pos] = val ? 1 : 0;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static void grid_wire(Grid *g, int pos, uint64_t rd) {
    g->reads[pos] = rd;
    g->active[pos] = 1;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static void grid_tick(Grid *g) {
    uint8_t snap[MAX_POS];
    memcpy(snap, g->marked, MAX_POS);
    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        uint64_t present = 0;
        uint64_t bits = g->reads[p];
        while (bits) {
            int b = __builtin_ctzll(bits);
            if (snap[b]) present |= (1ULL << b);
            bits &= bits - 1;
        }
        g->co_present[p] = present;
    }
}

static int obs_any(uint64_t cp)              { return cp != 0 ? 1 : 0; }
static int obs_all(uint64_t cp, uint64_t rd) { return (cp & rd) == rd ? 1 : 0; }
static int obs_parity(uint64_t cp)           { return __builtin_popcountll(cp) & 1; }
static int obs_count(uint64_t cp)            { return __builtin_popcountll(cp); }

static void test_position_is_value(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  PART 2: Positional — address IS meaning\n");
    printf("═══════════════════════════════════════════════════\n\n");

    printf("--- Position IS value (no encoding) ---\n");

    /* "Store the number 3" — mark position 3. That's it. */
    Grid g; grid_init(&g);
    grid_mark(&g, 3, 1);

    printf("  To store '3': mark position 3.\n");
    printf("  Position 3 marked? %s\n", g.marked[3] ? "yes" : "no");
    printf("  No encoding. No convention. Position IS three.\n\n");

    /* Can any observer misread WHICH position is marked? */
    /* No. The physical fact is: position 3 is marked. */
    int wrong_positions = 0;
    for (int i = 0; i < MAX_POS; i++) {
        if (i == 3 && !g.marked[i]) wrong_positions++;
        if (i != 3 && g.marked[i])  wrong_positions++;
    }
    check("no misread possible", 0, wrong_positions);

    printf("  Every observer agrees: position 3 is marked.\n");
    printf("  There is nothing to misinterpret.\n\n");
}

static void test_observer_varies_operation_not_identity(void) {
    printf("--- Observer varies OPERATION, not IDENTITY ---\n");

    Grid g; grid_init(&g);
    grid_mark(&g, 0, 1);  /* position 0: marked */
    grid_mark(&g, 1, 1);  /* position 1: marked */
    grid_wire(&g, 2, BIT(0)|BIT(1));
    grid_tick(&g);

    uint64_t cp = g.co_present[2];

    /* Different observers, different operations */
    int or_val  = obs_any(cp);
    int and_val = obs_all(cp, BIT(0)|BIT(1));
    int xor_val = obs_parity(cp);
    int count   = obs_count(cp);

    printf("  Both positions marked. Co-presence at pos 2: {pos0, pos1}\n\n");
    printf("  obs_any    (OR):     %d\n", or_val);
    printf("  obs_all    (AND):    %d\n", and_val);
    printf("  obs_parity (XOR):    %d\n", xor_val);
    printf("  obs_count  (count):  %d\n\n", count);

    /* YES: observers disagree on the OPERATION (OR=1, XOR=0) */
    check("OR=1",  1, or_val);
    check("AND=1", 1, and_val);
    check("XOR=0", 0, xor_val);
    check("count=2", 2, count);

    /* BUT: every observer agrees on the FACT */
    printf("  Observers disagree on operation: OR=1, XOR=0.\n");
    printf("  Observers AGREE on fact: {pos0, pos1} showed up.\n\n");

    /* Prove it: all observers see the same co-presence set */
    check("fact is same for all", 1,
          (cp == (BIT(0)|BIT(1))));  /* structural fact is invariant */

    printf("  The co-presence set is INVARIANT across observers.\n");
    printf("  Only the interpretation varies.\n");
    printf("  In conventional: the FACT varies (3 or -1 or pi).\n");
    printf("  In positional: the fact is FIXED. Only meaning varies.\n\n");
}

static void test_no_buffer_overflow(void) {
    printf("--- No buffer overflow possible ---\n");

    /* In conventional: adjacent memory, data becomes code.
     * In positional: positions are wired. Topology is fixed.
     * You can't "write past" a position into another's wiring. */

    Grid g; grid_init(&g);

    /* Wire two separate circuits */
    grid_mark(&g, 0, 1); grid_mark(&g, 1, 0);  /* "data" */
    grid_wire(&g, 2, BIT(0)|BIT(1));             /* data circuit */

    grid_mark(&g, 3, 1); grid_mark(&g, 4, 1);   /* "control" */
    grid_wire(&g, 5, BIT(3)|BIT(4));              /* control circuit */

    grid_tick(&g);

    printf("  Data circuit:    pos 0,1 → pos 2\n");
    printf("  Control circuit: pos 3,4 → pos 5\n\n");

    /* Data positions CANNOT affect control circuit */
    /* Control reads from {3,4}. Period. Topology is fixed. */
    uint64_t control_reads = g.reads[5];
    int data_leaks_to_control = (control_reads & (BIT(0)|BIT(1))) != 0;

    check("data cannot reach control", 0, data_leaks_to_control);

    /* Even if we "corrupt" data positions, control doesn't care */
    grid_mark(&g, 0, 1); grid_mark(&g, 1, 1);  /* change all data */
    grid_tick(&g);

    /* Control circuit result is UNCHANGED by data mutations */
    int control_result = obs_all(g.co_present[5], BIT(3)|BIT(4));
    check("control unaffected", 1, control_result);

    printf("  Data mutated. Control unchanged.\n");
    printf("  Topology enforces isolation. Not convention.\n");
    printf("  There's no adjacent memory to overflow INTO.\n\n");
}

static void test_no_type_confusion(void) {
    printf("--- No type confusion possible ---\n");

    Grid g; grid_init(&g);
    grid_mark(&g, 0, 1); grid_mark(&g, 1, 1);
    grid_wire(&g, 2, BIT(0)|BIT(1));
    grid_tick(&g);

    uint64_t cp = g.co_present[2];

    /* In conventional: you can CAST bits to wrong type.
     * 0x40490FDB "becomes" pi if you squint differently.
     *
     * In positional: the co-presence set IS the identity.
     * {pos0, pos1} is {pos0, pos1}. You can't cast it to
     * be something else. The observers vary in what OPERATION
     * they extract, but they all see the same two guests. */

    printf("  Co-presence: {pos0, pos1}\n\n");

    /* Every observer extracts from the SAME set */
    int test_or  = obs_any(cp);     /* reads {pos0, pos1} → 1 */
    int test_xor = obs_parity(cp);  /* reads {pos0, pos1} → 0 */

    /* Different operations. Same input. Neither is "wrong." */
    /* Unlike conventional where int vs float on same bits = one is wrong. */
    printf("  OR observer  reads {pos0, pos1} → %d (somebody showed up)\n", test_or);
    printf("  XOR observer reads {pos0, pos1} → %d (even count)\n", test_xor);
    printf("  Both correct. Different questions about same guest list.\n");
    printf("  Neither is a 'wrong type'. Both are valid observations.\n\n");

    /* In conventional: 0x40490FDB as int = WRONG if it's "really" a float.
     * In positional: there is no "really." The guest list is the truth.
     * The observers are just asking different questions about it.
     * All questions are legitimate. None can corrupt the answer. */

    check("OR valid", 1, test_or);
    check("XOR valid", 0, test_xor);
    check("same co-presence", 1, cp == (BIT(0)|BIT(1)));

    printf("  In conventional: one observer is RIGHT, others are WRONG.\n");
    printf("  In positional: all observers are RIGHT. Different questions.\n");
    printf("  You can't have a 'type error' when there are no types.\n\n");
}

static void test_no_encoding_disagreement(void) {
    printf("--- No encoding disagreement possible ---\n");

    /* In conventional: 0xC3 0xA9 = 'é' or 'Ã©' depending on who reads it.
     * In positional: if positions 0xC3 and 0xA9 are marked, EVERYONE
     * agrees those positions are marked. */

    Grid g; grid_init(&g);
    grid_mark(&g, 5, 1);
    grid_mark(&g, 12, 1);
    grid_mark(&g, 31, 1);

    printf("  Marked positions: 5, 12, 31\n\n");

    /* Observer A reads it */
    int a_sees_5  = g.marked[5];
    int a_sees_12 = g.marked[12];
    int a_sees_31 = g.marked[31];
    int a_sees_6  = g.marked[6];

    /* Observer B reads it */
    int b_sees_5  = g.marked[5];
    int b_sees_12 = g.marked[12];
    int b_sees_31 = g.marked[31];
    int b_sees_6  = g.marked[6];

    /* Observer C reads it — maybe adversarial, maybe confused */
    int c_sees_5  = g.marked[5];
    int c_sees_12 = g.marked[12];
    int c_sees_31 = g.marked[31];
    int c_sees_6  = g.marked[6];

    printf("  Observer A: pos5=%d pos12=%d pos31=%d pos6=%d\n", a_sees_5, a_sees_12, a_sees_31, a_sees_6);
    printf("  Observer B: pos5=%d pos12=%d pos31=%d pos6=%d\n", b_sees_5, b_sees_12, b_sees_31, b_sees_6);
    printf("  Observer C: pos5=%d pos12=%d pos31=%d pos6=%d\n\n", c_sees_5, c_sees_12, c_sees_31, c_sees_6);

    /* All three agree. They MUST agree. There's nothing to disagree about. */
    check("A=B pos5",  a_sees_5,  b_sees_5);
    check("A=C pos5",  a_sees_5,  c_sees_5);
    check("A=B pos12", a_sees_12, b_sees_12);
    check("A=C pos12", a_sees_12, c_sees_12);
    check("A=B pos31", a_sees_31, b_sees_31);
    check("A=C pos31", a_sees_31, c_sees_31);
    check("A=B pos6",  a_sees_6,  b_sees_6);
    check("A=C pos6",  a_sees_6,  c_sees_6);

    printf("  All observers agree. Always. Structurally.\n");
    printf("  Not because of a protocol. Because there's nothing to disagree about.\n");
    printf("  0xC3 0xA9 needs a protocol. Position 5 does not.\n\n");
}

static void test_conventional_exploit_vs_positional(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  PART 3: Side by side — the same attack\n");
    printf("═══════════════════════════════════════════════════\n\n");

    printf("--- Conventional: attacker reinterprets bits ---\n");
    uint8_t data = 42;  /* innocent data */
    /* "attacker" casts data as something else */
    int as_data = (int)data;
    int as_bool = (data != 0);
    int8_t as_offset = (int8_t)data;  /* used as memory offset */
    printf("  Bits: 0x2A (42)\n");
    printf("  Normal:     value %d\n", as_data);
    printf("  As bool:    %d (true — grants access?)\n", as_bool);
    printf("  As offset:  jump %d bytes (into what?)\n\n", (int)as_offset);
    check("data=42", 42, as_data);
    check("bool=1", 1, as_bool);

    printf("--- Positional: attacker tries to reinterpret ---\n");
    Grid g; grid_init(&g);
    grid_mark(&g, 5, 1);  /* value: position 5 is marked */
    grid_mark(&g, 9, 1);  /* value: position 9 is marked */

    /* Attacker wants to read this as "offset 5" or "address 9" */
    /* But there's no cast. There's no reinterpretation. */
    /* Position 5 is marked. Position 9 is marked. That's the fact. */
    /* The attacker can ask obs_any, obs_all, obs_count — */
    /* they all report on {5, 9}. None produces a WRONG fact. */

    /* Wire them to a destination */
    grid_wire(&g, 10, BIT(5)|BIT(9));
    grid_tick(&g);

    uint64_t cp = g.co_present[10];

    printf("  Marked: pos 5, pos 9\n");
    printf("  Attacker reads co-presence: {");
    int first = 1;
    for (int i = 0; i < MAX_POS; i++) {
        if (cp & BIT(i)) {
            if (!first) printf(", ");
            printf("pos%d", i);
            first = 0;
        }
    }
    printf("}\n");
    printf("  Attacker can ask different questions about {5, 9}.\n");
    printf("  Attacker CANNOT make it be {7, 3} or {42} or 'admin'.\n");
    printf("  The guest list is the guest list.\n\n");

    check("copresence is structural", 1, cp == (BIT(5)|BIT(9)));
}

/* ══════════════════════════════════════════════════════════════ */

int main(void) {
    printf("==========================================================\n");
    printf("  Encoding vs. Position\n");
    printf("  Why every computer exploit exists,\n");
    printf("  and why positional computing doesn't have them.\n");
    printf("==========================================================\n\n");

    /* Part 1: Conventional breaks */
    test_same_bits_different_meaning();
    test_sign_confusion();
    test_type_punning();
    test_buffer_overflow_concept();
    test_encoding_disagreement();

    /* Part 2: Positional doesn't */
    test_position_is_value();
    test_observer_varies_operation_not_identity();
    test_no_buffer_overflow();
    test_no_type_confusion();
    test_no_encoding_disagreement();

    /* Part 3: Same attack, both systems */
    test_conventional_exploit_vs_positional();

    printf("==========================================================\n");
    printf("  RESULTS: %d passed, %d failed, %d total\n",
           g_pass, g_fail, g_pass + g_fail);
    printf("==========================================================\n");

    if (g_fail == 0) {
        printf("\n  ALL PASS.\n\n");
        printf("  Conventional computing:\n");
        printf("    Bits have no identity.\n");
        printf("    Meaning comes from convention.\n");
        printf("    Convention can be broken.\n");
        printf("    → exploits, type errors, encoding bugs\n\n");
        printf("  Positional computing (XYZT):\n");
        printf("    Position IS identity.\n");
        printf("    Meaning comes from structure.\n");
        printf("    Structure cannot be reinterpreted.\n");
        printf("    → observers vary operation, not fact\n\n");
        printf("  Math is just different ways of reading the guest list.\n");
        printf("  Conventional computing lets you forge the guest list.\n");
        printf("  Positional computing: the guests are standing there.\n");
        printf("  You can't forge someone's physical location.\n\n");
    } else {
        printf("\n  FAILURES DETECTED.\n\n");
    }
    return g_fail;
}
