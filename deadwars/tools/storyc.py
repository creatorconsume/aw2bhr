#!/usr/bin/env python3
"""Dead Wars: The Last Boat - storylet compiler (design: docs/design/LAST_BOAT.md section 11).

  python3 -I tools/storyc.py <data dir> <out dir>   check data/story/*.tsv, write story_data.{c,h}
  python3 -I tools/storyc.py --check <data dir>     check only, write nothing
  python3 -I tools/storyc.py --selftest             every test/story/bad_*/ fixture must fail with
                                                    its expected message; data/story must pass

Every table is loaded into an in-memory SQLite database (foreign keys and CHECK constraints),
then cross-checked here: types and ranges of conditions and effects, roles, placeholders and
the dialogue box fit. Any problem prints "file:line: problem" and exits 1.
Game enums are read from src/lb.h and src/story.h, cast names from src/lb_data.c, and the
dialogue box layout from src/text.c, so the checks follow the code they protect.
Authoring guide: docs/tech/STORY.md. Python standard library only.
"""
import os
import re
import sqlite3
import sys
import tempfile
import zlib

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SRC = os.path.join(ROOT, 'src')
FIXTURES = os.path.join(ROOT, 'test', 'story')
DATA = os.path.join(ROOT, 'data', 'story')

MAX_ROLES = 4       # roles per storylet
MAX_LINES = 8       # lines per intro / per choice reply (story.c buffers)
MAX_CHOICES = 4     # menu rows that fit above the dialogue box
LABEL_MAX = 26      # choice label, after placeholders (menu window is 30 wide)
MAX_STORYLETS = 256 # LogRec.arg (u8) holds the storylet index
LOG_MAX_W = 25      # a Chronicle line in the journal (run.c draws 25 characters)


class Fatal(Exception):
    pass


class Log:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def err(self, where, msg):
        self.errors.append('%s: %s' % (where, msg))

    def warn(self, where, msg):
        self.warnings.append('%s: warning: %s' % (where, msg))


# ------------------------------------------------------------------ game facts from the C sources

def read_text(path):
    try:
        with open(path, encoding='utf-8') as f:
            return f.read()
    except OSError as e:
        raise Fatal('%s: cannot read (%s)' % (path, e.strerror))


def parse_enum(text, last, path):
    """Names of the enum whose terminator is `last` (e.g. SK_COUNT), without the terminator."""
    for m in re.finditer(r'enum\s*\{([^}]*)\}', text):
        body = re.sub(r'//[^\n]*|/\*.*?\*/', '', m.group(1), flags=re.S)
        names = [t.split('=')[0].strip() for t in body.split(',')]
        names = [n for n in names if n]
        if last in names:
            return names[:names.index(last)]
    raise Fatal('%s: cannot find the enum that ends with %s' % (path, last))


def parse_define(text, name, path):
    m = re.search(r'#define\s+%s\s+(\d+)' % name, text)
    if not m:
        raise Fatal('%s: cannot find #define %s' % (path, name))
    return int(m.group(1))


def need(m, path, what):
    if not m:
        raise Fatal('%s: cannot find %s (storyc.py reads it; update storyc.py if the code changed)' % (path, what))
    return m


class Game:
    """Enums, limits, cast and dialogue layout read from src/."""

    def __init__(self, src):
        p_lb, p_st = os.path.join(src, 'lb.h'), os.path.join(src, 'story.h')
        p_data, p_text = os.path.join(src, 'lb_data.c'), os.path.join(src, 'text.c')
        lb, st, data, text = read_text(p_lb), read_text(p_st), read_text(p_data), read_text(p_text)

        def short(names, prefix):
            return [(n[len(prefix):].lower(), n) for n in names]

        self.skills = dict(short(parse_enum(lb, 'SK_COUNT', p_lb), 'SK_'))
        self.traits = dict(short(parse_enum(lb, 'TR_COUNT', p_lb), 'TR_'))
        self.res = dict(short(parse_enum(lb, 'RES_COUNT', p_lb), 'RES_'))
        self.casts = dict(short(parse_enum(lb, 'CAST_COUNT', p_lb), 'CAST_'))
        self.phases = dict(short(parse_enum(st, 'SP_COUNT', p_st), 'SP_'))
        self.phase_order = list(self.phases)
        self.res_order = list(self.res)
        self.roster_max = parse_define(lb, 'ROSTER_MAX', p_lb)
        self.max_vars = parse_define(lb, 'STORY_VARS', p_lb)
        self.flag_bits = parse_define(lb, 'STORY_FLAG_BYTES', p_lb) * 8
        self.cool_slots = int(need(re.search(r'story_cool\[(\d+)\]', lb), p_lb, 'RunState.story_cool[]').group(1))
        self.name_max = int(need(re.search(r'char\s+name\[(\d+)\]', lb), p_lb, 'Person.name[]').group(1)) - 1

        # cast_info rows: name, portrait (PT_* or 0xFF) and pronoun (0 he, 1 she, 2 they)
        self.cast_name, self.cast_portrait, self.cast_he = {}, {}, {}
        for cid, enum in self.casts.items():
            m = need(re.search(r'\[\s*%s\s*\]\s*=\s*\{\s*"([^"]*)"[^\n]*,\s*(\w+)\s*,\s*(\d+)\s*\}\s*,?\s*$' % enum,
                               data, re.M), p_data, 'the cast_info[%s] row' % enum)
            self.cast_name[cid] = m.group(1)
            self.cast_portrait[cid] = m.group(2).startswith('PT_')
            self.cast_he[cid] = min(int(m.group(3)), 2)

        # dialogue() box (text.c): width/rows with and without a portrait, and the blinking arrow
        body = need(re.search(r'void dialogue\(const Line \*lines\)\s*\{(.*?)\n\}', text, re.S), p_text, 'dialogue()').group(1)
        g = need(re.search(r'bx = (\d+), by = (\d+), bw = (\d+), bh = (\d+);', body), p_text, 'the dialogue box size')
        bx, by, bw, bh = map(int, g.groups())
        g = need(re.search(r'int tx = bx \+ (\d+), tw = bw - (\d+);', body), p_text, 'the plain text width')
        plain = (bx + int(g.group(1)), bw - int(g.group(2)))
        g = need(re.search(r'tx = bx \+ (\d+);\s*tw = bw - (\d+);', body), p_text, 'the portrait text width')
        port = (bx + int(g.group(1)), bw - int(g.group(2)))
        g = need(re.search(r'rows = bh - (\d+);', body), p_text, 'the number of text rows')
        rows = bh - int(g.group(1))
        g = need(re.search(r'text_put\(tx, by \+ (\d+) \+ r,', body), p_text, 'the first text row')
        row0 = int(g.group(1))
        g = need(re.search(r'text_put\(bw - (\d+), by \+ bh - (\d+),', body), p_text, 'the "more" arrow position')
        acol, arow = bw - int(g.group(1)), bh - int(g.group(2)) - row0
        g = need(re.search(r'char out\[(\d+)\]\[(\d+)\];', body), p_text, 'the layout buffer')
        if rows > int(g.group(1)) or max(plain[1], port[1]) >= int(g.group(2)):
            raise Fatal('%s: dialogue() layout buffer is smaller than the box' % p_text)
        self.rows = rows
        self.layout = {}
        for key, (tx, tw) in (('plain', plain), ('portrait', port)):
            limits = [tw] * rows
            if 0 <= arow < rows and tx <= acol < tx + tw:
                limits[arow] = acol - tx  # the blinking arrow overwrites this column
            self.layout[key] = (tw, limits)


# ------------------------------------------------------------------ tables

INT, WORD, TEXT = 'int', 'word', 'text'

# (column, kind, header required, default)
COLUMNS = {
    'phase': [('id', WORD, True, None), ('quiet', INT, True, None), ('doc', TEXT, False, None)],
    'var': [('id', WORD, True, None), ('type', WORD, True, None), ('min', INT, True, None),
            ('max', INT, True, None), ('default', INT, True, None), ('doc', TEXT, False, None)],
    'storylet': [('id', WORD, True, None), ('phase', WORD, True, None), ('weight', INT, True, None),
                 ('priority', INT, False, 0), ('once', INT, False, 0), ('cooldown', INT, False, 0),
                 ('cool_group', WORD, False, None), ('excl_group', WORD, False, None), ('log', TEXT, False, None),
                 ('doc', TEXT, False, None)],
    'role': [('storylet', WORD, True, None), ('role', WORD, True, None), ('kind', WORD, False, 'alive'),
             ('cast', WORD, False, None), ('doc', TEXT, False, None)],
    'choice': [('storylet', WORD, True, None), ('choice', INT, True, None), ('label', TEXT, True, None),
               ('doc', TEXT, False, None)],
    'cond': [('storylet', WORD, True, None), ('choice', INT, False, None), ('role', WORD, False, None),
             ('input', WORD, True, None), ('other', WORD, False, None), ('op', WORD, True, None),
             ('value', WORD, True, None), ('doc', TEXT, False, None)],
    'text': [('storylet', WORD, True, None), ('choice', INT, False, None), ('speaker', WORD, False, None),
             ('body', TEXT, True, None)],
    'effect': [('storylet', WORD, True, None), ('choice', INT, False, None), ('op', WORD, True, None),
               ('role', WORD, False, None), ('other', WORD, False, None), ('target', WORD, False, None),
               ('value', INT, False, None), ('chance', INT, False, 100), ('doc', TEXT, False, None)],
}
LOAD_ORDER = ['phase', 'var', 'storylet', 'role', 'choice', 'cond', 'text', 'effect']

