#include <ncurses.h>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <cstring>

#ifndef MESON_BUILD_VERSION
#define MESON_BUILD_VERSION "dev"
#endif

// Color pairs
#define CP_HEADER    1
#define CP_NORMAL    2
#define CP_HIGHLIGHT 3
#define CP_BORDER    4
#define CP_FOOTER    5
#define CP_ABOUT     6

// Animation timing (ms)
constexpr int ANIM_STEP = 15;
constexpr int FLOAT_ANIM_STEPS = 7;

std::vector<std::string> get_bin_dirs() {
    std::vector<std::string> dirs = {"/bin", "/usr/local/bin", "/usr/local/sbin"};

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
    wattron(win, COLOR_PAIR(CP_HEADER));
    mvwprintw(win, 0, 2, "Kuznix TUI Desktop");
    for (int i = 19; i < width-1; ++i) mvwaddch(win, 0, i, ' ');
    wattroff(win, COLOR_PAIR(CP_HEADER));
}

void draw_footer(WINDOW* win, int width, bool filterMode, const std::string& filter) {
    wattron(win, COLOR_PAIR(CP_FOOTER));
    std::string msg = filterMode ? "Filter: " + filter + " (Enter=apply, ESC=cancel)" :
        "[Up/Down] Move  [Enter] Launch  [F1] About  [Ctrl+F] Filter  [q] Quit";
    mvwprintw(win, 0, 2, "%-*s", width-4, msg.c_str());
    wattroff(win, COLOR_PAIR(CP_FOOTER));
}

void animate_highlight(WINDOW* win, const std::vector<std::string>& items, int prev, int curr, int top, int visRows, int width) {
    if(prev == curr) return;
    int dir = (curr > prev) ? 1 : -1;
    int start = prev;
    int end = curr;

    for(int step = 0; step < std::abs(end-start); ++step) {
        int idx = start + dir*(step+1) - top;
        if(idx >= 0 && idx < visRows) {
            wattron(win, COLOR_PAIR(CP_HIGHLIGHT));
            mvwprintw(win, idx, 2, "%-*s", width-4, items[start + dir*(step+1)].c_str());
            wattroff(win, COLOR_PAIR(CP_HIGHLIGHT));

            // Unhighlight previous
            int prev_idx = idx - dir;
            if(prev_idx >= 0 && prev_idx < visRows) {
                wattron(win, COLOR_PAIR(CP_NORMAL));
                mvwprintw(win, prev_idx, 2, "%-*s", width-4, items[start + dir*step].c_str());
                wattroff(win, COLOR_PAIR(CP_NORMAL));
            }

            wrefresh(win);
            std::this_thread::sleep_for(std::chrono::milliseconds(ANIM_STEP));
        }
    }
}

void draw_list(WINDOW* win, const std::vector<std::string>& items, int highlight, int top, int visRows, int width, int prev_highlight) {
    for(int i=0; i<visRows; ++i) {
        int idx = top + i;
        if(idx >= (int)items.size()) {
            mvwprintw(win, i, 2, "%-*s", width-4, " ");
            continue;
        }
        if(idx == highlight) wattron(win, COLOR_PAIR(CP_HIGHLIGHT));
        else wattron(win, COLOR_PAIR(CP_NORMAL));

        mvwprintw(win, i, 2, "%-*s", width-4, items[idx].c_str());

        if(idx == highlight) wattroff(win, COLOR_PAIR(CP_HIGHLIGHT));
        else wattroff(win, COLOR_PAIR(CP_NORMAL));
    }
    wrefresh(win);
    animate_highlight(win, items, prev_highlight, highlight, top, visRows, width);
}

void filter_list(const std::vector<std::string>& src, std::vector<std::string>& out, const std::string& pat) {
    out.clear();
    for(auto &bin : src) {
        if(pat.empty() || bin.find(pat) != std::string::npos) out.push_back(bin);
    }
}

void animate_floating_window(WINDOW* win, int height, int width, int animSteps) {
    int y, x;
    getbegyx(win, y, x);
    for(int i=1;i<=animSteps;++i){
        int h = height*i/animSteps;
        int w = width*i/animSteps;
        int top = y + (height-h)/2;
        int left = x + (width-w)/2;
        WINDOW* temp = newwin(h, w, top, left);
        wattron(temp, COLOR_PAIR(CP_BORDER));
        box(temp, 0, 0);
        wattroff(temp, COLOR_PAIR(CP_BORDER));
        wrefresh(temp);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        delwin(temp);
    }
}

void show_about_dialog(int term_h, int term_w) {
    int aw = 40, ah = 9;
    int ay = (term_h-ah)/2, ax = (term_w-aw)/2;
    WINDOW* win = newwin(ah, aw, ay, ax);
    animate_floating_window(win, ah, aw, FLOAT_ANIM_STEPS);
    wattron(win, COLOR_PAIR(CP_BORDER));
    box(win, 0, 0);
    wattroff(win, COLOR_PAIR(CP_BORDER));
    wattron(win, COLOR_PAIR(CP_ABOUT));
    mvwprintw(win, 1, (aw-20)/2, "Kuznix TUI Desktop");
    wattroff(win, COLOR_PAIR(CP_ABOUT));
    mvwprintw(win, 3, 3, "A full-screen TUI desktop");
    mvwprintw(win, 4, 3, "for Kuznix GNU/Linux.");
    wattron(win, COLOR_PAIR(CP_ABOUT));
    mvwprintw(win, 6, 3, "Version: %s", MESON_BUILD_VERSION);
    wattroff(win, COLOR_PAIR(CP_ABOUT));
    mvwprintw(win, 7, aw-18, "[Press any key]");
    wrefresh(win);
    wgetch(win);
    delwin(win);
}

