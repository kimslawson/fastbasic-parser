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

// engine.cc: FastBasic parsing engine, see engine.h

#include "engine.h"
#include "ifile.h"
#include "synt-sm-list.h"

#include <algorithm>
#include <cmath>

using namespace syntax;
using dcode = statemachine::dcode;

//---------------------------------------------------------------------
// Basic parsing utilities, from FastBasic "parser.h"

void engine::new_line(const std::string &l, int ln)
{
    pos = max_pos = 0;
    var_stk.clear();
    str = l;
    saved_errors.clear();
    linenum = ln;
}

engine::saved_pos engine::save() const
{
    return saved_pos{pos, code->size(), toks.size(), nodes.size(), st.indent,
                     st.next_indent};
}

void engine::restore(const saved_pos &s)
{
    pos = s.pos;
    code->resize(s.opos, codew::ctok("TOK_END", 0));
    toks.resize(s.ntok);
    nodes.resize(s.nnode);
    st.indent = s.indent;
    st.next_indent = s.next_indent;
}

void engine::check_level()
{
    if(lvl > MAX_RECURSE_LEVEL)
        throw parse_error("expression too complex for the compiler", pos);
    lvl++;
}

bool engine::error(const std::string &msg)
{
    if(!msg.empty())
    {
        if(pos >= max_pos)
        {
            if(pos > max_pos)
                saved_errors.clear();
            saved_errors.insert(saved_error{lvl, msg});
            max_pos = pos;
        }
    }
    return false;
}

bool engine::loop_error(const std::string &msg)
{
    // Loop error takes precedence over all other errors
    saved_errors.clear();
    saved_errors.insert(saved_error{lvl, msg});
    max_pos = pos;
    return false;
}

bool engine::range(char c1, char c2)
{
    if(pos < str.length())
    {
        if(str[pos] >= c1 && str[pos] <= c2)
        {
            pos++;
            return true;
        }
    }
    return false;
}

bool engine::ident_start() const
{
    return pos < str.length() && ((str[pos] >= 'a' && str[pos] <= 'z') ||
                                  (str[pos] >= 'A' && str[pos] <= 'Z') || str[pos] == '_');
}

bool engine::get_ident(std::string &ret, std::string *orig)
{
    skipws();
    if(ident_start())
    {
        auto start = pos;
        while(pos < str.length())
        {
            char c = str[pos];
            if(c >= 'a' && c <= 'z')
                c = c - ('a' - 'A');
            if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            {
                ret += c;
                pos++;
            }
            else if(c == '.')
                return false;
            else
                break;
        }
        if(orig)
            *orig = str.substr(start, pos - start);
        skipws();
        return true;
    }
    else
        return false;
}

bool engine::peek(char c) const
{
    if(pos < str.length())
    {
        char p = str[pos];
        // NOTE: this replicates the FastBasic parser exactly.
        if(p >= 'a' && p >= 'z')
            p = p - ('a' - 'A');
        if(c >= 'a' && c <= 'z')
            c = c - ('a' - 'A');
        if(p == c)
            return true;
    }
    return false;
}

bool engine::skipws()
{
    while(pos < str.length() && (str[pos] == ' ' || str[pos] == '\t'))
        pos++;
    return true;
}

bool engine::expect(char c)
{
    if(pos < str.length())
    {
        char p = str[pos];
        if(p >= 'a' && p <= 'z')
            p = p - ('a' - 'A');
        if(c >= 'a' && c <= 'z')
            c = c - ('a' - 'A');
        if(p == c)
        {
            pos++;
            return true;
        }
    }
    // Add left parenthesis as possible error, provides better messages
    if(c == ')')
        return error("right parenthesis");
    else if(c == ']')
        return error("right bracket");
    return false;
}

bool engine::eol()
{
    if(pos < str.length())
    {
        // Three types of EOL:
        if((str[pos] == '\x9B') || // AT-ASCII EOL
           (str[pos] == '\n'))     // Unix EOL
        {
            pos++;
            return true;
        }
        // Windows EOL, two bytes
        if((str[pos] == '\r') && (pos < str.length() - 1) && (str[pos + 1] == '\n'))
        {
            pos += 2;
            return true;
        }
    }
    return false;
}

