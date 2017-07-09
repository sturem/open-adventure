#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include "dungeon.h"

#define LINESIZE       1024
#define TOKLEN         5          // № sigificant characters in a token */
#define NDWARVES       6          // number of dwarves
#define PIRATE         NDWARVES   // must be NDWARVES-1 when zero-origin
#define DALTLC         LOC_NUGGET // alternate dwarf location
#define INVLIMIT       7          // inverntory limit (№ of objects)
#define INTRANSITIVE   -1         // illegal object number
#define GAMELIMIT      330        // base limit of turns
#define NOVICELIMIT    1000       // limit of turns for novice
#define WARNTIME       30         // late game starts at game.limit-this
#define FLASHTIME      50         // turns from first warning till blinding flash
#define PANICTIME      15         // time left after closing
#define BATTERYLIFE    2500       // turn limit increment from batteries
#define WORD_NOT_FOUND -1         // "Word not found" flag value for the vocab hash functions.
#define WORD_EMPTY     0          // "Word empty" flag value for the vocab hash functions
#define CARRIED        -1         // Player is toting it
#define READ_MODE      "rb"       // b is not needed for POSIX but harmless
#define WRITE_MODE     "wb"       // b is not needed for POSIX but harmless

/* Special object-state values - integers > 0 are object-specific */
#define STATE_NOTFOUND  -1	  // 'Not found" state of treasures */
#define STATE_FOUND	0	  // After discovered, before messed with
#define STATE_IN_CAVITY	1	  // State value common to all gemstones

/* Special fixed object-state values - integers > 0 are location */
#define IS_FIXED -1
#define IS_FREE 0

/* Map a state property value to a negative range, where the object cannot be
 * picked up but the value can be recovered later.  Avoid colliding with -1,
 * which has its own meaning. */
#define STASHED(obj)	(-1 - game.prop[obj])

/*
 *  MOD(N,M)    = Arithmetic modulus
 *  AT(OBJ)     = true if on either side of two-placed object
 *  CNDBIT(L,N) = true if COND(L) has bit n set (bit 0 is units bit)
 *  DARK(LOC)   = true if location "LOC" is dark
 *  FORCED(LOC) = true if LOC moves without asking for input (COND=2)
 *  FOREST(LOC) = true if LOC is part of the forest
 *  GSTONE(OBJ) = true if OBJ is a gemstone
 *  HERE(OBJ)   = true if the OBJ is at "LOC" (or is being carried)
 *  LIQUID()    = object number of liquid in bottle
 *  LIQLOC(LOC) = object number of liquid (if any) at LOC
 *  PCT(N)      = true N% of the time (N integer from 0 to 100)
 *  TOTING(OBJ) = true if the OBJ is being carried */
#define DESTROY(N)   move(N, LOC_NOWHERE)
#define MOD(N,M)     ((N) % (M))
#define TOTING(OBJ)  (game.place[OBJ] == CARRIED)
#define AT(OBJ)      (game.place[OBJ] == game.loc || game.fixed[OBJ] == game.loc)
#define HERE(OBJ)    (AT(OBJ) || TOTING(OBJ))
#define CNDBIT(L,N)  (tstbit(conditions[L],N))
#define LIQUID()     (game.prop[BOTTLE] == WATER_BOTTLE? WATER : game.prop[BOTTLE] == OIL_BOTTLE ? OIL : NO_OBJECT )
#define LIQLOC(LOC)  (CNDBIT((LOC),COND_FLUID)? CNDBIT((LOC),COND_OILY) ? OIL : WATER : NO_OBJECT)
#define FORCED(LOC)  CNDBIT(LOC, COND_FORCED)
#define DARK(DUMMY)  (!CNDBIT(game.loc,COND_LIT) && (game.prop[LAMP] == LAMP_DARK || !HERE(LAMP)))
#define PCT(N)       (randrange(100) < (N))
#define GSTONE(OBJ)  ((OBJ) == EMERALD || (OBJ) == RUBY || (OBJ) == AMBER || (OBJ) == SAPPH)
#define FOREST(LOC)  CNDBIT(LOC, COND_FOREST)
#define OUTSID(LOC)  (CNDBIT(LOC, COND_ABOVE) || FOREST(LOC))
#define INDEEP(LOC)  ((LOC) >= LOC_MISTHALL && !OUTSID(LOC))
#define BUG(x)       bug(x, #x)
#define MOTION_WORD(n)  ((n) + 0)
#define OBJECT_WORD(n)  ((n) + 1000)
#define ACTION_WORD(n)  ((n) + 2000)
#define SPECIAL_WORD(n) ((n) + 3000)
#define PROMOTE_WORD(n) ((n) + 1000)
#define DEMOTE_WORD(n)  ((n) - 1000)

