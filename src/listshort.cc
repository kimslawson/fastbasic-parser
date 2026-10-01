/*
 *  fbp - The missing parser for FastBasic
 *  Copyright (C) 2026 Kim Slawson
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program.  If not, see <http://www.gnu.org/licenses/>
 */

// listshort.cc: Writes the short (minimized) listing.
//
// The program is written with all keywords abbreviated, variables renamed,
// no comments, the minimum number of spaces and as many statements per line
// as allowed by the line length.
//
// Each statement is parsed again to verify that it produces exactly the same
// code as the original, if not, the statement is written with more spaces
// and less abbreviations until it does.
//
// The optimizations are implemented as transformations of the tokens of
// each statement, the transformations are only accepted if the resulting
// statement compiles to the same code.

#include "listing.h"
#include "optimize.h"
#include "rename.h"
#include <algorithm>
#include <iostream>

namespace
{

// A statement in the output
struct ostmt
{
    std::vector<token> toks;
    // Range of original statements represented
    size_t first, last;
    // Rendered text
    std::string text;
};

// A rendered token with its possible texts
struct piece
{
    const token *t;
    std::vector<std::string> alts;
    size_t choice = 0;
    const std::string &text() const { return alts[choice]; }
};

token make_tok(tk kind, const std::string &lit, const std::string &table)
{
    token t;
    t.kind = kind;
    t.lit = lit;
    t.table = table;
    return t;
}

token make_num(int value)
{
    token t;
    t.kind = tk::num;
    t.value = value & 0xFFFF;
    t.src = std::to_string(t.value);
    t.table = "T_EXPR";
    return t;
}

bool is_kw(const token &t, const char *lit)
{
    return t.kind == tk::kw && t.lit == lit;
}

bool is_punct(const token &t, const char *lit)
{
    return t.kind == tk::punct && t.lit == lit;
}

bool opens(const token &t)
{
    return (t.kind == tk::punct || t.kind == tk::kw) && !t.lit.empty() &&
           t.lit.back() == '(';
}

bool closes(const token &t)
{
    return is_punct(t, ")");
}

// Returns the index of the parenthesis matching the one at "i", or 0
size_t matching(const std::vector<token> &toks, size_t i)
{
    int depth = 0;
    for(size_t j = i; j < toks.size(); j++)
    {
        if(opens(toks[j]))
            depth++;
        else if(closes(toks[j]))
        {
            depth--;
            if(depth == 0)
                return j;
        }
    }
    return 0;
}

bool same_tokens(const std::vector<token> &a, size_t ab, size_t ae,
                 const std::vector<token> &b, size_t bb, size_t be)
{
    if(ae - ab != be - bb)
        return false;
    for(size_t i = 0; i < ae - ab; i++)
    {
        auto &x = a[ab + i], &y = b[bb + i];
        if(x.kind != y.kind || x.lit != y.lit || x.str != y.str)
            return false;
        if((x.kind == tk::num && x.value != y.value) ||
           (x.kind == tk::fpnum && atari_fp(x.fp).to_asm() != atari_fp(y.fp).to_asm()))
            return false;
    }
    return true;
}

//---------------------------------------------------------------------
// Constant expression evaluator, with FastBasic integer semantics.
class const_eval
{
    const std::vector<token> &t;
    size_t p, e;

    bool at_punct(const char *s) const { return p < e && is_punct(t[p], s); }
    bool at_kw(const char *s) const { return p < e && is_kw(t[p], s); }

    static int16_t div(int16_t a, int16_t b)
    {
        if(b)
            return a / b;
        return a < 0 ? 1 : -1;
    }
    static int16_t mod(int16_t a, int16_t b)
    {
        if(b)
            return a % b;
        return a;
    }

