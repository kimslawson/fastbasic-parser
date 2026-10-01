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

// listing.cc: Utility functions for the listing writers

#include "listing.h"
#include "synt-sm-list.h"
#include <algorithm>
#include <cmath>

std::string ucase(std::string s)
{
    for(auto &c : s)
        if(c >= 'a' && c <= 'z')
            c = c - 'a' + 'A';
    return s;
}

std::string lcase(std::string s)
{
    for(auto &c : s)
        if(c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
    return s;
}

std::string kw_full(const std::string &lit)
{
    return ucase(lit);
}

std::string kw_abbrev(const std::string &lit)
{
    // Same as the FastBasic parser: the uppercase letters are needed, the
    // rest can be replaced with a dot.
    std::string ret;
    for(auto c : lit)
    {
        if(c >= 'a' && c <= 'z')
        {
            ret += '.';
            break;
        }
        ret += c;
    }
    return ret;
}

static char hexd(int c)
{
    return "0123456789ABCDEF"[15 & c];
}

std::string str_long(const std::string &s)
{
    std::string ret = "\"";
    bool in = true;
    for(auto ch : s)
    {
        unsigned c = static_cast<unsigned char>(ch);
        bool special = c < 32 || c > 126;
        if(in)
        {
            if(c == '"')
                ret += "\"\"";
            else if(special)
            {
                in = false;
                ret += std::string("\"$") + hexd(c >> 4) + hexd(c);
            }
            else
                ret += ch;
        }
        else
        {
            if(special || c == '"')
                ret += std::string("$") + hexd(c >> 4) + hexd(c);
            else
            {
                in = true;
                ret += '"';
                ret += ch;
            }
        }
    }
    if(in)
        ret += '"';
    return ret;
}

std::string str_short(const std::string &s)
{
    std::string ret = "\"";
    bool in = true;
    for(auto ch : s)
    {
        unsigned c = static_cast<unsigned char>(ch);
        if(in)
        {
            if(c == '"')
                ret += "\"\"";
            else if(c == 155)
            {
                in = false;
                ret += std::string("\"$") + hexd(c >> 4) + hexd(c);
            }
            else
                ret += ch;
        }
        else
        {
            if(c == 155 || c == '"')
                ret += std::string("$") + hexd(c >> 4) + hexd(c);
            else
            {
                in = true;
                ret += '"';
                ret += ch;
            }
        }
    }
    if(in)
        ret += '"';
    return ret;
}

std::string num_long(const token &t)
{
    if(t.kind == tk::fpnum)
        return ucase(t.src);
    if(!t.src.empty() && t.src[0] == '$')
        return ucase(t.src);
    return t.src;
}

// Parses a floating point number like the FastBasic parser, returns false
// if the whole string is not a number.
static bool parse_fp(const std::string &s, atari_fp &ret)
{
    size_t pos = 0;
    bool sign = false;
    if(pos < s.size() && s[pos] == '-')
    {
        sign = true;
        pos++;
    }
    bool ok = false;
    double num = 0;
    int dot = -1, norm = 0;
    while(pos < s.size())
    {
        char c = s[pos];
        if(c >= '0' && c <= '9')
        {
            num = num * 10 + (c - '0');
            if(num > 1e30)
            {
                num = num / 1000;
                norm += 3;
            }
            ok = true;
        }
        else if(c == '.')
            dot = pos + 1;
        else
            break;
        pos++;
    }
    if(!ok)
        return false;
    dot = dot >= 0 ? pos - dot : 0;
    int exp = 0;
    if(pos < s.size() && (s[pos] == 'E' || s[pos] == 'e'))
    {
        pos++;
        bool esign = false;
        if(pos < s.size() && (s[pos] == '-' || s[pos] == '+'))
            esign = s[pos++] == '-';
        if(pos >= s.size() || s[pos] < '0' || s[pos] > '9')
            return false;
        exp = s[pos++] - '0';
        if(pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
            exp = exp * 10 + s[pos++] - '0';
        if(esign)
            exp = -exp;
    }
    if(pos != s.size())
        return false;
    num = num * std::pow(10, exp - dot + norm);
    ret = atari_fp(sign ? -num : num);
    return ret.valid();
}

static std::vector<std::string> fp_short(const token &t)
{
    atari_fp x = t.fp;
    std::string bcd = x.to_asm();
    std::vector<std::string> cand;
    auto add = [&](const std::string &s)
    {
        atari_fp y;
        if(!s.empty() && parse_fp(s, y) && y.to_asm() == bcd &&
           std::find(cand.begin(), cand.end(), s) == cand.end())
            cand.push_back(s);
    };

    std::string s = x.to_string();
    // Split into sign, digits and exponent (value = digits * 10^exp)
    std::string sign, digits;
    int exp = 0;
    {
        size_t i = 0;
        if(i < s.size() && s[i] == '-')
            sign = "-", i++;
        int dotpos = -1;
        for(; i < s.size() && s[i] != 'E'; i++)
        {
            if(s[i] == '.')
                dotpos = digits.size();
            else
                digits += s[i];
        }
        if(i < s.size())
            exp = std::stoi(s.substr(i + 1));
        if(dotpos >= 0)
            exp -= digits.size() - dotpos;
        // Normalize: remove leading and trailing zeros
        while(digits.size() > 1 && digits[0] == '0')
            digits.erase(0, 1);
        while(digits.size() > 1 && digits.back() == '0')
        {
            digits.pop_back();
            exp++;
        }
    }
    if(digits == "0")
    {
        add("0.");
        add(".0");
        add("0");
    }
    else
    {
        int nd = digits.size();
        // Plain decimal forms
        if(exp >= 0)
        {
            std::string p = sign + digits + std::string(exp, '0');
            add(p);
            add(p + ".");
        }
        else if(-exp < nd)
            add(sign + digits.substr(0, nd + exp) + "." + digits.substr(nd + exp));
        else
            add(sign + "." + std::string(-exp - nd, '0') + digits);
        // Exponent forms
        add(sign + digits + "E" + std::to_string(exp));
        if(nd > 1)
            add(sign + digits.substr(0, 1) + "." + digits.substr(1) + "E" +
                std::to_string(exp + nd - 1));
    }
    add(s);
    // Shortest first, prefer numbers with a dot (unambiguously floating point)
    std::stable_sort(cand.begin(), cand.end(),
                     [](const std::string &a, const std::string &b)
                     {
                         if(a.size() != b.size())
                             return a.size() < b.size();
                         bool da = a.find_first_of(".E") != a.npos;
                         bool db = b.find_first_of(".E") != b.npos;
                         return da && !db;
                     });
    // The original text is always valid
    auto src = ucase(t.src);
    if(std::find(cand.begin(), cand.end(), src) == cand.end())
        cand.push_back(src);
    return cand;
}

std::vector<std::string> num_short(const token &t)
{
    if(t.kind == tk::fpnum)
        return fp_short(t);
    std::vector<std::string> cand;
    unsigned v = t.value & 0xFFFF;
    cand.push_back(std::to_string(v));
    if(!t.byte && v > 0)
    {
        auto neg = "-" + std::to_string(65536 - v);
        if(neg.size() < cand[0].size())
            cand.insert(cand.begin(), neg);
        else
            cand.push_back(neg);
    }
    auto src = ucase(t.src);
    if(std::find(cand.begin(), cand.end(), src) == cand.end())
        cand.push_back(src);
    return cand;
}

int outer_node_at(const std::vector<node> &nodes, size_t idx)
{
    int best = -1;
    for(size_t i = 0; i < nodes.size(); i++)
    {
        auto &n = nodes[i];
        if(n.tbeg != idx || n.tend <= n.tbeg)
            continue;
        if(best < 0 || n.tend > nodes[best].tend ||
           (n.tend == nodes[best].tend && n.level < nodes[best].level))
            best = i;
    }
    return best;
}

int function_arg(const std::vector<node> &nodes, size_t idx)
{
    int n = outer_node_at(nodes, idx + 1);
    if(n < 0)
        return -1;
    auto is_arg = [](const std::string &t)
    { return t == "T_EXPR" || t == "FP_T_EXPR" || t == "STR_EXPR"; };
    if(is_arg(nodes[n].table))
        return n;
    // PADDLE, STICK, PTRIG and STRIG have the argument inside "RD_PORT"
    if(nodes[n].table == "RD_PORT")
        for(size_t i = 0; i < nodes.size(); i++)
            if(nodes[i].tbeg == nodes[n].tbeg && nodes[i].tend == nodes[n].tend &&
               is_arg(nodes[i].table))
                return i;
    return -1;
}

static bool opens_paren(const token &t)
{
    return (t.kind == tk::punct || t.kind == tk::kw) && !t.lit.empty() &&
           t.lit.back() == '(';
}

static bool closes_paren(const token &t)
{
    return t.kind == tk::punct && t.lit == ")";
}

bool is_wrapped(const std::vector<token> &toks, size_t b, size_t e)
{
    if(e <= b + 1 || toks[b].kind != tk::punct || toks[b].lit != "(" ||
       !closes_paren(toks[e - 1]))
        return false;
    int depth = 0;
    for(size_t i = b; i < e; i++)
    {
        if(opens_paren(toks[i]))
            depth++;
        else if(closes_paren(toks[i]))
            depth--;
        if(depth == 0 && i + 1 < e)
            return false;
    }
    return depth == 0;
}

std::set<std::string> grammar_keywords(const grammar &g)
{
    std::set<std::string> ret;
    for(auto &sm : g.sl.sms)
        for(auto &line : sm.second->get_code())
            for(auto &pc : line.pc)
                if(pc.type == syntax::statemachine::pcode::c_literal)
                {
                    ret.insert(ucase(pc.str));
                    auto a = kw_abbrev(pc.str);
                    if(!a.empty() && a.back() == '.')
                        ret.insert(a.substr(0, a.size() - 1));
                }
    return ret;
}
