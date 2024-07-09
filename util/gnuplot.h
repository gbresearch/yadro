//-----------------------------------------------------------------------------
//  Copyright (C) 2011-2023, Gene Bushuyev
//  
//  Boost Software License - Version 1.0 - August 17th, 2003
//
//  Permission is hereby granted, free of charge, to any person or organization
//  obtaining a copy of the software and accompanying documentation covered by
//  this license (the "Software") to use, reproduce, display, distribute,
//  execute, and transmit the Software, and to prepare derivative works of the
//  Software, and to permit third-parties to whom the Software is furnished to
//  do so, all subject to the following:
//
//  The copyright notices in the Software and this entire statement, including
//  the above license grant, this restriction and the following disclaimer,
//  must be included in all copies of the Software, in whole or in part, and
//  all derivative works of the Software, unless such copies or derivative
//  works are solely in the form of machine-executable object code generated by
//  a source language processor.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
//  SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
//  FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
//  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#pragma once

#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <format>
#include <cstdlib>
#include <memory>
#include <ranges>
#include "gbwin.h"
#include "misc.h"
#include "gberror.h"
#include "string_util.h"

//-------------------------------------------------------------------------------------------------
// C++ interface for gnuplot (Windows only)
//-------------------------------------------------------------------------------------------------

namespace gb::yadro::util
{
    // exception thrown when gnuplot fails
    using gnuplot_error = error_t<10>;

    //---------------------------------------------------------------------------------------------
    enum class plotstyle
    {
        s_line = 0, s_point, s_linepoint, s_impulse, s_dot, s_step, s_fstep, s_histep, s_box, s_filledcurve,
        s_histogram, s_label, s_xerrorbar, s_xerrorline, s_errorbar, s_errorline, s_yerrorbar,
        s_boxerrorbar, s_xyerrorbar, s_xyerrorline, s_boxxyerrorbar, s_financebar, s_candlestick,
        s_vector, s_image, s_rgbimage, s_pm3d,
        // smooth
        s_unique = 100, s_frequency, s_cspline, s_acspline, s_bezier, s_sbezier
    };

    namespace detail
    {
        //---------------------------------------------------------------------------------------------
        inline auto style_str(plotstyle s) -> std::string
        {
            static const std::map<plotstyle, std::string> s_map
            {
                {plotstyle::s_line, "lines"},
                {plotstyle::s_point, "points"},
                {plotstyle::s_linepoint, "linespoints"},
                {plotstyle::s_impulse, "impulses"},
                {plotstyle::s_dot, "dots"},
                {plotstyle::s_step, "steps"},
                {plotstyle::s_fstep, "fsteps"},
                {plotstyle::s_histep, "histeps"},
                {plotstyle::s_box, "boxes"},
                {plotstyle::s_filledcurve, "filledcurves"},
                {plotstyle::s_histogram, "histograms"},
                {plotstyle::s_label, "labels"},                // 3 columns of data are required
                {plotstyle::s_xerrorbar, "xerrorbars"},        // 3-4 columns of data are required
                {plotstyle::s_xerrorline, "xerrorlines"},      // 3-4 columns of data are required
                {plotstyle::s_errorbar, "errorbars"},          // 3-4 columns of data are required
                {plotstyle::s_errorline, "errorlines"},        // 3-4 columns of data are required
                {plotstyle::s_yerrorbar, "yerrorbars"},        // 3-4 columns of data are required
                {plotstyle::s_boxerrorbar, "boxerrorbars"},    // 3-5 columns of data are required
                {plotstyle::s_xyerrorbar, "xyerrorbars"},      // 4,6,7 columns of data are required
                {plotstyle::s_xyerrorline, "xyerrorlines"},    // 4,6,7 columns of data are required
                {plotstyle::s_boxxyerrorbar, "boxxyerrorbars"},// 4,6,7 columns of data are required
                {plotstyle::s_financebar, "financebars"},      // 5 columns of data are required
                {plotstyle::s_candlestick, "candlesticks"},    // 5 columns of data are required
                {plotstyle::s_vector, "vectors"},
                {plotstyle::s_image, "image"},
                {plotstyle::s_rgbimage, "rgbimage"},
                {plotstyle::s_pm3d, "pm3d"},

                {plotstyle::s_unique, "unique"},
                {plotstyle::s_frequency, "frequency"},
                {plotstyle::s_cspline, "csplines"},
                {plotstyle::s_acspline, "acsplines"},
                {plotstyle::s_bezier, "bezier"},
                {plotstyle::s_sbezier, "sbezier"},
            };

            return (s < plotstyle::s_unique ? "with " : "smooth ") + s_map.at(s);
        }