    bool unary(int16_t &v)
    {
        if(at_punct("-") || at_punct("+"))
        {
            bool neg = t[p].lit == "-";
            p++;
            if(!unary(v))
                return false;
            if(neg)
                v = -v;
            return true;
        }
        if(p < e && t[p].kind == tk::num)
        {
            v = t[p++].value;
            return true;
        }
        if(at_punct("("))
        {
            p++;
            if(!expr(v) || !at_punct(")"))
                return false;
            p++;
            return true;
        }
        return false;
    }
    bool bit(int16_t &v)
    {
        if(!unary(v))
            return false;
        while(1)
        {
            int16_t r;
            if(at_punct("&") && t[p].table == "BIT_EXPR_MORE")
            {
                p++;
                if(!unary(r))
                    return false;
                v = v & r;
            }
            else if(at_punct("!"))
            {
                p++;
                if(!unary(r))
                    return false;
                v = v | r;
            }
            else if(at_kw("Exor"))
            {
                p++;
                if(!unary(r))
                    return false;
                v = v ^ r;
            }
            else
                return true;
        }
    }
    bool term(int16_t &v)
    {
        if(!bit(v))
            return false;
        while(1)
        {
            int16_t r;
            if(at_punct("*"))
            {
                p++;
                if(!bit(r))
                    return false;
                v = v * r;
            }
            else if(at_punct("/"))
            {
                p++;
                if(!bit(r))
                    return false;
                v = div(v, r);
            }
            else if(at_kw("Mod"))
            {
                p++;
                if(!bit(r))
                    return false;
                v = mod(v, r);
            }
            else
                return true;
        }
    }
    bool expr(int16_t &v)
    {
        if(!term(v))
            return false;
        while(1)
        {
            int16_t r;
            if(at_punct("+") && t[p].table != "T_EXPR")
            {
                p++;
                if(!term(r))
                    return false;
                v = v + r;
            }
            else if(at_punct("-") && t[p].table != "T_EXPR")
            {
                p++;
                if(!term(r))
                    return false;
                v = v - r;
            }
            else
                return true;
        }
    }

  public:
    const_eval(const std::vector<token> &t, size_t b, size_t e) : t(t), p(b), e(e) {}
    bool eval(int16_t &v) { return expr(v) && p == e; }
};

// Returns true if the token can be part of an integer constant expression
bool const_token(const token &t)
{
    if(t.kind == tk::num)
        return true;
    if(t.kind == tk::kw)
        return (t.lit == "Mod" && t.table == "M_EXPR_MORE") ||
               (t.lit == "Exor" && t.table == "BIT_EXPR_MORE");
    if(t.kind != tk::punct)
        return false;
    // Only integer expression tables
    if(t.table.compare(0, 3, "FP_") == 0)
        return false;
    if(t.lit == "&")
        return t.table == "BIT_EXPR_MORE";
    return t.lit == "+" || t.lit == "-" || t.lit == "*" || t.lit == "/" ||
           t.lit == "!" || t.lit == "(" || t.lit == ")";
}

//---------------------------------------------------------------------
class short_writer
{
    const grammar &g;
    const program &p;
    const list_options &opt;
    opt_settings O;
    renamer ren;
    name_map nm;
    verifier ver;
    std::set<std::string> kwset;
    verify_mode mode;
    std::vector<ostmt> out;

    //-----------------------------------------------------------------
    // Rendering

    std::vector<piece> make_pieces(const std::vector<token> &toks) const
    {
        std::vector<piece> ret;
        for(auto &t : toks)
        {
            piece pc;
            pc.t = &t;
            switch(t.kind)
            {
            case tk::kw:
                if(t.lit == "PRInt")
                    pc.alts = {"?", "PRINT"};
                else if(t.lit == "EXEc")
                    pc.alts = {"@", "EXEC"};
                else
                {
                    auto a = kw_abbrev(t.lit), f = kw_full(t.lit);
                    pc.alts.push_back(a);
                    if(a != f)
                        pc.alts.push_back(f);
                }
                break;
            case tk::punct:
                pc.alts = {t.lit};
                break;
            case tk::var:
            {
                auto i = ren.vars.find(t.lit);
                pc.alts = {i == ren.vars.end() ? t.lit : i->second};
                break;
            }
            case tk::label:
            {
                auto i = ren.labels.find(t.lit);
                pc.alts = {i == ren.labels.end() ? t.lit : i->second};
                break;
            }
            case tk::num:
            case tk::fpnum:
                pc.alts = num_short(t);
                break;
            case tk::str:
            case tk::datafile:
                pc.alts = {str_short(t.str)};
                break;
            case tk::asmsym:
                pc.alts = {ucase(t.src)};
                break;
            case tk::segment:
                pc.alts = {t.lit};
                break;
            case tk::rem:
                continue;
            }
            ret.push_back(pc);
        }
        return ret;
    }

