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

// verify.cc: Checks that transformed source code compiles to the same
//            bytecode as the original.

#include "verify.h"
#include "peephole.h"
#include <cstdlib>
#include <iostream>

std::set<std::string> phantom_vars(const program &p)
{
    std::set<std::string> used, ret;
    for(auto &s : p.stmts)
        for(auto &t : s.toks)
            if(t.kind == tk::var)
                used.insert(t.lit);
    for(auto &v : p.final_state.vars)
        if(!used.count(v.first))
            ret.insert(v.first);
    return ret;
}

std::set<std::string> phantom_labels(const program &p)
{
    std::set<std::string> used, ret;
    for(auto &s : p.stmts)
        for(auto &t : s.toks)
            if(t.kind == tk::label)
                used.insert(t.lit);
    for(auto &l : p.final_state.labels)
        if(!used.count(l.first))
            ret.insert(l.first);
    return ret;
}

std::string name_map::var_orig(const std::string &n) const
{
    auto i = var.find(n);
    if(i != var.end())
        return i->second;
    // The parser can create variables with the name of a DATA label
    auto j = label.find(n);
    return j == label.end() ? n : j->second;
}

void name_map::add_var(const std::string &orig, const std::string &n)
{
    var[n] = orig;
    var_new[orig] = n;
}

void name_map::add_label(const std::string &orig, const std::string &n)
{
    label[n] = orig;
    label_new[orig] = n;
}

engine_state name_map::translate(const engine_state &st,
                                 const std::set<std::string> &unused,
                                 const std::set<std::string> &unused_labels) const
{
    if(var_new.empty() && label_new.empty())
        return st;
    auto tv = [&](const std::string &n)
    {
        auto i = var_new.find(n);
        return i == var_new.end() ? n : i->second;
    };
    auto tl = [&](const std::string &n)
    {
        auto i = label_new.find(n);
        return i == label_new.end() ? n : i->second;
    };
    engine_state r = st;
    r.vars.clear();
    for(auto &v : st.vars)
    {
        if(!unused.count(v.first))
            r.vars[tv(v.first)] = v.second;
        else if(label_new.count(v.first))
            r.vars[label_new.at(v.first)] = v.second;
    }
    r.labels.clear();
    for(auto &l : st.labels)
        if(!unused_labels.count(l.first))
            r.labels[tl(l.first)] = l.second;
    r.last_label = tl(st.last_label);
    r.last_var_name = tv(st.last_var_name);
    return r;
}

std::string name_map::label_orig(const std::string &n) const
{
    auto i = label.find(n);
    return i == label.end() ? n : i->second;
}

static std::string map_label(std::string s, const name_map &m)
{
    static const std::string pfx = engine::label_prefix;
    if(s.compare(0, pfx.size(), pfx) == 0)
        return pfx + m.label_orig(s.substr(pfx.size()));
    return s;
}

// Returns a string that represents the code word, with names mapped.
// Variables are compared by name, as the numbering can change if the
// original had unused variables.
static std::string normalize(codew c, const name_map &m)
{
    if(c.is_varn())
    {
        // Get variable name from the assembly output
        auto s = c.to_asm();
        auto b = s.find('"'), e = s.rfind('"');
        if(b != s.npos && e > b)
            return "V" + m.var_orig(s.substr(b + 1, e - b - 1));
        return "V" + std::to_string(c.get_varn());
    }
    if(c.is_label())
        return "L" + map_label(c.get_str(), m);
    if(c.is_sword())
        return "W" + map_label(c.get_str(), m);
    return c.to_asm();
}

bool code_equal(const std::vector<codew> &orig, const std::vector<codew> &cand,
                const name_map &m)
{
    if(orig.size() != cand.size())
        return false;
    static const name_map empty;
    for(size_t i = 0; i < orig.size(); i++)
        if(normalize(orig[i], empty) != normalize(cand[i], m))
            return false;
    return true;
}