        //---------------------------------------------------------------------------------------------
        inline auto write_data_file(auto&& write_fn, auto&& ... args)
        {
            auto path = get_temp_file_path(".gnuplot");
            test_condition<gnuplot_error>(!path.empty(), "Failed to obtain temp file path");

            std::ofstream tmp(path);
            test_condition<gnuplot_error>(tmp, "Failed to create temp file: " + path.string());

            write_fn(tmp, std::forward<decltype(args)>(args)...);

            tmp_file_cleaner_t::add(path);

            auto posix_path{ path.string() };
            std::replace(posix_path.begin(), posix_path.end(), '\\', '/'); // gnuplot expects posix path

            return posix_path;
        }

        //---------------------------------------------------------------------------------------------
        // cmd_t class wraps a command string that can be used with async_gnuplots function
        // this command string is executed together with plots in the order specified
        // it is not used explicitly, rather with operator""_cmd
        //---------------------------------------------------------------------------------------------
        struct cmd_t
        {
            std::string cmd;
            const auto& get_cmd() const { return cmd; }
            std::size_t size() const { return -1; }
            auto is_plot_cmd() const { return cmd.starts_with("plot") || cmd.starts_with("splot") || cmd.starts_with("replot"); }
        };

        //---------------------------------------------------------------------------------------------
        // pane_t class is not used explicitly, it's created by the pane function
        //---------------------------------------------------------------------------------------------
        template<class Tuple>
        struct pane_t
        {
            Tuple plots;

            auto get_cmd() const
            {
                std::ostringstream os;

                os << "set grid\n";
                auto size = std::apply([](auto ...p)
                    {
                        return std::min({ p.size()... });
                    }, plots);

                // size == -1 means there are only cmd_t commands in the pane
                if (size != -1)
                {
                    os << "set xrange[0:" << size << "]\n";
                    os << "plot ";
                }

                std::apply([&](auto&& p, auto&& ...ps) mutable
                    {
                        os << p.get_cmd();
                        ((os << ", " << ps.get_cmd()), ...);
                    }, plots);
                return os.str();
            }
        };

    }

    //---------------------------------------------------------------------------------------------
    // plot_t class is used to create a single plot in a pane
    //---------------------------------------------------------------------------------------------
    template<std::ranges::range Data>
    struct plot_t
    {
        plot_t(Data&& data, const std::string& title = "",
            plotstyle style = plotstyle::s_line, const std::string& color = "")
            : data(std::forward<decltype(data)>(data)),
            title(title), style(style), color(color)
        {
        }

        auto get_cmd() const
        {
            auto posix_path = detail::write_data_file([&](auto& tmp)
                {
                    for (auto v : data)
                        tmp << v << "\n";
                });

            auto cmd_str = std::format("\"{}\" using 1 ", posix_path);

            if (title.empty())
                cmd_str += " notitle ";
            else
                cmd_str += " title \"" + title + "\" ";

            cmd_str += detail::style_str(style);

            if (!color.empty())
                cmd_str += " lt rgb \"" + color + "\"";

            return cmd_str;
        }

        auto size() const { return data.size(); }

    private:
        Data data;
        std::string title;
        plotstyle style;
        std::string color;
    };