enum bugtype {
    SPECIAL_TRAVEL_500_GT_L_GT_300_EXCEEDS_GOTO_LIST,
    VOCABULARY_TYPE_N_OVER_1000_NOT_BETWEEN_0_AND_3,
    INTRANSITIVE_ACTION_VERB_EXCEEDS_GOTO_LIST,
    TRANSITIVE_ACTION_VERB_EXCEEDS_GOTO_LIST,
    CONDITIONAL_TRAVEL_ENTRY_WITH_NO_ALTERATION,
    LOCATION_HAS_NO_TRAVEL_ENTRIES,
    HINT_NUMBER_EXCEEDS_GOTO_LIST,
    SPEECHPART_NOT_TRANSITIVE_OR_INTRANSITIVE_OR_UNKNOWN,
    ACTION_RETURNED_PHASE_CODE_BEYOND_END_OF_SWITCH,
};

enum speaktype {touch, look, hear, study, change};

enum termination {endgame, quitgame, scoregame};

enum speechpart {unknown, intransitive, transitive};

/* Phase codes for action returns.
 * These were at one time FORTRAN line numbers.
 * The values don't matter, but perturb their order at your peril.
 */
enum phase_codes {
    GO_TERMINATE,
    GO_MOVE,
    GO_TOP,
    GO_CLEAROBJ,
    GO_CHECKHINT,
    GO_CHECKFOO,
    GO_DIRECTION,
    GO_LOOKUP,
    GO_WORD2,
    GO_SPECIALS,
    GO_UNKNOWN,
    GO_ACTION,
    GO_DWARFWAKE,
};

typedef long token_t;  // word token - someday this will be char[TOKLEN+1] */
typedef long vocab_t;  // index into a vocabulary array */
typedef long verb_t;   // index into an actions array */
typedef long obj_t;    // index into the object array */
typedef long loc_t;    // index into the locations array */

struct game_t {
    unsigned long lcg_a, lcg_c, lcg_m, lcg_x;
    long abbnum;                 // How often to print non-abbreviated descriptions
    long bonus;
    long chloc;
    long chloc2;
    long clock1;                 // # turns from finding last treasure till closing
    long clock2;                 // # turns from first warning till blinding flash
    bool clshnt;                 // has player read the clue in the endgame?
    bool closed;                 // whether we're all the way closed
    bool closng;                 // whether it's closing time yet
    long conds;                  // min value for cond(loc) if loc has any hints
    long detail;

    /*  dflag controls the level of activation of dwarves:
     *	0	No dwarf stuff yet (wait until reaches Hall Of Mists)
     *	1	Reached Hall Of Mists, but hasn't met first dwarf
     *	2	Met first dwarf, others start moving, no knives thrown yet
     *	3	A knife has been thrown (first set always misses)
     *	3+	Dwarves are mad (increases their accuracy) */
    long dflag;