std::string engine::new_label()
{
    st.label_num++;
    return "jump_lbl_" + std::to_string(st.label_num);
}

std::string engine::push_loop(LoopType type)
{
    auto lbl = new_label();
    st.jumps.push_back({type, lbl, linenum});
    if(loop_add_indent(type))
        st.next_indent = st.next_indent + 1;
    return lbl;
}

bool engine::peek_loop(LoopType type)
{
    if(!st.jumps.size())
        return false;
    auto last = st.jumps.back();
    return !(last.type != type && (type != LT_ELSE || last.type != LT_IF));
}

std::string engine::pop_loop(LoopType type)
{
    if(!st.jumps.size())
    {
        if(type == LT_ELSE || type == LT_ELIF)
            type = LT_IF;
        loop_error("missing " + get_loop_name(type));
        return std::string();
    }
    auto last = st.jumps.back();
    if(last.type != type)
    {
        if(type != LT_ELSE || last.type != LT_IF)
        {
            if(type == LT_ELSE || type == LT_ELIF)
                type = LT_IF;
            loop_error("missing " + get_loop_name(type));
            return std::string();
        }
    }
    if(loop_add_indent(type))
    {
        st.next_indent -= 1;
        st.indent = st.next_indent;
    }
    auto lbl = last.label;
    st.jumps.pop_back();
    return lbl;
}

std::string engine::check_loops()
{
    // Checks that there are no unclosed loops at the end of compilation.
    if(!st.jumps.size())
        return std::string();
    for(; st.jumps.size(); st.jumps.pop_back())
    {
        auto j = st.jumps.back();
        auto type = j.type;
        if(type != LT_EXIT)
            return "unclosed " + get_loop_name(type) + " at line " +
                   std::to_string(j.linenum);
    }
    return "EXIT without loop";
}

codew engine::remove_last()
{
    if(code->empty())
        throw parse_error("internal error: empty code", pos);
    codew ret = code->back();
    code->pop_back();
    return ret;
}

bool engine::emit_word(const std::string &s)
{
    code->push_back(codew::cword(s, code_id));
    return true;
}

bool engine::emit_word(int x)
{
    code->push_back(codew::cword(x, code_id));
    return true;
}

bool engine::emit_fp(atari_fp x)
{
    code->push_back(codew::cfp(x, code_id));
    return true;
}

bool engine::emit_label(const std::string &s)
{
    code->push_back(codew::clabel(s, code_id));
    return true;
}

bool engine::emit_tok(const std::string &tk)
{
    code->push_back(codew::ctok(tk, code_id));
    return true;
}

bool engine::emit_str(const std::string &s)
{
    code->push_back(codew::cstring(s, code_id));
    return true;
}

bool engine::emit_byte(const std::string &s)
{
    code->push_back(codew::cbyte(s, code_id));
    return true;
}

bool engine::emit_byte(int x)
{
    code->push_back(codew::cbyte(x, code_id));
    return true;
}

bool engine::emit_varn(const std::string &vname)
{
    auto it = st.vars.find(vname);
    if(it == st.vars.end())
        throw std::runtime_error("invalid var name");
    code->push_back(codew::cvarn(vname, it->second >> 8, code_id));
    return true;
}

void engine::push_proc(const std::string &l)
{
    st.proc_stack.push_back(l);
    code = &procs[l];
}

void engine::pop_proc(const std::string &l)
{
    if(!st.proc_stack.size())
        throw std::runtime_error("empty proc stack");
    if(st.proc_stack.back() != l)
        throw std::runtime_error("invalid proc stack");
    st.proc_stack.pop_back();
    if(!st.proc_stack.size())
        code = &procs[std::string()];
    else
        code = &procs[st.proc_stack.back()];
}

void engine::add_token(token t)
{
    t.table = cur_table;
    toks.push_back(std::move(t));
}

//---------------------------------------------------------------------
// Parsing actions, from FastBasic "parser-actions.cc"

