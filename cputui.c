#include <stdio.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_CPUS 128

void draw_menu(WINDOW *win, int highlight, int visible_cpus[], int visible_count) {
    int x, y, i;
    char path[256];
    char buffer[2];

    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, "CPU Control");
    mvwprintw(win, 1, 2, "-----------");
    
    // Initialize color system
    start_color();
    use_default_colors(); // enable default colors
    init_pair(1, COLOR_CYAN, -1); // cyan on default background
    init_pair(2, COLOR_RED, -1); // red on default background

    for (i = 0; i < visible_count; i++) {
        sprintf(path, "/sys/devices/system/cpu/cpu%d/online", visible_cpus[i]);
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            fread(buffer, 1, 1, file);
            fclose(file);
            if (i == highlight) {
                wattron(win, A_STANDOUT | COLOR_PAIR(1)); // cyan on default background
            } else if (buffer[0] == '0') {
                wattron(win, COLOR_PAIR(2)); // red on default background
            }
            mvwprintw(win, i + 2, 2, "CPU %d: %s", visible_cpus[i], buffer[0] == '1' ? "online" : "offline");
            wattroff(win, A_STANDOUT | COLOR_PAIR(1) | COLOR_PAIR(2));
        }
    }
    wrefresh(win);
}

int main() {
    initscr(); // initialize ncurses
    curs_set(0); // hide the cursor
    noecho(); // don't echo user input

    WINDOW *menu_win = newwin(20, 30, 2, 2); // create a window for the menu
    box(menu_win, 0, 0); // draw a border around the window
    keypad(menu_win, TRUE);

    int visible_cpus[MAX_CPUS]; // array to store the indices of visible CPUs
    int visible_count = 0; // count of visible CPUs

    int i;
    char path[256];
    char buffer[2];
    for (i = 1; i <= MAX_CPUS; i++) {
        sprintf(path, "/sys/devices/system/cpu/cpu%d/online", i);
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            fread(buffer, 1, 1, file);
            fclose(file);
            if (buffer[0] == '1' || buffer[0] == '0') { // only show CPUs with a known status
                visible_cpus[visible_count] = i;
                visible_count++;
            }
        }
    }

    int highlight = 0; // initial highlight position
    int choice;

while (1) {
    draw_menu(menu_win, highlight, visible_cpus, visible_count);
    choice = wgetch(menu_win); // get user input
    switch (choice) {
        case KEY_UP:
            printf("Up arrow key pressed\n"); // debug statement
            if (highlight > 0) {
                highlight--; // decrement highlight
                printf("Highlight: %d\n", highlight); // debug statement
                wrefresh(menu_win); // refresh the screen
            }
            break;
        case KEY_DOWN:
            printf("Down arrow key pressed\n"); // debug statement
            if (highlight < visible_count - 1) {
                highlight++; // increment highlight
                printf("Highlight: %d\n", highlight); // debug statement
                wrefresh(menu_win); // refresh the screen
            }
            break;
        case 10: // enter key
            if (highlight < visible_count) {
                // handle CPU selection
                sprintf(path, "/sys/devices/system/cpu/cpu%d/online", visible_cpus[highlight]); 
                FILE *file = fopen(path, "r");
                if (file != NULL) {
                    fread(buffer, 1, 1, file);
                    fclose(file);
                    if (buffer[0] == '1') {
                        // disable CPU
                        sprintf(path, "/sys/devices/system/cpu/cpu%d/online", visible_cpus[highlight]);
                        int fd = open(path, O_RDWR);
                        if (fd != -1) {
                            write(fd, "0", 1);
                            close(fd);
                        } else {
                            printf("Failed to disable CPU %d\n", visible_cpus[highlight]);
                        }
                    } else {
                        // enable CPU
                        sprintf(path, "/sys/devices/system/cpu/cpu%d/online", visible_cpus[highlight]);
                        int fd = open(path, O_RDWR);
                        if (fd != -1) {
                            write(fd, "1", 1);
                            close(fd);
                        } else {
                            printf("Failed to enable CPU %d\n", visible_cpus[highlight]);
                        }
                    }
                }
            }
            break;
        default:
            printf("Unknown key pressed\n"); // debug statement
            break;
        }
    }
endwin(); // clean up ncurses
return 0;
}