void show_filter_dialog(int term_h, int term_w, std::string& filter, bool& apply) {
    int fw=38, fh=5;
    int fy=(term_h-fh)/2, fx=(term_w-fw)/2;
    WINDOW* win = newwin(fh, fw, fy, fx);
    animate_floating_window(win, fh, fw, FLOAT_ANIM_STEPS);

    wattron(win,COLOR_PAIR(CP_BORDER)); box(win,0,0); wattroff(win,COLOR_PAIR(CP_BORDER));
    mvwprintw(win,1,2,"Enter filter: ");
    wattron(win,COLOR_PAIR(CP_ABOUT));
    mvwprintw(win,3,fw-18,"[Enter=apply, ESC]");
    wattroff(win,COLOR_PAIR(CP_ABOUT));
    wmove(win,1,16);
    wrefresh(win);

    filter.clear();
    curs_set(1); keypad(win,TRUE);
    int ch;
    while((ch=wgetch(win))){
        if(ch==10 || ch==KEY_ENTER){apply=true; break;}
        if(ch==27){apply=false; break;}
        if(ch==KEY_BACKSPACE||ch==127||ch==8){
            if(!filter.empty()){filter.pop_back(); mvwprintw(win,1,16,"%-18s",filter.c_str()); wmove(win,1,16+filter.size());}
        }else if(isprint(ch) && filter.size()<16){
            filter.push_back(ch); mvwaddch(win,1,16+filter.size()-1,ch); wmove(win,1,16+filter.size());
        }
        wrefresh(win);
    }
    curs_set(0); delwin(win);
}

int main() {
    auto dirs = get_bin_dirs();
    auto all_binaries = find_binaries(dirs);
    std::vector<std::string> filtered = all_binaries;
    std::string filter;
    int highlight=0, top=0, prev_highlight=0;

    initscr();
    start_color(); use_default_colors();
    noecho(); cbreak(); keypad(stdscr,TRUE); curs_set(0);

    init_pair(CP_HEADER,COLOR_BLACK,COLOR_CYAN);
    init_pair(CP_NORMAL,COLOR_WHITE,-1);
    init_pair(CP_HIGHLIGHT,COLOR_YELLOW,COLOR_BLUE);
    init_pair(CP_BORDER,COLOR_CYAN,-1);
    init_pair(CP_FOOTER,COLOR_BLACK,COLOR_CYAN);
    init_pair(CP_ABOUT,COLOR_YELLOW,-1);

    int term_h, term_w; getmaxyx(stdscr,term_h,term_w);
    WINDOW* header=newwin(1,term_w,0,0);
    WINDOW* footer=newwin(1,term_w,term_h-1,0);
    WINDOW* listwin=newwin(term_h-2,term_w,1,0);

    bool running=true, filterMode=false;

    while(running){
        werase(header); werase(listwin); werase(footer);
        getmaxyx(stdscr,term_h,term_w);
        wresize(header,1,term_w); wresize(footer,1,term_w); wresize(listwin,term_h-2,term_w);
        mvwin(footer,term_h-1,0);

        draw_header(header,term_w);
        draw_footer(footer,term_w,filterMode,filter);

        int visRows=term_h-2;
        if(highlight<top) top=highlight;
        if(highlight>=top+visRows) top=highlight-visRows+1;

        draw_list(listwin,filtered,highlight,top,visRows,term_w,prev_highlight);
        wrefresh(header); wrefresh(listwin); wrefresh(footer);

        int ch=wgetch(listwin); prev_highlight=highlight;
        switch(ch){
            case 'q': case 'Q': running=false; break;
            case KEY_UP: if(highlight>0) highlight--; break;
            case KEY_DOWN: if(highlight<(int)filtered.size()-1) highlight++; break;
            case 10: case KEY_ENTER:
                if(filtered.empty()) break;
                endwin();
                {
                    std::string cmd=filtered[highlight], path;
                    for(const auto &dir: dirs){
                        std::string p=dir+"/"+cmd;
                        struct stat st;
                        if(stat(p.c_str(),&st)==0 && (st.st_mode & S_IXUSR)){path=p; break;}
                    }
                    if(!path.empty()) execlp(path.c_str(),cmd.c_str(),nullptr);
                    initscr(); start_color(); use_default_colors(); noecho(); cbreak(); keypad(stdscr,TRUE); curs_set(0);
                    getmaxyx(stdscr,term_h,term_w);
                    header=newwin(1,term_w,0,0); footer=newwin(1,term_w,term_h-1,0); listwin=newwin(term_h-2,term_w,1,0);
                }
                break;
            case KEY_F(1): case 59: show_about_dialog(term_h,term_w); break;
            case 6: {
                std::string newfilter; bool doApply=false;
                show_filter_dialog(term_h,term_w,newfilter,doApply);
                if(doApply){filter=newfilter; filter_list(all_binaries,filtered,filter); highlight=0; top=0;}
                break;
            }
            default: break;
        }
    }

    endwin();
    return 0;
}