    long dkill;
    long dtotal;
    long foobar;                 // current progress in saying "FEE FIE FOE FOO".
    long holdng;                 // number of objects being carried
    long iwest;                  // How many times he's said "west" instead of "w"
    long knfloc;                 // 0 if no knife here, loc if knife , -1 after caveat
    long limit;                  // lifetime of lamp (not set here)
    bool lmwarn;                 // has player been warned about lamp going dim?
    long loc;
    long newloc;
    bool novice;                 // asked for instructions at start-up?
    long numdie;                 // number of times killed so far
    long oldloc;
    long oldlc2;
    long oldobj;
    bool panic;                  // has player found out he's trapped in the cave?
    long saved;                  // point penalty for saves
    long tally;
    long thresh;
    long trndex;
    long trnluz;                 // № points lost so far due to number of turns used
    long turns;                  // how many commands he's given (ignores yes/no)
    bool wzdark;                 // whether the loc he's leaving was dark
    char zzword[TOKLEN + 1];     // randomly generated magic word from bird
    bool blooded;                // has player drunk of dragon's blood?
    long abbrev[NLOCATIONS + 1];
    long atloc[NLOCATIONS + 1];
    long dseen[NDWARVES + 1];    // true if dwarf has seen him
    loc_t dloc[NDWARVES + 1];     // location of dwarves, initially hard-wired in
    loc_t odloc[NDWARVES + 1];    // prior loc of each dwarf, initially garbage
    loc_t fixed[NOBJECTS + 1];
    long link[NOBJECTS * 2 + 1];
    loc_t place[NOBJECTS + 1];
    long hinted[NHINTS];         // hintlc[i] is how long he's been at LOC with cond bit i
    long hintlc[NHINTS];         // hinted[i] is true iff hint i has been used.
    long prop[NOBJECTS + 1];
};

/*
 * Game application settings - settings, but not state of the game, per se.
 * This data is not saved in a saved game.
 */
struct settings_t {
    FILE *logfp;
    bool oldstyle;
    bool prompt;
};

struct command_t {
    enum speechpart part;
    verb_t verb;
    obj_t   obj;
    token_t wd1;
    token_t wd2;
    long id1;
    long id2;
    char raw1[LINESIZE], raw2[LINESIZE];
};

extern struct game_t game;
extern struct settings_t settings;

extern void packed_to_token(long, char token[]);
extern long token_to_packed(const char token[]);
extern void tokenize(char*, struct command_t *);
extern void vspeak(const char*, bool, va_list);
extern bool wordeq(token_t, token_t);
extern bool wordempty(token_t);
extern void wordclear(token_t *);
extern void speak(const char*, ...);
extern void sspeak(long msg, ...);
extern void pspeak(vocab_t, enum speaktype, int, bool, ...);
extern void rspeak(vocab_t, ...);
extern void echo_input(FILE*, const char*, const char*);
extern int word_count(char*);
extern char* get_input(void);
extern bool silent_yes(void);
extern bool yes(const char*, const char*, const char*);
extern int get_motion_vocab_id(const char*);
extern int get_object_vocab_id(const char*);
extern int get_action_vocab_id(const char*);
extern int get_special_vocab_id(const char*);
extern long get_vocab_id(const char*);
extern void juggle(obj_t);
extern void move(obj_t, loc_t);
extern long put(obj_t, long, long);
extern void carry(obj_t, loc_t);
extern void drop(obj_t, loc_t);
extern long atdwrf(loc_t);
extern long setbit(long);
extern bool tstbit(long, int);
extern void make_zzword(char*);
extern void set_seed(long);
extern unsigned long get_next_lcg_value(void);
extern long randrange(long);
extern long score(enum termination);
extern void terminate(enum termination) __attribute__((noreturn));
extern int savefile(FILE *, long);
extern int suspend(void);
extern int resume(void);
extern int restore(FILE *);
extern long initialise(void);
extern int action(struct command_t *command);
extern void state_change(obj_t, long);


void bug(enum bugtype, const char *) __attribute__((__noreturn__));

/* end */