static unsigned long get_hex(engine &s)
{
    unsigned num = 0;
    auto start = s.pos;
    while(s.pos < s.str.length())
    {
        char c = s.str[s.pos];
        if(c >= '0' && c <= '9')
            num = num * 16 + (c - '0');
        else if(c >= 'a' && c <= 'f')
            num = num * 16 + 10 + (c - 'a');
        else if(c >= 'A' && c <= 'F')
            num = num * 16 + 10 + (c - 'A');
        else
            break;
        s.pos++;
        if(num > 0xFFFF)
            return num;
    }
    if(s.pos == start)
        return 65536; // No digits: error
    return num;
}

static unsigned long get_dec(engine &s)
{
    unsigned num = 0;
    auto start = s.pos;
    while(s.pos < s.str.length())
    {
        char c = s.str[s.pos];
        if(c >= '0' && c <= '9')
            num = num * 10 + (c - '0');
        else
            break;
        s.pos++;
        if(num > 0xFFFF)
            return num;
    }
    if(s.pos == start)
        return 65536; // No digits: error
    return num;
}

static unsigned long get_number(engine &s)
{
    auto start = s.pos;
    if(s.expect('$'))
    {
        auto h = get_hex(s);
        if(h > 65535)
            return 65536;
        return h;
    }
    else
    {
        bool sign = s.expect('-');
        int num = get_dec(s);

        if(num > 65535 || num < 0)
            return 65536;

        if(s.expect('.')) // If ends in a DOT, it's a fp number
        {
            s.pos = start;
            return 65536;
        }
        if(sign)
            return 65536 - num;
        else
            return num;
    }
}

static void add_number(engine &s, size_t start, unsigned long num, bool byte)
{
    token t;
    t.kind = tk::num;
    t.src = s.str.substr(start, s.pos - start);
    t.value = int(num & 0xFFFF);
    t.byte = byte;
    s.add_token(t);
}

static bool get_asm_constant(engine &s, bool byte)
{
    auto start = s.pos;
    if(s.expect('@') && (!byte || s.expect('@')))
    {
        std::string name;
        // Reads ASM constant
        if(s.get_ident(name))
        {
            token t;
            t.kind = tk::asmsym;
            t.lit = name;
            t.src = s.str.substr(start, s.pos - start);
            t.byte = byte;
            s.add_token(t);
            if(byte)
                s.emit_byte(name);
            else
                s.emit_word(name);
            s.skipws();
            return true;
        }
    }
    s.pos = start;
    return false;
}

static bool SMB_E_NUMBER_WORD(engine &s)
{
    s.skipws();
    if(get_asm_constant(s, false))
        return true;
    auto start = s.pos;
    auto num = get_number(s);
    if(num > 65535)
        return false;
    add_number(s, start, num, false);
    s.emit_word(num);
    s.skipws();
    return true;
}

static bool SMB_E_NUMBER_BYTE(engine &s)
{
    s.skipws();
    if(get_asm_constant(s, true))
        return true;
    auto start = s.pos;
    auto num = get_number(s);
    if(num > 255)
        return false;
    add_number(s, start, num, true);
    s.emit_byte(num);
    s.skipws();
    return true;
}

static bool get_const_string(engine &s, std::string &str)
{
    while(!s.eos())
    {
        if(s.expect('"'))
        {
            if(s.expect('"'))
                str += '"';
            else if(s.expect('$'))
            {
                do
                {
                    auto c = get_hex(s);
                    if(c > 255)
                        return false;
                    str += char(c);
                } while(s.expect('$'));
                if(!s.expect('"'))
                    return true;
            }
            else
                return true;
        }
        else
        {
            str += char(s.str[s.pos]);
            s.pos++;
        }
    }
    return false;
}

// Joins the starting quote with the string contents into one token
static void add_string(engine &s, tk kind, const std::string &str, size_t start)
{
    if(!s.toks.empty() && s.toks.back().kind == tk::punct && s.toks.back().lit == "\"")
        s.toks.pop_back();
    token t;
    t.kind = kind;
    t.str = str;
    t.src = s.str.substr(start ? start - 1 : 0, s.pos - (start ? start - 1 : 0));
    s.add_token(t);
}