ROLE_OPS = ('hp_add', 'infect', 'cure', 'trait_add', 'trait_remove', 'rel_add', 'skill_xp')
EFFECT_OPS = ('res_add', 'morale_add', 'var_set', 'var_add') + ROLE_OPS
COND_OPS = ('=', '!=', '<', '<=', '>', '>=', 'has', 'lacks')
IDENT = "%s GLOB '[a-z]*' AND %s NOT GLOB '*[^a-z0-9_]*'"


def ident(col):
    return IDENT % (col, col)


def schema_sql(game):
    phases = ', '.join("'%s'" % p for p in game.phase_order)
    q = lambda s: '"%s"' % s.replace('"', "'")
    ops = ', '.join("'%s'" % o for o in EFFECT_OPS)
    rops = ', '.join("'%s'" % o for o in ROLE_OPS)
    cops = ', '.join("'%s'" % o for o in COND_OPS)
    return '''
CREATE TABLE castsel(id TEXT PRIMARY KEY, code TEXT NOT NULL);
CREATE TABLE input(id TEXT PRIMARY KEY, scope TEXT NOT NULL, type TEXT NOT NULL, lo INTEGER, hi INTEGER,
                   code TEXT NOT NULL, arg TEXT);
CREATE TABLE phase(line INTEGER,
  id TEXT NOT NULL PRIMARY KEY CONSTRAINT %(c_phase)s CHECK(id IN (%(phases)s)),
  quiet INTEGER NOT NULL CONSTRAINT %(c_quiet)s CHECK(quiet BETWEEN 0 AND 255),
  doc TEXT);
CREATE TABLE var(line INTEGER,
  id TEXT NOT NULL PRIMARY KEY CONSTRAINT %(c_ident)s CHECK(%(id_ok)s),
  type TEXT NOT NULL CONSTRAINT %(c_vtype)s CHECK(type IN ('bool', 'int')),
  min INTEGER NOT NULL, max INTEGER NOT NULL, "default" INTEGER NOT NULL, doc TEXT,
  CONSTRAINT %(c_vorder)s CHECK(min <= "default" AND "default" <= max),
  CONSTRAINT %(c_vs16)s CHECK(min >= -32768 AND max <= 32767),
  CONSTRAINT %(c_vbool)s CHECK(type <> 'bool' OR (min = 0 AND max = 1)));
CREATE TABLE storylet(line INTEGER,
  id TEXT NOT NULL PRIMARY KEY CONSTRAINT %(c_ident)s CHECK(%(id_ok)s),
  phase TEXT NOT NULL REFERENCES phase(id),
  weight INTEGER NOT NULL CONSTRAINT %(c_weight)s CHECK(weight BETWEEN 0 AND 255),
  priority INTEGER NOT NULL CONSTRAINT %(c_prio)s CHECK(priority BETWEEN 0 AND 9),
  once INTEGER NOT NULL CONSTRAINT %(c_once)s CHECK(once IN (0, 1)),
  cooldown INTEGER NOT NULL CONSTRAINT %(c_cool)s CHECK(cooldown BETWEEN 0 AND 255),
  cool_group TEXT CONSTRAINT %(c_group)s CHECK(cool_group IS NULL OR (%(cg_ok)s)),
  excl_group TEXT CONSTRAINT %(c_group)s CHECK(excl_group IS NULL OR (%(eg_ok)s)),
  log TEXT, doc TEXT,
  CONSTRAINT %(c_oncecool)s CHECK(NOT (once = 1 AND cooldown > 0)),
  CONSTRAINT %(c_cgcool)s CHECK(cool_group IS NULL OR cooldown > 0));
CREATE TABLE role(line INTEGER,
  storylet TEXT NOT NULL REFERENCES storylet(id),
  role TEXT NOT NULL CONSTRAINT %(c_role)s CHECK(role GLOB '[A-Z]*' AND role NOT GLOB '*[^A-Z0-9]*' AND length(role) <= 8),
  kind TEXT NOT NULL CONSTRAINT %(c_kind)s CHECK(kind IN ('alive', 'subject', 'subject_dead')),
  "cast" TEXT REFERENCES castsel(id),
  doc TEXT,
  PRIMARY KEY(storylet, role));
CREATE UNIQUE INDEX role_one_subject ON role(storylet) WHERE kind <> 'alive';
CREATE TABLE choice(line INTEGER,
  storylet TEXT NOT NULL REFERENCES storylet(id),
  choice INTEGER NOT NULL CONSTRAINT %(c_choice)s CHECK(choice BETWEEN 1 AND %(maxch)d),
  label TEXT NOT NULL, doc TEXT,
  PRIMARY KEY(storylet, choice));
CREATE TABLE cond(line INTEGER,
  storylet TEXT NOT NULL REFERENCES storylet(id),
  choice INTEGER,
  role TEXT,
  input TEXT NOT NULL REFERENCES input(id),
  other TEXT,
  op TEXT NOT NULL CONSTRAINT %(c_cop)s CHECK(op IN (%(cops)s)),
  value TEXT NOT NULL, doc TEXT,
  FOREIGN KEY(storylet, role) REFERENCES role(storylet, role),
  FOREIGN KEY(storylet, other) REFERENCES role(storylet, role),
  FOREIGN KEY(storylet, choice) REFERENCES choice(storylet, choice));
CREATE TABLE text(line INTEGER,
  storylet TEXT NOT NULL REFERENCES storylet(id),
  choice INTEGER,
  speaker TEXT,
  body TEXT NOT NULL,
  FOREIGN KEY(storylet, speaker) REFERENCES role(storylet, role),
  FOREIGN KEY(storylet, choice) REFERENCES choice(storylet, choice));
CREATE TABLE effect(line INTEGER,
  storylet TEXT NOT NULL REFERENCES storylet(id),
  choice INTEGER,
  op TEXT NOT NULL CONSTRAINT %(c_eop)s CHECK(op IN (%(ops)s)),
  role TEXT, other TEXT, target TEXT, value INTEGER,
  chance INTEGER NOT NULL CONSTRAINT %(c_chance)s CHECK(chance BETWEEN 1 AND 100),
  doc TEXT,
  CONSTRAINT %(c_erole)s CHECK((op IN (%(rops)s)) = (role IS NOT NULL)),
  CONSTRAINT %(c_eother)s CHECK((op = 'rel_add') = (other IS NOT NULL)),
  CONSTRAINT %(c_eself)s CHECK(other IS NULL OR other <> role),
  FOREIGN KEY(storylet, role) REFERENCES role(storylet, role),
  FOREIGN KEY(storylet, other) REFERENCES role(storylet, role),
  FOREIGN KEY(storylet, choice) REFERENCES choice(storylet, choice));
''' % dict(
        phases=phases, ops=ops, rops=rops, cops=cops, maxch=MAX_CHOICES,
        id_ok=ident('id'), cg_ok=ident('cool_group'), eg_ok=ident('excl_group'),
        c_phase=q('phase id must be one of: ' + ' '.join(game.phase_order)),
        c_quiet=q('quiet must be 0..255'),
        c_ident=q('id must be lower-case letters, digits and _, starting with a letter'),
        c_vtype=q('type must be bool or int'),
        c_vorder=q('need min <= default <= max'),
        c_vs16=q('min and max must fit a s16 (-32768..32767)'),
        c_vbool=q('a bool var has min 0 and max 1'),
        c_weight=q('weight must be 0..255 (0 = disabled)'),
        c_prio=q('priority must be 0..9'),
        c_once=q('once must be 0 or 1'),
        c_cool=q('cooldown must be 0..255 days'),
        c_group=q('group names are lower-case letters, digits and _, starting with a letter'),
        c_oncecool=q('once=1 with a cooldown makes no sense (a once storylet never comes back)'),
        c_cgcool=q('cool_group needs cooldown > 0'),
        c_role=q('role names are 1-8 upper-case letters/digits, starting with a letter (A, B, FRIEND)'),
        c_kind=q('kind must be alive, subject or subject_dead'),
        c_choice=q('choice must be 1..%d' % MAX_CHOICES),
        c_cop=q('op must be one of: ' + ' '.join(COND_OPS)),
        c_eop=q('op must be one of: ' + ' '.join(EFFECT_OPS)),
        c_chance=q('chance must be 1..100 (percent)'),
        c_erole=q('ops ' + ' '.join(ROLE_OPS) + ' need a role; the others take no role'),
        c_eother=q('rel_add needs other (the second role); no other op takes one'),
        c_eself=q('rel_add: role and other must be different roles'),
    )


