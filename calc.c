// Haralds Roskošs 251RDC034
// https://github.com/haralds0511/Graded-lab-Arithmetic-Parser-in-C-Haralds_Rosko-s
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: cloude was used for tehnical difficulties on running this program on my machine, https://www.w3schools.com/c/, https://stackoverflow.com/questions/52444173/use-of-typedef-in-c-programming-language


#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
static int is_sep(char c) { return c == '/' || c == '\\'; }
static char *resolve_path(const char *p) { return _fullpath(NULL, p, 0); }
#else
#define MAKE_DIR(path) mkdir(path, 0777)
static int is_sep(char c) { return c == '/'; }
static char *resolve_path(const char *p) { return realpath(p, NULL); }
#endif


#define STUDENT_NAME     "haralds"
#define STUDENT_LASTNAME "roskoss"
#define STUDENT_ID       "251rdc034"


static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "calc: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

static void *xrealloc(void *old, size_t n) {
    void *p = realloc(old, n ? n : 1);
    if (!p) {
        fprintf(stderr, "calc: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

// Copy exactly n bytes of src into a fresh NUL-terminated string.
static char *copy_n(const char *src, size_t n) {
    char *d = xmalloc(n + 1);
    memcpy(d, src, n);
    d[n] = '\0';
    return d;
}


// line by line because "if any error occurs, the file contains only the
// error": we must be able to throw away everything produced so far.
typedef struct {
    char *data;
    size_t len, cap;
} Buf;


static void buf_append(Buf *b, const char *src, size_t n) {
    if (b->len + n > b->cap) {
        size_t ncap = b->cap ? b->cap : 4096;
        while (ncap < b->len + n) ncap *= 2;
        b->data = xrealloc(b->data, ncap);
        b->cap = ncap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}


// int/float decision

typedef struct {
    int is_float;
    long long i;
    double d;
} Value;

static double as_double(const Value *v) {
    return v->is_float ? v->d : (double)v->i;
}


static long long int_pow(long long base, long long exp) {
    unsigned long long r = 1, b = (unsigned long long)base;
    unsigned long long e = (unsigned long long)exp;
    while (e) {
        if (e & 1) r *= b;
        e >>= 1;
        if (e) b *= b;
    }
    return (long long)r;
}


// Tokenize
typedef enum {
    T_NUM, T_PLUS, T_MINUS, T_STAR, T_SLASH, T_POW,
    T_LPAREN, T_RPAREN,
    T_EOL,  // end of the current line: a '\n' or the end of the file
    T_BAD   // any character that cannot start a token (&, #, ., e, \r ...)
} TokKind;

typedef struct {
    TokKind kind;
    size_t pos;  
    Value val;   // only meaningful for T_NUM
} Token;


typedef struct {
    char *s;       // file contents; s[len] is a '\0' sentinel
    size_t len;
    size_t i;      // next unread character
    int have;      
    Token tok;
    size_t errpos; // 1-based position of the first error, set by fail()
} Parser;

static int is_digit(char c) { return c >= '0' && c <= '9'; }


static void lex(Parser *p) {
    char *s = p->s;
    size_t i = p->i;
    while (s[i] == ' ' || s[i] == '\t') i++;  // blanks never form tokens

    Token *t = &p->tok;
    t->pos = i;
    t->val.is_float = 0;
    t->val.i = 0;
    t->val.d = 0.0;

    if (i >= p->len || s[i] == '\n') {

        t->kind = T_EOL;
    } else if (is_digit(s[i])) {
        size_t start = i;
        unsigned long long n = 0;  // unsigned: wrap-around is defined
        while (is_digit(s[i])) {
            n = n * 10 + (unsigned long long)(s[i] - '0');
            i++;
        }

        if (s[i] == '.' && is_digit(s[i + 1])) {
            i++;
            while (is_digit(s[i])) i++;

            char saved = s[i];
            s[i] = '\0';
            t->val.is_float = 1;
            t->val.d = strtod(s + start, NULL);
            s[i] = saved;
        } else {
            t->val.i = (long long)n;
        }
        t->kind = T_NUM;
    } else {
        switch (s[i]) {
        case '+': t->kind = T_PLUS;   i++; break;
        case '-': t->kind = T_MINUS;  i++; break;
        case '/': t->kind = T_SLASH;  i++; break;
        case '(': t->kind = T_LPAREN; i++; break;
        case ')': t->kind = T_RPAREN; i++; break;
        case '*':
            // Greedy: ** is one token so "2 ** 3" is a power but
            // "2 * * 3" (two separate tokens) is two operators in a row.
            if (s[i + 1] == '*') { t->kind = T_POW;  i += 2; }
            else                 { t->kind = T_STAR; i++;    }
            break;
        default:

            t->kind = T_BAD;
            i++;
            break;
        }
    }
    p->i = i;
    p->have = 1;
}

// One token of lookahead. The token is lexed on first use and then cached
// until next() consumes it.
static const Token *peek(Parser *p) {
    if (!p->have) lex(p);
    return &p->tok;
}

static void next(Parser *p) { p->have = 0; }


// Errors

static int fail(Parser *p, size_t index) {
    p->errpos = index + 1;
    return 0;
}


// Arithmetic

static int apply(Parser *p, TokKind op, Value *a, const Value *b,
                 size_t oppos, size_t rpos) {

    if (!a->is_float && !b->is_float) {
        unsigned long long x = (unsigned long long)a->i;
        unsigned long long y = (unsigned long long)b->i;
        switch (op) {
        case T_PLUS:  a->i = (long long)(x + y); return 1;
        case T_MINUS: a->i = (long long)(x - y); return 1;
        case T_STAR:  a->i = (long long)(x * y); return 1;
        case T_SLASH:
            if (b->i == 0) return fail(p, rpos);
            // LLONG_MIN / -1 overflows (undefined behaviour); negating via
            // unsigned arithmetic is the same result without the UB.
            a->i = (b->i == -1) ? (long long)(0ULL - x) : a->i / b->i;
            return 1;
        default:  // T_POW
            // A negative exponent would give a fraction, which an integer cannot hold.
            if (b->i < 0) return fail(p, rpos);
            a->i = int_pow(a->i, b->i);
            return 1;
        }
    }

    double x = as_double(a), y = as_double(b), r;
    switch (op) {
    case T_PLUS:  r = x + y; break;
    case T_MINUS: r = x - y; break;
    case T_STAR:  r = x * y; break;
    case T_SLASH:

        if (y == 0.0) return fail(p, rpos);
        r = x / y;
        break;
    default:      r = pow(x, y); break;  // T_POW
    }
    // inf or nan (e.g. 10.0 ** 400, or (0 - 8) ** 0.5) is an error at the
    // operator: the operation itself produced the bad value.
    if (!isfinite(r)) return fail(p, oppos);
    a->is_float = 1;
    a->d = r;
    return 1;
}


static int expr(Parser *p, Value *out);

static int primary(Parser *p, Value *out) {
    const Token *t = peek(p);
    if (t->kind == T_NUM) {
        *out = t->val;
        next(p);
        return 1;
    }
    if (t->kind == T_LPAREN) {
        next(p);
        if (!expr(p, out)) return 0;
        t = peek(p);
        // Whatever token stands where ')' should be is the error
        if (t->kind != T_RPAREN) return fail(p, t->pos);
        next(p);
        return 1;
    }

    return fail(p, t->pos);
}

static int power(Parser *p, Value *out) {
    if (!primary(p, out)) return 0;
    const Token *t = peek(p);
    if (t->kind == T_POW) {
        size_t oppos = t->pos;
        next(p);
        size_t rpos = peek(p)->pos;
        Value e = {0, 0, 0.0};

        if (!power(p, &e)) return 0;
        return apply(p, T_POW, out, &e, oppos, rpos);
    }
    return 1;
}

static int term(Parser *p, Value *out) {
    if (!power(p, out)) return 0;
    for (;;) {
        const Token *t = peek(p);
        TokKind op = t->kind;
        if (op != T_STAR && op != T_SLASH) return 1;
        // Copy what we need NOW: next()/peek() reuse the same Token slot.
        size_t oppos = t->pos;
        next(p);
        size_t rpos = peek(p)->pos;
        Value r = {0, 0, 0.0};
        if (!power(p, &r)) return 0;
        // Applying the operator inside the loop, before looking for the next one
        if (!apply(p, op, out, &r, oppos, rpos)) return 0;
    }
}

static int expr(Parser *p, Value *out) {
    if (!term(p, out)) return 0;
    for (;;) {
        const Token *t = peek(p);
        TokKind op = t->kind;
        if (op != T_PLUS && op != T_MINUS) return 1;
        size_t oppos = t->pos;
        next(p);
        size_t rpos = peek(p)->pos;
        Value r = {0, 0, 0.0};
        if (!term(p, &r)) return 0;
        if (!apply(p, op, out, &r, oppos, rpos)) return 0;
    }
}

// Append one result line. Integers use %lld; floats use %.15g, which prints
static void emit_value(Buf *out, const Value *v) {
    char tmp[64];
    int n = v->is_float ? snprintf(tmp, sizeof tmp, "%.15g\n", v->d)
                        : snprintf(tmp, sizeof tmp, "%lld\n", v->i);
    buf_append(out, tmp, (size_t)n);
}


// Whole-file evaluation

static void evaluate(char *s, size_t len, Buf *out) {
    Parser p = {0};
    p.s = s;
    p.len = len;

    size_t i = 0;  // start of the current line
    while (i < len) {
        size_t j = i;
        while (s[j] == ' ' || s[j] == '\t') j++;
        if (j >= len) break;                         // blanks, then EOF
        if (s[j] == '\n') { i = j + 1; continue; }   // blank line: no output
        if (s[j] == '#') {

            const char *nl = memchr(s + j, '\n', len - j);
            i = nl ? (size_t)(nl - s) + 1 : len;
            continue;
        }

        p.i = j;
        p.have = 0;
        Value v = {0, 0, 0.0};
        int ok = expr(&p, &v);
        if (ok) {

            const Token *t = peek(&p);
            if (t->kind != T_EOL) ok = fail(&p, t->pos);
        }
        if (!ok) {
            // First error wins and replaces all earlier results.
            char tmp[48];
            int n = snprintf(tmp, sizeof tmp, "ERROR:%zu\n", p.errpos);
            out->len = 0;
            buf_append(out, tmp, (size_t)n);
            return;
        }
        emit_value(out, &v);
        i = p.tok.pos + 1;  // tok is the T_EOL token: continue after its '\n'
    }
}


// Files and folders

static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "calc: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap = 65536;
    struct stat st;
    // +1 so that after reading st_size bytes there is still room

    if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
        cap = (size_t)st.st_size + 1;

    char *buf = xmalloc(cap + 1);  // +1 for the sentinel
    size_t n = 0;
    for (;;) {
        if (n == cap) {
            cap *= 2;
            buf = xrealloc(buf, cap + 1);
        }
        size_t r = fread(buf + n, 1, cap - n, f);
        if (r == 0) break;
        n += r;
    }
    if (ferror(f)) {  // e.g. the path is a directory
        fprintf(stderr, "calc: cannot read '%s': %s\n", path, strerror(errno));
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *len_out = n;
    return buf;
}


static int ensure_dir(const char *path) {

    size_t n = strlen(path);
    while (n > 1 && is_sep(path[n - 1])) n--;
    char *clean = copy_n(path, n);

    int rc = 0;
    struct stat st;
    if (stat(clean, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "calc: '%s' exists and is not a folder\n", path);
            rc = -1;
        }
    } else if (MAKE_DIR(clean) != 0) {
        fprintf(stderr, "calc: cannot create folder '%s': %s\n", path,
                strerror(errno));
        rc = -1;
    }
    free(clean);
    return rc;
}


static char *path_basename(const char *path, int strip_ext) {
    size_t end = strlen(path);
    while (end > 1 && is_sep(path[end - 1])) end--;
    size_t start = end;
    while (start > 0 && !is_sep(path[start - 1])) start--;
    size_t n = end - start;
    if (strip_ext && n >= 4 && memcmp(path + start + n - 4, ".txt", 4) == 0)
        n -= 4;
    return copy_n(path + start, n);
}

// "<a>/<b>"
static char *join_path(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    char *r = xmalloc(la + lb + 2);
    memcpy(r, a, la);
    r[la] = '/';
    memcpy(r + la + 1, b, lb + 1);
    return r;
}

static int process_file(const char *in_path, const char *base,
                        const char *outdir) {
    size_t len = 0;
    char *text = read_file(in_path, &len);
    if (!text) return -1;

    Buf out = {NULL, 0, 0};
    evaluate(text, len, &out);
    free(text);

    int rc = -1;
// remember this part (return for change)
    if (ensure_dir(outdir) == 0) {
        size_t need = strlen(outdir) + strlen(base) + strlen(STUDENT_NAME) +
                      strlen(STUDENT_LASTNAME) + strlen(STUDENT_ID) + 16;
        char *out_path = xmalloc(need);
        snprintf(out_path, need, "%s/%s_%s_%s_%s.txt", outdir, base,
                 STUDENT_NAME, STUDENT_LASTNAME, STUDENT_ID);

        FILE *f = fopen(out_path, "wb");
        if (!f) {
            fprintf(stderr, "calc: cannot write '%s': %s\n", out_path,
                    strerror(errno));
        } else {
            int bad = out.len > 0 && fwrite(out.data, 1, out.len, f) != out.len;
            // fclose can fail too (the data is flushed there), so check it.
            if (fclose(f) != 0) bad = 1;
            if (bad)
                fprintf(stderr, "calc: error writing '%s'\n", out_path);
            else
                rc = 0;
        }
        free(out_path);
    }
    free(out.data);
    return rc;
}

//default flder
static char *default_outdir(const char *base) {
    size_t need = strlen(base) + strlen(STUDENT_NAME) + strlen(STUDENT_ID) + 8;
    char *r = xmalloc(need);
    snprintf(r, need, "%s_%s_%s", base, STUDENT_NAME, STUDENT_ID);
    return r;
}

static int cmp_names(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// calc [-o OUTDIR] input.txt
static int run_file(const char *input, const char *outdir) {
    char *base = path_basename(input, 1);
    char *def = outdir ? NULL : default_outdir(base);
    int rc = process_file(input, base, outdir ? outdir : def);
    free(def);
    free(base);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

// calc -d DIR [-o OUTDIR]: every regular *.txt directly inside DIR.
static int run_dir(const char *dir, const char *outdir) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "calc: cannot open folder '%s': %s\n", dir,
                strerror(errno));
        return EXIT_FAILURE;
    }

    char **names = NULL;
    size_t count = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n <= 4 || strcmp(e->d_name + n - 4, ".txt") != 0) continue;
        char *full = join_path(dir, e->d_name);
        struct stat st;
        int regular = stat(full, &st) == 0 && S_ISREG(st.st_mode);
        free(full);
        if (!regular) continue;  // subfolders named x.txt, broken links
        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            names = xrealloc(names, cap * sizeof *names);
        }
        names[count++] = copy_n(e->d_name, n);
    }
    closedir(d);
    if (count > 1) qsort(names, count, sizeof *names, cmp_names);


    // say nothing about the folders real name so resolve those.
    char *base = path_basename(dir, 0);
    if (strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        char *real = resolve_path(dir);
        if (real) {
            free(base);
            base = path_basename(real, 0);
            free(real);
        }
    }
    char *def = outdir ? NULL : default_outdir(base);
    const char *out = outdir ? outdir : def;

    int status = EXIT_SUCCESS;
    // Create the output folder even if there is nothing to process.
    if (ensure_dir(out) != 0) status = EXIT_FAILURE;

    for (size_t k = 0; k < count; k++) {
        if (status == EXIT_SUCCESS) {
            char *full = join_path(dir, names[k]);
            char *fbase = path_basename(names[k], 1);
            if (process_file(full, fbase, out) != 0) status = EXIT_FAILURE;
            free(fbase);
            free(full);
        }
        free(names[k]);
    }
    free(names);
    free(def);
    free(base);
    return status;
}

