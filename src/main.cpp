#include <ncurses.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <chrono>
#include <thread>

// Define color pairs
#define COLOR_HEADER 1
#define COLOR_HIGHLIGHT 2
#define COLOR_NORMAL 3
#define COLOR_FOOTER 4
#define COLOR_FILTER 5

std::vector<std::string> get_bin_dirs() {
    std::vector<std::string> dirs = {
        "/bin",
        "/usr/local/bin",
        "/usr/local/sbin"
    };

    // /opt/*/{bin,sbin}
    DIR *opt = opendir("/opt");
    if (opt) {
        struct dirent *entry;
        while ((entry = readdir(opt)) != NULL) {
            if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
                std::string base = std::string("/opt/") + entry->d_name;
                dirs.push_back(base + "/bin");
                dirs.push_back(base + "/sbin");
            }
        }
        closedir(opt);
    }

    // ~/.local/{bin,sbin}
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        std::string h(home);
        dirs.push_back(h + "/.local/bin");
        dirs.push_back(h + "/.local/sbin");
    }
    return dirs;
}

std::vector<std::string> find_binaries(const std::vector<std::string>& dirs) {
    std::set<std::string> bins;
    for (const auto& dir : dirs) {
        DIR *d = opendir(dir.c_str());
        if (!d) continue;
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (entry->d_type == DT_REG || entry->d_type == DT_LNK) {
                std::string path = dir + "/" + entry->d_name;
                struct stat st;
                if (stat(path.c_str(), &st) == 0 && (st.st_mode & S_IXUSR)) {
                    bins.insert(entry->d_name);
                }
            }
        }
        closedir(d);
    }
    return std::vector<std::string>(bins.begin(), bins.end());
}

void draw_header(WINDOW* win, int width) {
    wattron(win, COLOR_PAIR(COLOR_HEADER));
    mvwprintw(win, 0, 0, " Kuznix TUI Desktop ");
    for (int i = 18; i < width; ++i) mvwaddch(win, 0, i, ' ');
    wattroff(win, COLOR_PAIR(COLOR_HEADER));
}

void draw_footer(WINDOW* win, int height, int width, bool filtering, const std::string& filter) {
    wattron(win, COLOR_PAIR(COLOR_FOOTER));
    std::string msg = filtering ? "Filter (Ctrl+F, Enter=Apply, Esc=Cancel): " + filter : 
        "Up/Down: Move | Enter: Launch | Ctrl+F: Filter | q: Quit";
    mvwprintw(win, height-1, 0, "%-*s", width, msg.c_str());
    wattroff(win, COLOR_PAIR(COLOR_FOOTER));
}

void draw_list(WINDOW* win, const std::vector<std::string>& items, int highlight, int top, int height, int width, int prev_highlight) {
    for (int i = 0; i < height; ++i) {
        int idx = top + i;
        if (idx >= (int)items.size()) {
            mvwprintw(win, i, 0, "%-*s", width, " ");
            continue;
        }
        if (idx == highlight) {
            wattron(win, COLOR_PAIR(COLOR_HIGHLIGHT));
            mvwprintw(win, i, 0, "%-*s", width, items[idx].c_str());
            wattroff(win, COLOR_PAIR(COLOR_HIGHLIGHT));
        } else {
            wattron(win, COLOR_PAIR(COLOR_NORMAL));
            mvwprintw(win, i, 0, "%-*s", width, items[idx].c_str());
            wattroff(win, COLOR_PAIR(COLOR_NORMAL));
        }
    }
    wrefresh(win);

    // Animation: highlight moves smoothly from prev_highlight to highlight
    if (prev_highlight >= 0 && prev_highlight != highlight && abs(highlight - prev_highlight) < height) {
        int step = (highlight > prev_highlight) ? 1 : -1;
        for (int pos = prev_highlight + step; pos != highlight + step; pos += step) {
            int local_idx = pos - top;
            if (local_idx >= 0 && local_idx < height) {
                wattron(win, COLOR_PAIR(COLOR_HIGHLIGHT));
                mvwprintw(win, local_idx, 0, "%-*s", width, items[pos].c_str());
                wattroff(win, COLOR_PAIR(COLOR_HIGHLIGHT));
                wrefresh(win);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                // unhighlight previous
                wattron(win, COLOR_PAIR(COLOR_NORMAL));
                mvwprintw(win, local_idx, 0, "%-*s", width, items[pos].c_str());
                wattroff(win, COLOR_PAIR(COLOR_NORMAL));
                wrefresh(win);
            }
        }
        // finally highlight the selected one
        int local_idx = highlight - top;
        wattron(win, COLOR_PAIR(COLOR_HIGHLIGHT));
        mvwprintw(win, local_idx, 0, "%-*s", width, items[highlight].c_str());
        wattroff(win, COLOR_PAIR(COLOR_HIGHLIGHT));
        wrefresh(win);
    }
}