    static bool is_suffix(const token &x, const token &y)
    {
        if(y.kind != tk::punct)
            return false;
        if(x.kind == tk::label)
            return y.lit == "%" || y.lit == "()" || y.lit == "%()";
        if(x.kind != tk::var)
            return false;
        if(y.lit == "$")
            return x.value == VT_STRING || x.value == VT_ARRAY_STRING;
        if(y.lit == "%")
            return x.value == VT_FLOAT || x.value == VT_ARRAY_FLOAT;
        return false;
    }

    // Returns true if a space is needed between the two pieces
    bool need_space(const piece &a, const piece &b) const
    {
        auto &ta = a.text(), &tb = b.text();
        if(ta.empty() || tb.empty())
            return false;
        char la = ta.back(), fb = tb.front();
        auto &x = *a.t, &y = *b.t;
        bool digit = fb >= '0' && fb <= '9';
        switch(x.kind)
        {
        case tk::var:
        case tk::label:
        case tk::asmsym:
        case tk::segment:
            if(ident_char(fb) || fb == '.')
                return true;
            if((fb == '$' || fb == '%') && !is_suffix(x, y))
                return true;
            return false;
        case tk::num:
            return digit || fb == '.';
        case tk::fpnum:
            return digit || fb == '.' || fb == 'E' || fb == 'e';
        case tk::kw:
        {
            if(!ident_char(la) || !ident_char(fb))
                return false;
            // Full keyword, check if it could be read as a longer keyword
            auto ext = ucase(ta) + char(fb >= 'a' && fb <= 'z' ? fb - 32 : fb);
            auto it = kwset.lower_bound(ext);
            return it != kwset.end() && it->compare(0, ext.size(), ext) == 0;
        }
        case tk::str:
        case tk::datafile:
            return fb == '"' || fb == '$';
        case tk::punct:
            // Keep "- 1" as negation of a number in raw mode
            if((x.lit == "-" || x.lit == "+") &&
               (x.table == "T_EXPR" || x.table == "FP_T_EXPR") &&
               mode == verify_mode::raw && (digit || fb == '.'))
                return true;
            return false;
        default:
            return false;
        }
    }

    // Returns true if a space could be needed between the pieces
    static bool risky(const piece &a, const piece &b)
    {
        auto &ta = a.text(), &tb = b.text();
        if(ta.empty() || tb.empty())
            return false;
        char la = ta.back(), fb = tb.front();
        bool l = ident_char(la) || la == '"' || la == '.';
        bool r = ident_char(fb) || fb == '.' || fb == '$' || fb == '%' || fb == '"';
        if(a.t->kind == tk::punct && (la == '-' || la == '+') && (ident_char(fb) || fb == '.'))
            return true;
        return l && r;
    }

    std::string join(const std::vector<piece> &pc, const std::vector<bool> &force) const
    {
        std::string ret;
        for(size_t i = 0; i < pc.size(); i++)
        {
            if(i && (force[i] || need_space(pc[i - 1], pc[i])))
                ret += ' ';
            ret += pc[i].text();
        }
        return ret;
    }

    bool check(size_t first, size_t last, const std::string &txt) const
    {
        return ver.check(first, last, {txt}, mode);
    }