# foreign keys, for readable messages: (columns, parent table, parent columns, message)
FKS = {
    'storylet': [(('phase',), 'phase', ('id',), "phase '{phase}' is not in phase.tsv")],
    'role': [(('storylet',), 'storylet', ('id',), "storylet '{storylet}' is not in storylet.tsv"),
             (('cast',), 'castsel', ('id',), "cast '{cast}' is not a cast id ({casts}), 'named' or 'unnamed'")],
    'choice': [(('storylet',), 'storylet', ('id',), "storylet '{storylet}' is not in storylet.tsv")],
    'cond': [(('storylet',), 'storylet', ('id',), "storylet '{storylet}' is not in storylet.tsv"),
             (('input',), 'input', ('id',), "input '{input}' is not a builtin or a var in var.tsv"),
             (('storylet', 'role'), 'role', ('storylet', 'role'), "role '{role}' is not declared for '{storylet}' in role.tsv"),
             (('storylet', 'other'), 'role', ('storylet', 'role'), "other role '{other}' is not declared for '{storylet}' in role.tsv"),
             (('storylet', 'choice'), 'choice', ('storylet', 'choice'), "choice {choice} of '{storylet}' is not in choice.tsv")],
    'text': [(('storylet',), 'storylet', ('id',), "storylet '{storylet}' is not in storylet.tsv"),
             (('storylet', 'speaker'), 'role', ('storylet', 'role'), "speaker '{speaker}' is not a role of '{storylet}' in role.tsv"),
             (('storylet', 'choice'), 'choice', ('storylet', 'choice'), "choice {choice} of '{storylet}' is not in choice.tsv")],
    'effect': [(('storylet',), 'storylet', ('id',), "storylet '{storylet}' is not in storylet.tsv"),
               (('storylet', 'role'), 'role', ('storylet', 'role'), "role '{role}' is not declared for '{storylet}' in role.tsv"),
               (('storylet', 'other'), 'role', ('storylet', 'role'), "other role '{other}' is not declared for '{storylet}' in role.tsv"),
               (('storylet', 'choice'), 'choice', ('storylet', 'choice'), "choice {choice} of '{storylet}' is not in choice.tsv")],
}
# unique keys: (columns, message)
UNIQUE = {
    'phase': [(('id',), "phase '{id}' is listed twice")],
    'var': [(('id',), "var '{id}' is declared twice")],
    'storylet': [(('id',), "storylet '{id}' is declared twice")],
    'role': [(('storylet', 'role'), "role '{role}' is declared twice for '{storylet}'")],
    'choice': [(('storylet', 'choice'), "choice {choice} of '{storylet}' is declared twice")],
}


def read_tsv(path, table, log):
    """Rows as (line, {column: value}); comments (#) and blank lines are skipped."""
    rel = path
    try:
        with open(path, 'rb') as f:
            raw = f.read()
    except OSError:
        log.err(rel, 'missing (every table needs its file, even with only the header row)')
        return None
    try:
        text = raw.decode('utf-8')
    except UnicodeDecodeError as e:
        log.err(rel, 'not UTF-8 text (byte %d)' % e.start)
        return None
    if text.startswith('\ufeff'):
        text = text[1:]
    spec = COLUMNS[table]
    known = {c[0]: c for c in spec}
    header, rows = None, []
    for n, line in enumerate(text.split('\n'), 1):
        line = line.rstrip('\r')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        cells = line.split('\t')
        where = '%s:%d' % (rel, n)
        if header is None:
            header = [c.strip() for c in cells]
            for c in header:
                if c not in known:
                    log.err(where, "unknown column '%s' (columns: %s)" % (c, ' '.join(known)))
            if len(set(header)) != len(header):
                log.err(where, 'a column is listed twice')
            for c in spec:
                if c[2] and c[0] not in header:
                    log.err(where, "missing column '%s'" % c[0])
            continue
        if len(cells) > len(header):
            if any(c.strip() for c in cells[len(header):]):
                log.err(where, 'more cells (%d) than columns (%d): a stray tab?' % (len(cells), len(header)))
                continue
        row = {}
        ok = True
        for name, kind, _, default in spec:
            v = cells[header.index(name)] if name in header and header.index(name) < len(cells) else ''
            v = v.strip()
            if v == '':
                row[name] = default
                continue
            if kind == INT:
                try:
                    row[name] = int(v, 10)
                except ValueError:
                    log.err(where, "column '%s': '%s' is not a whole number" % (name, v))
                    ok = False
            elif kind == WORD:
                if re.search(r'\s', v):
                    log.err(where, "column '%s': '%s' must be one word" % (name, v))
                    ok = False
                row[name] = v
            else:
                row[name] = v
        if ok:
            rows.append((n, row))
    if header is None:
        log.err(rel, 'empty file: the first line must be the header row (%s)' % ' '.join(c[0] for c in spec))
        return None
    return rows


def explain_failure(db, table, row, err, casts):
    """Turn an sqlite3.IntegrityError into a readable message."""
    msg = str(err)
    if msg.startswith('NOT NULL constraint failed'):
        col = msg.rsplit('.', 1)[-1]
        return "column '%s' must not be empty" % col
    if msg.startswith('CHECK constraint failed'):
        return msg.split(':', 1)[1].strip() if ':' in msg else 'a CHECK constraint failed'
    if msg.startswith('UNIQUE constraint failed'):
        for cols, text in UNIQUE.get(table, []):
            vals = [row.get(c) for c in cols]
            q = 'SELECT line FROM %s WHERE %s' % (table, ' AND '.join('%s IS ?' % c for c in cols))
            hit = db.execute(q, vals).fetchone()
            if hit:
                return text.format(**row) + ' (first at line %d)' % hit[0]
        if table == 'role':
            hit = db.execute("SELECT line FROM role WHERE storylet = ? AND kind <> 'alive'", (row['storylet'],)).fetchone()
            if hit:
                return "'%s' already has a subject role (line %d); a storylet has at most one" % (row['storylet'], hit[0])
        return msg
    if msg.startswith('FOREIGN KEY constraint failed'):
        for cols, parent, pcols, text in FKS.get(table, []):
            vals = [row.get(c) for c in cols]
            if any(v is None for v in vals):
                continue
            q = 'SELECT 1 FROM %s WHERE %s' % (parent, ' AND '.join('%s = ?' % c for c in pcols))
            if not db.execute(q, vals).fetchone():
                return text.format(casts=' '.join(casts), **row)
        return msg
    return msg


# ------------------------------------------------------------------ builtin condition inputs

def builtin_inputs(game):
    """id -> (scope, type, lo, hi, code, arg)"""
    b = {
        'day': ('world', 'int', 0, 255, 'SI_DAY', None),
        'days_left': ('world', 'int', -255, 255, 'SI_DAYS_LEFT', None),
        'morale': ('world', 'int', 0, 10, 'SI_MORALE', None),
        'alive_count': ('world', 'int', 0, game.roster_max, 'SI_ALIVE', None),
        'cast': ('role', 'cast', None, None, 'SI_CAST', None),
        'trait': ('role', 'trait', None, None, 'SI_TRAIT', None),
        'hp': ('role', 'int', 0, 99, 'SI_HP', None),
        'hp_pct': ('role', 'int', 0, 100, 'SI_HP_PCT', None),
        'infected': ('role', 'bool', 0, 1, 'SI_INFECTED', None),
        'amputee': ('role', 'bool', 0, 1, 'SI_AMPUTEE', None),
        'grit': ('role', 'int', 0, 5, 'SI_GRIT', None),
        'aim': ('role', 'int', 0, 5, 'SI_AIM', None),
        'speed': ('role', 'int', 0, 5, 'SI_SPEED', None),
        'nerve': ('role', 'int', 0, 5, 'SI_NERVE', None),
        'rel': ('pair', 'int', -3, 3, 'SI_REL', None),
    }
    for r, enum in game.res.items():
        b[r] = ('world', 'int', 0, 9999, 'SI_RES', enum)
    for s, enum in game.skills.items():
        b[s] = ('role', 'int', 0, 5, 'SI_SKILL', enum)
    return b


# ------------------------------------------------------------------ text: placeholders and box fit