void filter_binaries(const std::vector<std::string>& all, std::vector<std::string>& filtered, const std::string& pattern) {
    filtered.clear();
    for (const auto& bin : all) {
        if (pattern.empty() || bin.find(pattern) != std::string::npos) {
            filtered.push_back(bin);
        }
    }
}

int main() {
    auto dirs = get_bin_dirs();
    auto all_binaries = find_binaries(dirs);
    std::vector<std::string> filtered = all_binaries;
    std::string filter;
    int highlight = 0, top = 0;
    int ch;

    initscr();
    start_color();
    use_default_colors();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);

    init_pair(COLOR_HEADER, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_HIGHLIGHT, COLOR_YELLOW, COLOR_BLUE);
    init_pair(COLOR_NORMAL, COLOR_WHITE, -1);
    init_pair(COLOR_FOOTER, COLOR_BLACK, COLOR_CYAN);
    init_pair(COLOR_FILTER, COLOR_BLACK, COLOR_GREEN);

    int height, width;
    getmaxyx(stdscr, height, width);

    WINDOW* header = newwin(1, width, 0, 0);
    WINDOW* footer = newwin(1, width, height-1, 0);
    WINDOW* listwin = newwin(height-2, width, 1, 0);

    bool filtering = false;
    int prev_highlight = -1;

    while (true) {
        werase(header); werase(listwin); werase(footer);
        draw_header(header, width);
        draw_footer(footer, height, width, filtering, filter);

        int list_height = height - 2;
        if (highlight < top) top = highlight;
        if (highlight >= top + list_height) top = highlight - list_height + 1;
        draw_list(listwin, filtered, highlight, top, list_height, width, prev_highlight);

        wrefresh(header);
        wrefresh(listwin);
        wrefresh(footer);

        ch = wgetch(listwin);

        if (filtering) {
            if (ch == 10 || ch == KEY_ENTER) {
                filtering = false;
                filter_binaries(all_binaries, filtered, filter);
                highlight = 0; top = 0;
            } else if (ch == 27) { // ESC
                filtering = false;
                filter.clear();
                filter_binaries(all_binaries, filtered, filter);
                highlight = 0; top = 0;
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (!filter.empty()) filter.pop_back();
            } else if (isprint(ch)) {
                filter.push_back((char)ch);
            }
            continue;
        }

        if (ch == 'q') break;
        else if (ch == 10 || ch == KEY_ENTER) {
            if (filtered.empty()) continue;
            endwin();
            std::string cmd = filtered[highlight];
            std::string path;
            for (const auto& dir : dirs) {
                std::string p = dir + "/" + cmd;
                struct stat st;
                if (stat(p.c_str(), &st) == 0 && (st.st_mode & S_IXUSR)) {
                    path = p;
                    break;
                }
            }
            if (!path.empty()) execlp(path.c_str(), cmd.c_str(), nullptr);
            // Relaunch UI if execlp fails
            initscr();
            start_color();
            use_default_colors();
            noecho();
            cbreak();
            keypad(stdscr, TRUE);
            curs_set(0);
            getmaxyx(stdscr, height, width);
            header = newwin(1, width, 0, 0);
            footer = newwin(1, width, height-1, 0);
            listwin = newwin(height-2, width, 1, 0);
        }
        else if (ch == KEY_UP) {
            prev_highlight = highlight;
            if (highlight > 0) --highlight;
        }
        else if (ch == KEY_DOWN) {
            prev_highlight = highlight;
            if (highlight < (int)filtered.size() - 1) ++highlight;
        }
        else if (ch == 6) { // Ctrl+F
            filtering = true;
            filter.clear();
        }
    }

    endwin();
    return 0;
}