    template<std::ranges::range Data>
    plot_t(Data&& data, const std::string & = "", plotstyle = plotstyle::s_line, const std::string & = "") -> plot_t<Data>;

    //---------------------------------------------------------------------------------------------
    // function "pane" is used to create an r-value pane_t structure containing multiple plots for a single pane
    // it's used to describe a single pane in mulipane chart
    //---------------------------------------------------------------------------------------------
    inline auto pane(auto&& plot, auto&& ...plots)
    {
        using namespace detail;
        auto make_tuple = [](auto&& p)
            {
                return overloaded(
                    []<std::ranges::range Data>(plot_t<Data> && d) // matching plot, always rvalue
                {
                    return std::tuple(std::move(d));
                },
                    []<std::ranges::range Data>(Data && d) // matching sequence
                {
                    return std::tuple(plot_t(std::forward<Data>(d)));
                },
                    [](detail::cmd_t&& cmd) // matching command, always rvalue
                {
                    return std::tuple(std::move(cmd));
                }
                )(std::forward<decltype(p)>(p));
            };

        if constexpr (sizeof...(plots))
            return pane_t{ std::tuple_cat(make_tuple(std::forward<decltype(plot)>(plot)), pane(std::forward<decltype(plots)>(plots)...).plots) };
        else
            return pane_t{ make_tuple(std::forward<decltype(plot)>(plot)) };
    }


    //---------------------------------------------------------------------------------------------
    // user-defined literal that creates a cmd_t class, which incapsulates command string
    //---------------------------------------------------------------------------------------------
    inline auto operator ""_cmd(const char* cmd, std::size_t) { return detail::cmd_t{ cmd }; }

    //---------------------------------------------------------------------------------------------
    // gnuplot class
    struct gnuplot
    {
        //---------------------------------------------------------------------------------------------
        // constructor accepts option string and gnuplot path
        // if gnuplot_exe_path is empty then the PATH variable is searched for gnuplot.exe
        gnuplot(const std::string& options = "-p", const std::string& gnuplot_exe_path = "");

        //---------------------------------------------------------------------------------------------
        // plots
        auto& plot(auto&& ... args) const
        {
            return cmd("plot " + plot_cmd(std::forward<decltype(args)>(args)...));
        }
        auto& replot(auto&& ... args) const
        {
            return cmd("replot " + plot_cmd(std::forward<decltype(args)>(args)...));
        }
        auto& splot(auto&& ... args) const
        {
            return cmd("splot " + plot_cmd(std::forward<decltype(args)>(args)...));
        }

        auto& multiplot(auto&& ...args) const
        {
            return cmd(plot_cmd(std::forward<decltype(args)>(args)...));
        }

        auto async_plot(auto&& ... args) const
        {
            return std::async(std::launch::async, [p = *this](auto&& ...args)
                {
                    p.plot(std::forward < decltype(args) >(args)...);
                }, std::forward < decltype(args) >(args)...);
        }

        auto async_multiplot(auto&& ... args) const
        {
            return std::async(std::launch::async, [p = *this](auto&& ...args)
                {
                    p.multiplot(std::forward < decltype(args) >(args)...);
                }, std::forward < decltype(args) >(args)...);
        }

        //---------------------------------------------------------------------------------------------
        auto& set_terminal_output()  const { return cmd("set output").cmd("set terminal windows"); }

        //---------------------------------------------------------------------------------------------
        // saves a gnuplot session to a postscript file
        auto& set_postscript_output(const std::string& filename)  const
        {
            return cmd("set terminal postscript color").cmd(std::format("set output \"{}.ps\"", filename));
        }

        //---------------------------------------------------------------------------------------------
        // saves a gnuplot session to a png file
        auto& set_png_output(const std::string& filename) const
        {
            return cmd("set term png").cmd(std::format("set output \"{}.png\"", filename));
        }

