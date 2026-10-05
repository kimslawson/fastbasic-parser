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

// rename.h: Assigns short names to variables and labels
#pragma once

#include "program.h"
#include <map>
#include <set>
#include <string>
#include <vector>

struct symbol_info
{
    std::string name;    // Original name (uppercase)
    std::string spell;   // Spelling at first use
    std::string new_name;
    int count = 0;       // Number of uses
    int first = 0;       // Order of first use
    bool is_label = false;
    bool is_proc = false;
    int type = 0;        // Variable type
    bool no_share = false; // Its name can't be shared between a variable and a label
};

class renamer
{
  public:
    // All symbols, in order of first appearance
    std::vector<symbol_info> symbols;
    // orig -> new
    std::map<std::string, std::string> vars, labels;
    // A DATA or DLI name shares the name of a variable
    bool shared = false;

    // Collects the symbols in the program
    explicit renamer(const program &p);
    // Assigns short names, "reserved" are names that can't be used. With
    // "share_labels", DATA and DLI names can reuse the names of variables.
    void assign_short(const std::set<std::string> &reserved, bool share_labels = true,
                      const std::set<std::string> &extra_used = {});
    // Keeps the original names
    void assign_same();
    // Returns a new unused short name for a variable, or empty if none.
    std::string new_var_name(const std::set<std::string> &reserved) const;
};

// Generates the n-th short name: A to Z, _, then two-character names
std::string short_name(int n);
