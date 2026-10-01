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

// grammar.cc: loads the FastBasic grammar for a compiler target.
//
// This mirrors "target.cc" from the FastBasic compiler, but reads the
// target and syntax files from the copies embedded into the executable.

#include "grammar.h"
#include "embedded.h"
#include "synt-optimize.h"
#include "synt-parser.h"
#include "synt-preproc.h"
#include "synt-pstate.h"
#include <sstream>
#include <stdexcept>

static const char *find_file(const std::string &name)
{
    for(const embedded_file *f = embedded_files; f->name; f++)
        if(name == f->name)
            return f->data;
    return nullptr;
}

static std::string sub(const std::string &inp, size_t s, size_t e)
{
    if(s >= inp.size() || s >= e)
        return std::string();
    else if(e >= inp.size())
        return inp.substr(s);
    else
        return inp.substr(s, e - s);
}

// Reads a target file, appending syntax file names
static void read_target(std::string fname, std::vector<std::string> &slist, int level)
{
    if(level > 16)
        throw std::runtime_error("too many nested includes in target files");
    if(fname.empty())
        fname = "default";
    if(fname.find('.') == fname.npos)
        fname += ".tgt";

    const char *data = find_file(fname);
    if(!data)
        throw std::runtime_error("invalid target '" + fname.substr(0, fname.size() - 4) +
                                 "', use '-t help' for a list");

    std::istringstream f(data);
    std::string line;
    while(std::getline(f, line))
    {
        auto s = line.find_first_not_of(" \t\r\n");
        if(s == line.npos || line[s] == '#')
            continue;
        auto e = line.find_first_of(" \t\r\n", s);
        auto a = line.find_first_not_of(" \t\r\n", e);
        auto key = sub(line, s, e);
        auto args = sub(line, a, line.npos);
        if(key == "include")
            read_target(args, slist, level + 1);
        else if(key == "syntax")
        {
            size_t i = 0;
            while(i < args.size())
            {
                auto e = args.find_first_of(" \t\r\n", i);
                slist.push_back(sub(args, i, e));
                i = args.find_first_not_of(" \t\r\n", e);
            }
        }
        // Other keys (library, config, ca65, extension) are not used.
    }
}

void grammar::load(const std::string &target_name)
{
    target = target_name;
    syntax_files.clear();
    read_target(target_name, syntax_files, 0);

    syntax::preproc pre;
    syntax::parse_state p;
    syntax::syntax_parser pf(p, sl);
    for(auto &name : syntax_files)
    {
        const char *src = find_file(name);
        if(!src)
            throw std::runtime_error("missing syntax file '" + name + "'");
        std::istringstream ifile(src);
        auto data = pre.read_input(ifile);
        p.reset(data.c_str(), name);
        if(!pf.parse_file())
            throw std::runtime_error("error parsing syntax file: '" + name + "'");
    }
    // Same optimization as the FastBasic compiler
    syntax::syntax_optimize(sl, false, false);
}

std::vector<std::string> grammar::targets()
{
    std::vector<std::string> ret;
    for(const embedded_file *f = embedded_files; f->name; f++)
    {
        std::string n = f->name;
        if(n.size() > 4 && n.substr(n.size() - 4) == ".tgt")
            ret.push_back(n.substr(0, n.size() - 4));
    }
    return ret;
}

bool grammar::has_table(const std::string &name) const
{
    return sl.sms.find(name) != sl.sms.end();
}