    // Renders the tokens to the shortest text that produces the same code as
    // the original statements [first,last). If "full" is false, only tries
    // adding spaces to fix the output.
    bool render(const std::vector<token> &toks, size_t first, size_t last,
                std::string &result, bool full) const
    {
        auto pc = make_pieces(toks);
        size_t n = pc.size();
        std::vector<bool> force(n, false);

        auto txt = join(pc, force);
        if(check(first, last, txt))
        {
            result = txt;
            return true;
        }
        // Add spaces in all places that could need them
        for(size_t i = 1; i < n; i++)
            force[i] = risky(pc[i - 1], pc[i]);
        txt = join(pc, force);
        if(!check(first, last, txt))
        {
            if(!full)
                return false;
            // Use the longest form of all tokens
            for(auto &x : pc)
                x.choice = x.alts.size() - 1;
            for(size_t i = 1; i < n; i++)
                force[i] = force[i] || risky(pc[i - 1], pc[i]);
            txt = join(pc, force);
            if(!check(first, last, txt))
                return false;
            // Try to shorten each token
            for(auto &x : pc)
            {
                auto old = x.choice;
                for(size_t c = 0; c < old; c++)
                {
                    x.choice = c;
                    if(check(first, last, join(pc, force)))
                        break;
                    x.choice = old;
                }
            }
        }
        // Remove extra spaces
        for(size_t i = 1; i < n; i++)
        {
            if(!force[i])
                continue;
            force[i] = false;
            if(!check(first, last, join(pc, force)))
                force[i] = true;
        }
        result = join(pc, force);
        return true;
    }

    // Tries to replace the tokens of the statement, returns true if valid.
    bool try_toks(ostmt &s, std::vector<token> cand) const
    {
        std::string txt;
        if(!render(cand, s.first, s.last, txt, false))
            return false;
        // Only accept if not longer
        if(txt.size() > s.text.size())
            return false;
        s.toks = std::move(cand);
        s.text = txt;
        return true;
    }

    //-----------------------------------------------------------------
    // Transformations

    // ADR(x) to &x, always done in short listings
    void opt_adr(ostmt &s) const
    {
        for(size_t i = 0; i < s.toks.size(); i++)
        {
            if(!is_kw(s.toks[i], "ADR("))
                continue;
            size_t j = matching(s.toks, i);
            if(!j)
                continue;
            auto c = s.toks;
            c.erase(c.begin() + j);
            c[i] = make_tok(tk::punct, "&", "INT_FUNCTIONS");
            try_toks(s, c);
        }
    }

    void opt_const_fold(ostmt &s) const
    {
        // Only for statements not modified yet, as we use the parse nodes
        if(s.last != s.first + 1)
            return;
        auto &st = p.stmts[s.first];
        if(st.toks.size() != s.toks.size() + (st.has_comment() ? 1 : 0))
            return;
        // Collect constant expressions, larger first
        std::vector<std::pair<size_t, size_t>> spans;
        for(auto &n : st.nodes)
        {
            if(n.tend < n.tbeg + 2 || n.tend > s.toks.size())
                continue;
            bool ok = true;
            for(size_t i = n.tbeg; ok && i < n.tend; i++)
                ok = const_token(s.toks[i]);
            int16_t v;
            if(ok && const_eval(s.toks, n.tbeg, n.tend).eval(v))
                spans.emplace_back(n.tbeg, n.tend);
        }
        std::sort(spans.begin(), spans.end(),
                  [](auto &a, auto &b)
                  { return a.second - a.first > b.second - b.first; });
        spans.erase(std::unique(spans.begin(), spans.end()), spans.end());

        // Apply replacements, tracking the original positions
        std::vector<int> repl(s.toks.size(), 0); // 0: keep, 1: replaced, 2: removed
        std::vector<token> rtok(s.toks.size());
        auto orig = s.toks;
        auto build = [&]()
        {
            std::vector<token> c;
            for(size_t i = 0; i < repl.size(); i++)
                if(repl[i] == 0)
                    c.push_back(orig[i]);
                else if(repl[i] == 1)
                    c.push_back(rtok[i]);
            return c;
        };
        for(auto &sp : spans)
        {
            bool free = true;
            for(size_t i = sp.first; free && i < sp.second; i++)
                free = repl[i] == 0;
            if(!free)
                continue;
            int16_t v;
            const_eval(orig, sp.first, sp.second).eval(v);
            auto save_r = repl;
            repl[sp.first] = 1;
            rtok[sp.first] = make_num(v);
            for(size_t i = sp.first + 1; i < sp.second; i++)
                repl[i] = 2;
            if(!try_toks(s, build()))
                repl = save_r;
        }
    }