        //---------------------------------------------------------------------------------------------
        // send a command to gnuplot via opened pipe
        const gnuplot& cmd(const std::string& cmdstr) const
        {
#if defined(_DEBUG)
            std::cout << cmdstr << std::endl;
#endif
            fputs((cmdstr + "\n").c_str(), _exe_pipe.get());
            fflush(_exe_pipe.get());
            return *this;
        }

        //---------------------------------------------------------------------------------------------
        // reset gnuplot
        auto& reset() const
        {
            cmd("reset");
            cmd("clear");
            set_terminal_output();
            return *this;
        }

    private:
        // data
        std::shared_ptr<FILE> _exe_pipe{};

        //---------------------------------------------------------------------------------------------
        // create a command string to display multiple plots in multiple panes
        // parameters can be a combination of types: pane_t, plot_t, cmd_t
        // e.g. plot_cmd(1, "set pointsize 5"_cmd,
        // pane(plot_t(std::vector<int>{10, 20, 30, 40, 50, 60}), plot_t(v, "v", plotstyle::s_step), std::vector<double>{2, 5, 6, 9, 11, 10, 3}),
        //    "set style fill solid 0.5 border"_cmd, pane(plot_t(d, "another_dv", plotstyle::s_histogram, "red"), "sin(x)"_cmd)
        //    );
        static auto plot_cmd(unsigned columns, auto&& ...panes)
        {
            auto pane_count = [](this auto&& self, const auto& val, const auto& ...vals)
                {
                    auto count = 0;
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(val)>, detail::cmd_t>)
                    {
                        if (val.is_plot_cmd())
                            ++count;
                    }
                    else
                        ++count;

                    if constexpr (sizeof...(vals) != 0)
                        return count + std::invoke(self, vals...);
                    else
                        return count;
                }(panes...);

                auto get_cmd = [](auto&& val)
                    {
                        return overloaded(
                            []<std::ranges::range Data>(plot_t<Data> && d) // matching plot, create a pane for it
                        {
                            return pane(std::move(d)).get_cmd();
                        },
                            []<std::ranges::range Data>(Data && d) // matching sequence, create a pane for it
                        {
                            return pane(plot_t(std::forward<Data>(d))).get_cmd();
                        },
                            []<class Tuple>(detail::pane_t<Tuple>&& p) // matching pane
                        {
                            return p.get_cmd();
                        },
                            [](detail::cmd_t&& cmd) // matching command, always rvalue
                        {
                            // if it's a plot command, create a pane for it
                            if (cmd.is_plot_cmd())
                                return pane(std::move(cmd)).get_cmd();
                            else
                                return cmd.get_cmd();
                        }
                            // no other types are allowed
                        )(std::forward<decltype(val)>(val));
                    };

                std::string cmds = std::format("set multiplot layout {},{} columnsfirst", 
                    pane_count / columns + (pane_count % columns != 0 ? 1 : 0), columns);

                ((cmds += "\n" + get_cmd(std::forward<decltype(panes)>(panes))), ...);
                
                cmds += "\nunset multiplot";

                return cmds;
        }

        // plot implementations for one or more data sequences
        static auto plot_cmd(const std::string& title, plotstyle style, std::ranges::range auto&& ... data)
        {
            auto posix_path = detail::write_data_file([&](auto&& tmp)
                {
                    auto size = std::min({ std::distance(std::begin(data), std::end(data))... });
                    test_condition<gnuplot_error>(size != 0, "empty data range");

                    for (auto i = 0; i < size; ++i)
                    {
                        ((tmp << *(std::end(data) - size + i) << ' '), ...);
                        tmp << "\n";
                    }
                });

            auto cmd_str = std::format("\"{}\" using ", posix_path);

            for (size_t i = 1; i <= sizeof...(data); ++i)
            {
                cmd_str += std::to_string(i);
                if (i != sizeof...(data))
                    cmd_str += ":";
            }

            if (title.empty())
                cmd_str += " notitle ";
            else
                cmd_str += " title \"" + title + "\" ";

            cmd_str += detail::style_str(style);

            return cmd_str;
        }