bool code_equal(const code_map &orig, const code_map &cand, const name_map &m,
                verify_mode mode)
{
    // Remove empty entries
    code_map o, c;
    for(auto &x : orig)
        if(!x.second.empty())
            o[x.first] = x.second;
    for(auto &x : cand)
        if(!x.second.empty())
            c[x.first] = x.second;
    if(o.size() != c.size())
        return false;
    for(auto &x : o)
    {
        auto i = c.find(x.first);
        if(i == c.end())
            return false;
        if(mode == verify_mode::optimized)
        {
            auto a = x.second, b = i->second;
            do_peephole(a);
            do_peephole(b);
            if(!code_equal(a, b, m))
                return false;
        }
        else if(!code_equal(x.second, i->second, m))
            return false;
    }
    return true;
}

static bool label_equal(labelType a, labelType b)
{
    return !(a != b) && a.is_defined() == b.is_defined() &&
           a.num_params() == b.num_params() && a.get_segment() == b.get_segment();
}

static bool fake_var(const std::string &name)
{
    return name.compare(0, 6, "-fake-") == 0;
}

bool state_equal(const engine_state &orig, const engine_state &cand, const name_map &m,
                 const std::set<std::string> *optional,
                 const std::set<std::string> *optional_labels)
{
    // Compare variables by name and type
    std::set<std::string> found;
    for(auto &v : cand.vars)
    {
        if(fake_var(v.first))
            continue;
        auto name = m.var_orig(v.first);
        auto i = orig.vars.find(name);
        if(i == orig.vars.end() || (i->second & 0xFF) != (v.second & 0xFF))
            return false;
        found.insert(name);
    }
    for(auto &v : orig.vars)
        if(!fake_var(v.first) && !found.count(v.first) &&
           !(optional && optional->count(v.first)))
            return false;
    std::set<std::string> lfound;
    for(auto &l : cand.labels)
    {
        auto name = m.label_orig(l.first);
        auto i = orig.labels.find(name);
        if(i == orig.labels.end() || !label_equal(i->second, l.second))
            return false;
        lfound.insert(name);
    }
    for(auto &l : orig.labels)
        if(!lfound.count(l.first) && !(optional_labels && optional_labels->count(l.first)))
            return false;
    // Note: "last_label" and "current_params" are only used inside a statement
    return orig.jumps == cand.jumps && orig.proc_stack == cand.proc_stack &&
           orig.label_num == cand.label_num;
}

int code_size(const code_map &code)
{
    int sz = 0;
    for(auto &p : code)
        for(auto c : p.second)
        {
            if(c.is_tok() || c.is_byte() || c.is_varn() || c.is_sbyte())
                sz += 1;
            else if(c.is_word() || c.is_sword())
                sz += 2;
            else if(c.is_string())
                sz += 1 + c.get_str().size();
            else if(!c.is_label())
                sz += 6; // FP
        }
    return sz;
}

bool verifier::parse(const engine_state &start, const std::string &text,
                     engine_state &end, code_map &code, std::vector<token> *toks,
                     std::vector<node> *nodes) const
{
    checks++;
    engine e(g.sl);
    e.st = start;
    e.in_fname = p.fname;
    e.new_line(text, 0);
    try
    {
        if(!e.parse_statement(1) || e.pos != text.length())
            return false;
    }
    catch(std::exception &)
    {
        return false;
    }
    e.st.indent = e.st.next_indent;
    end = e.st;
    code = std::move(e.procs);
    if(toks)
        *toks = std::move(e.toks);
    if(nodes)
        *nodes = std::move(e.nodes);
    return true;
}

static void append_code(code_map &dst, const code_map &src)
{
    for(auto &c : src)
    {
        auto &v = dst[c.first];
        v.insert(v.end(), c.second.begin(), c.second.end());
    }
}

bool verifier::check(size_t first, size_t last, const std::vector<std::string> &texts,
                     verify_mode mode) const
{
    static const name_map empty;
    const name_map &m = names ? *names : empty;

    code_map orig_code, new_code;
    for(size_t i = first; i < last; i++)
        append_code(orig_code, p.stmts[i].code);

    engine_state st = m.translate(p.stmts[first].before, phantoms, phantom_lbls);
    for(auto &t : texts)
    {
        engine_state end;
        code_map code;
        if(debug)
            std::cerr << "  check: [" << t << "]\n";
        if(!parse(st, t, end, code))
        {
            if(debug)
                std::cerr << "  -> parse error\n";
            return false;
        }
        append_code(new_code, code);
        st = end;
    }
    if(!state_equal(p.after(last - 1), st, m, &phantoms, &phantom_lbls))
    {
        if(debug)
            std::cerr << "  -> different parser state\n";
        return false;
    }
    if(!code_equal(orig_code, new_code, m, mode))
    {
        if(debug)
        {
            std::cerr << "  -> different code:\n";
            for(auto &c : orig_code)
                for(auto x : c.second)
                    std::cerr << "    O " << x.to_asm() << "\n";
            for(auto &c : new_code)
                for(auto x : c.second)
                    std::cerr << "    N " << x.to_asm() << "\n";
        }
        return false;
    }
    return true;
}

