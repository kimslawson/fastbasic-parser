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

// optimize.h: Optimization options
#pragma once

#include <ostream>
#include <string>

enum opt_id
{
    // Rewrites that produce the same compiled code, enabled by "-O"
    OPT_PARENS,
    OPT_PRINT_SEP,
    OPT_NEXT_VAR,
    OPT_DEFAULTS,
    OPT_CONST_FOLD,
    OPT_CMP_ZERO,
    OPT_INC_DEC,
    OPT_IF_THEN,
    OPT_END,
    // Trade-offs, only enabled explicitly or by "-S"
    OPT_CONST_REPLACE,
    OPT_COUNT
};

struct opt_info
{
    const char *name;
    // Produces the same compiled code, enabled by plain "-O"
    bool same_code;
    // Makes the listing shorter, enabled by "-S"
    bool shorter;
    const char *desc;
};

struct opt_settings
{
    bool on[OPT_COUNT] = {};

    bool operator[](opt_id i) const { return on[i]; }
    bool any() const;
    // Enables all "same code" optimizations
    void set_default();
    // Enables all optimizations that make the listing shorter
    void set_shorter();
    // Applies an option from the command line, "+name" or "name" enables,
    // "-name" disables. Returns false if the name is invalid.
    bool apply(const std::string &arg);
};

const opt_info &opt_get(int id);
// Returns the id of the named optimization or -1
int opt_find(const std::string &name);
// Lists all optimizations
void opt_list(std::ostream &os);