PRONOUNS = {  # field -> forms for he / she / they
    'he': ('he', 'she', 'they'), 'him': ('him', 'her', 'them'), 'his': ('his', 'her', 'their'),
    'self': ('himself', 'herself', 'themself'),
    'He': ('He', 'She', 'They'), 'Him': ('Him', 'Her', 'Them'), 'His': ('His', 'Her', 'Their'),
    'is': ('is', 'is', 'are'), 'was': ('was', 'was', 'were'), 'has': ('has', 'has', 'have'),
    's': ('s', 's', ''), 'es': ('es', 'es', ''),
}
# placeholder codes (story_data.h PH_*): a placeholder is stored as \001, code, argument
PH_CODES = ['name', 'he', 'him', 'his', 'self', 'He', 'Him', 'His', 'is', 'was', 'has', 's', 'es',
            'res', 'day', 'days_left']
PH_ENUM = {f: 'PH_' + ('CAP_' + f.upper() if f[0].isupper() else f.upper()) for f in PH_CODES}
PH_NUM = {f: i + 1 for i, f in enumerate(PH_CODES)}
NUM_WIDTH = {'res': 5, 'day': 3, 'days_left': 4}  # u16, u8, -255..255


class Ctx:
    """What a storylet's text may refer to."""

    def __init__(self, game, roles, world=True, all_roles=None):
        self.game = game
        self.roles = roles  # name -> role dict (idx, kind, cast)
        self.world = world  # {res.x} {day} {days_left} allowed
        self.all_roles = all_roles or roles

    def name_len(self, role):
        c = role['cast']
        return len(self.game.cast_name[c]) if c in self.game.cast_name else self.game.name_max

    def pron_len(self, role, field):
        c = role['cast']
        forms = PRONOUNS[field]
        if c in self.game.cast_he:
            return len(forms[self.game.cast_he[c]])
        return max(len(f) for f in forms)


def parse_text(s, ctx, where, log, what):
    """Check one text cell. Returns (encoded bytes as str, worst-case expansion) or None."""
    for i, ch in enumerate(s):
        if not (32 <= ord(ch) <= 126):
            log.err(where, "%s: character %r at column %d is not plain ASCII (the font has ASCII only;"
                    " use ' \" - ... instead of curly quotes and dashes)" % (what, ch, i + 1))
            return None
    enc, worst = [], []
    i = 0
    ok = True
    while i < len(s):
        ch = s[i]
        if ch == '\\':
            nxt = s[i + 1:i + 2]
            if nxt not in ('n', '\\'):
                log.err(where, "%s: unknown escape '\\%s' (\\n is a line break, \\\\ a backslash)" % (what, nxt))
                return None
            enc.append('\n' if nxt == 'n' else '\\')
            worst.append('\n' if nxt == 'n' else '\\')
            i += 2
            continue
        if ch == '}':
            log.err(where, "%s: '}' without '{'" % what)
            return None
        if ch != '{':
            enc.append(ch)
            worst.append(ch)
            i += 1
            continue
        j = s.find('}', i)
        if j < 0 or '{' in s[i + 1:j]:
            log.err(where, "%s: '{' without '}'" % what)
            return None
        ph = s[i + 1:j]
        i = j + 1
        m = re.fullmatch(r'([A-Za-z][A-Za-z0-9_]*)(?:\.([A-Za-z_]+))?', ph)
        if not m:
            log.err(where, "%s: bad placeholder {%s}" % (what, ph))
            ok = False
            continue
        head, field = m.group(1), m.group(2)
        if not ctx.world and (head in ('res', 'day', 'days_left') or head in ctx.all_roles and head not in ctx.roles):
            log.err(where, "%s: {%s}: the Chronicle line can only name the actor role (%s); no other roles,"
                    " {res.x} or days" % (what, ph, ' '.join(ctx.roles) or 'none: the storylet has no roles'))
            ok = False
            continue
        if field is None and head in ('day', 'days_left'):
            enc.append('\x01' + chr(PH_NUM[head]) + chr(1))
            worst.append('9' * NUM_WIDTH[head])
        elif head in ctx.roles and field is None:
            log.err(where, "%s: {%s}: a role needs a field, like {%s.name} or {%s.he}" % (what, ph, head, head))
            ok = False
        elif head == 'res':
            if field not in ctx.game.res:
                log.err(where, "%s: {%s}: name a resource (%s)" % (what, ph, ' '.join('res.' + r for r in ctx.game.res_order)))
                ok = False
                continue
            enc.append('\x01' + chr(PH_NUM['res']) + chr(1 + ctx.game.res_order.index(field)))
            worst.append('9' * NUM_WIDTH['res'])
        elif head in ctx.roles and field is not None:
            role = ctx.roles[head]
            if field == 'name':
                n = ctx.name_len(role)
            elif field in PRONOUNS:
                n = ctx.pron_len(role, field)
            else:
                log.err(where, "%s: {%s.%s}: unknown field (name %s)" % (what, head, field, ' '.join(PRONOUNS)))
                ok = False
                continue
            enc.append('\x01' + chr(PH_NUM[field]) + chr(1 + role['idx']))
            worst.append('W' * n)
        elif head[0].isupper():
            log.err(where, "%s: {%s}: role '%s' is not declared for this storylet" % (what, ph, head))
            ok = False
        else:
            log.err(where, "%s: unknown placeholder {%s} (roles: {A.name}; world: {res.food} {day} {days_left})" % (what, ph))
            ok = False
    if not ok:
        return None
    plain = re.sub(r'\{[^{}]*\}', ' ', s)
    for cid, name in ctx.game.cast_name.items():
        if re.search(r'\b%s\b' % re.escape(name), plain):
            log.err(where, "%s names '%s' directly; bind a role with cast=%s and write {X.name}" % (what, name, cid))
            ok = False
    return (''.join(enc), ''.join(worst)) if ok else None


def wrap(text, tw, rows):
    """Mirror of dialogue() in src/text.c: (rows of text, None) or (rows, problem)."""
    out = [''] * rows
    cx = cy = 0
    i, n = 0, len(text)
    while i < n and cy < rows:
        j = i
        while j < n and text[j] not in ' \n':
            j += 1
        ln = j - i
        if cx + ln > tw:
            cy += 1
            cx = 0
            if cy >= rows:
                break
        if ln > tw:
            return out, 'the word "%s" is longer than a row (%d)' % (text[i:j], tw)
        out[cy] += text[i:j]
        cx += ln
        i = j
        if i < n and text[i] == '\n':
            cy += 1
            cx = 0
            i += 1
        elif i < n and text[i] == ' ':
            if cx < tw:
                out[cy] += ' '
                cx += 1
            i += 1
    if text[i:].strip():
        return out, 'needs more than %d rows' % rows
    return out, None


def check_fit(worst, game, layouts, prefix_len, where, log):
    """Worst-case text must fit every layout the speaker can get."""
    for key in layouts:
        tw, limits = game.layout[key]
        text = (('W' * prefix_len + ': ') if prefix_len else '') + worst
        out, problem = wrap(text, tw, game.rows)
        if not problem:
            for r, lim in enumerate(limits):
                if len(out[r].rstrip()) > lim:
                    problem = 'row %d is %d wide; the last row holds %d (the blinking arrow)' % (
                        r + 1, len(out[r].rstrip()), lim)
                    break
        if problem:
            shown = ' | '.join(o.rstrip() for o in out if o.strip())
            log.err(where, 'line does not fit the dialogue box (%s, %d wide x %d rows, names at their longest): %s.'
                    ' Worst case: "%s"' % ('with portrait' if key == 'portrait' else 'no portrait', tw, game.rows,
                                          problem, shown))
            return False
    return True


# ------------------------------------------------------------------ the compiler

class Storylet:
    pass


def load(data_dir, game, log):
    db = sqlite3.connect(':memory:')
    db.execute('PRAGMA foreign_keys = ON')
    db.executescript(schema_sql(game))
    for cid, enum in game.casts.items():
        db.execute('INSERT INTO castsel VALUES (?, ?)', (cid, enum))
    db.execute("INSERT INTO castsel VALUES ('named', 'STORY_CAST_NAMED')")
    db.execute("INSERT INTO castsel VALUES ('unnamed', 'STORY_CAST_NONE')")
    builtins = builtin_inputs(game)
    for k, (scope, typ, lo, hi, code, arg) in builtins.items():
        db.execute('INSERT INTO input VALUES (?, ?, ?, ?, ?, ?, ?)', (k, scope, typ, lo, hi, code, arg))

    for table in LOAD_ORDER:
        path = os.path.join(data_dir, table + '.tsv')
        rows = read_tsv(path, table, log)
        if rows is None:
            continue
        cols = ['line'] + [c[0] for c in COLUMNS[table]]
        sql = 'INSERT INTO %s (%s) VALUES (%s)' % (table, ', '.join('"%s"' % c for c in cols), ', '.join('?' * len(cols)))
        for line, row in rows:
            where = '%s:%d' % (path, line)
            if table == 'var' and row.get('id') in builtins:
                log.err(where, "var '%s' has the name of a builtin input; pick another name" % row['id'])
                continue
            try:
                db.execute(sql, [line] + [row[c] for c in cols[1:]])
            except sqlite3.IntegrityError as e:
                log.err(where, explain_failure(db, table, row, e, list(game.casts)))
                continue
            if table == 'var':
                db.execute('INSERT INTO input VALUES (?, ?, ?, ?, ?, ?, ?)',
                           (row['id'], 'world', row['type'], row['min'], row['max'], 'SI_VAR', row['id']))
    if not log.errors:
        bad = db.execute('PRAGMA foreign_key_check').fetchall()
        if bad:
            log.err(data_dir, 'foreign key check failed: %r' % (bad[:5],))
    return db