// Returns the name of a variable code word
static std::string var_name(codew c)
{
    auto s = c.to_asm();
    auto b = s.find('"'), e = s.rfind('"');
    if(b != s.npos && e > b)
        return s.substr(b + 1, e - b - 1);
    return std::string();
}

bool code_equal_subst(const std::vector<codew> &a, const std::vector<codew> &b,
                      const const_vars &cv)
{
    static const name_map empty;
    size_t i = 0, j = 0;
    while(i < a.size() && j < b.size())
    {
        // Variable instead of constant
        if(b[j].is_tok("TOK_VAR_LOAD") && j + 1 < b.size() && b[j + 1].is_varn() &&
           i + 1 < a.size())
        {
            auto it = cv.find(var_name(b[j + 1]));
            if(it != cv.end())
            {
                auto x = a[i], y = a[i + 1];
                std::string val;
                size_t len = 2;
                if((x.is_tok("TOK_BYTE") && y.is_byte()) ||
                   (x.is_tok("TOK_NUM") && y.is_word()))
                {
                    val = "N" + std::to_string(y.get_val() & 0xFFFF);
                    // A CHR$(n) replaced by a string variable
                    if(i + 2 < a.size() && a[i + 2].is_tok("TOK_CHR"))
                    {
                        val = "S" + std::string(1, char(y.get_val() & 0xFF));
                        len = 3;
                    }
                }
                else if(x.is_tok("TOK_CSTRING") && y.is_string())
                    val = "S" + y.get_str();
                if(val != it->second)
                    return false;
                i += len;
                j += 2;
                continue;
            }
        }
        // Constant string instead of CHR$
        if(b[j].is_tok("TOK_CSTRING") && j + 1 < b.size() && b[j + 1].is_string() &&
           i + 2 < a.size() && a[i + 2].is_tok("TOK_CHR"))
        {
            auto x = a[i], y = a[i + 1], z = b[j + 1];
            auto str = z.get_str();
            if(((x.is_tok("TOK_BYTE") && y.is_byte()) ||
                (x.is_tok("TOK_NUM") && y.is_word())) &&
               str.size() == 1 && (y.get_val() & 0xFF) == (str[0] & 0xFF))
            {
                i += 3;
                j += 2;
                continue;
            }
        }
        if(normalize(a[i], empty) != normalize(b[j], empty))
            return false;
        i++;
        j++;
    }
    return i == a.size() && j == b.size();
}

bool code_equal_subst(const code_map &orig, const code_map &cand, const const_vars &cv)
{
    code_map o, c;
    for(auto &x : orig)
        if(!x.second.empty())
            o[x.first] = x.second;
    for(auto &x : cand)
        if(!x.second.empty())
            c[x.first] = x.second;
    if(o.size() != c.size())
        return false;
    for(auto &x : o)
    {
        auto i = c.find(x.first);
        if(i == c.end() || !code_equal_subst(x.second, i->second, cv))
            return false;
    }
    return true;
}

bool verifier::check_subst(size_t first, const std::string &before,
                           const std::string &after, const const_vars &cv) const
{
    static const name_map empty;
    const name_map &m = names ? *names : empty;
    engine_state st = m.translate(p.stmts[first].before, phantoms, phantom_lbls);
    for(auto &v : cv)
        st.vars[v.first] =
            256 * st.vars.size() + (v.second[0] == 'S' ? VT_STRING : VT_WORD);
    engine_state ea, eb;
    code_map ca, cb;
    if(debug)
        std::cerr << "  subst: [" << before << "] -> [" << after << "]\n";
    if(!parse(st, before, ea, ca) || !parse(st, after, eb, cb))
        return false;
    return state_equal(ea, eb, empty) && code_equal_subst(ca, cb, cv);
}
