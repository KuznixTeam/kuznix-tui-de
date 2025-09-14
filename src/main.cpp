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

std::vector<std::string> get_bin_dirs() {
    std::vector<std::string> dirs = {
        "/bin",
        "/usr/local/bin",
        "/usr/local/sbin"
    };

    // Add /opt/*/{bin,sbin}
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

    // Add ~/.local/{bin,sbin}
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

void draw_list(WINDOW *win, const std::vector<std::string>& items, int highlight, int top, int height) {
    werase(win);
    int end = std::min((int)items.size(), top + height);
    for (int i = top; i < end; ++i) {
        if (i == highlight) wattron(win, A_REVERSE);
        mvwprintw(win, i-top, 0, "%s", items[i].c_str());
        if (i == highlight) wattroff(win, A_REVERSE);
    }
    wrefresh(win);
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
    noecho();
    cbreak();
    keypad(stdscr, TRUE);

    int height = LINES-2;
    WINDOW *listwin = newwin(height, COLS, 0, 0);

    draw_list(listwin, filtered, highlight, top, height);
    mvprintw(LINES-1, 0, "Enter: Launch | Up/Down: Move | Ctrl+F: Filter | q: Quit");
    refresh();

    bool filtering = false;
    while ((ch = wgetch(listwin))) {
        if (filtering) {
            if (ch == 10 || ch == KEY_ENTER) { // finish filtering
                filtering = false;
                filter_binaries(all_binaries, filtered, filter);
                highlight = 0; top = 0;
                draw_list(listwin, filtered, highlight, top, height);
                move(LINES-1, 0); clrtoeol();
                mvprintw(LINES-1, 0, "Enter: Launch | Up/Down: Move | Ctrl+F: Filter | q: Quit");
                refresh();
            } else if (ch == 27) { // escape cancel filter
                filtering = false;
                filter.clear();
                filter_binaries(all_binaries, filtered, filter);
                highlight = 0; top = 0;
                draw_list(listwin, filtered, highlight, top, height);
                move(LINES-1, 0); clrtoeol();
                mvprintw(LINES-1, 0, "Enter: Launch | Up/Down: Move | Ctrl+F: Filter | q: Quit");
                refresh();
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (!filter.empty()) filter.pop_back();
                move(LINES-1, 0); clrtoeol();
                mvprintw(LINES-1, 0, "Filter: %s", filter.c_str());
                refresh();
            } else if (isprint(ch)) {
                filter.push_back((char)ch);
                move(LINES-1, 0); clrtoeol();
                mvprintw(LINES-1, 0, "Filter: %s", filter.c_str());
                refresh();
            }
            continue;
        }

        if (ch == 'q') break;
        else if (ch == 10 || ch == KEY_ENTER) {
            if (filtered.empty()) continue;
            endwin();
            std::string cmd = filtered[highlight];
            // execvp style: search in dirs
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
            // If launch fails, restart UI
            initscr();
            noecho();
            cbreak();
            keypad(stdscr, TRUE);
            listwin = newwin(height, COLS, 0, 0);
            draw_list(listwin, filtered, highlight, top, height);
            mvprintw(LINES-1, 0, "Failed to launch %s", cmd.c_str());
            refresh();
        }
        else if (ch == KEY_UP) {
            if (highlight > 0) --highlight;
            if (highlight < top) --top;
            draw_list(listwin, filtered, highlight, top, height);
        }
        else if (ch == KEY_DOWN) {
            if (highlight < (int)filtered.size() - 1) ++highlight;
            if (highlight >= top + height) ++top;
            draw_list(listwin, filtered, highlight, top, height);
        }
        else if (ch == 6) { // Ctrl+F
            filtering = true;
            filter.clear();
            move(LINES-1, 0); clrtoeol();
            mvprintw(LINES-1, 0, "Filter: ");
            refresh();
        }
    }

    endwin();
    return 0;
}