static bool SMB_E_CONST_STRING(engine &s)
{
    std::string str;
    auto start = s.pos;
    if(get_const_string(s, str))
    {
        add_string(s, tk::str, str, start);
        return s.emit_str(str);
    }
    return false;
}

static bool do_rem(engine &s, char style)
{
    auto start = s.pos;
    while(!s.eos() && !s.expect('\n') && !s.expect('\x9b'))
        s.pos++;
    // Remove the EOL from the comment text
    auto end = s.pos;
    while(end > start && (s.str[end - 1] == '\n' || s.str[end - 1] == '\r' ||
                          s.str[end - 1] == '\x9b'))
        end--;
    // Remove the "." token
    if(style == '.' && !s.toks.empty() && s.toks.back().kind == tk::punct &&
       s.toks.back().lit == ".")
        s.toks.pop_back();
    token t;
    t.kind = tk::rem;
    t.lit = s.str.substr(start, end - start);
    t.value = style;
    s.add_token(t);
    return true;
}

static bool SMB_E_REM(engine &s)
{
    return do_rem(s, '.');
}

static bool SMB_E_EOL(engine &s)
{
    s.skipws();
    if(s.expect('\''))
        return do_rem(s, '\'');
    return (s.eos() || s.peek(':') || s.eol());
}

static bool SMB_E_PUSH_VAR(engine &s)
{
    s.var_stk_push();
    return true;
}

static bool SMB_E_POP_VAR(engine &s)
{
    return s.var_stk_pop();
}

static bool SMB_E_PUSH_LT(engine &s)
{
    auto t = get_looptype(s.remove_last().get_str());
    auto l = s.push_loop(t);
    switch(t)
    {
    case LT_DO_LOOP:
    case LT_REPEAT:
    case LT_WHILE_1:
    case LT_FOR_1:
        s.emit_label(l);
        break;
    case LT_WHILE_2:
    case LT_FOR_2:
    case LT_IF:
        s.emit_word(l);
        break;
    case LT_EXIT:
    case LT_ELSE:
    case LT_ELIF:
    case LT_PROC_2:
    case LT_LAST_JUMP:
        break;
    case LT_PROC_DATA:
        // Optimize by switching codep
        s.remove_last();
        s.push_proc(l);
        break;
    }
    return true;
}

static bool SMB_E_POP_LOOP(engine &s)
{
    auto l = s.pop_loop(LT_DO_LOOP);
    if(l.empty())
        return s.loop_error("loop start missing");
    s.emit_word(l);
    s.emit_label(l + "_x");
    return true;
}

static bool SMB_E_POP_WHILE(engine &s)
{
    auto l1 = s.pop_loop(LT_WHILE_2);
    auto l2 = s.pop_loop(LT_WHILE_1);
    if(l1.empty() || l2.empty())
        return false;
    s.emit_word(l2);
    s.emit_label(l1);
    s.emit_label(l2 + "_x");
    return true;
}

static bool SMB_E_POP_IF(engine &s)
{
    auto l = s.pop_loop(LT_ELSE);
    if(l.empty())
        return false;
    s.emit_label(l);
    while(s.peek_loop(LT_ELIF))
        s.emit_label(s.pop_loop(LT_ELIF));
    return true;
}

static bool SMB_E_ELSEIF(engine &s)
{
    auto l1 = s.pop_loop(LT_IF);
    if(l1.empty())
        return false;
    auto t = get_looptype(s.remove_last().get_str());
    auto l2 = s.push_loop(t);
    s.emit_word(l2);
    s.emit_label(l1);
    return true;
}

static bool SMB_E_EXIT_LOOP(engine &s)
{
    auto last = s.st.jumps.size();
    while(1)
    {
        if(last == 0)
            return s.loop_error("EXIT without loop");
        last--;
        auto type = s.st.jumps[last].type;
        if(type == LT_ELIF || type == LT_IF || type == LT_ELSE || type == LT_FOR_2 ||
           type == LT_WHILE_2)
            continue;
        else if(type <= LT_EXIT)
            return s.loop_error("invalid EXIT");
        break;
    }
    s.emit_word(s.st.jumps[last].label + "_x");
    return true;
}

