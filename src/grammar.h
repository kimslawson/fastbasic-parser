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

// grammar.h: loads the FastBasic grammar for a compiler target.
#pragma once

#include "synt-sm-list.h"
#include <string>
#include <vector>

class grammar
{
  public:
    syntax::sm_list sl;
    std::string target;
    std::vector<std::string> syntax_files;

    // Loads the syntax tables for the given target name (as in the
    // FastBasic "-t:" option), throws std::runtime_error on errors.
    void load(const std::string &target_name);

    // Lists all targets available
    static std::vector<std::string> targets();

    // Returns true if the grammar has the given syntax table
    bool has_table(const std::string &name) const;
};
