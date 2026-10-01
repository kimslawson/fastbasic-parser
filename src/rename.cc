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
// the type (so "A", "A$" and "A%" can't coexist), PROC and DATA names share
// another name space. As DATA names are used in expressions like array
// variables, those are given names different from all variables.

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
        for(auto &t : s.toks)
        {
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
                        si.type = v->second & 0xFF;
                }
                it = idx.emplace(t.lit, symbols.size()).first;
                symbols.push_back(si);
            }
            symbols[it->second].count++;
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

void renamer::assign_short(const std::set<std::string> &reserved,
                           const std::set<std::string> &extra_used)
{
    vars.clear();
    labels.clear();
    // Sort by usage, most used first
    std::vector<symbol_info *> values, procs;
    for(auto &s : symbols)
        (s.is_proc ? procs : values).push_back(&s);
    auto cmp = [](const symbol_info *a, const symbol_info *b)
    { return a->count != b->count ? a->count > b->count : a->first < b->first; };
    std::stable_sort(values.begin(), values.end(), cmp);
    std::stable_sort(procs.begin(), procs.end(), cmp);

    // Names that can't be used
    auto bad = [&](const std::string &n)
    { return n.size() > 1 && reserved.count(n); };

    // Variables and DATA labels
    std::set<std::string> data_names;
    int n = 0;
    for(auto s : values)
    {
        std::string nm;
        do
            nm = short_name(n++);
        while(bad(nm) || extra_used.count(nm));
        s->new_name = nm;
        if(s->is_label)
        {
            labels[s->name] = nm;
            data_names.insert(nm);
        }
        else
            vars[s->name] = nm;
    }
    // PROC names
    n = 0;
    for(auto s : procs)
    {
        std::string nm;
        do
            nm = short_name(n++);
        while(bad(nm) || data_names.count(nm));
        s->new_name = nm;
        labels[s->name] = nm;
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
