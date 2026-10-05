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

// listing.h: Writes FastBasic listings
#pragma once

#include "program.h"
#include "verify.h"
#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

struct opt_settings;

struct list_options
{
    // Long listing
    bool upper = false;     // Keywords in uppercase
    int indent = 2;         // Spaces per indentation level
    // Short listing
    int max_line = 120;     // Maximum line length
    bool full_names = false; // Don't rename variables
    bool ascii_eol = false;  // Use ASCII EOL instead of ATASCII
    // Optimizations
    const opt_settings *opts = nullptr;
    // Messages
    int verbose = 1;
    // Annotated listing (-a): a long listing that uses the names of a short
    // listing, with a comment line before the statements that start each of
    // its lines (key: number of the statement in the source).
    const name_map *rename = nullptr;
    std::map<size_t, std::string> marks;
};

// Statistics returned from listing
struct list_stats
{
    int lines = 0;
    int bytes = 0;
    int max_len = 0;
    int long_stmts = 0; // statements longer than max line length
    name_map names;     // Renamed symbols, new -> original
    // Used when applying optimizations that change the code:
    std::string pre_text;  // Listing before the transformations
    ::const_vars cvars;    // Constants replaced by variables
    int init_stmts = 0;    // Number of statements added at the start
    bool reverse_subst = false; // Variables replaced by constants (fixed_vars)
    // Short listing: for each output line, the number of the first source
    // statement in it (or SIZE_MAX if it has only added statements), and its length.
    std::vector<size_t> line_first;
    std::vector<int> line_len;
};

//---------------------------------------------------------------------
// Utility functions

// Returns true if the character can be part of an identifier
inline bool ident_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_';
}

// Full keyword text from a grammar literal, in uppercase
std::string kw_full(const std::string &lit);
// Abbreviated keyword from a grammar literal
std::string kw_abbrev(const std::string &lit);
// String constant in the long (readable) format
std::string str_long(const std::string &s);
// String constant in the short format
std::string str_short(const std::string &s);
// Number in the long format
std::string num_long(const token &t);
// Candidate texts for a number in the short format, shortest first
std::vector<std::string> num_short(const token &t);
// Uppercase a string
std::string ucase(std::string s);
std::string lcase(std::string s);

// Returns the index of the outermost non-empty node that starts at token
// "idx", or -1 if none.
int outer_node_at(const std::vector<node> &nodes, size_t idx);
// Returns the node covering the argument of a function keyword at "idx",
// or -1 if the keyword is not a function.
int function_arg(const std::vector<node> &nodes, size_t idx);
// Returns true if tokens [b,e) are fully enclosed in parenthesis
bool is_wrapped(const std::vector<token> &toks, size_t b, size_t e);

// List of all keywords in the grammar, uppercase full and abbreviated forms
std::set<std::string> grammar_keywords(const grammar &g);

//---------------------------------------------------------------------
// Writers

// Writes the long (readable) listing
bool list_long(std::ostream &out, const grammar &g, const program &p,
               const list_options &opt, list_stats &stats);

// Applies the optimizations useful for the long listing, returns the text
// (in short form) of each modified statement.
std::map<size_t, std::string> optimize_for_long(const grammar &g, const program &p,
                                                const list_options &opt);

// Writes the short (minimized) listing
bool list_short(std::ostream &out, const grammar &g, const program &p,
                const list_options &opt, list_stats &stats);