        // plot bitmap image
        static auto plot_cmd(const std::string& title, const unsigned char* bitmap_buffer, unsigned width, unsigned height)
        {
            test_condition<gnuplot_error>(width != 0 && height != 0, "Bitmap must have non-zero width and height");

            auto posix_path = detail::write_data_file([=](auto&& tmp)
                {
                    for (auto row = 0u, i = 0u; row < height; ++row)
                        for (auto column = 0u; column < width; ++column)
                        {
                            tmp << column << " " << row << " "
                                << static_cast<float>(bitmap_buffer[i++]) << std::endl;
                        }
                });

            auto cmd_str = title.empty() ?
                std::format("\"{}\" notitle with image", posix_path)
                : std::format("\"{}\" title \"{}\" with image", posix_path, title);

            return cmd_str;
        }

        // plot line ax + b
        static auto plot_cmd(plotstyle s, double a, double b, const std::string& title = "")
        {
            return std::format("{} * x + {} title \"{}\" {}",
                a, b, title.empty() ? std::format("{} * x + {}", a, b) : title, detail::style_str(s));
        }


        // binary operators: ** exponentiation, * multiply, / divide, + add, - substract, % modulo
        // unary operators: - minus, ! factorial
        // elementary functions: rand(x), abs(x), sgn(x), ceil(x), floor(x), int(x), imag(x), real(x), arg(x),
        //   sqrt(x), exp(x), log(x), log10(x), sin(x), cos(x), tan(x), asin(x), acos(x), atan(x), atan2(y,x),
        //   sinh(x), cosh(x), tanh(x), asinh(x), acosh(x), atanh(x)
        // special functions: erf(x), erfc(x), inverf(x), gamma(x), igamma(a,x), lgamma(x), ibeta(p,q,x),
        //   besj0(x), besj1(x), besy0(x), besy1(x), lambertw(x)
        // statistical fuctions: norm(x), invnorm(x)
        // 2D plots can use variable "x", 3D plots can use variables "x" and "y"
        static auto plot_cmd(plotstyle s, const std::string& equation, const std::string& title = "")
        {
            return std::format("{} title \"{}\" {}", equation,
                title.empty() ? equation : title, detail::style_str(s));
        }

        // friend for testing
        friend auto get_plot_cmd(auto&& ... args);
    };
    
    inline auto get_plot_cmd(auto&& ... args)
    {
        return gnuplot::plot_cmd(std::forward<decltype(args)>(args)...);
    }

    inline auto foo()
    {
        return get_plot_cmd(1, pane(std::vector<int>{1, 2, 3, 4, 5}));
    }
    //------------------------------------------------------------------------------
    // constructor creates a pipe to gnuplot.exe
    //
    inline gnuplot::gnuplot(const std::string& options, const std::string& gnuplot_exe_path)
    {
        FILE* exe_pipe{};

        if (std::filesystem::exists(gnuplot_exe_path))
        {
            exe_pipe = _popen(('"' + gnuplot_exe_path + "\" " + options).c_str(), "w");
            test_condition<gnuplot_error>(exe_pipe, "Failed to open: " + gnuplot_exe_path);
        }
        else if (auto path = std::getenv("PATH"); path)
        {
            for (auto s : tokenize(std::string(path), ';'))
                if (auto exe = s + "\\gnuplot.exe"; std::filesystem::exists(exe))
                {
                    exe_pipe = _popen(('"' + exe + "\" " + options).c_str(), "w");
                    test_condition<gnuplot_error>(exe_pipe, "Failed to open: " + exe);
                    break;
                }

            test_condition<gnuplot_error>(exe_pipe, "Path doesn't contain gnuplot.exe. path = " + std::string(path));
        }
        else
        {
            failed_condition<gnuplot_error>("Path is not set");
        }
     
        _exe_pipe = std::shared_ptr<FILE>(exe_pipe, [](auto&& p) { _pclose(p); });
        set_terminal_output();
    }
}
