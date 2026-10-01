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
#include <iostream>
#include <map>

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
    const grammar &g;
    const program &p;
    const list_options &opt;
    verifier ver;
    // Variable and label spelling, from first appearance
    std::map<std::string, std::string> var_name, label_name;

  public:
    long_writer(const grammar &g, const program &p, const list_options &opt)
        : g(g), p(p), opt(opt), ver(g, p, nullptr)
    {
        for(auto &s : p.stmts)
            for(auto &t : s.toks)
            {
                if(t.kind == tk::var && !var_name.count(t.lit))
                    var_name[t.lit] = t.src.empty() ? t.lit : t.src;
                if(t.kind == tk::label && !label_name.count(t.lit))
                    label_name[t.lit] = t.src.empty() ? t.lit : t.src;
            }
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
            auto &s = st[n];
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
            auto txt = statement_text(n, next_var, failed);
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
            lines.push_back(std::string(s.indent * opt.indent, ' ') + txt);
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
    long_writer w(g, p, opt);
    return w.write(out, stats);
}
