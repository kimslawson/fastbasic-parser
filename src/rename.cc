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

// rename.cc: Assigns short names to variables and labels
//
// In FastBasic, all variables share the same name space independently of
// the type (so "A", "A$" and "A%" can't coexist), and PROC, DATA and DLI names
// share another name space. PROC names can reuse any variable name. DATA and
// DLI names can reuse variable names too, except where the parser would pick
// the wrong one:
//  - "&X" and "ADR(X)" take the address of DATA X before the address of
//    variable X, and "X(n)" reads an array variable X before DATA X. So labels
//    don't reuse the names of array variables or of variables whose address
//    is taken.
//  - Floating point DATA is tried after the variables: "&X%" is read as "&X"
//    if X is an integer variable, and "X%(n)" as "X%" if X% is a floating
//    point variable. So floating point DATA doesn't reuse any variable name.
//  - Assigning to DATA ("X(0)=1", also after THEN) makes the parser create a
//    variable X while it tries the other rules, which it can't do if X
//    already exists, changing the code. So DATA that is assigned to doesn't
//    reuse any variable name either.
// These rules come from the grammar, which differs between targets, so
// list_short verifies the listing and makes it again with separate names if
// sharing them fails.

#include "rename.h"
#include <algorithm>

std::string short_name(int n)
{
    static const char first[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    static const char second[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    if(n < 27)
        return std::string(1, first[n]);
    n -= 27;
    if(n < 27 * 37)
        return std::string(1, first[n / 37]) + second[n % 37];
    n -= 27 * 37;
    return std::string(1, first[(n / 37 / 37) % 27]) + second[(n / 37) % 37] +
           second[n % 37];
}

renamer::renamer(const program &p)
{
    std::map<std::string, size_t> vidx, lidx;
    for(auto &s : p.stmts)
        for(size_t ti = 0; ti < s.toks.size(); ti++)
        {
            auto &t = s.toks[ti];
            if(t.kind != tk::var && t.kind != tk::label)
                continue;
            bool lbl = t.kind == tk::label;
            auto &idx = lbl ? lidx : vidx;
            auto it = idx.find(t.lit);
            if(it == idx.end())
            {
                symbol_info si;
                si.name = t.lit;
                si.spell = t.src.empty() ? t.lit : t.src;
                si.first = symbols.size();
                si.is_label = lbl;
                if(lbl)
                {
                    auto l = p.final_state.labels.find(t.lit);
                    si.is_proc = l == p.final_state.labels.end() ||
                                 labelType(l->second).is_proc();
                }
                else
                {
                    auto v = p.final_state.vars.find(t.lit);
                    if(v != p.final_state.vars.end())
                    {
                        si.type = v->second & 0xFF;
                        si.no_share = var_type_is_array(VarType(si.type));
                    }
                }
                it = idx.emplace(t.lit, symbols.size()).first;
                symbols.push_back(si);
            }
            symbols[it->second].count++;
            if(lbl)
            {
                // Assigned to: at the start of a statement, or after THEN
                if(ti == 0 || (s.toks[ti - 1].kind == tk::kw && s.toks[ti - 1].lit == "Then"))
                    symbols[it->second].no_share = true;
                // Floating point DATA: "X%" or "X%()"
                if(ti + 1 < s.toks.size() && s.toks[ti + 1].kind == tk::punct &&
                   s.toks[ti + 1].lit.compare(0, 1, "%") == 0)
                    symbols[it->second].no_share = true;
            }
            else if(ti > 0)
            {
                // Address taken: "&X" (the address operator, not the bitwise AND) or "ADR(X"
                auto &pt = s.toks[ti - 1];
                if((pt.kind == tk::punct && pt.lit == "&" && pt.table != "BIT_EXPR_MORE") ||
                   (pt.kind == tk::kw && pt.lit == "ADR("))
                    symbols[it->second].no_share = true;
            }
        }
}

void renamer::assign_same()
{
    vars.clear();
    labels.clear();
    for(auto &s : symbols)
    {
        s.new_name = s.name;
        (s.is_label ? labels : vars)[s.name] = s.name;
    }
}

void renamer::assign_short(const std::set<std::string> &reserved, bool share_labels,
                           const std::set<std::string> &extra_used)
{
    vars.clear();
    labels.clear();
    shared = false;
    // Sort by usage, most used first
    std::vector<symbol_info *> values, procs;
    for(auto &s : symbols)
        (s.is_label ? procs : values).push_back(&s);
    auto cmp = [](const symbol_info *a, const symbol_info *b)
    { return a->count != b->count ? a->count > b->count : a->first < b->first; };
    std::stable_sort(values.begin(), values.end(), cmp);
    std::stable_sort(procs.begin(), procs.end(), cmp);

    // Names that can't be used
    auto bad = [&](const std::string &n)
    { return n.size() > 1 && reserved.count(n); };

    // Variables
    std::set<std::string> no_share, all_vars;
    int n = 0;
    for(auto s : values)
    {
        std::string nm;
        do
            nm = short_name(n++);
        while(bad(nm) || extra_used.count(nm));
        s->new_name = nm;
        vars[s->name] = nm;
        all_vars.insert(nm);
        if(s->no_share)
            no_share.insert(nm);
    }
    // PROC, DATA and DLI names: their own name space, so each one takes the
    // first name not used by another label, skipping the names it must not
    // share with variables.
    std::set<std::string> used_labels;
    for(auto s : procs)
    {
        bool apart = s->no_share || !share_labels;
        std::string nm;
        n = 0;
        do
            nm = short_name(n++);
        while(bad(nm) || used_labels.count(nm) ||
              (!s->is_proc && (no_share.count(nm) || extra_used.count(nm) ||
                               (apart && all_vars.count(nm)))));
        used_labels.insert(nm);
        s->new_name = nm;
        labels[s->name] = nm;
        shared = shared || (!s->is_proc && all_vars.count(nm));
    }
}

std::string renamer::new_var_name(const std::set<std::string> &reserved) const
{
    std::set<std::string> used;
    for(auto &s : symbols)
        if(!s.is_proc)
            used.insert(s.new_name);
    for(int n = 0; n < 27 * 38; n++)
    {
        auto nm = short_name(n);
        if(!used.count(nm) && !(nm.size() > 1 && reserved.count(nm)))
            return nm;
    }
    return std::string();
}
