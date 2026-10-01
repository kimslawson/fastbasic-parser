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

// program.cc: A parsed FastBasic program

#include "program.h"
#include "peephole.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

bool statement::empty() const
{
    return toks.empty();
}

bool statement::is_comment() const
{
    return toks.size() == 1 && toks[0].kind == tk::rem;
}

bool statement::has_comment() const
{
    return !toks.empty() && toks.back().kind == tk::rem;
}

bool statement::starts_block() const
{
    // A new code block is created by PROC, DATA and DLI SET, the code
    // blocks are sorted by source line in the compiled program.
    auto cur = before.proc_stack.empty() ? std::string() : before.proc_stack.back();
    for(auto &c : code)
        if(c.first != cur && !c.first.empty() && !c.second.empty())
            return true;
    return false;
}

// Reads a complete source line, respecting ATASCII and ASCII EOL only
// outside strings. This is the same as "readLine" in FastBasic compile.cc
int read_source_line(std::string &r, const std::string &in, size_t &ipos)
{
    bool in_string = false;
    bool in_comment = false;
    bool in_start = true;
    int num_lines = 0;
    while(ipos < in.size())
    {
        char c = in[ipos++];
        r += c;
        if(in_string)
        {
            if(c == '\x0A')
                num_lines++;
            else if(c == '\"')
                in_string = false;
            continue;
        }
        // Check for DOS end of line
        if(c == '\x0D' && ipos < in.size() && in[ipos] == '\x0A')
        {
            c = in[ipos++];
            r[r.size() - 1] = c;
        }
        // Check for any end of line
        if(c == '\x0A' || c == '\x9B')
            return num_lines + 1;
        // Check we are not entering a string or a comment
        if(in_start)
        {
            if(c == '.' || c == '\'')
                in_comment = true;
            if(c != ' ')
                in_start = false;
        }
        if(!in_comment)
        {
            if(c == '\'')
                in_comment = true;
            else if(c == ':')
                in_start = true;
            else if(c == '\"')
                in_string = true;
        }
    }
    return 0;
}

static char printable(char c)
{
    if(c < 32 || c > 126)
        return '.';
    return c;
}

static std::string format_error(const std::string &fname, int ln, const engine &s,
                                size_t epos, const std::string &msg)
{
    std::ostringstream os;
    // Get start/end of current line, removing last EOL
    size_t min = 0, max = s.str.length();
    while(max && (s.str[max - 1] == '\n' || s.str[max - 1] == '\r' ||
                  s.str[max - 1] == '\x9b'))
        max--;
    if(epos > max)
        epos = max;
    // Only show up to 76 characters total
    if(max > 76)
    {
        if(epos > 50)
            min = epos - 50;
        if(max - min > 76)
            max = min + 76;
    }
    os << fname << ":" << ln << ":" << epos << ": " << msg << "\n  ";
    for(auto i = min; i < epos; i++)
        os << printable(s.str[i]);
    os << " ";
    for(auto i = epos; i < max; i++)
        os << printable(s.str[i]);
    os << "\n  ";
    for(auto i = min; i < epos; i++)
        os << "-";
    os << "^\n";
    return os.str();
}

std::string program::parse_text(const grammar &g, const std::string &text,
                                const std::string &file_name, bool no_data_files)
{
    fname = file_name;
    stmts.clear();
    engine s(g.sl);
    s.in_fname = file_name;
    s.no_data_files = no_data_files;

    size_t ipos = 0;
    int ln = 1;
    int id = 0;
    while(1)
    {
        std::string line;
        int lines = read_source_line(line, text, ipos);
        if(!lines && line.empty())
            break;
        s.new_line(line, ln);
        bool line_start = true;
        try
        {
            while(s.pos != line.length())
            {
                statement st;
                st.before = s.st;
                st.line = ln;
                st.line_start = line_start;
                auto start = s.pos;
                if(!s.parse_statement(++id) ||
                   (s.pos != line.length() && !s.peek(':')))
                    return format_error(fname, ln, s, s.max_pos, s.error_message());
                st.indent = s.st.indent;
                s.st.indent = s.st.next_indent;
                st.toks = std::move(s.toks);
                st.nodes = std::move(s.nodes);
                st.code = std::move(s.procs);
                st.text = line.substr(start, s.pos - start);
                stmts.push_back(std::move(st));
                line_start = false;
                s.expect(':');
            }
        }
        catch(parse_error &e)
        {
            return format_error(fname, ln, s, e.pos, e.what());
        }
        ln += lines;
    }
    // Check unclosed loops
    final_state = s.st;
    auto loop_error = s.check_loops();
    if(loop_error.size())
        return fname + ":" + std::to_string(ln) + ": " + loop_error + "\n";
    return std::string();
}

bool program::parse_file(const grammar &g, const std::string &file_name)
{
    std::ifstream ifile(file_name, std::ios::binary);
    if(!ifile.is_open())
    {
        std::cerr << "fbp: can't open input file '" << file_name << "'\n";
        return false;
    }
    std::ostringstream data;
    data << ifile.rdbuf();
    auto err = parse_text(g, data.str(), file_name, false);
    if(err.size())
    {
        std::cerr << err;
        return false;
    }
    return true;
}

std::vector<codew> program::full_code(bool optimize) const
{
    code_map all;
    for(auto &st : stmts)
        for(auto &c : st.code)
        {
            auto &v = all[c.first];
            v.insert(v.end(), c.second.begin(), c.second.end());
        }
    // The compiler adds a TOK_END at the end of the parsing, in the current
    // procedure (normally the main program).
    auto cur =
        final_state.proc_stack.empty() ? std::string() : final_state.proc_stack.back();
    all[cur].push_back(codew::ctok("TOK_END", 0));

    std::vector<codew> p = all[std::string()];
    if(!p.size() || !p.back().is_tok("TOK_END"))
        p.push_back(codew::ctok("TOK_END", 0));
    // Emit procs sorted by source line number, exactly as the FastBasic
    // compiler does - the code is identified by statement number.
    std::vector<const std::vector<codew> *> sprocs;
    for(auto &c : all)
        if(!c.first.empty() && c.second.size())
            sprocs.push_back(&c.second);
    auto line_of = [&](const std::vector<codew> *v)
    {
        int id = (*v)[0].linenum();
        return (id >= 1 && id <= int(stmts.size())) ? stmts[id - 1].line : 0;
    };
    std::sort(std::begin(sprocs), std::end(sprocs),
              [&](const std::vector<codew> *a, const std::vector<codew> *b)
              { return line_of(a) < line_of(b); });
    for(auto &c : sprocs)
        p.insert(std::end(p), std::begin(*c), std::end(*c));
    if(optimize)
        do_peephole(p);
    return p;
}