    void opt_cmp_zero(ostmt &s) const
    {
        for(size_t i = 0; i + 1 < s.toks.size(); i++)
        {
            if(!is_punct(s.toks[i], "<>") || s.toks[i + 1].kind != tk::num ||
               s.toks[i + 1].value != 0)
                continue;
            auto c = s.toks;
            c.erase(c.begin() + i, c.begin() + i + 2);
            if(try_toks(s, c))
                i--;
        }
    }

    void opt_inc_dec(ostmt &s) const
    {
        auto &t = s.toks;
        if(t.size() < 5 || t[0].kind != tk::var)
            return;
        // Find the end of the left side
        size_t eq = 1;
        if(is_punct(t[1], "("))
        {
            eq = matching(t, 1);
            if(!eq)
                return;
            eq++;
        }
        if(eq >= t.size() || !is_punct(t[eq], "="))
            return;
        size_t n = eq; // Length of left side
        auto r = eq + 1;
        if(t.size() != r + n + 2)
            return;
        // X = X + 1 or X = X - 1
        bool is_inc;
        if(same_tokens(t, 0, n, t, r, r + n) && t[r + n + 1].kind == tk::num &&
           t[r + n + 1].value == 1 && (is_punct(t[r + n], "+") || is_punct(t[r + n], "-")))
            is_inc = t[r + n].lit == "+";
        // X = 1 + X
        else if(t[r].kind == tk::num && t[r].value == 1 && is_punct(t[r + 1], "+") &&
                same_tokens(t, 0, n, t, r + 2, r + 2 + n))
            is_inc = true;
        else
            return;
        std::vector<token> c;
        c.push_back(make_tok(tk::kw, is_inc ? "INC" : "DEc", "STATEMENT"));
        c.insert(c.end(), t.begin(), t.begin() + n);
        try_toks(s, c);
    }

    void opt_defaults(ostmt &s) const
    {
        auto &t = s.toks;
        // STEP 1 at end of FOR
        if(t.size() > 2 && is_kw(t[0], "For") && is_kw(t[t.size() - 2], "Step") &&
           t.back().kind == tk::num && t.back().value == 1)
        {
            auto c = t;
            c.resize(c.size() - 2);
            try_toks(s, c);
        }
        // PAUSE 0
        if(t.size() == 2 && is_kw(t[0], "PAuse") && t[1].kind == tk::num && t[1].value == 0)
        {
            auto c = t;
            c.resize(1);
            try_toks(s, c);
        }
        // WORD type in DIM / DATA
        for(size_t i = 0; i < s.toks.size(); i++)
        {
            if(!is_kw(s.toks[i], "Word"))
                continue;
            auto c = s.toks;
            c.erase(c.begin() + i);
            if(try_toks(s, c))
                i--;
        }
    }

    void opt_next_var(ostmt &s) const
    {
        if(s.toks.size() == 2 && is_kw(s.toks[0], "Next") && s.toks[1].kind == tk::var)
        {
            auto c = s.toks;
            c.resize(1);
            try_toks(s, c);
        }
    }

    void opt_print_sep(ostmt &s) const
    {
        for(size_t i = 0; i + 1 < s.toks.size(); i++)
        {
            if(!is_punct(s.toks[i], ";") || s.toks[i].table != "PRINT_SEP")
                continue;
            auto c = s.toks;
            c.erase(c.begin() + i);
            if(try_toks(s, c))
                i--;
        }
    }