def compile_story(data_dir, game, log):
    db = load(data_dir, game, log)
    if log.errors:
        return None
    path = lambda t: os.path.join(data_dir, t + '.tsv')
    at = lambda t, line: '%s:%d' % (path(t), line)
    q = lambda sql, *a: db.execute(sql, a).fetchall()
    inputs = {r[0]: r[1:] for r in q('SELECT id, scope, type, lo, hi, code, arg FROM input')}
    vars_ = q('SELECT line, id, type, min, max, "default" FROM var ORDER BY line')
    var_ix = {v[1]: i for i, v in enumerate(vars_)}
    var_info = {v[1]: v for v in vars_}
    if len(vars_) > game.max_vars:
        log.err(path('var'), '%d vars; RunState holds %d (STORY_VARS)' % (len(vars_), game.max_vars))

    quiet = {p: 0 for p in game.phase_order}
    for _, pid, qv in q('SELECT line, id, quiet FROM phase'):
        quiet[pid] = qv

    sts = []
    for line, sid, phase, weight, prio, once, cool, cg, eg, logt in q(
            'SELECT line, id, phase, weight, priority, once, cooldown, cool_group, excl_group, log FROM storylet ORDER BY line'):
        s = Storylet()
        s.line, s.id, s.phase, s.weight, s.prio, s.once, s.cool, s.cg, s.eg = line, sid, phase, weight, prio, once, cool, cg, eg
        s.logt = logt
        s.where = at('storylet', line)
        sts.append(s)
    if len(sts) > MAX_STORYLETS:
        log.err(path('storylet'), '%d storylets; the Chronicle record holds an index up to %d' % (len(sts), MAX_STORYLETS - 1))
    if not sts:
        log.warn(path('storylet'), 'no storylets')

    vars_read, vars_written = set(), set()
    for s in sts:
        compile_storylet(s, q, at, game, inputs, var_ix, var_info, vars_read, vars_written, log)

    # groups: exclusive groups take story_flags bits from the top; cooldown keys take story_cool slots
    excl, cools = {}, {}
    for s in sts:
        if s.eg:
            excl.setdefault(s.eg, []).append(s)
        if s.cool:
            cools.setdefault(s.cg or ('#' + s.id), []).append(s)
    for g, members in excl.items():
        if len(members) < 2:
            log.err(members[0].where, "excl_group '%s' has only this storylet (a typo? use once=1 for a one-off)" % g)
    for g, members in cools.items():
        if not g.startswith('#') and len(members) < 2:
            log.err(members[0].where, "cool_group '%s' has only this storylet (a typo? leave it empty)" % g)
    excl_names = list(excl)
    if len(sts) + len(excl_names) > game.flag_bits:
        log.err(path('storylet'), '%d storylets + %d excl groups need more than %d story_flags bits'
                % (len(sts), len(excl_names), game.flag_bits))
    cool_names = list(cools)
    if len(cool_names) > game.cool_slots:
        log.err(path('storylet'), '%d cooldown slots needed (storylets with a cooldown, cool groups counted once);'
                ' RunState has %d. Share slots with cool_group.' % (len(cool_names), game.cool_slots))
    for s in sts:
        s.excl_bit = game.flag_bits - 1 - excl_names.index(s.eg) if s.eg else None
        s.cool_slot = cool_names.index(s.cg or ('#' + s.id)) if s.cool else None

    game_set = {r[0] for r in q("SELECT id FROM var WHERE doc LIKE '[game]%'")}  # the C code writes it
    for v in vars_:
        where = at('var', v[0])
        if v[1] not in vars_read and v[1] not in vars_written:
            log.warn(where, "var '%s' is never used" % v[1])
        elif v[1] not in vars_written and v[1] not in game_set:
            log.warn(where, "var '%s' is read but no effect ever changes it" % v[1])
        elif v[1] not in vars_read:
            log.warn(where, "var '%s' is set but no condition reads it" % v[1])
    for s in sts:
        if s.weight == 0:
            log.warn(s.where, "'%s' has weight 0 and never fires" % s.id)
    if log.errors:
        return None
    return dict(vars=vars_, sts=sts, quiet=quiet, excl=excl_names, cools=cool_names)


def compile_storylet(s, q, at, game, inputs, var_ix, var_info, vars_read, vars_written, log):
    # roles: the subject first, then the most constrained, so binding prunes early
    roles = q('SELECT line, role, kind, "cast" FROM role WHERE storylet = ? ORDER BY line', s.id)
    nconds = {r[1]: 0 for r in roles}
    for (role,) in q('SELECT role FROM cond WHERE storylet = ? AND choice IS NULL AND role IS NOT NULL', s.id):
        nconds[role] += 1
    if len(roles) > MAX_ROLES:
        log.err(at('role', roles[MAX_ROLES][0]), "'%s' has %d roles; the most is %d" % (s.id, len(roles), MAX_ROLES))
        return
    first = roles[0][1] if roles else None
    order = sorted(roles, key=lambda r: (r[2] == 'alive', r[3] is None or r[3] in ('named', 'unnamed'),
                                         r[3] is None, -nconds[r[1]], r[0]))
    s.roles = {}
    nerr = len(log.errors)
    for i, (line, name, kind, cast) in enumerate(order):
        s.roles[name] = dict(idx=i, kind=kind, cast=cast, line=line)
        where = at('role', line)
        if kind == 'subject_dead' and s.phase != 'death':
            log.err(where, "kind subject_dead only exists in phase death (the dead person); '%s' is %s" % (s.id, s.phase))
        if kind == 'subject' and s.phase != 'bite':
            if s.phase == 'death':
                log.err(where, "in phase death the subject is dead: use kind subject_dead")
            else:
                log.err(where, "kind subject only exists in phase bite (story_fire passes no subject for %s)" % s.phase)
    s.order = [r[1] for r in order]
    subject = [r[1] for r in order if r[2] != 'alive']
    actor = subject[0] if subject else first
    s.actor = s.roles[actor]['idx'] if actor else None
    ctx = Ctx(game, s.roles)

    # the Chronicle line: the journal only knows the actor (the record's person)
    s.log = None
    if s.logt and len(log.errors) == nerr:  # (a misplaced subject role would make it look wrong too)
        lctx = Ctx(game, {actor: s.roles[actor]} if actor else {}, world=False, all_roles=s.roles)
        r = parse_text(s.logt, lctx, s.where, log, 'log')
        if r:
            if '\n' in r[1]:
                log.err(s.where, 'the log line is one row: no \\n')
            elif len(r[1]) > LOG_MAX_W:
                log.err(s.where, 'the log line is %d characters with names at their longest; the journal shows %d: "%s"'
                        % (len(r[1]), LOG_MAX_W, r[1]))
            else:
                s.log = r[0]

    # choices
    s.choices = []
    chs = q('SELECT line, choice, label FROM choice WHERE storylet = ? ORDER BY choice', s.id)
    for k, (line, num, label) in enumerate(chs):
        where = at('choice', line)
        if num != k + 1:
            log.err(where, "choices of '%s' must be numbered 1, 2, 3... without gaps (found %d)" % (s.id, num))
        if '\\n' in label:
            log.err(where, 'a choice label is one row: no \\n')
        r = parse_text(label, ctx, where, log, 'label')
        if r and len(r[1]) > LABEL_MAX:
            log.err(where, 'label is %d characters with names at their longest; the most is %d: "%s"'
                    % (len(r[1]), LABEL_MAX, r[1]))
        s.choices.append(dict(num=num, line=line, label=r[0] if r else '', conds=[], effects=[], lines=[]))
    if len(chs) == 1:
        log.err(at('choice', chs[0][0]), "'%s' has a single choice; give it two or more, or none" % s.id)
    chmap = {c['num']: c for c in s.choices}

    # conditions
    s.conds = []
    for line, choice, role, inp, other, op, value in q(
            'SELECT line, choice, role, input, other, op, value FROM cond WHERE storylet = ? ORDER BY line', s.id):
        where = at('cond', line)
        c = compile_cond(where, s, ctx, inputs, var_ix, role, inp, other, op, value, game, log)
        if c is None:
            continue
        if inputs[inp][4] == 'SI_VAR':
            vars_read.add(inp)
        if choice is None:
            c['level'] = -1 if role is None else max(s.roles[role]['idx'], s.roles[other]['idx'] if other else -1)
            s.conds.append(c)
        else:
            chmap[choice]['conds'].append(c)
    s.conds.sort(key=lambda c: c['level'])  # stable: file order within a level
    if len(s.conds) > 255:
        log.err(s.where, 'too many conditions')
    why = contradiction(s.conds)
    if why:
        log.err(why[0], "the conditions of '%s' can never all be true: %s" % (s.id, why[1]))
    else:
        for ch in s.choices:
            why = contradiction(s.conds + ch['conds'])
            if why:
                log.err(why[0], "choice %d of '%s' can never be available: %s" % (ch['num'], s.id, why[1]))

    # text
    s.lines = []
    for line, choice, speaker, body in q(
            'SELECT line, choice, speaker, body FROM text WHERE storylet = ? ORDER BY line', s.id):
        where = at('text', line)
        r = parse_text(body, ctx, where, log, 'text')
        sp = s.roles.get(speaker) if speaker else None
        if sp and sp['kind'] == 'subject_dead':
            log.err(where, "speaker '%s' is the dead subject; the dead don't speak (narrate instead)" % speaker)
            continue
        if r:
            if sp is None:
                layouts, prefix = ['plain'], 0
            else:
                c = sp['cast']
                if c in game.cast_portrait:
                    layouts = ['portrait'] if game.cast_portrait[c] else ['plain']
                elif c == 'unnamed':
                    layouts = ['plain']
                else:
                    layouts = ['portrait', 'plain']
                prefix = ctx.name_len(sp)
            # the plain layout shows "Name: " before a speaker's line (no portrait to show who talks)
            for lay in layouts:
                check_fit(r[1], game, [lay], prefix if (lay == 'plain' and sp) else 0, where, log)
        entry = dict(text=r[0] if r else '', speaker=sp['idx'] if sp else None, line=line)
        (chmap[choice]['lines'] if choice is not None else s.lines).append(entry)
    if not s.lines:
        log.err(s.where, "'%s' has no intro text (text.tsv rows with an empty choice column)" % s.id)
    for lst, where in [(s.lines, s.where)] + [(c['lines'], at('choice', c['line'])) for c in s.choices]:
        if len(lst) > MAX_LINES:
            log.err(where, '%d lines in one go; the most is %d' % (len(lst), MAX_LINES))

    # effects
    s.effects = []
    for line, choice, op, role, other, target, value, chance in q(
            'SELECT line, choice, op, role, other, target, value, chance FROM effect WHERE storylet = ? ORDER BY line', s.id):
        where = at('effect', line)
        e = compile_effect(where, s, op, role, other, target, value, chance, var_info, var_ix, game, log)
        if e is None:
            continue
        if op in ('var_set', 'var_add'):
            vars_written.add(target)
        (chmap[choice]['effects'] if choice is not None else s.effects).append(e)
    total = len(s.effects) + max([len(c['effects']) for c in s.choices] or [0])
    if total > 255 or any(len(c['conds']) > 255 for c in s.choices):
        log.err(s.where, 'too many conditions or effects')