static bool SMB_E_POP_PROC_DATA(engine &s)
{
    auto l = s.pop_loop(LT_PROC_DATA);
    if(l.empty())
        return false;
    s.pop_proc(l);
    return true;
}

static bool SMB_E_POP_PROC_2(engine &s)
{
    auto l = s.pop_loop(LT_PROC_2);
    if(l.empty())
        return false;
    s.emit_label(l + "_x");
    return true;
}

static bool SMB_E_POP_FOR(engine &s)
{
    auto l2 = s.pop_loop(LT_FOR_1);
    auto l1 = s.pop_loop(LT_FOR_2);
    if(l1.empty() || l2.empty())
        return false;
    s.remove_last();
    s.emit_word(l2);
    s.emit_label(l1);
    s.emit_label(l2 + "_x");
    return true;
}

static bool SMB_E_POP_REPEAT(engine &s)
{
    auto l = s.pop_loop(LT_REPEAT);
    if(l.empty())
        return false;
    s.emit_word(l);
    s.emit_label(l + "_x");
    return true;
}

static void add_var_token(engine &s, const std::string &name, const std::string &orig,
                          int type)
{
    token t;
    t.kind = tk::var;
    t.lit = name;
    t.src = orig;
    t.value = type;
    s.add_token(t);
}

static bool SMB_E_VAR_CREATE(engine &s)
{
    auto &v = s.st.vars;
    std::string name, orig;
    if(!s.get_ident(name, &orig))
        return false;
    if(v.find(name) != v.end())
        return false;
    auto v_num = v.size();
    v[name] = 0 + 256 * v_num;
    s.emit_varn(name);
    s.st.last_var_name = name;
    add_var_token(s, name, orig, VT_UNDEF);
    return true;
}

static bool SMB_E_VAR_SET_TYPE(engine &s)
{
    s.skipws();
    // Get type
    enum VarType type = get_vartype(s.remove_last().get_str());
    auto &v = s.st.vars;
    v[s.st.last_var_name] = (v[s.st.last_var_name] & ~0xFF) + type;
    // If type is FLOAT, allocate two more invisible variables
    if(type == VT_FLOAT)
    {
        v["-fake-" + std::to_string(v.size())] = 0;
        v["-fake-" + std::to_string(v.size())] = 0;
    }
    // Update the type in the token
    for(auto i = s.toks.rbegin(); i != s.toks.rend(); ++i)
        if(i->kind == tk::var && i->lit == s.st.last_var_name)
        {
            i->value = type;
            break;
        }
    // This rule only succeeds on array types (defined with "DIM"), other
    // variable types create the variable and then fail so the parser can retry
    // with the new created variable.
    return var_type_is_array(type);
}

static bool var_check(engine &s, enum VarType type)
{
    auto &v = s.st.vars;
    std::string name, orig;
    if(!s.get_ident(name, &orig))
        return false;
    if(v.find(name) == v.end())
        return s.error("variable name but got '" + name + "'");
    if((v[name] & 0xFF) != type)
        return s.error(get_vt_name(type) + " and got '" + name + "'");
    add_var_token(s, name, orig, type);
    s.emit_varn(name);
    return true;
}

static bool SMB_E_VAR_WORD(engine &s)
{
    return var_check(s, VT_WORD);
}

static bool SMB_E_VAR_SEARCH(engine &s)
{
    enum VarType type = get_vartype(s.remove_last().get_str());
    return var_check(s, type);
}

// Get optional exponent for FP number
static int parse_fp_exp(engine &s)
{
    auto spos = s.save();
    if(s.expect('E'))
    {
        // Expect either a '-' or a '+'
        int exp = 0;
        bool esign = s.expect('-') || (s.expect('+'), false);
        if(s.range('0', '9'))
        {
            exp = s.str[s.pos - 1] - '0';
            if(s.range('0', '9'))
                exp = exp * 10 + s.str[s.pos - 1] - '0';
            return esign ? -exp : exp;
        }
    }
    s.restore(spos);
    return 0;
}

