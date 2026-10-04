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

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

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
           "  -a      Annotated listing: the long listing, with the comments of the\n"
           "          source, but with the names of the short listing, a list of\n"
           "          the renamed ones, and a comment marking where each of its\n"
           "          lines starts. The short listing options apply (-n, -f, -O).\n"
           "  -C fb   Compile the input and the output with the FastBasic compiler\n"
           "          'fb' and check that the binaries are identical. Also\n"
           "          '--compiler fb'.\n"
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
        // With fixed_vars, the constants are in the output; with
        // const_replace, the variables are in the output.
        bool eq = st.reverse_subst
                      ? code_equal_subst(r.full_code(false), q.full_code(false), st.cvars)
                      : code_equal_subst(q.full_code(false), r.full_code(false, st.init_stmts),
                                         st.cvars);
        if(!eq)
            return "the output does not produce equivalent code.";
    }
    return std::string();
}

enum class out_type
{
    longlist,
    shortlist,
    annotated
};

static std::string read_file(const std::string &name)
{
    std::ifstream f(name, std::ios::binary);
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

#ifdef _WIN32
static int process_id() { return _getpid(); }
#else
static int process_id() { return getpid(); }
#endif

// Quotes an argument for the shell that runs std::system()
static std::string quote(const std::string &s)
{
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string r = "'";
    for(char c : s)
        r += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return r + "'";
#endif
}

// Returns true if a path can be quoted safely for the shell. In Windows
// "cmd" expands "%" even inside quotes.
static bool can_quote(const std::string &s)
{
#ifdef _WIN32
    return s.find_first_of("\"%") == s.npos;
#else
    (void)s;
    return true;
#endif
}

static bool write_file(const std::string &name, const std::string &text)
{
    std::ofstream f(name, std::ios::binary);
    return f && f.write(text.data(), text.size());
}

// Extensions of the files the compiler can make
static const char *const compile_exts[] = {".bas", ".asm", ".o",   ".xex", ".rom",
                                           ".bin", ".log", ".lbl", ".lst"};

static void remove_files(const std::string &base)
{
    std::error_code ec;
    for(auto ext : compile_exts)
        std::filesystem::remove(base + ext, ec);
}

// Returns a base name in "dir" for the files of one check, "a" and "b"
// added, that no existing file uses.
static std::string check_base(const std::filesystem::path &dir)
{
    for(int n = 0;; n++)
    {
        auto base = (dir / ("fbp_check_" + std::to_string(process_id()) + "_" +
                            std::to_string(n)))
                        .string();
        bool used = false;
        std::error_code ec;
        for(auto sfx : {"a", "b"})
            for(auto ext : compile_exts)
                used = used || std::filesystem::exists(base + sfx + ext, ec);
        if(!used)
            return base;
    }
}

// Compiles "base.bas" with the FastBasic compiler "fb". Returns the binary in
// "bin", or an error message. Removes all the files.
static std::string fb_compile(const std::string &fb, const std::string &target,
                              const std::string &base, std::string &bin)
{
    namespace fs = std::filesystem;
    auto path = [&](const char *ext) { return base + ext; };
    auto cmd = quote(fb) + " " + quote("-t:" + target) + " " + quote(path(".bas")) + " > " +
               quote(path(".log")) + " 2>&1";
#ifdef _WIN32
    cmd = "\"" + cmd + "\"";
#endif
    int r = std::system(cmd.c_str());
    std::string err;
    bin.clear();
    for(auto ext : {".xex", ".rom", ".bin"})
        if(fs::exists(path(ext)))
            bin = read_file(path(ext));
    if(r != 0 || bin.empty())
    {
        err = read_file(path(".log"));
        while(!err.empty() && (err.back() == '\n' || err.back() == '\r'))
            err.pop_back();
        err = "the compiler failed" + (err.empty() ? std::string() : ":\n" + err);
    }
    remove_files(base);
    return err;
}

// Compiles the original and the output with the given compiler and checks
// that the binaries are the same. Returns an error message, or empty.
static std::string compiler_check(const std::string &fb, const std::string &target,
                                  const program &p, const std::string &in,
                                  const std::string &text, int verbose)
{
    namespace fs = std::filesystem;
    // Compile next to the input, so DATA ... FILE finds its files; if that
    // folder can't be written, use the temporary folder.
    std::error_code ec;
    std::string original = read_file(in), base;
    for(auto dir : {fs::absolute(in, ec).parent_path(), fs::temp_directory_path(ec)})
    {
        if(dir.empty() || !can_quote(dir.string()))
            continue;
        base = check_base(dir);
        if(write_file(base + "a.bas", original) && write_file(base + "b.bas", text))
            break;
        remove_files(base + "a");
        remove_files(base + "b");
        base.clear();
    }
    if(base.empty())
        return "can't write the files to compile";
    std::string b1, b2;
    auto err = fb_compile(fb, target, base + "a", b1);
    if(!err.empty())
    {
        remove_files(base + "b");
        return "compiling the original with '" + fb + "': " + err;
    }
    err = fb_compile(fb, target, base + "b", b2);
    if(!err.empty())
        return "compiling the output with '" + fb + "': " + err;
    if(b1 == b2)
    {
        if(verbose > 0)
            std::cerr << in << ": checked with '" << fb << "', the binaries are identical ("
                      << b1.size() << " bytes).\n";
        return std::string();
    }
    std::string msg = "the output compiled with '" + fb + "' is not identical to the original";
    auto ph = phantom_vars(p);
    if(!ph.empty())
    {
        msg += " (the original creates " + std::to_string(ph.size()) +
               " unused variables while parsing, like '" + *ph.begin() +
               "', the output does not, so the variable area can differ)";
    }
    return msg + ".";
}

int main(int argc, char **argv)
{
    const char *prog = "fbp";
    out_type mode = out_type::shortlist;
    list_options lopt;
    opt_settings opts;
    std::string output, extension, target = "default", compiler;
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
        if(arg.compare(0, 10, "--compiler") == 0)
        {
            if(arg.size() > 11 && arg[10] == '=')
                compiler = arg.substr(11);
            else if(arg.size() == 10 && i + 1 < argc)
                compiler = argv[++i];
            else
                cmd_error(prog, "option '--compiler' needs an argument");
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
            if(c == 'n' || c == 'o' || c == 't' || c == 'C')
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
                else if(c == 'C')
                    compiler = val;
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
            case 'a':
                mode = out_type::annotated;
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

        // Note for programmers used to TurboBasic XL
        if(verbose > 1)
            for(size_t i = 0; i + 1 < p.stmts.size(); i++)
            {
                bool then = false;
                for(auto &t : p.stmts[i].toks)
                    then = then || (t.kind == tk::kw && t.lit == "Then" &&
                                    t.table == "THEN_OR_MULTILINE");
                auto &n = p.stmts[i + 1];
                if(then && !n.line_start && !n.empty() && !n.is_comment())
                    std::cerr << in << ":" << n.line
                              << ": note, statements after 'IF ... THEN statement:' are "
                                 "not conditional in FastBasic.\n";
            }

        // Write the listing to memory, so we can verify it
        std::ostringstream os;
        list_stats st;
        bool ok;
        if(mode == out_type::longlist)
            ok = list_long(os, g, p, lopt, st);
        else if(mode == out_type::shortlist)
            ok = list_short(os, g, p, lopt, st);
        else
        {
            // Annotated: make the short listing first, for its names and lines
            std::ostringstream ss;
            list_stats sst;
            list_options so = lopt;
            so.verbose = verbose > 1 ? verbose : 0;
            ok = list_short(ss, g, p, so, sst);
            if(ok)
            {
                list_options lo = lopt;
                lo.rename = &sst.names;
                int nl = sst.line_first.size();
                for(int k = 0; k < nl; k++)
                    if(sst.line_first[k] != SIZE_MAX)
                        lo.marks[sst.line_first[k]] =
                            "' ==== line " + std::to_string(k + 1) + " of " +
                            std::to_string(nl) + " (" + std::to_string(sst.line_len[k]) +
                            " characters) ====";
                // The long listing does not apply code-changing optimizations
                opt_settings lopts = opts;
                lopts.unset_code_changing();
                lo.opts = &lopts;
                ok = list_long(os, g, p, lo, st);
                st.names = sst.names;
                if(ok && verbose)
                    std::cerr << in << ": annotated listing of " << nl << " lines, "
                              << sst.bytes - nl << " characters.\n";
            }
        }
        if(!ok)
        {
            all_ok = false;
            continue;
        }
        auto text = os.str();

        // Verify the complete output
        bool optimized = opts.any();
        auto verify_err = verify_output(g, p, in, text, st, optimized);
        // And with the real compiler, if given. With optimizations that change
        // the code, the listing before those is the one that must be identical.
        if(verify_err.empty() && !compiler.empty())
        {
            auto same = st.pre_text.empty() ? text : st.pre_text;
            auto err = compiler_check(compiler, target, p, in, same, verbose);
            if(!err.empty())
            {
                std::cerr << in << ": error, " << err << "\n";
                all_ok = false;
            }
            else if(!st.pre_text.empty() && verbose > 0)
                std::cerr << in << ": note, the final output differs from that only by "
                             "the code-changing optimizations, verified by fbp.\n";
        }
        if(!verify_err.empty())
        {
            std::cerr << in << ": internal error, " << verify_err << "\n";
            all_ok = false;
        }
        else if(verbose > 1)
        {
            if(st.pre_text.empty())
                std::cerr << in << ": verified, the output compiles to the same code"
                          << (optimized ? " (after the FastBasic optimizer).\n" : ".\n");
            else
                std::cerr << in << ": verified, the output compiles to equivalent code.\n";
        }

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