def contradiction(conds):
    """(where, why) if the conditions can't all hold at once, else None."""
    groups = {}
    for c in conds:
        groups.setdefault(c['key'], []).append(c)
    for cs in groups.values():
        typ, name, where = cs[0]['typ'], cs[0]['name'], cs[-1]['where']
        if typ == 'trait':
            has = {c['v'] for c in cs if c['op'] == 'has'}
            lacks = {c['v'] for c in cs if c['op'] == 'lacks'}
            if has & lacks:
                return where, '%s both has and lacks a trait' % name
            if len(has) > 2:
                return where, '%s needs %d traits; people have 2' % (name, len(has))
        elif typ == 'cast':
            eq = {c['v'] for c in cs if c['op'] == '='}
            ne = {c['v'] for c in cs if c['op'] == '!='}
            if len(eq) > 1 or eq & ne:
                return where, '%s: the cast conditions exclude each other' % name
        else:
            lo, hi, out = cs[0]['lo'], cs[0]['hi'], set()
            for c in cs:
                v, op = c['v'], c['op']
                if op == '=':
                    lo, hi = max(lo, v), min(hi, v)
                elif op == '!=':
                    out.add(v)
                elif op == '<':
                    hi = min(hi, v - 1)
                elif op == '<=':
                    hi = min(hi, v)
                elif op == '>':
                    lo = max(lo, v + 1)
                elif op == '>=':
                    lo = max(lo, v)
            if hi < lo or hi - lo + 1 <= sum(1 for x in out if lo <= x <= hi):
                return where, 'no value of %s passes them all' % name
    return None


def compile_cond(where, s, ctx, inputs, var_ix, role, inp, other, op, value, game, log):
    scope, typ, lo, hi, code, arg = inputs[inp]
    if scope == 'world' and (role or other):
        log.err(where, "input '%s' is about the world: leave role and other empty" % inp)
        return None
    if scope == 'role' and (not role or other):
        log.err(where, "input '%s' is about a person: set role (and leave other empty)" % inp)
        return None
    if scope == 'pair':
        if not role or not other:
            log.err(where, "input '%s' compares two people: set role and other" % inp)
            return None
        if role == other:
            log.err(where, "rel needs two different roles")
            return None
    c = dict(input=code, op=op, role=ctx.roles[role]['idx'] if role else None, arg=None, val=None,
             typ=typ, lo=lo, hi=hi, where=where)
    # contradiction groups: same input on the same person (rel is symmetric)
    if scope == 'pair':
        c['key'] = (inp,) + tuple(sorted((role, other)))
        c['name'] = 'rel(%s, %s)' % (role, other)
    else:
        c['key'] = (inp, role)
        c['name'] = '%s.%s' % (role, inp) if role else inp
    if code == 'SI_RES' or code == 'SI_SKILL':
        c['arg'] = arg
    elif code == 'SI_VAR':
        c['arg'] = 'SV_' + inp.upper()
    elif code == 'SI_REL':
        c['arg'] = ctx.roles[other]['idx']
    if typ == 'trait':
        if op not in ('has', 'lacks'):
            log.err(where, "trait takes op has or lacks, not '%s'" % op)
            return None
        if value not in game.traits or value == 'none':
            log.err(where, "'%s' is not a trait (%s)" % (value, ' '.join(t for t in game.traits if t != 'none')))
            return None
        c['val'] = c['v'] = game.traits[value]
        return c
    if op in ('has', 'lacks'):
        log.err(where, "op '%s' is only for trait" % op)
        return None
    if typ == 'cast':
        if op not in ('=', '!='):
            log.err(where, "cast takes op = or !=, not '%s'" % op)
            return None
        if value == 'unnamed':
            c['val'] = 'STORY_CAST_NONE'
        elif value in game.casts:
            c['val'] = game.casts[value]
        else:
            log.err(where, "'%s' is not a cast id (%s) or unnamed" % (value, ' '.join(game.casts)))
            return None
        c['v'] = value
        filt = ctx.roles[role]['cast']
        if filt not in (None, 'named'):
            log.err(where, "role '%s' already has cast=%s in role.tsv; drop this condition" % (role, filt))
            return None
        if filt == 'named' and op == '=':
            log.err(where, "role '%s' is cast=named; %s" % (
                role, 'an unnamed survivor never matches' if value == 'unnamed' else 'put cast=%s in role.tsv instead' % value))
            return None
        return c
    try:
        v = int(value, 10)
    except ValueError:
        log.err(where, "input '%s' compares numbers; '%s' is not a whole number" % (inp, value))
        return None
    if typ == 'bool' and op not in ('=', '!='):
        log.err(where, "'%s' is a bool (0/1): use = or !=, not '%s'" % (inp, op))
        return None
    if not lo <= v <= hi:
        log.err(where, "value %d is out of range for '%s' (%d..%d)" % (v, inp, lo, hi))
        return None
    # comparisons that can never be true (or are always true) are mistakes too
    if (op == '<' and v == lo) or (op == '>' and v == hi):
        log.err(where, "'%s %s %d' can never be true (range %d..%d)" % (inp, op, v, lo, hi))
        return None
    if (op == '>=' and v == lo) or (op == '<=' and v == hi):
        log.err(where, "'%s %s %d' is always true (range %d..%d); drop it" % (inp, op, v, lo, hi))
        return None
    c['val'] = c['v'] = v
    return c


