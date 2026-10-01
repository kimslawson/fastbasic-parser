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

// listlong.cc: Writes the long (readable) listing.
//
// Each statement is written in its own line, with indentation, all keywords
// expanded and spaces between operators. Function arguments are always
// enclosed in parenthesis, and the variable is added to NEXT statements.
// Each written statement is parsed again to verify that it produces the
// same code.

#include "listing.h"
#include "optimize.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace
{
enum class cls
{
    word,
    open,
    close,
    comma,
    semi,
    binop,
    prefix,
    suffix,
    comment
};

struct wtok
{
    token t;
    bool func = false;
    cls c = cls::word;
};

class long_writer
{
    const program &p;
    const list_options &opt;
    verifier ver;
    // Variable and label spelling, from first appearance
    std::map<std::string, std::string> var_name, label_name;
    // Statements modified by the optimizations
    std::map<size_t, std::string> optimized;
    // Changes planned by ELIF conversion and fixed variables:
    std::map<size_t, statement> repl;          // replaced statements
    std::map<size_t, std::string> fixed_text;  // already verified texts
    std::set<size_t> removed;                  // removed statements
    std::map<size_t, int> indent_adj;          // indentation changes
    bool do_elif, do_fixed;
    verify_mode mode;

  public:
    // Variables replaced by fixed_vars
    const_vars fixed;

    long_writer(const grammar &g, const program &p, const list_options &opt,
                bool fixed_vars)
        : p(p), opt(opt), ver(g, p, nullptr),
          optimized(optimize_for_long(g, p, opt))
    {
        do_elif = !opt.opts || !opt.opts->given[OPT_ELIF] || (*opt.opts)[OPT_ELIF];
        do_fixed = fixed_vars;
        for(auto &s : p.stmts)
            for(auto &t : s.toks)
            {
                if(t.kind == tk::var && !var_name.count(t.lit))
                    var_name[t.lit] = t.src.empty() ? t.lit : t.src;
                if(t.kind == tk::label && !label_name.count(t.lit))
                    label_name[t.lit] = t.src.empty() ? t.lit : t.src;
            }
        mode = (opt.opts && opt.opts->any()) ? verify_mode::optimized : verify_mode::raw;
        apply_optimized();
        if(do_elif)
            plan_elif();
        if(do_fixed)
            plan_fixed_vars();
    }

    std::string kw(const std::string &s) const { return opt.upper ? ucase(s) : lcase(s); }

    std::string text(const wtok &w) const
    {
        auto &t = w.t;
        switch(t.kind)
        {
        case tk::kw:
            return kw(t.lit);
        case tk::punct:
            if(t.lit == "?")
                return kw("PRINT");
            if(t.lit == "@")
                return kw("EXEC");
            return t.lit;
        case tk::var:
            return var_name.count(t.lit) ? var_name.at(t.lit) : t.lit;
        case tk::label:
            return label_name.count(t.lit) ? label_name.at(t.lit) : t.lit;
        case tk::num:
        case tk::fpnum:
            return num_long(t);
        case tk::str:
        case tk::datafile:
            return str_long(t.str);
        case tk::rem:
            return "'" + t.lit;
        case tk::asmsym:
        case tk::segment:
            return t.src.empty() ? t.lit : t.src;
        }
        return t.lit;
    }

    static cls classify(const std::vector<wtok> &w, size_t i)
    {
        auto &t = w[i].t;
        const token *prev = i ? &w[i - 1].t : nullptr;
        bool after_name = prev && (prev->kind == tk::var || prev->kind == tk::label);
        switch(t.kind)
        {
        case tk::kw:
            if(t.table == "AND_EXPR_RIGHT" || t.table == "OR_EXPR_RIGHT" ||
               t.table == "M_EXPR_MORE" || t.table == "BIT_EXPR_MORE")
                return cls::binop;
            if(t.lit.back() == '(')
                return cls::open;
            return cls::word;
        case tk::punct:
            if(t.lit == "(" || t.lit == "[")
                return cls::open;
            if(t.lit == ")" || t.lit == "]")
                return cls::close;
            if(t.lit == ",")
                return cls::comma;
            if(t.lit == ";")
                return cls::semi;
            if(t.lit == "#")
                return cls::prefix;
            if(t.lit == "()" || t.lit == "%()")
                return cls::suffix;
            if(t.lit == "$" || t.lit == "%")
                return after_name ? cls::suffix : cls::prefix;
            if(t.lit == "+" || t.lit == "-")
                return (t.table == "T_EXPR" || t.table == "FP_T_EXPR") ? cls::prefix
                                                                         : cls::binop;
            if(t.lit == "&")
                return t.table == "INT_FUNCTIONS" ? cls::prefix : cls::binop;
            if(t.lit == "?" || t.lit == "@")
                return cls::word;
            return cls::binop;
        case tk::rem:
            return cls::comment;
        default:
            return cls::word;
        }
    }

    std::string render(const std::vector<wtok> &w) const
    {
        std::string out;
        for(size_t i = 0; i < w.size(); i++)
        {
            auto &c = w[i];
            auto txt = text(c);
            if(i > 0)
            {
                auto &p = w[i - 1];
                bool space = true;
                if(c.c == cls::close || c.c == cls::comma || c.c == cls::semi ||
                   c.c == cls::suffix)
                    space = false;
                else if(p.c == cls::open || p.c == cls::prefix)
                    space = false;
                else if(c.c == cls::open && c.t.lit == "(")
                {
                    if(p.t.kind == tk::var || p.t.kind == tk::label ||
                       p.c == cls::suffix || p.func)
                        space = false;
                }
                else if(c.c == cls::open && c.t.lit == "[")
                    space = c.t.table == "DATA_EXT_TYPE";
                else if(c.t.kind == tk::punct && c.t.lit == "+" &&
                        c.t.table == "LINE_ASSIGNMENT" && p.t.lit == "=")
                    space = false; // String concatenation "=+"
                if(space)
                    out += ' ';
            }
            out += txt;
        }
        return out;
    }

    // Converts the statement tokens to the readable form
    std::vector<wtok> expand(const statement &s, const std::string &next_var,
                             bool add_parens) const
    {
        auto &toks = s.toks;
        std::map<size_t, std::vector<token>> before, after;
        std::vector<wtok> w(toks.size());
        for(size_t i = 0; i < toks.size(); i++)
            w[i].t = toks[i];

        auto mk = [](const char *lit, const char *table)
        {
            token t;
            t.kind = tk::punct;
            t.lit = lit;
            t.table = table;
            return t;
        };

        for(size_t i = 0; i < toks.size(); i++)
        {
            auto &t = toks[i];
            if(add_parens && t.kind == tk::punct && t.lit == "&" &&
               t.table == "INT_FUNCTIONS")
            {
                int n = outer_node_at(s.nodes, i + 1);
                if(n >= 0 && s.nodes[n].table == "ADR_EXPR")
                {
                    w[i].t.kind = tk::kw;
                    w[i].t.lit = "ADR(";
                    after[s.nodes[n].tend - 1].push_back(mk(")", "INT_FUNCTIONS"));
                }
            }
            else if(t.kind == tk::kw)
            {
                int n = function_arg(s.nodes, i);
                if(n >= 0)
                {
                    w[i].func = true;
                    auto &nd = s.nodes[n];
                    if(add_parens && !is_wrapped(toks, nd.tbeg, nd.tend))
                    {
                        before[nd.tbeg].push_back(mk("(", "PAR_EXPR"));
                        after[nd.tend - 1].push_back(mk(")", "PAR_EXPR"));
                    }
                }
            }
        }
        // Add variable to NEXT
        if(!next_var.empty() && toks.size() >= 1 && toks[0].kind == tk::kw &&
           toks[0].lit == "Next" && (toks.size() == 1 || toks[1].kind != tk::var))
        {
            token t;
            t.kind = tk::var;
            t.lit = next_var;
            t.table = "NEXT_VARNAME";
            after[0].push_back(t);
        }

        std::vector<wtok> ret;
        for(size_t i = 0; i < w.size(); i++)
        {
            for(auto &t : before[i])
                ret.push_back(wtok{t});
            ret.push_back(w[i]);
            for(auto &t : after[i])
                ret.push_back(wtok{t});
        }
        for(size_t i = 0; i < ret.size(); i++)
            ret[i].c = classify(ret, i);
        return ret;
    }

    static std::string trim(const std::string &s)
    {
        size_t b = 0, e = s.size();
        while(b < e && (s[b] == ' ' || s[b] == '\t'))
            b++;
        while(e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' ||
                        s[e - 1] == '\r' || s[e - 1] == '\x9b'))
            e--;
        return s.substr(b, e - b);
    }

    //-----------------------------------------------------------------
    // Planned changes

    const statement &eff(size_t n) const
    {
        auto i = repl.find(n);
        return i == repl.end() ? p.stmts[n] : i->second;
    }

    // Current text of statement "n", used for verification of changes
    std::string current_text(size_t n) const
    {
        auto i = fixed_text.find(n);
        return i == fixed_text.end() ? trim(p.stmts[n].text) : i->second;
    }

    // Next statement not removed
    size_t next_stmt(size_t n) const
    {
        for(n++; n < p.stmts.size() && removed.count(n); n++)
            ;
        return n;
    }

    static bool is_kw(const token &t, const char *lit)
    {
        return t.kind == tk::kw && t.lit == lit;
    }

    static size_t then_pos(const std::vector<token> &t)
    {
        for(size_t i = 0; i < t.size(); i++)
            if(is_kw(t[i], "Then") && t[i].table == "THEN_OR_MULTILINE")
                return i;
        return 0;
    }

    bool is_endif(size_t n) const
    {
        auto &t = eff(n).toks;
        return !t.empty() && is_kw(t[0], "Endif");
    }

    // Statement with a part of the tokens of another
    static statement sub_statement(const statement &s, size_t b, size_t e)
    {
        statement r = s;
        r.toks.assign(s.toks.begin() + b, s.toks.begin() + e);
        r.nodes.clear();
        for(auto n : s.nodes)
            if(n.tbeg >= b && n.tend <= e)
            {
                n.tbeg -= b;
                n.tend -= b;
                r.nodes.push_back(n);
            }
        return r;
    }

    // Uses the statements modified by the optimizations
    void apply_optimized()
    {
        for(auto &o : optimized)
        {
            size_t n = o.first;
            auto &s = p.stmts[n];
            statement m = s;
            engine_state end;
            code_map code;
            if(!ver.parse(s.before, o.second, end, code, &m.toks, &m.nodes))
                continue;
            if(s.has_comment())
                m.toks.push_back(s.toks.back());
            auto txt = render(expand(m, std::string(), true));
            if(ver.check(n, n + 1, {txt}, verify_mode::optimized))
            {
                repl[n] = m;
                fixed_text[n] = txt;
            }
        }
    }

    // Converts ELSE followed by IF to ELIF, innermost first so that the
    // verification of each change starts from the original state.
    void plan_elif()
    {
        auto &st = p.stmts;
        for(size_t n = st.size(); n-- > 0;)
        {
            if(removed.count(n))
                continue;
            auto e = eff(n);
            if(e.toks.size() != 1 || !is_kw(e.toks[0], "ELse"))
                continue;
            size_t ni = next_stmt(n);
            if(ni != n + 1 || ni >= st.size())
                continue;
            auto iff = eff(ni);
            if(iff.toks.empty() || !is_kw(iff.toks[0], "If"))
                continue;
            size_t tp = then_pos(iff.toks);
            // Find the end of the IF block, followed by the outer ENDIF
            size_t m = ni;
            if(!tp)
            {
                int depth = 0;
                for(m = ni; m < st.size(); m = next_stmt(m))
                {
                    auto &t = eff(m).toks;
                    if(!t.empty() && is_kw(t[0], "If") && !then_pos(t))
                        depth++;
                    else if(is_endif(m) && --depth == 0)
                        break;
                }
                if(m >= st.size())
                    continue;
            }
            size_t j = next_stmt(m);
            if(j != m + 1 || j >= st.size() || !is_endif(j))
                continue;

            // Plan the change
            auto save_repl = repl;
            auto save_removed = removed;
            auto save_adj = indent_adj;
            auto save_fixed = fixed_text;

            size_t len = iff.toks.size();
            if(tp)
            {
                // ELSE / IF c THEN s / ENDIF  ->  ELIF c / s / ENDIF
                statement el = sub_statement(iff, 0, tp);
                el.toks[0] = e.toks[0];
                el.toks[0].lit = "ELIf";
                el.indent = e.indent;
                statement y = sub_statement(iff, tp + 1, len);
                y.indent = e.indent + 1;
                repl[n] = el;
                repl[ni] = y;
                fixed_text[n] = render(expand(el, std::string(), true));
                fixed_text[ni] = render(expand(y, std::string(), true));
            }
            else
            {
                // ELSE / IF c / ... / ENDIF / ENDIF  ->  ELIF c / ... / ENDIF
                // Keep the ENDIF with a comment
                bool cm = eff(m).has_comment(), cj = eff(j).has_comment();
                if(cm && cj)
                    continue;
                statement el = iff;
                el.toks[0].lit = "ELIf";
                el.indent = e.indent;
                repl[n] = el;
                fixed_text[n] = render(expand(el, std::string(), true));
                removed.insert(ni);
                for(size_t i = ni + 1; i < m; i++)
                    indent_adj[i]--;
                if(cm)
                {
                    removed.insert(j);
                    indent_adj[m]--;
                }
                else
                    removed.insert(m);
            }
            // Verify all the statements in the range
            std::vector<std::string> texts;
            for(size_t i = n; i <= j; i++)
                if(!removed.count(i))
                    texts.push_back(current_text(i));
            if(!ver.check(n, j + 1, texts, mode))
            {
                repl = save_repl;
                removed = save_removed;
                indent_adj = save_adj;
                fixed_text = save_fixed;
            }
        }
    }

    // Replaces variables assigned only once with a constant
    void plan_fixed_vars()
    {
        auto &st = p.stmts;
        for(auto &s : st)
            for(auto &t : s.toks)
                if(is_kw(t, "CLR"))
                    return;
        for(auto &v : p.final_state.vars)
        {
            auto name = v.first;
            int type = v.second & 0xFF;
            if(type != VT_WORD && type != VT_STRING)
                continue;
            const char *rtable = type == VT_WORD ? "INT_FUNCTIONS" : "STRING_FUNCTIONS";
            // Find all uses
            size_t assign = st.size(), reads = 0;
            bool ok = true;
            for(size_t n = 0; n < st.size() && ok; n++)
                for(auto &t : eff(n).toks)
                {
                    if(t.kind != tk::var || t.lit != name)
                        continue;
                    if(t.table == rtable)
                        reads++;
                    else if(assign == st.size())
                        assign = n;
                    else
                        ok = false;
                }
            if(!ok || !reads || assign >= st.size() || removed.count(assign))
                continue;
            // Check the assignment: X = number, or X$ = "string"
            auto &a = eff(assign);
            auto t = a.toks;
            if(a.has_comment())
                t.pop_back();
            size_t vp = type == VT_WORD ? 2 : 3;
            if(t.size() != vp + 1 || t[0].kind != tk::var || t[vp - 1].lit != "=" ||
               (type == VT_WORD ? t[vp].kind != tk::num : t[vp].kind != tk::str))
                continue;
            if(type == VT_STRING && t[1].lit != "$")
                continue;
            // Must be executed before any use: in the main program, outside
            // any loop or condition, and with no PROC called before.
            if(!a.before.jumps.empty() || !a.before.proc_stack.empty())
                continue;
            bool exec = false;
            for(size_t n = 0; n < assign; n++)
                for(auto &x : eff(n).toks)
                    exec = exec || (x.kind == tk::kw && x.lit == "EXEc") ||
                           (x.kind == tk::punct && x.lit == "@");
            if(exec)
                continue;

            token val = t[vp];
            val.table = type == VT_WORD ? "T_EXPR" : "STRING_FUNCTIONS";
            const_vars cv;
            cv[name] = type == VT_WORD ? "N" + std::to_string(val.value & 0xFFFF)
                                       : "S" + val.str;
            // Replace in all statements, all must be verified
            std::map<size_t, std::string> texts;
            std::map<size_t, statement> stmts;
            for(size_t n = 0; n < st.size() && ok; n++)
            {
                if(n == assign || removed.count(n))
                    continue;
                auto s = eff(n);
                bool changed = false;
                std::vector<token> nt;
                for(size_t i = 0; i < s.toks.size(); i++)
                {
                    auto &x = s.toks[i];
                    if(x.kind == tk::var && x.lit == name && x.table == rtable)
                    {
                        // After a sign, use parenthesis so the number is not
                        // read as a negative number.
                        bool sign = i > 0 && s.toks[i - 1].kind == tk::punct &&
                                    (s.toks[i - 1].lit == "-" || s.toks[i - 1].lit == "+") &&
                                    (s.toks[i - 1].table == "T_EXPR" ||
                                     s.toks[i - 1].table == "FP_T_EXPR");
                        token po, pc;
                        po.kind = pc.kind = tk::punct;
                        po.lit = "(";
                        pc.lit = ")";
                        po.table = pc.table = "PAR_EXPR";
                        if(sign)
                            nt.push_back(po);
                        nt.push_back(val);
                        if(sign)
                            nt.push_back(pc);
                        changed = true;
                        if(type == VT_STRING && i + 1 < s.toks.size() &&
                           s.toks[i + 1].lit == "$")
                            i++;
                    }
                    else
                        nt.push_back(x);
                }
                if(!changed)
                    continue;
                // Render, parse again to get the nodes, and render again
                statement m = s;
                m.toks = nt;
                m.nodes.clear();
                if(m.has_comment())
                    m.toks.pop_back();
                auto plain = render(expand(m, std::string(), false));
                engine_state end;
                code_map code;
                if(!ver.parse(st[n].before, plain, end, code, &m.toks, &m.nodes))
                {
                    ok = false;
                    break;
                }
                if(s.has_comment())
                    m.toks.push_back(s.toks.back());
                auto txt = render(expand(m, std::string(), true));
                if(!ver.check_subst(n, txt, current_text(n), cv))
                {
                    txt = plain + (s.has_comment() ? " '" + s.toks.back().lit : "");
                    if(!ver.check_subst(n, txt, current_text(n), cv))
                        ok = false;
                }
                texts[n] = txt;
                stmts[n] = m;
            }
            if(!ok)
                continue;
            for(auto &x : texts)
                fixed_text[x.first] = x.second;
            for(auto &x : stmts)
                repl[x.first] = x.second;
            // Replace the assignment with a comment
            statement c = a;
            token r;
            r.kind = tk::rem;
            r.value = '\'';
            r.lit = " fbp: fixed " + (var_name.count(name) ? var_name.at(name) : name) +
                    (type == VT_WORD ? " = " + num_long(val) : "$ = " + str_long(val.str));
            if(a.has_comment())
                r.lit += " -" + a.toks.back().lit;
            c.toks = {r};
            c.nodes.clear();
            repl[assign] = c;
            fixed_text[assign] = "'" + r.lit;
            fixed[name] = cv[name];
            if(opt.verbose > 1)
                std::cerr << p.fname << ":" << a.line << ": replaced variable "
                          << (var_name.count(name) ? var_name.at(name) : name)
                          << (type == VT_WORD ? "" : "$") << " with its value.\n";
        }
    }

    // Returns the text for statement "n"
    std::string statement_text(size_t n, const std::string &next_var, int &failed) const
    {
        auto &s = p.stmts[n];
        auto txt = render(expand(s, next_var, true));
        if(ver.check(n, n + 1, {txt}, verify_mode::raw))
            return txt;
        txt = render(expand(s, std::string(), false));
        if(ver.check(n, n + 1, {txt}, verify_mode::raw))
            return txt;
        failed++;
        if(opt.verbose > 1)
            std::cerr << p.fname << ":" << s.line
                      << ": note, statement written as in the source.\n";
        return trim(s.text);
    }

    bool write(std::ostream &out, list_stats &stats) const
    {
        // Stack of FOR variables, to add to NEXT
        std::vector<std::string> for_vars;
        std::vector<std::string> lines;
        int failed = 0, block_line = -1;
        bool last_comment = false, need_blank = false;
        auto last_blank = [&]() { return lines.empty() || lines.back().empty(); };
        auto &st = p.stmts;
        for(size_t n = 0; n < st.size(); n++)
        {
            if(removed.count(n))
                continue;
            auto &s = eff(n);
            bool alone =
                s.line_start && (n + 1 == st.size() || st[n + 1].line_start);
            if(s.empty())
            {
                if(alone && !last_blank())
                {
                    lines.emplace_back();
                    need_blank = false;
                }
                continue;
            }
            // Track FOR / NEXT
            std::string next_var;
            if(s.toks[0].kind == tk::kw && s.toks[0].lit == "For")
            {
                if(s.toks.size() > 2 && s.toks[1].kind == tk::var &&
                   s.toks[2].kind == tk::punct && s.toks[2].lit == "=")
                    for_vars.push_back(s.toks[1].lit);
                else
                    for_vars.push_back(std::string());
            }
            else if(s.toks[0].kind == tk::kw && s.toks[0].lit == "Next" &&
                    !for_vars.empty())
            {
                next_var = for_vars.back();
                for_vars.pop_back();
            }
            auto ft = fixed_text.find(n);
            auto txt =
                ft != fixed_text.end() ? ft->second : statement_text(n, next_var, failed);
            // Code blocks (PROC / DATA) are sorted by line in the compiled
            // program, keep them in the same line if they were in the source
            if(s.starts_block())
            {
                if(block_line == s.line && !lines.empty())
                {
                    lines.back() += " : " + txt;
                    continue;
                }
                block_line = s.line;
            }
            // Blank lines around PROC
            bool is_proc = s.toks[0].kind == tk::kw && s.toks[0].lit == "PRoc";
            if((need_blank || (is_proc && !last_comment)) && !last_blank() &&
               !s.is_comment())
                lines.emplace_back();
            need_blank = s.toks[0].kind == tk::kw && s.toks[0].lit == "ENDProc";
            int ind = s.indent + (indent_adj.count(n) ? indent_adj.at(n) : 0);
            lines.push_back(std::string(std::max(ind, 0) * opt.indent, ' ') + txt);
            last_comment = s.is_comment();
        }
        while(!lines.empty() && lines.back().empty())
            lines.pop_back();
        for(auto &l : lines)
        {
            out << l << "\n";
            stats.lines++;
            stats.bytes += l.size() + 1;
            if(int(l.size()) > stats.max_len)
                stats.max_len = l.size();
        }
        if(failed && opt.verbose > 0)
            std::cerr << p.fname << ": " << failed
                      << " statements could not be reformatted, written as in the "
                         "source.\n";
        return true;
    }
};
} // namespace

bool list_long(std::ostream &out, const grammar &g, const program &p,
               const list_options &opt, list_stats &stats)
{
    bool fixed = opt.opts && (*opt.opts)[OPT_FIXED_VARS];
    long_writer w(g, p, opt, fixed);
    if(!w.fixed.empty())
    {
        // Write the listing without the fixed variables, used to verify.
        long_writer pre(g, p, opt, false);
        std::ostringstream os;
        list_stats pst;
        pre.write(os, pst);
        stats.pre_text = os.str();
        stats.cvars = w.fixed;
        stats.reverse_subst = true;
    }
    return w.write(out, stats);
}
