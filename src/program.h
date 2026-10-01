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

// program.h: A parsed FastBasic program
#pragma once

#include "engine.h"
#include "grammar.h"
#include <string>
#include <vector>

struct statement
{
    std::vector<token> toks;
    std::vector<node> nodes;
    code_map code;
    // Parser state before the statement
    engine_state before;
    // Source line number
    int line = 0;
    // True if this is the first statement in the source line
    bool line_start = false;
    // Indentation level in the long listing
    int indent = 0;
    // Source text
    std::string text;

    // True if the statement has no tokens (empty or only a comment)
    bool empty() const;
    // True if the statement has only a comment
    bool is_comment() const;
    // True if the statement has a comment at the end
    bool has_comment() const;
    // True if the statement starts a new code block (PROC / DATA)
    bool starts_block() const;
    // Names of the code blocks started in this statement
    std::vector<std::string> new_blocks() const;
};

class program
{
  public:
    std::string fname;
    std::vector<statement> stmts;
    // Parser state at end of program
    engine_state final_state;

    // Parses the given file, returns false on error (errors are printed)
    bool parse_file(const grammar &g, const std::string &file_name);
    // Parses a program from memory, used to verify output.
    // Returns an error message if the program is not valid.
    std::string parse_text(const grammar &g, const std::string &text,
                           const std::string &file_name, bool no_data_files);

    // State after statement "n"
    const engine_state &after(size_t n) const
    {
        return n + 1 < stmts.size() ? stmts[n + 1].before : final_state;
    }

    // Returns the full code of the program, as the FastBasic compiler would
    // produce, optionally running the peephole optimizer. The first "skip"
    // statements are not included.
    std::vector<codew> full_code(bool optimize, size_t skip = 0) const;
};

// Reads a complete source line, from FastBasic "compile.cc"
int read_source_line(std::string &r, const std::string &in, size_t &ipos);