static atari_fp get_fp_number(engine &s)
{
    // Optional sign
    bool sign = s.expect('-');

    // Get all digits:
    bool ok = false;
    double num = 0;
    int dot = -1, norm = 0;
    while(s.pos < s.str.length())
    {
        char c = s.str[s.pos];
        if(c >= '0' && c <= '9')
        {
            num = num * 10 + (c - '0');
            // Normalize numbers too big
            if(num > 1e30)
            {
                num = num / 1000;
                norm += 3;
            }
            ok = true;
        }
        else if(c == '.')
            dot = s.pos + 1;
        else
            break;
        s.pos++;
    }
    if(!ok)
        return atari_fp(HUGE_VAL); // return invalid number

    // Calculate dot exponent
    dot = dot >= 0 ? s.pos - dot : 0;

    // Optional exponent and resulting number
    num = num * std::pow(10, parse_fp_exp(s) - dot + norm);
    num = sign ? -num : num;

    return atari_fp(num);
}

static bool SMB_E_NUMBER_FP(engine &s)
{
    s.skipws();
    auto start = s.pos;
    auto num = get_fp_number(s);
    if(!num.valid())
        return false;
    token t;
    t.kind = tk::fpnum;
    t.fp = num;
    t.src = s.str.substr(start, s.pos - start);
    s.add_token(t);
    s.emit_fp(num);
    s.skipws();
    return true;
}

static void add_label_token(engine &s, const std::string &name, const std::string &orig)
{
    token t;
    t.kind = tk::label;
    t.lit = name;
    t.src = orig;
    s.add_token(t);
}

static bool SMB_E_LABEL_DEF(engine &s)
{
    auto l = s.push_loop(LT_PROC_DATA);
    s.remove_last();
    s.push_proc(l);

    auto &v = s.st.labels;
    auto name = s.st.last_label;
    if(v[name].is_defined())
        return s.loop_error("new label, got label already defined '" + name + "'");
    s.st.current_params = 0;
    s.emit_label(engine::label_prefix + name);
    return true;
}

static bool SMB_E_LABEL(engine &s)
{
    // Get type
    auto ltype = labelType(s.remove_last().get_str());
    // Get identifier
    std::string name, orig;
    if(!s.get_ident(name, &orig))
        return false;
    auto it = s.st.labels.find(name);
    if(it == s.st.labels.end())
        return false;
    // Check type
    if(it->second != ltype)
        return false;
    add_label_token(s, name, orig);
    s.emit_word(engine::label_prefix + name);
    return true;
}

static bool SMB_E_COUNT_PARAM(engine &s)
{
    s.st.current_params++;
    return false;
}

// Called in EXEC, creates a label if not exists, if already exists checks
// that it is a PROC.
static bool SMB_E_LABEL_CREATE(engine &s)
{
    std::string name, orig;
    if(!s.get_ident(name, &orig))
        return false;
    // Get type, create if not exists
    auto &v = s.st.labels[name];
    // Check type
    if(!v.is_proc())
        return s.loop_error("new label, got label already defined '" + name + "'");
    // Store variable name
    add_label_token(s, name, orig);
    s.st.last_label = name;
    s.st.current_params = 0;
    return true;
}

static bool SMB_E_DO_EXEC(engine &s)
{
    int pnum = s.st.current_params;
    auto &l = s.st.labels[s.st.last_label];
    if(!l.add_proc_params(pnum))
        throw parse_error("invalid number of parameters in EXEC, expected " +
                              std::to_string(l.num_params()) + ", got " +
                              std::to_string(pnum),
                          s.pos);
    s.emit_word(engine::label_prefix + s.st.last_label);
    return true;
}

static bool SMB_E_PROC_CHECK(engine &s)
{
    int pnum = s.st.current_params - 1;
    auto &l = s.st.labels[s.st.last_label];
    if(!l.add_proc_params(pnum))
        throw parse_error("invalid number of parameters in PROC, expected " +
                              std::to_string(l.num_params()) + ", got " +
                              std::to_string(pnum),
                          s.pos);
    l.define();
    return true;
}

static bool SMB_E_LABEL_SET_TYPE(engine &s)
{
    s.skipws();
    // Get type
    s.st.labels[s.st.last_label].set_type(s.remove_last().get_str());
    return true;
}

