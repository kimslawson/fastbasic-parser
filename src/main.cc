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

// main.cc: fbp command line tool

#include "embedded.h"
#include "listing.h"
#include "optimize.h"
#include "program.h"
#include "verify.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#define FBP_VERSION "0.1.0"

static int verbose = 1;

static std::string fb_version()
{
    for(const embedded_file *f = embedded_files; f->name; f++)
        if(std::string(f->name) == "VERSION")
        {
            std::istringstream is(f->data);
            std::string line;
            while(std::getline(is, line))
                if(line.compare(0, 9, "describe:") == 0)
                {
                    auto p = line.find_first_not_of(" \t", 9);
                    return p == line.npos ? std::string() : line.substr(p);
                }
        }
    return "unknown";
}

static void print_header()
{
    std::cerr << "fbp - The missing parser for FastBasic - version " FBP_VERSION "\n"
                 "Using the FastBasic " << fb_version() << " grammar, by dmsc.\n\n";
}

[[noreturn]] static void cmd_error(const char *prog, const std::string &msg)
{
    if(!msg.empty())
        std::cerr << prog << ": error, " << msg << ".\n";
    std::cerr << "Try '" << prog << " -h' for help.\n";
    std::exit(EXIT_FAILURE);
}

[[noreturn]] static void show_help(const char *prog)
{
    print_header();
    std::cerr
        << "Usage: " << prog << " [options] [-o output] filenames\n"
        << "\n"
           "Options:\n"
           "  -l      Output long (readable) listing, with one statement per line,\n"
           "          indentation and all keywords expanded.\n"
           "  -s      Output short (minimized) listing, with abbreviated keywords,\n"
           "          short variable names and Atari end of lines. (default)\n"
           "  -S      Like '-s', also enables all optimizations that make the\n"
           "          listing shorter, including the ones that change the code.\n"
           "  -n num  In short listing, sets the max line length (default 120).\n"
           "  -f      In short listing, keep the full variable and PROC names.\n"
           "  -e      In short listing, use standard (ASCII) end of lines.\n"
           "  -u      In long listing, write keywords in uppercase.\n"
           "  -O      Optimize the program, an optional argument with '+' or '-'\n"
           "          enables/disables a specific optimization. Use '-O help' for\n"
           "          a list of all available options.\n"
           "  -t tgt  Selects the FastBasic target (default 'default', same as the\n"
           "          compiler), use '-t help' for a list.\n"
           "  -v      Shows more information (verbose mode).\n"
           "  -q      Don't show any information, only errors (quiet mode).\n"
           "  -o out  Sets the output file name, or extension if starts with a dot.\n"
           "  -c      Output to standard output instead of a file.\n"
           "  -h      Shows help and exit.\n";
    std::exit(EXIT_FAILURE);
}

static std::string out_filename(const std::string &in, const std::string &output,
                                const std::string &ext)
{
    if(!output.empty())
        return output;
    auto out = in;
    auto slash = out.find_last_of("/\\");
    auto dot = out.rfind('.');
    if(dot != out.npos && (slash == out.npos || dot > slash))
        out.resize(dot);
    return out + ext;
}

static bool same_file(const std::string &a, const std::string &b)
{
    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec);
}

// Verifies the output, returns an error message on failure
static std::string verify_output(const grammar &g, const program &p,
                                 const std::string &in, const std::string &text,
                                 const list_stats &st, bool optimized)
{
    // If optimizations that change the code were applied, verify the text
    // before those against the original, and the output against that text
    // allowing only the substitutions done.
    bool subst = !st.pre_text.empty();
    program q;
    auto err = q.parse_text(g, subst ? st.pre_text : text, in, false);
    if(!err.empty())
        return "the output can't be parsed:\n" + err;
    if(!code_equal(p.full_code(optimized), q.full_code(optimized), st.names))
        return "the output does not produce the same code.";
    if(subst)
    {
        program r;
        err = r.parse_text(g, text, in, false);
        if(!err.empty())
            return "the output can't be parsed:\n" + err;
        if(!code_equal_subst(q.full_code(false), r.full_code(false, st.init_stmts),
                             st.cvars))
            return "the output does not produce equivalent code.";
    }
    return std::string();
}

enum class out_type
{
    longlist,
    shortlist
};