def compile_effect(where, s, op, role, other, target, value, chance, var_info, var_ix, game, log):
    e = dict(op=op, role=None, arg=None, val=0, chance=chance)
    if role:
        r = s.roles[role]
        if r['kind'] == 'subject_dead':
            log.err(where, "role '%s' is the dead subject: the dead take no effects" % role)
            return None
        e['role'] = r['idx']

    def num(lo, hi, nonzero=True):
        if value is None:
            log.err(where, "%s needs a value" % op)
            return False
        if not lo <= value <= hi or (nonzero and value == 0):
            log.err(where, "%s value %d is out of range (%d..%d%s)" % (op, value, lo, hi, ', not 0' if nonzero else ''))
            return False
        e['val'] = value
        return True

    def no_target():
        if target is not None:
            log.err(where, "%s takes no target" % op)
            return False
        return True

    def no_value():
        if value is not None:
            log.err(where, "%s takes no value" % op)
            return False
        return True

    if op == 'res_add':
        if target not in game.res:
            log.err(where, "res_add target must be a resource (%s), not %r" % (' '.join(game.res_order), target))
            return None
        e['arg'] = game.res[target]
        return e if num(-999, 999) else None
    if op == 'morale_add':
        return e if no_target() and num(-10, 10) else None
    if op in ('var_set', 'var_add'):
        if target not in var_info:
            log.err(where, "%s target must be a var from var.tsv, not %r" % (op, target))
            return None
        _, _, typ, lo, hi, _ = var_info[target]
        e['arg'] = 'SV_' + target.upper()
        if op == 'var_set':
            return e if num(lo, hi, nonzero=False) else None
        if typ == 'bool':
            log.err(where, "var '%s' is a bool: use var_set" % target)
            return None
        span = min(hi - lo, 32767)  # StoryEffect.val is a s16
        return e if num(-span, span) else None
    if op == 'hp_add':
        return e if no_target() and num(-99, 99) else None
    if op in ('infect', 'cure'):
        return e if no_target() and no_value() else None
    if op in ('trait_add', 'trait_remove'):
        if target not in game.traits or target == 'none':
            log.err(where, "%s target must be a trait, not %r" % (op, target))
            return None
        e['arg'] = game.traits[target]
        return e if no_value() else None
    if op == 'rel_add':
        o = s.roles[other]
        if o['kind'] == 'subject_dead':
            log.err(where, "role '%s' is the dead subject: the dead take no effects" % other)
            return None
        e['arg'] = o['idx']
        return e if no_target() and num(-6, 6) else None
    if op == 'skill_xp':
        if target not in game.skills:
            log.err(where, "skill_xp target must be a skill (%s), not %r" % (' '.join(game.skills), target))
            return None
        e['arg'] = game.skills[target]
        return e if num(1, 100) else None
    log.err(where, "unknown op '%s'" % op)
    return None


# ------------------------------------------------------------------ C output