static bool SMB_E_DATA_SET_ROM_SEG(engine &s)
{
    s.skipws();
    s.st.labels[s.st.last_label].set_segment("CODE");
    return true;
}

static bool SMB_E_DATA_SET_SEGMENT(engine &s)
{
    s.skipws();
    // Get segment name
    std::string seg, orig;
    if(s.get_ident(seg, &orig))
    {
        token t;
        t.kind = tk::segment;
        t.lit = seg;
        t.src = orig;
        s.add_token(t);
        s.st.labels[s.st.last_label].set_segment(seg);
        return true;
    }
    else
        return s.error("segment name");
}

// Reads a DATA array from a file
static bool SMB_E_DATA_FILE(engine &s)
{
    s.skipws();
    // Get file name until the '"'
    std::string fname;
    auto pos = s.pos;
    if(!get_const_string(s, fname))
        return false;
    add_string(s, tk::datafile, fname, pos);

    if(s.no_data_files)
    {
        // Emit a placeholder with the file name, so the code can be compared.
        s.emit_str(fname);
        return true;
    }

    auto f = open_include_file(s.in_fname, fname);
    if(!f)
        throw parse_error("can't open data file '" + fname + "'", pos);

    // Read the file to a buffer of max 64k
    for(unsigned i = 0; i < 65536; i++)
    {
        int c = f->get();
        if(c < 0 || c > 255)
            break;
        s.emit_byte(c);
    }
    return true;
}

typedef bool (*action_fn)(engine &s);
static const std::map<std::string, action_fn> actions = {
    {"E_CONST_STRING", SMB_E_CONST_STRING},
    {"E_COUNT_PARAM", SMB_E_COUNT_PARAM},
    {"E_DATA_FILE", SMB_E_DATA_FILE},
    {"E_DO_EXEC", SMB_E_DO_EXEC},
    {"E_ELSEIF", SMB_E_ELSEIF},
    {"E_EOL", SMB_E_EOL},
    {"E_EXIT_LOOP", SMB_E_EXIT_LOOP},
    {"E_LABEL", SMB_E_LABEL},
    {"E_LABEL_CREATE", SMB_E_LABEL_CREATE},
    {"E_LABEL_DEF", SMB_E_LABEL_DEF},
    {"E_LABEL_SET_TYPE", SMB_E_LABEL_SET_TYPE},
    {"E_DATA_SET_ROM_SEG", SMB_E_DATA_SET_ROM_SEG},
    {"E_DATA_SET_SEGMENT", SMB_E_DATA_SET_SEGMENT},
    {"E_NUMBER_BYTE", SMB_E_NUMBER_BYTE},
    {"E_NUMBER_FP", SMB_E_NUMBER_FP},
    {"E_NUMBER_WORD", SMB_E_NUMBER_WORD},
    {"E_POP_FOR", SMB_E_POP_FOR},
    {"E_POP_IF", SMB_E_POP_IF},
    {"E_POP_LOOP", SMB_E_POP_LOOP},
    {"E_POP_PROC_2", SMB_E_POP_PROC_2},
    {"E_POP_PROC_DATA", SMB_E_POP_PROC_DATA},
    {"E_POP_REPEAT", SMB_E_POP_REPEAT},
    {"E_POP_VAR", SMB_E_POP_VAR},
    {"E_POP_WHILE", SMB_E_POP_WHILE},
    {"E_PROC_CHECK", SMB_E_PROC_CHECK},
    {"E_PUSH_LT", SMB_E_PUSH_LT},
    {"E_PUSH_VAR", SMB_E_PUSH_VAR},
    {"E_REM", SMB_E_REM},
    {"E_VAR_CREATE", SMB_E_VAR_CREATE},
    {"E_VAR_SEARCH", SMB_E_VAR_SEARCH},
    {"E_VAR_SET_TYPE", SMB_E_VAR_SET_TYPE},
    {"E_VAR_WORD", SMB_E_VAR_WORD}};

bool engine::call_action(const std::string &name)
{
    auto i = actions.find(name);
    if(i != actions.end())
        return i->second(*this);
    else
        return false;
}