int main(int argc, char **argv)
{
    const char *prog = "fbp";
    out_type mode = out_type::shortlist;
    list_options lopt;
    opt_settings opts;
    std::string output, extension, target = "default";
    std::vector<std::string> files;

    // Parse options, similar to getopt
    for(int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if(arg.size() < 2 || arg[0] != '-')
        {
            files.push_back(arg);
            continue;
        }
        if(arg == "--")
        {
            for(i++; i < argc; i++)
                files.push_back(argv[i]);
            break;
        }
        for(size_t j = 1; j < arg.size(); j++)
        {
            char c = arg[j];
            // Options with arguments
            if(c == 'n' || c == 'o' || c == 't')
            {
                std::string val;
                if(j + 1 < arg.size())
                    val = arg.substr(j + 1);
                else if(i + 1 < argc)
                    val = argv[++i];
                else
                    cmd_error(prog, std::string("option '-") + c + "' needs an argument");
                if(c == 'n')
                {
                    char *end;
                    long n = std::strtol(val.c_str(), &end, 10);
                    if(*end || n < 16 || n > 1024)
                        cmd_error(prog, "maximum line length invalid, must be from 16 "
                                        "to 1024");
                    lopt.max_line = n;
                }
                else if(c == 'o')
                {
                    if(val[0] == '.')
                        extension = val;
                    else
                        output = val;
                }
                else
                    target = val;
                break;
            }
            switch(c)
            {
            case 'l':
                mode = out_type::longlist;
                break;
            case 's':
                mode = out_type::shortlist;
                break;
            case 'S':
                mode = out_type::shortlist;
                opts.set_shorter();
                break;
            case 'f':
                lopt.full_names = true;
                break;
            case 'e':
                lopt.ascii_eol = true;
                break;
            case 'u':
                lopt.upper = true;
                break;
            case 'v':
                verbose++;
                break;
            case 'q':
                verbose = 0;
                break;
            case 'c':
                output = "-";
                break;
            case 'h':
                show_help(prog);
            case 'O':
                if(j + 1 == arg.size() && i + 1 < argc)
                {
                    std::string o = argv[i + 1];
                    if(o == "help")
                    {
                        print_header();
                        opt_list(std::cerr);
                        return EXIT_FAILURE;
                    }
                    if(opts.apply(o))
                        i++;
                    else if(o[0] == '+')
                        cmd_error(prog, "invalid optimization option '" + o.substr(1) +
                                            "', use '-O help'");
                    else
                        opts.set_default();
                }
                else
                    opts.set_default();
                break;
            default:
                cmd_error(prog, std::string("invalid option '-") + c + "'");
            }
        }
    }

    if(target == "help")
    {
        print_header();
        std::cerr << "Available targets:\n";
        for(auto &t : grammar::targets())
            std::cerr << "  " << t << "\n";
        return EXIT_FAILURE;
    }

    if(files.empty())
        cmd_error(prog, "expected at least one input file");
    if(!output.empty() && output != "-" && files.size() != 1)
        cmd_error(prog, "when setting output file, only one input file should be supplied");
    if(!output.empty() && !extension.empty())
        cmd_error(prog, "only one of output file name or extension should be supplied");
    if(extension.empty())
        extension = ".lst";
    if(mode == out_type::longlist && opts.any() && verbose)
        std::cerr << prog << ": note, optimizations only apply to the short listing.\n";
    if(mode == out_type::shortlist && lopt.max_line > 255 && verbose)
        std::cerr << prog << ": warning, lines longer than 255 characters can't be "
                             "edited in the FastBasic IDE.\n";

    lopt.verbose = verbose;
    lopt.opts = &opts;

    grammar g;
    try
    {
        g.load(target);
    }
    catch(std::exception &e)
    {
        std::cerr << prog << ": " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    if(verbose > 1)
        print_header();

    bool all_ok = true;
    for(auto &in : files)
    {
        auto outname = out_filename(in, output, extension);
        if(same_file(in, outname))
        {
            std::cerr << in << ": error, output file '" << outname
                      << "' is the same as input.\n";
            return EXIT_FAILURE;
        }
        if(verbose)
            std::cerr << in << ": parsing to '" << outname << "'\n";

        program p;
        if(!p.parse_file(g, in))
        {
            all_ok = false;
            continue;
        }

        // Write the listing to memory, so we can verify it
        std::ostringstream os;
        list_stats st;
        bool ok;
        if(mode == out_type::longlist)
            ok = list_long(os, g, p, lopt, st);
        else
            ok = list_short(os, g, p, lopt, st);
        if(!ok)
        {
            all_ok = false;
            continue;
        }
        auto text = os.str();

        // Verify the complete output
        bool optimized = mode == out_type::shortlist && opts.any();
        auto verify_err = verify_output(g, p, in, text, st, optimized);
        if(!verify_err.empty())
        {
            std::cerr << in << ": internal error, " << verify_err << "\n";
            all_ok = false;
        }
        else if(verbose > 1)
            std::cerr << in << ": verified, the output compiles to the same code"
                      << (optimized ? " (after the FastBasic optimizer)" : "")
                      << (st.cvars.empty() ? ".\n" : ", with constants replaced.\n");

        // Write the output
        if(outname == "-")
            std::cout.write(text.data(), text.size());
        else
        {
            std::ofstream f(outname, std::ios::binary);
            if(!f || !f.write(text.data(), text.size()))
            {
                std::cerr << outname << ": error writing file: " << std::strerror(errno)
                          << "\n";
                return EXIT_FAILURE;
            }
        }
        if(verbose)
        {
            std::cerr << in << ": " << st.lines << " lines, " << st.bytes
                      << " bytes, longest line " << st.max_len << " characters.\n";
        }
    }
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