def c_str(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif ch == '?':
            out.append('\\?')  # no trigraphs
        elif ch == '\n':
            out.append('\\n')
        elif 32 <= o <= 126:
            out.append(ch)
        else:
            out.append('\\%03o' % o)
    return '"%s"' % ''.join(out)


def none(v):
    return 'STORY_NONE' if v is None else str(v)


SI_CODES = ['SI_DAY', 'SI_DAYS_LEFT', 'SI_RES', 'SI_MORALE', 'SI_ALIVE', 'SI_VAR', 'SI_CAST', 'SI_TRAIT', 'SI_SKILL',
            'SI_HP', 'SI_HP_PCT', 'SI_INFECTED', 'SI_AMPUTEE', 'SI_GRIT', 'SI_AIM', 'SI_SPEED', 'SI_NERVE', 'SI_REL']
SO_CODES = {'=': 'SO_EQ', '!=': 'SO_NE', '<': 'SO_LT', '<=': 'SO_LE', '>': 'SO_GT', '>=': 'SO_GE',
            'has': 'SO_HAS', 'lacks': 'SO_LACKS'}
SE_CODES = {op: 'SE_' + op.upper() for op in EFFECT_OPS}


def emit(out_dir, game, prog):
    sts, vars_ = prog['sts'], prog['vars']
    sig_src = '\n'.join(['S:' + s.id for s in sts] + ['V:' + v[1] for v in vars_] +
                        ['E:' + e for e in prog['excl']] + ['C:' + c for c in prog['cools']])
    sig = zlib.crc32(sig_src.encode()) & 0xFFFFFFFF
    gen = '// Generated by tools/storyc.py from data/story/*.tsv. Do not edit: change the TSV files.\n'

    h = [gen, '#ifndef STORY_DATA_H', '#define STORY_DATA_H', '#include "gba.h"', '']
    h.append('// story vars (var.tsv): run.vars[SV_*]')
    h.append('enum {')
    for v in vars_:
        h.append('\tSV_%s,' % v[1].upper())
    h.append('\tSV_COUNT')
    h.append('};')
    h.append('')
    h += ['#define STORY_COUNT      %d' % len(sts),
          '#define STORY_ROLES      %d   // most roles in one storylet' % MAX_ROLES,
          '#define STORY_LINES      %d   // most lines in an intro or a choice reply' % MAX_LINES,
          '#define STORY_CHOICES    %d' % MAX_CHOICES,
          '#define STORY_COOL_SLOTS %d   // run.story_cool[] slots in use' % len(prog['cools']),
          '#define STORY_EXCL_COUNT %d   // excl groups: run.story_flags bits from the top down' % len(prog['excl']),
          '#define STORY_SIG        0x%08Xu // changes when storylets/vars/groups are added, removed or reordered' % sig,
          '#define STORY_NONE       0xFF',
          '#define STORY_NO_EXCL    0xFFFF',
          '']
    h += ['// role kinds and cast filters',
          'enum { RK_ALIVE, RK_SUBJECT, RK_SUBJECT_DEAD };',
          '#define STORY_CAST_ANY   0xFD',
          '#define STORY_CAST_NAMED 0xFE',
          '#define STORY_CAST_NONE  0xFF   // an unnamed survivor (Person.cast 0xFF)',
          '',
          '// condition inputs, ops and effect ops',
          'enum { %s, SI_COUNT };' % ', '.join(SI_CODES),
          'enum { SO_EQ, SO_NE, SO_LT, SO_LE, SO_GT, SO_GE, SO_HAS, SO_LACKS };',
          'enum { %s, SE_COUNT };' % ', '.join(SE_CODES[o] for o in EFFECT_OPS),
          '// text placeholders: \\001, PH_*, argument (role index + 1, RES_* + 1, or 1)',
          'enum { PH_NONE, %s, PH_COUNT };' % ', '.join(PH_ENUM[f] for f in PH_CODES),
          '',
          '// role/arg: role index or STORY_NONE; arg: RES_*/SK_*/SV_* or the other role (rel); val: number, TR_*, CAST_*',
          'typedef struct { u8 in, op, role, arg; s16 val; } StoryCond;',
          '// role: role index or STORY_NONE; arg: RES_*/SV_*/TR_*/SK_* or the other role (rel_add); chance: percent',
          'typedef struct { u8 op, role, arg, chance; s16 val; } StoryEffect;',
          '// speaker: role index or STORY_NONE (narration)',
          'typedef struct { const char *text; u8 speaker; } StoryLine;',
          'typedef struct { const char *label; u16 cond0, eff0, line0; u8 ncond, neff, nline; } StoryChoice;',
          'typedef struct {',
          '\tconst char *log;                      // Chronicle line (placeholders: the actor role only) or NULL',
          '\tu8 phase, weight, priority, once;',
          '\tu8 nrole, actor, cool_slot, cool_days; // actor: role named in the Chronicle; cool_slot: run.story_cool[] or STORY_NONE',
          '\tu16 excl_bit;                         // run.story_flags bit of the excl group or STORY_NO_EXCL',
          '\tu16 cond0, eff0, line0, choice0;',
          '\tu8 neff, nline, nchoice;',
          '\tu8 cend[STORY_ROLES + 1];             // conds [0, cend[0]) world; role k: [cend[k], cend[k + 1])',
          '\tu8 role_kind[STORY_ROLES], role_cast[STORY_ROLES];',
          '} Storylet;',
          '',
          'extern const Storylet story_list[];',
          'extern const StoryCond story_cond[];',
          'extern const StoryEffect story_effect[];',
          'extern const StoryLine story_line[];',
          'extern const StoryChoice story_choice[];',
          'extern const s16 story_var_def[], story_var_min[], story_var_max[];',
          'extern const u8 story_phase_quiet[];      // [SP_*] weight of "nothing happens"',
          'extern const u16 story_phase_start[];     // [SP_* .. SP_COUNT]: range in story_phase_list',
          'extern const u8 story_phase_list[];       // storylet indices grouped by phase',
          'extern const char *const story_ids[];     // storylet names (debug, Chronicle)',
          '',
          '#endif', '']

    conds, effects, lines, choices = [], [], [], []

    def add_conds(lst):
        start = len(conds)
        for c in lst:
            conds.append('{ %s, %s, %s, %s, %s }' % (c['input'], SO_CODES[c['op']], none(c['role']),
                                                     none(c['arg']), c['val']))
        return start

    def add_effects(lst):
        start = len(effects)
        for e in lst:
            effects.append('{ %s, %s, %s, %d, %d }' % (SE_CODES[e['op']], none(e['role']), none(e['arg']),
                                                       e['chance'], e['val']))
        return start

    def add_lines(lst):
        start = len(lines)
        for ln in lst:
            lines.append('{ %s, %s }' % (c_str(ln['text']), none(ln['speaker'])))
        return start

    recs = []
    kinds = {'alive': 'RK_ALIVE', 'subject': 'RK_SUBJECT', 'subject_dead': 'RK_SUBJECT_DEAD'}
    for i, s in enumerate(sts):
        cond0 = add_conds(s.conds)
        cend = []
        for k in range(MAX_ROLES + 1):  # end of world conds, then end of each role's conds
            cend.append(sum(1 for c in s.conds if c['level'] < k))
        eff0 = add_effects(s.effects)
        line0 = add_lines(s.lines)
        choice0 = len(choices)
        for ch in s.choices:
            cc0, ce0, cl0 = add_conds(ch['conds']), add_effects(ch['effects']), add_lines(ch['lines'])
            choices.append('{ %s, %d, %d, %d, %d, %d, %d }' % (c_str(ch['label']), cc0, ce0, cl0,
                                                               len(ch['conds']), len(ch['effects']), len(ch['lines'])))
        rk, rc = [], []
        for name in s.order:
            r = s.roles[name]
            rk.append(kinds[r['kind']])
            c = r['cast']
            rc.append('STORY_CAST_ANY' if c is None else 'STORY_CAST_NAMED' if c == 'named'
                      else 'STORY_CAST_NONE' if c == 'unnamed' else game.casts[c])
        while len(rk) < MAX_ROLES:
            rk.append('0')
            rc.append('0')
        recs.append('\t/* %3d %s */ { %s, SP_%s, %d, %d, %d, %d, %s, %s, %d, %s, %d, %d, %d, %d, %d, %d, %d, { %s }, { %s }, { %s } },'
                    % (i, s.id, c_str(s.log) if s.log else 'NULL', s.phase.upper(), s.weight, s.prio, s.once, len(s.order), none(s.actor),
                       none(s.cool_slot), s.cool, 'STORY_NO_EXCL' if s.excl_bit is None else str(s.excl_bit),
                       cond0, eff0, line0, choice0, len(s.effects), len(s.lines), len(s.choices),
                       ', '.join(map(str, cend)), ', '.join(rk), ', '.join(rc)))

    c = [gen, '#include "lb.h"', '#include "story.h"', '#include "story_data.h"', '',
         '_Static_assert(SP_COUNT == %d, "story.h phases changed: rebuild with tools/storyc.py");' % len(game.phase_order),
         '']
    c.append('// log phase weight prio once nrole actor cool_slot cool_days excl cond0 eff0 line0 choice0 neff nline nchoice cend role_kind role_cast')
    c.append('const Storylet story_list[%s] = {' % ('STORY_COUNT' if sts else '1'))
    c += recs or ['\t{ 0 }']
    c.append('};')
    c.append('')

    def table(decl, rows, empty):
        c.append('%s = {' % decl)
        c.extend('\t%s,' % r for r in rows) if rows else c.append('\t%s,' % empty)
        c.append('};')
        c.append('')

    table('const StoryCond story_cond[]', conds, '{ 0 }')
    table('const StoryEffect story_effect[]', effects, '{ 0 }')
    table('const StoryLine story_line[]', lines, '{ "", STORY_NONE }')
    table('const StoryChoice story_choice[]', choices, '{ "" }')
    table('const s16 story_var_def[]', [str(v[5]) for v in vars_], '0')
    table('const s16 story_var_min[]', [str(v[3]) for v in vars_], '0')
    table('const s16 story_var_max[]', [str(v[4]) for v in vars_], '0')
    table('const u8 story_phase_quiet[SP_COUNT]', ['[SP_%s] = %d' % (p.upper(), prog['quiet'][p]) for p in game.phase_order], '0')
    by_phase = {p: [i for i, s in enumerate(sts) if s.phase == p] for p in game.phase_order}
    starts, n = [], 0
    for p in game.phase_order:
        starts.append('[SP_%s] = %d' % (p.upper(), n))
        n += len(by_phase[p])
    starts.append('[SP_COUNT] = %d' % n)
    table('const u16 story_phase_start[SP_COUNT + 1]', starts, '0')
    table('const u8 story_phase_list[]', ['%d /* %s */' % (i, sts[i].id) for p in game.phase_order for i in by_phase[p]], '0')
    table('const char *const story_ids[]', [c_str(s.id) for s in sts], '""')

    os.makedirs(out_dir, exist_ok=True)
    for name, text in (('story_data.h', '\n'.join(h)), ('story_data.c', '\n'.join(c))):
        dst = os.path.join(out_dir, name)
        fd, tmp = tempfile.mkstemp(dir=out_dir, prefix='.' + name)
        with os.fdopen(fd, 'w', encoding='ascii', newline='\n') as f:
            f.write(text)
        os.replace(tmp, dst)


# ------------------------------------------------------------------ driver

def run(data_dir, out_dir):
    """Returns (ok, errors + warnings, program)."""
    log = Log()
    prog = None
    try:
        game = Game(SRC)
        prog = compile_story(data_dir, game, log)
        if prog and out_dir:
            emit(out_dir, game, prog)
    except Fatal as e:
        log.err('storyc', str(e))
        prog = None
    return (not log.errors and prog is not None), log.errors + log.warnings, prog


def overlay(base, src, work):
    """work = base's tables, then the fixture: X.tsv replaces, X.tsv.append adds rows, X.tsv.delete drops."""
    import shutil
    os.makedirs(work)
    for fn in os.listdir(base):
        if fn.endswith('.tsv'):
            shutil.copy(os.path.join(base, fn), work)
    for fn in sorted(os.listdir(src)):
        path = os.path.join(src, fn)
        if fn.endswith('.tsv'):
            shutil.copy(path, work)
        elif fn.endswith('.tsv.append'):
            dst = os.path.join(work, fn[:-len('.append')])
            with open(dst, 'rb') as f:
                old = f.read()
            with open(path, 'rb') as f:
                add = f.read()
            with open(dst, 'wb') as f:
                f.write(old + (b'' if old.endswith(b'\n') else b'\n') + add)
        elif fn.endswith('.tsv.delete'):
            os.remove(os.path.join(work, fn[:-len('.delete')]))


def selftest():
    fails = 0
    for name, path in (('test/story/min', os.path.join(FIXTURES, 'min')),
                       ('test/story/edge', os.path.join(FIXTURES, 'edge')), ('data/story', DATA)):
        ok, msgs, _ = run(path, None)
        if not ok:
            print('FAIL %s must compile:\n  %s' % (name, '\n  '.join(msgs)))
            fails += 1
    base = os.path.join(FIXTURES, 'min')
    fixtures = sorted(d for d in os.listdir(FIXTURES) if d.startswith('bad_') and os.path.isdir(os.path.join(FIXTURES, d)))
    with tempfile.TemporaryDirectory(prefix='storyc-') as tmp:
        for d in fixtures:
            src = os.path.join(FIXTURES, d)
            try:
                with open(os.path.join(src, 'expect'), encoding='utf-8') as f:
                    expect = f.read().strip()
            except OSError:
                print('FAIL %s: missing the expect file (the message substring it must produce)' % d)
                fails += 1
                continue
            work = os.path.join(tmp, d)
            overlay(base, src, work)
            ok, msgs, _ = run(work, None)
            errors = [m for m in msgs if ': warning: ' not in m]
            if ok:
                print('FAIL %s: compiled, but must fail with: %s' % (d, expect))
                fails += 1
            elif not any(expect in m for m in errors):
                print('FAIL %s: expected "%s" in:\n  %s' % (d, expect, '\n  '.join(errors)))
                fails += 1
    if fails:
        print('selftest: %d failure(s)' % fails)
        return 1
    print('selftest: OK (min, edge and data/story compile; %d bad fixtures fail as expected)' % len(fixtures))
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == '--selftest':
        return selftest()
    if len(argv) == 3 and argv[1] == '--check':
        data_dir, out_dir = argv[2], None
    elif len(argv) == 3 and not argv[1].startswith('-'):
        data_dir, out_dir = argv[1], argv[2]
    else:
        sys.stderr.write(__doc__)
        return 2
    ok, msgs, prog = run(data_dir, out_dir)
    for m in msgs:
        sys.stderr.write(m + '\n')
    if not ok:
        n = sum(1 for m in msgs if ': warning: ' not in m)
        sys.stderr.write('storyc: failed, %d problem%s\n' % (n, '' if n == 1 else 's'))
        return 1
    if prog and out_dir:
        print('storyc: %d storylets, %d vars -> %s' % (len(prog['sts']), len(prog['vars']), out_dir))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