void engine::var_stk_push()
{
    var_stk.push_back(remove_last());
}

bool engine::var_stk_pop()
{
    if(var_stk.empty())
        throw parse_error("variable stack empty", pos);
    code->push_back(var_stk.back());
    var_stk.pop_back();
    return true;
}

//---------------------------------------------------------------------
// Table interpreter, from FastBasic "parser.cc"

static void emit_bytes(engine &s, const std::vector<dcode> &data)
{
    for(auto &c : data)
    {
        switch(c.type)
        {
        case dcode::d_word_sym:
            s.emit_word(c.str);
            break;
        case dcode::d_word_val:
            s.emit_word(c.num);
            break;
        case dcode::d_byte_sym:
            s.emit_byte(c.str);
            break;
        case dcode::d_byte_val:
            s.emit_byte(c.num);
            break;
        case dcode::d_token:
            s.emit_tok(c.str);
            break;
        }
    }
}

static std::string ucase(std::string s)
{
    for(auto &c : s)
        if(c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
    return s;
}

// Returns true if the literal is a keyword (has letters)
static bool is_keyword(const std::string &lit)
{
    for(auto c : lit)
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            return true;
    return false;
}

bool engine::parse_literal(const std::string &lit)
{
    error("'" + ucase(lit) + "'");
    auto start = pos;
    for(auto ch : lit)
    {
        if(ch >= 'a' && ch <= 'z')
        {
            ch = ch - 'a' + 'A';
            if(!expect(ch))
            {
                if(!expect('.'))
                    return false;
                else
                    break;
            }
        }
        else if(!expect(ch))
            return false;
    }
    token t;
    t.kind = is_keyword(lit) ? tk::kw : tk::punct;
    t.lit = lit;
    t.src = str.substr(start, pos - start);
    add_token(t);
    return true;
}

bool engine::parse_line(const std::string &name, const statemachine::line &line)
{
    for(const auto &c : line.pc)
    {
        cur_table = name;
        switch(c.type)
        {
        case statemachine::pcode::c_literal:
            if(!parse_literal(c.str))
                return false;
            break;
        case statemachine::pcode::c_emit:
            emit_bytes(*this, c.data);
            break;
        case statemachine::pcode::c_emit_return:
            emit_bytes(*this, c.data);
            return true;
        case statemachine::pcode::c_call_ext:
            if(!call_action(c.str))
                return false;
            break;
        case statemachine::pcode::c_call_table:
            if(!parse_table(c.str))
                return false;
            break;
        case statemachine::pcode::c_return:
            return true;
        }
    }
    return true;
}

bool engine::parse_table(const std::string &name)
{
    auto smi = sl.sms.find(name);
    if(smi == sl.sms.end())
        throw std::runtime_error("missing syntax table for '" + name + "'");
    const auto &current = *(smi->second);

    check_level();
    skipws();
    error(current.error_text());
    auto spos = save();
    auto old_table = cur_table;

    for(const auto &line : current.get_code())
    {
        if(parse_line(name, line))
        {
            lvl--;
            cur_table = old_table;
            nodes.push_back(node{name, spos.ntok, toks.size(), size_t(lvl)});
            return true;
        }
        restore(spos);
    }
    cur_table = old_table;
    lvl--;
    return false;
}

bool engine::parse_statement(int id)
{
    code_id = id;
    procs.clear();
    toks.clear();
    nodes.clear();
    lvl = 0;
    code = &procs[st.proc_stack.empty() ? std::string() : st.proc_stack.back()];
    cur_table.clear();
    return parse_table("PARSE_START");
}

std::string engine::error_message() const
{
    std::string msg = "parse error";
    if(!saved_errors.empty())
    {
        // Get min level
        auto ml = std::min_element(saved_errors.begin(), saved_errors.end(),
                                   [](auto &a, auto &b) { return a.lvl < b.lvl; });
        msg += ", expected: ";
        bool first = true;
        for(const auto &i : saved_errors)
        {
            if(i.lvl == ml->lvl)
            {
                if(!first)
                    msg += ", ";
                msg += i.msg;
                first = false;
            }
        }
    }
    return msg;
}
