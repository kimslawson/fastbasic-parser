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

// optimize.cc: Optimization options

#include "optimize.h"

static const opt_info opts[OPT_COUNT] = {
    {"parens", true, true,
     "Remove parenthesis that are not needed, including the ones around\n"
     "function arguments: 'PEEK(712)' becomes 'P.712'."},
    {"print_sep", true, true,
     "Remove ';' separators in PRINT when not needed: '? \"A=\";A' becomes\n"
     "'?\"A=\"A'."},
    {"print_join", true, true,
     "Join constant strings and CHR$ in PRINT: '? \"A\";CHR$(66)' becomes\n"
     "'?\"AB\"', the FastBasic optimizer does the same in the compiled code."},
    {"next_var", true, true,
     "Remove the variable name after NEXT, FastBasic does not use it."},
    {"defaults", true, true,
     "Remove default values: 'STEP 1' in FOR, '0' in PAUSE and the 'WORD'\n"
     "type in DIM and DATA."},
    {"const_fold", true, true,
     "Replace integer operations on constants with the result: 'POKE 704+4,0'\n"
     "becomes 'POKE 708,0'."},
    {"cmp_zero", true, true,
     "Remove comparisons with zero in conditions: 'IF X<>0' becomes 'IF X'."},
    {"if_then", true, true,
     "Replace IF / ENDIF blocks with only one statement with IF / THEN."},
    {"end", true, true, "Remove the END statement at the end of the program."},
    {"inc_dec", false, true,
     "Replace 'X=X+1' with 'INC X' and 'X=X-1' with 'DEC X', also on array\n"
     "elements. The compiled code is smaller and faster, as the FastBasic\n"
     "optimizer does not do this."},
    {"const_replace", false, true,
     "Replace numeric constants used many times with a new variable, this\n"
     "makes the listing shorter but the compiled code a little bigger and\n"
     "slower. Only applies to the short listing."},
    {"chr_str", false, true,
     "Replace 'CHR$(n)' with a string containing the character, this makes\n"
     "the listing shorter but the compiled code a little bigger. Only\n"
     "applies to the short listing."},
};

const opt_info &opt_get(int id)
{
    return opts[id];
}

int opt_find(const std::string &name)
{
    for(int i = 0; i < OPT_COUNT; i++)
        if(name == opts[i].name)
            return i;
    return -1;
}

bool opt_settings::any() const
{
    for(int i = 0; i < OPT_COUNT; i++)
        if(on[i])
            return true;
    return false;
}

void opt_settings::set_default()
{
    for(int i = 0; i < OPT_COUNT; i++)
        if(opts[i].same_code)
            on[i] = true;
}

void opt_settings::set_shorter()
{
    for(int i = 0; i < OPT_COUNT; i++)
        if(opts[i].shorter)
            on[i] = true;
}

bool opt_settings::apply(const std::string &arg)
{
    bool set = true;
    std::string name = arg;
    if(!name.empty() && (name[0] == '+' || name[0] == '-'))
    {
        set = name[0] == '+';
        name = name.substr(1);
    }
    int id = opt_find(name);
    if(id < 0)
        return false;
    on[id] = set;
    return true;
}

void opt_list(std::ostream &os)
{
    os << "Optimizations enabled with plain '-O', the compiled code is the same:\n\n";
    for(int i = 0; i < OPT_COUNT; i++)
        if(opts[i].same_code)
        {
            os << "  " << opts[i].name << "\n    ";
            for(const char *c = opts[i].desc; *c; c++)
                os << (*c == '\n' ? "\n    " : std::string(1, *c));
            os << "\n";
        }
    os << "\nOptimizations that change the compiled code, enable with '-O +name'\n"
          "(all are enabled with '-S'), the result is verified to be equivalent:\n\n";
    for(int i = 0; i < OPT_COUNT; i++)
        if(!opts[i].same_code)
        {
            os << "  " << opts[i].name << "\n    ";
            for(const char *c = opts[i].desc; *c; c++)
                os << (*c == '\n' ? "\n    " : std::string(1, *c));
            os << "\n";
        }
}
