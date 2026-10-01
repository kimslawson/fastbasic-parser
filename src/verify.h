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

// verify.h: Checks that transformed source code compiles to the same
//           bytecode as the original.
#pragma once

#include "program.h"
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

// Maps new variable and label names back to the original names
struct name_map
{
    std::map<std::string, std::string> var;   // new name -> original name
    std::map<std::string, std::string> label; // new name -> original name
    std::map<std::string, std::string> var_new;   // original name -> new name
    std::map<std::string, std::string> label_new; // original name -> new name
    std::string var_orig(const std::string &n) const;
    std::string label_orig(const std::string &n) const;
    // Adds a renamed variable or label
    void add_var(const std::string &orig, const std::string &n);
    void add_label(const std::string &orig, const std::string &n);
    // Translates a parser state to the new names, removing unused variables
    engine_state translate(const engine_state &st, const std::set<std::string> &unused,
                           const std::set<std::string> &unused_labels) const;
};

// How to compare the code
enum class verify_mode
{
    raw,      // Identical bytecode as produced by the parser
    optimized // Identical bytecode after the FastBasic peephole optimizer
};

// Compares two code maps
bool code_equal(const code_map &orig, const code_map &cand, const name_map &m,
                verify_mode mode);
// Compares two code vectors
bool code_equal(const std::vector<codew> &orig, const std::vector<codew> &cand,
                const name_map &m);
// Compares two parser states, "optional" are variables in the original that
// can be missing in the candidate (unused variables).
bool state_equal(const engine_state &orig, const engine_state &cand, const name_map &m,
                 const std::set<std::string> *optional = nullptr,
                 const std::set<std::string> *optional_labels = nullptr);

// Returns the variables never referenced in the program, those are created
// by the parser when trying alternative parsings.
std::set<std::string> phantom_vars(const program &p);
std::set<std::string> phantom_labels(const program &p);

// Size in bytes of the bytecode
int code_size(const code_map &code);

// Constants replaced by variables: variable name -> value, the value is
// "N" followed by the number or "S" followed by the string bytes.
typedef std::map<std::string, std::string> const_vars;

// Compares two code maps allowing substitutions done by the optimizations
// that change the code: constants replaced by variables in "cand", and
// CHR$(n) replaced by a constant string. Compares the raw code.
bool code_equal_subst(const code_map &orig, const code_map &cand, const const_vars &cv);
bool code_equal_subst(const std::vector<codew> &orig, const std::vector<codew> &cand,
                      const const_vars &cv);

class verifier
{
    const grammar &g;
    const program &p;

  public:
    const name_map *names;
    // Unused variables in the original program
    std::set<std::string> phantoms, phantom_lbls;
    // Show verification attempts
    bool debug = std::getenv("FBP_DEBUG") != nullptr;
    // Number of statement checks performed
    mutable long checks = 0;

    verifier(const grammar &g, const program &p, const name_map *names)
        : g(g), p(p), names(names), phantoms(phantom_vars(p)),
          phantom_lbls(phantom_labels(p))
    {
    }
    // Parses the given statement texts starting from the state before
    // statement "first", returns true if the result is equivalent to the
    // original statements [first, last).
    bool check(size_t first, size_t last, const std::vector<std::string> &texts,
               verify_mode mode) const;
    // Checks that "after" is equivalent to "before", both parsed from the
    // state before statement "first" with the given constant variables
    // already defined.
    bool check_subst(size_t first, const std::string &before, const std::string &after,
                     const const_vars &cv) const;
    // Parses one statement text from a given state, returns false on error.
    bool parse(const engine_state &start, const std::string &text, engine_state &end,
               code_map &code, std::vector<token> *toks = nullptr,
               std::vector<node> *nodes = nullptr) const;
};
