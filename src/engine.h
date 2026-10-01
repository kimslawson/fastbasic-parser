/*
 *  fbp - The missing parser for FastBasic
 *  Copyright (C) 2017-2025 Daniel Serpell (FastBasic parser, from which
 *                          this file is derived)
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

// engine.h: FastBasic parsing engine.
//
// This is a port of "parser.h" and "parser-actions.cc" from the FastBasic
// cross compiler, interpreting the same syntax tables.  In addition to the
// bytecode, it records the parsed tokens and the grammar tables matched, so
// that the program can be listed back in different forms.
//
// When updating from upstream FastBasic, compare with the files in
// "vendor/fastbasic/reference".
#pragma once

#include "codew.h"
#include "looptype.h"
#include "vartype.h"

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "synt-sm.h"

namespace syntax
{
class sm_list;
}

// Exception class for a parsing error
class parse_error : public std::runtime_error
{
  public:
    size_t pos;
    parse_error(std::string msg, size_t pos) : std::runtime_error(msg), pos(pos) {}
};

// Kind of parsed tokens
enum class tk
{
    kw,       // A keyword literal from the grammar ("PRInt", "Mod", "Fre()")
    punct,    // A non-alphabetic literal from the grammar ("(", ",", "?", "<>")
    var,      // A variable name
    label,    // A PROC / DATA / DLI name
    num,      // An integer number
    fpnum,    // A floating point number
    str,      // A constant string
    rem,      // A comment
    asmsym,   // An assembly symbol, "@name" or "@@name"
    segment,  // A segment name in DATA [segment]
    datafile, // A file name in DATA ... FILE "name"
};

struct token
{
    tk kind;
    // kw/punct: grammar literal, var/label/asmsym/segment: name in uppercase,
    // rem: text of the comment.
    std::string lit;
    // Source text as written, used to keep the original formatting.
    std::string src;
    // Grammar table where the token was parsed
    std::string table;
    // num: 16 bit value. var: VarType. rem: comment style, '.' or '\''
    int value = 0;
    // num: parsed as a byte. asmsym: byte symbol ("@@").
    bool byte = false;
    // fpnum: the value
    atari_fp fp;
    // str/datafile: the string bytes
    std::string str;
};

// A successfully parsed grammar table, covering tokens [tbeg, tend)
struct node
{
    std::string table;
    size_t tbeg, tend;
    size_t level;
};

// Parser state that persists between statements
struct engine_state
{
    struct jump
    {
        LoopType type;
        std::string label;
        int linenum;
        bool operator==(const jump &j) const
        {
            return type == j.type && label == j.label;
        }
    };
    std::map<std::string, int> vars;
    std::map<std::string, labelType> labels;
    std::vector<jump> jumps;
    std::vector<std::string> proc_stack;
    int label_num = 0;
    std::string last_label;
    std::string last_var_name;
    int current_params = 0;
    int indent = 0, next_indent = 0;
};

// Code emitted, by "procedure" name (the main program is the empty name)
typedef std::map<std::string, std::vector<codew>> code_map;

class engine
{
  private:
    static const int MAX_RECURSE_LEVEL = 200;
    const syntax::sm_list &sl;

    struct saved_error
    {
        int lvl;
        std::string msg;
        bool operator<(const saved_error &b) const
        {
            return lvl < b.lvl || (lvl == b.lvl && msg < b.msg);
        }
    };
    struct saved_pos
    {
        size_t pos, opos, ntok, nnode;
        int indent, next_indent;
    };

    std::vector<codew> var_stk;
    std::set<saved_error> saved_errors;
    std::vector<codew> *code;
    std::string cur_table;

    bool parse_table(const std::string &name);
    bool parse_line(const std::string &name, const syntax::statemachine::line &line);
    bool parse_literal(const std::string &lit);
    bool call_action(const std::string &name);

  public:
    // Persistent state
    engine_state st;
    // Current line being parsed
    std::string str;
    size_t pos = 0, max_pos = 0;
    int lvl = 0;
    // Source line number, used in error messages
    int linenum = 0;
    // Id used for the emitted code
    int code_id = 0;
    // Current input file name (used to load DATA files)
    std::string in_fname;
    // Don't load DATA from files
    bool no_data_files = false;

    // Output of current statement
    code_map procs;
    std::vector<token> toks;
    std::vector<node> nodes;

    static constexpr const char *label_prefix = "fb_lbl_";

    explicit engine(const syntax::sm_list &sl) : sl(sl), code(nullptr) {}

    // Starts parsing a new line
    void new_line(const std::string &l, int ln);
    // Parses one statement from the current position, returns false on
    // a parse error. The tokens, nodes and code are stored in the class.
    bool parse_statement(int id);
    // Returns the error message from a failed parse
    std::string error_message() const;
    // Checks unclosed loops at end of program
    std::string check_loops();

    // Utility functions used by the parsing actions
    saved_pos save() const;
    void restore(const saved_pos &s);
    void check_level();
    bool error(const std::string &msg);
    bool loop_error(const std::string &msg);
    bool eos() const { return pos >= str.length(); }
    bool range(char c1, char c2);
    bool ident_start() const;
    bool get_ident(std::string &ret, std::string *orig = nullptr);
    bool peek(char c) const;
    bool skipws();
    bool expect(char c);
    bool eol();
    std::string new_label();
    std::string push_loop(LoopType type);
    bool peek_loop(LoopType type);
    std::string pop_loop(LoopType type);
    codew remove_last();
    bool emit_word(const std::string &s);
    bool emit_word(int x);
    bool emit_fp(atari_fp x);
    bool emit_label(const std::string &s);
    bool emit_tok(const std::string &tk);
    bool emit_str(const std::string &s);
    bool emit_byte(const std::string &s);
    bool emit_byte(int x);
    bool emit_varn(const std::string &vname);
    void push_proc(const std::string &l);
    void pop_proc(const std::string &l);
    void add_token(token t);
    void var_stk_push();
    bool var_stk_pop();
    const std::string &table() const { return cur_table; }
};