    void opt_parens(ostmt &s) const
    {
        for(size_t i = 0; i < s.toks.size(); i++)
        {
            if(!is_punct(s.toks[i], "("))
                continue;
            size_t j = matching(s.toks, i);
            if(!j)
                continue;
            auto c = s.toks;
            c.erase(c.begin() + j);
            c.erase(c.begin() + i);
            if(try_toks(s, c))
                i--;
        }
    }

    // Converts IF/ENDIF blocks with one statement to IF/THEN
    void opt_if_then()
    {
        bool changed = true;
        while(changed)
        {
            changed = false;
            for(size_t k = 0; k + 2 < out.size(); k++)
            {
                auto &a = out[k], &b = out[k + 1], &c = out[k + 2];
                if(a.toks.empty() || !is_kw(a.toks[0], "If") || c.toks.size() != 1 ||
                   !is_kw(c.toks[0], "Endif") || b.toks.empty())
                    continue;
                // Must be a multi-line IF
                bool has_then = false;
                for(auto &t : a.toks)
                    has_then = has_then || (is_kw(t, "Then") &&
                                            t.table == "THEN_OR_MULTILINE");
                if(has_then)
                    continue;
                // Limit the size of the statement for the native compiler
                code_map code;
                for(size_t i = a.first; i < c.last; i++)
                    for(auto &x : p.stmts[i].code)
                        code[x.first].insert(code[x.first].end(), x.second.begin(),
                                             x.second.end());
                if(code_size(code) > 240)
                    continue;
                ostmt m;
                m.first = a.first;
                m.last = c.last;
                m.toks = a.toks;
                m.toks.push_back(make_tok(tk::kw, "Then", "THEN_OR_MULTILINE"));
                m.toks.insert(m.toks.end(), b.toks.begin(), b.toks.end());
                if(!render(m.toks, m.first, m.last, m.text, false))
                    continue;
                if(m.text.size() >= a.text.size() + b.text.size() + c.text.size() + 2)
                    continue;
                out[k] = m;
                out.erase(out.begin() + k + 1, out.begin() + k + 3);
                changed = true;
            }
        }
    }

    // Removes END at the end of the program
    void opt_end()
    {
        if(out.empty())
            return;
        auto &s = out.back();
        if(s.toks.size() != 1 || !is_kw(s.toks[0], "END") ||
           !p.final_state.proc_stack.empty())
            return;
        // Check that the END is in the main program
        code_map code;
        for(size_t i = s.first; i < s.last; i++)
            for(auto &x : p.stmts[i].code)
                code[x.first].insert(code[x.first].end(), x.second.begin(),
                                     x.second.end());
        if(code.size() != 1 || !code.count(std::string()) ||
           code[std::string()].size() != 1)
            return;
        out.pop_back();
    }

  public:
    std::vector<std::string> notes;

    short_writer(const grammar &g, const program &p, const list_options &opt)
        : g(g), p(p), opt(opt), ren(p), ver(g, p, &nm), kwset(grammar_keywords(g))
    {
        if(opt.opts)
            O = *opt.opts;
        mode = O.any() ? verify_mode::optimized : verify_mode::raw;
        if(opt.full_names)
            ren.assign_same();
        else
        {
            std::set<std::string> reserved;
            for(auto &k : kwset)
            {
                bool alpha = true;
                for(auto c : k)
                    alpha = alpha && ident_char(c);
                if(alpha)
                    reserved.insert(k);
            }
            ren.assign_short(reserved);
        }
        for(auto &v : ren.vars)
            nm.add_var(v.first, v.second);
        for(auto &l : ren.labels)
            nm.add_label(l.first, l.second);
    }

    const renamer &names() const { return ren; }
    const name_map &map() const { return nm; }
    long checks() const { return ver.checks; }