static int usage_error(const char *what, const char *arg) {
    fprintf(stderr, "calc: %s%s%s\n", what, arg ? " " : "", arg ? arg : "");
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] "
                    "[-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
    return 2;
}

int main(int argc, char **argv) {
    const char *dir = NULL, *outdir = NULL, *input = NULL;

    for (int a = 1; a < argc; a++) {
        const char *arg = argv[a];
        if (strcmp(arg, "-d") == 0 || strcmp(arg, "--dir") == 0) {
            if (a + 1 >= argc) return usage_error("missing value for", arg);
            dir = argv[++a];
        } else if (strncmp(arg, "--dir=", 6) == 0) {
            dir = arg + 6;
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output-dir") == 0) {
            if (a + 1 >= argc) return usage_error("missing value for", arg);
            outdir = argv[++a];
        } else if (strncmp(arg, "--output-dir=", 13) == 0) {
            outdir = arg + 13;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            return usage_error("unknown option", arg);
        } else if (input) {
            return usage_error("more than one input file given:", arg);
        } else {
            input = arg;
        }
    }

    if (dir && input)
        return usage_error("use either -d DIR or an input file, not both", NULL);
    if (!dir && !input)
        return usage_error("no input file or folder given", NULL);

    return dir ? run_dir(dir, outdir) : run_file(input, outdir);
}



// check error messeges and test if correct output