    bool build()
    {
        // Create output statements, joining empty statements with the
        // previous ones.
        for(size_t i = 0; i < p.stmts.size(); i++)
        {
            auto &s = p.stmts[i];
            bool empty = s.empty() || s.is_comment();
            if(empty && !out.empty())
            {
                out.back().last = i + 1;
                continue;
            }
            ostmt o;
            o.first = i;
            o.last = i + 1;
            if(!empty)
            {
                o.toks = s.toks;
                if(!o.toks.empty() && o.toks.back().kind == tk::rem)
                    o.toks.pop_back();
            }
            out.push_back(o);
        }
        // Remove empty statement at start
        if(!out.empty() && out[0].toks.empty())
        {
            if(out.size() > 1)
                out[1].first = out[0].first;
            out.erase(out.begin());
        }

        // Render all statements
        bool ok = true;
        for(auto &s : out)
        {
            if(!render(s.toks, s.first, s.last, s.text, true))
            {
                std::cerr << p.fname << ":" << p.stmts[s.first].line
                          << ": internal error, can't write statement in short form.\n";
                ok = false;
            }
        }
        if(!ok)
            return false;

        // Transformations
        for(auto &s : out)
        {
            if(O[OPT_CONST_FOLD])
                opt_const_fold(s);
            opt_adr(s);
            if(O[OPT_CMP_ZERO])
                opt_cmp_zero(s);
            if(O[OPT_INC_DEC])
                opt_inc_dec(s);
            if(O[OPT_DEFAULTS])
                opt_defaults(s);
            if(O[OPT_NEXT_VAR])
                opt_next_var(s);
            if(O[OPT_PRINT_SEP])
                opt_print_sep(s);
            if(O[OPT_PARENS])
                opt_parens(s);
        }
        if(O[OPT_IF_THEN])
            opt_if_then();
        if(O[OPT_END])
            opt_end();
        return true;
    }

    void write(std::ostream &os, list_stats &stats) const
    {
        const char *eol = opt.ascii_eol ? "\n" : "\x9b";
        std::string line;
        auto flush = [&]()
        {
            if(line.empty())
                return;
            os << line << eol;
            stats.lines++;
            stats.bytes += line.size() + 1;
            stats.max_len = std::max<int>(stats.max_len, line.size());
            line.clear();
        };
        // Source line of the code block started in the current line, the
        // FastBasic compiler sorts code blocks by line, so two blocks can't
        // be in the same line unless they were in the original.
        int block_line = -1;
        for(auto &s : out)
        {
            if(s.text.empty())
                continue;
            if(int(s.text.size()) > opt.max_line)
            {
                stats.long_stmts++;
                if(opt.verbose > 0)
                    std::cerr << p.fname << ":" << p.stmts[s.first].line
                              << ": warning, statement longer than line length ("
                              << s.text.size() << " > " << opt.max_line << ").\n";
            }
            int blk = -1;
            for(size_t i = s.first; i < s.last; i++)
                if(p.stmts[i].starts_block())
                    blk = p.stmts[i].line;
            if(line.empty())
                line = s.text;
            else if(int(line.size() + 1 + s.text.size()) <= opt.max_line &&
                    (blk < 0 || block_line < 0 || blk == block_line))
                line += ":" + s.text;
            else
            {
                flush();
                block_line = -1;
                line = s.text;
            }
            if(blk >= 0)
                block_line = blk;
        }
        flush();
    }
};
} // namespace

bool list_short(std::ostream &out, const grammar &g, const program &p,
                const list_options &opt, list_stats &stats)
{
    short_writer w(g, p, opt);
    if(!w.build())
        return false;
    w.write(out, stats);
    stats.names = w.map();
    if(opt.verbose > 1)
    {
        auto &syms = w.names().symbols;
        bool renamed = false;
        for(auto &s : syms)
            renamed = renamed || s.name != s.new_name;
        if(renamed)
        {
            std::cerr << p.fname << ": renamed symbols (uses):\n";
            for(auto &s : syms)
                if(s.name != s.new_name)
                    std::cerr << "  " << s.spell
                              << (s.is_label ? (s.is_proc ? " (PROC)" : " (DATA)") : "")
                              << " -> " << s.new_name << " (" << s.count << ")\n";
        }
        std::cerr << p.fname << ": " << w.checks() << " statement verifications.\n";
    }
    return true;
}
