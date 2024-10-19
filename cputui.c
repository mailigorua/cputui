#include <stdio.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>
#include <signal.h>

#define MAX_CPUS 128
#define MAX_RYZENADJ_PARAMS 100
#define MAX_PARAM_NAME_LENGTH 20
#define MAX_PARAM_VALUE_LENGTH 10
#define RYZENADJ_WINDOW_PADDING 6  // 2 for borders, 3 for separators, 1 for safety

// Function prototypes
void trim(char *str);
void handle_resize(int sig);
void do_resize(void);
void draw_menu(WINDOW *win, int highlight, int visible_cpus[], int visible_count);
void parse_ryzenadj_output(void);
void draw_ryzenadj_window(WINDOW *win);
void execute_ryzenadj_command(const char* param, int increment);

WINDOW *menu_win;
WINDOW *ryzenadj_win;
int term_lines, term_cols;
volatile sig_atomic_t resize_needed = 0;
int highlight = 0;
int visible_cpus[MAX_CPUS];
int visible_count = 0;
int active_window = 0; // 0 for CPU Control, 1 for Ryzenadj Parameters
int ryzenadj_highlight = 0;

typedef struct {
    char name[50];
    char value[50];
    char parameter[50];
    int adjustable;
    int user_modified;
} ryzenadj_param_t;

ryzenadj_param_t ryzenadj_params[MAX_RYZENADJ_PARAMS];

void trim(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

void handle_resize(int sig) {
    (void)sig; // To avoid unused parameter warning
    resize_needed = 1;
}

void do_resize() {
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr, term_lines, term_cols);

    // Calculate sizes for CPU Control window
    int cpu_control_width = 30;  // Fixed width
    int cpu_control_height = visible_count + 3;  // All CPUs + 2 for header, 1 for border

    // Calculate sizes for Ryzenadj Parameters window
    int ryzenadj_width = MAX_PARAM_NAME_LENGTH + MAX_PARAM_VALUE_LENGTH + RYZENADJ_WINDOW_PADDING;
    int ryzenadj_height = 0;
    for (int i = 0; i < MAX_RYZENADJ_PARAMS; i++) {
        if (ryzenadj_params[i].name[0] == '\0') break;
        ryzenadj_height++;
    }
    ryzenadj_height += 3;  // 2 for header, 1 for border

    // Use the larger of the two heights, but not exceeding the terminal height
    int window_height = (cpu_control_height > ryzenadj_height) ? cpu_control_height : ryzenadj_height;
    window_height = (window_height < term_lines) ? window_height : term_lines;

    // Ensure windows fit within the terminal
    if (cpu_control_width + ryzenadj_width > term_cols) {
        ryzenadj_width = term_cols - cpu_control_width - 1;
    }

    // Resize and move windows
    wresize(menu_win, window_height, cpu_control_width);
    wresize(ryzenadj_win, window_height, ryzenadj_width);
    mvwin(menu_win, 0, 0);
    mvwin(ryzenadj_win, 0, cpu_control_width + 1);

    // Redraw everything
    draw_menu(menu_win, highlight, visible_cpus, visible_count);
    draw_ryzenadj_window(ryzenadj_win);

    wrefresh(menu_win);
    wrefresh(ryzenadj_win);
    refresh();
}

void draw_menu(WINDOW *win, int highlight, int visible_cpus[], int visible_count) {
    int max_y, max_x;
    getmaxyx(win, max_y, max_x);

    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, "CPU Control");
    mvwprintw(win, 1, 2, "-----------");

    int start_y = 2;
    int available_lines = max_y - start_y - 1; // -1 for bottom border
    int displayed_cpus = (visible_count < available_lines) ? visible_count : available_lines;

    for (int i = 0; i < displayed_cpus; i++) {
        char path[256];
        char buffer[2] = {0};
        sprintf(path, "/sys/devices/system/cpu/cpu%d/online", visible_cpus[i]);
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            if (fread(buffer, 1, 1, file) == 1) {
                if (active_window == 0 && i == highlight) {
                    wattron(win, A_STANDOUT | COLOR_PAIR(1));
                } else if (buffer[0] == '0') {
                    wattron(win, COLOR_PAIR(2));
                }
                mvwprintw(win, i + start_y, 2, "CPU %d: %s", visible_cpus[i], buffer[0] == '1' ? "online" : "offline");
                wattroff(win, A_STANDOUT | COLOR_PAIR(1) | COLOR_PAIR(2));
            }
            fclose(file);
        }
    }
    wrefresh(win);
}

void parse_ryzenadj_output() {
    FILE *ryzenadj_output = popen("sudo ryzenadj -i", "r");
    if (ryzenadj_output == NULL) {
        return;
    }

    char buffer[1024];
    int parsed_count = 0;

    // Clear previous data
    memset(ryzenadj_params, 0, sizeof(ryzenadj_params));

    struct {
        const char *name;
        const char *param;
    } target_params[] = {
        {"STAPM LIMIT", "a"},
        {"STAPM VALUE", ""},
        {"PPT LIMIT FAST", "b"},
        {"PPT VALUE FAST", ""},
        {"PPT LIMIT SLOW", "c"},
        {"PPT VALUE SLOW", ""},
        {"TDC LIMIT VDD", "g"},
        {"TDC VALUE VDD", ""},
        {"TDC LIMIT SOC", "j"},
        {"TDC VALUE SOC", ""},
        {"EDC LIMIT SOC", "l"},
        {"EDC VALUE SOC", ""},
        {"THM LIMIT CORE", "f"},
        {"THM VALUE CORE", ""}
    };
    const int num_target_params = sizeof(target_params) / sizeof(target_params[0]);

    // Skip the header lines
    fgets(buffer, sizeof(buffer), ryzenadj_output);
    fgets(buffer, sizeof(buffer), ryzenadj_output);

    while (fgets(buffer, sizeof(buffer), ryzenadj_output) != NULL && parsed_count < num_target_params) {
        buffer[strcspn(buffer, "\n")] = 0;

        for (int i = 0; i < num_target_params; i++) {
            if (strstr(buffer, target_params[i].name) != NULL) {
                char name[50], value[50], param[50];
                if (sscanf(buffer, "| %49[^|] | %49[^|] | %49[^|]", name, value, param) >= 2) {
                    trim(name);
                    trim(value);
                    trim(param);

                    // Check if this parameter already exists and is user-modified
                    int existing_index = -1;
                    for (int j = 0; j < parsed_count; j++) {
                        if (strcmp(ryzenadj_params[j].name, name) == 0) {
                            existing_index = j;
                            break;
                        }
                    }

                    if (existing_index != -1 && ryzenadj_params[existing_index].user_modified) {
                        // Preserve user-modified value
                    } else {
                        // Update or add new parameter
                        if (existing_index == -1) {
                            existing_index = parsed_count;
                            parsed_count++;
                        }
                        strncpy(ryzenadj_params[existing_index].name, name, sizeof(ryzenadj_params[existing_index].name) - 1);
                        strncpy(ryzenadj_params[existing_index].value, value, sizeof(ryzenadj_params[existing_index].value) - 1);
                        strncpy(ryzenadj_params[existing_index].parameter, target_params[i].param, sizeof(ryzenadj_params[existing_index].parameter) - 1);
                        ryzenadj_params[existing_index].adjustable = (target_params[i].param[0] != '\0');
                        ryzenadj_params[existing_index].user_modified = 0;  // Reset user_modified flag for non-user-modified parameters
                    }
                    break;
                }
            }
        }
    }

    pclose(ryzenadj_output);

    // Ensure ryzenadj_highlight is pointing to a valid, adjustable parameter
    if (!ryzenadj_params[ryzenadj_highlight].adjustable || ryzenadj_params[ryzenadj_highlight].name[0] == '\0') {
        for (int i = 0; i < parsed_count; i++) {
            if (ryzenadj_params[i].adjustable) {
                ryzenadj_highlight = i;
                break;
            }
        }
    }
}

void draw_ryzenadj_window(WINDOW *win) {
    int max_y, max_x;
    getmaxyx(win, max_y, max_x);

    wclear(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, "Ryzenadj Parameters");
    mvwprintw(win, 1, 2, "-------------------");

    int start_y = 2;
    int available_lines = max_y - start_y - 1; // -1 for bottom border
    int displayed = 0;

    for (int i = 0; i < MAX_RYZENADJ_PARAMS && strlen(ryzenadj_params[i].name) > 0 && displayed < available_lines; i++) {
        if (ryzenadj_params[i].adjustable && active_window == 1 && i == ryzenadj_highlight) {
            wattron(win, A_STANDOUT | COLOR_PAIR(1));
        }
        mvwprintw(win, displayed + start_y, 2, "%-*s | %-*s",
                  MAX_PARAM_NAME_LENGTH, ryzenadj_params[i].name,
                  MAX_PARAM_VALUE_LENGTH, ryzenadj_params[i].value);
        if (ryzenadj_params[i].adjustable && active_window == 1 && i == ryzenadj_highlight) {
            wattroff(win, A_STANDOUT | COLOR_PAIR(1));
        }
        displayed++;
    }

    if (displayed == 0) {
        mvwprintw(win, start_y, 2, "No data available.");
    }

    wrefresh(win);
}

void update_single_parameter(int index) {
    char command[100];
    snprintf(command, sizeof(command), "sudo ryzenadj -i | grep '%s'", ryzenadj_params[index].name);

    FILE *fp = popen(command, "r");
    if (fp != NULL) {
        char buffer[1024];
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            char name[50], value[50];
            if (sscanf(buffer, "| %49[^|] | %49[^|] |", name, value) == 2) {
                trim(value);
                if (!ryzenadj_params[index].user_modified) {
                    strncpy(ryzenadj_params[index].value, value, sizeof(ryzenadj_params[index].value) - 1);
                }
            }
        }
        pclose(fp);
    }
}

void execute_ryzenadj_command(const char* param, int increment) {
    char command[100];
    int current_value = (int)(atof(ryzenadj_params[ryzenadj_highlight].value) * 1000);
    int new_value = current_value + increment;

    snprintf(command, sizeof(command), "sudo ryzenadj -%s %d", param, new_value);

    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        return;
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Read output but don't do anything with it
    }

    int status = pclose(fp);
    if (status != -1 && WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0) {
            // Only update the value if ryzenadj executed successfully
            snprintf(ryzenadj_params[ryzenadj_highlight].value, sizeof(ryzenadj_params[ryzenadj_highlight].value), "%.3f", new_value / 1000.0);
            ryzenadj_params[ryzenadj_highlight].user_modified = 1;

            // Update the display for this parameter
            update_single_parameter(ryzenadj_highlight);
        }
    }

    // Update only the changed parameter
    update_single_parameter(ryzenadj_highlight);
}

int main() {
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    // Hide the cursor
    curs_set(0);

    // Initialize colors
    start_color();
    use_default_colors();
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_RED, -1);

    // Get initial terminal size
    getmaxyx(stdscr, term_lines, term_cols);

    // Set up signal handling for resize
    signal(SIGWINCH, handle_resize);

    // Find visible CPUs
    for (int i = 0; i < MAX_CPUS; i++) {
        char path[256];
        sprintf(path, "/sys/devices/system/cpu/cpu%d/online", i);
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            char buffer[2];
            if (fread(buffer, 1, 1, file) == 1) {
                if (buffer[0] == '0' || buffer[0] == '1') {
                    visible_cpus[visible_count++] = i;
                }
            }
            fclose(file);
        }
    }

    // Calculate initial window sizes
    int cpu_control_width = 30;
    int cpu_control_height = visible_count + 3;  // All CPUs + 2 for header, 1 for border
    int ryzenadj_width = MAX_PARAM_NAME_LENGTH + MAX_PARAM_VALUE_LENGTH + RYZENADJ_WINDOW_PADDING;
    int ryzenadj_height = 17;  // 14 parameters + 2 for header, 1 for border

    int window_height = (cpu_control_height > ryzenadj_height) ? cpu_control_height : ryzenadj_height;
    window_height = (window_height < term_lines) ? window_height : term_lines;

    // Ensure windows fit within the terminal
    if (cpu_control_width + ryzenadj_width > term_cols) {
        ryzenadj_width = term_cols - cpu_control_width - 1;
    }

    // Create windows
    menu_win = newwin(window_height, cpu_control_width, 0, 0);
    ryzenadj_win = newwin(window_height, ryzenadj_width, 0, cpu_control_width + 1);

    // Enable keypad input for menu window
    keypad(menu_win, TRUE);

    // Initial parse of ryzenadj output
    parse_ryzenadj_output();

    // Update all parameters
    for (int i = 0; i < MAX_RYZENADJ_PARAMS && ryzenadj_params[i].name[0] != '\0'; i++) {
        update_single_parameter(i);
    }

    // Main loop
    int ch;
    struct timeval tv;
    fd_set fds;
    time_t last_update = time(NULL);
    time_t last_user_action = time(NULL);

    while (1) {
        if (resize_needed) {
            resize_needed = 0;
            do_resize();
            last_user_action = time(NULL);
            continue;
        }

        // Draw both windows
        draw_menu(menu_win, highlight, visible_cpus, visible_count);
        draw_ryzenadj_window(ryzenadj_win);

        // Refresh both windows
        wrefresh(menu_win);
        wrefresh(ryzenadj_win);

        // Set up the file descriptor set
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        // Set up the timeout
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms

        // Wait for input or timeout
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);

        if (ret == -1) {
            // Error
            if (errno != EINTR) {  // Ignore interrupted system call
                break;
            }
        } else if (ret == 0) {
            // Timeout (no input)
        } else {
            // Input is available
            ch = wgetch(menu_win);
            last_user_action = time(NULL);

            switch (ch) {
                case KEY_UP:
                    if (active_window == 1) {
                        int original_highlight = ryzenadj_highlight;
                        do {
                            ryzenadj_highlight = (ryzenadj_highlight - 1 + MAX_RYZENADJ_PARAMS) % MAX_RYZENADJ_PARAMS;
                            if (ryzenadj_highlight == original_highlight) {
                                // We've looped through all options, stop here
                                break;
                            }
                        } while (!ryzenadj_params[ryzenadj_highlight].adjustable || ryzenadj_params[ryzenadj_highlight].name[0] == '\0');
                    } else {
                        if (highlight > 0)
                            --highlight;
                    }
                    break;
                case KEY_DOWN:
                    if (active_window == 1) {
                        int original_highlight = ryzenadj_highlight;
                        do {
                            ryzenadj_highlight = (ryzenadj_highlight + 1) % MAX_RYZENADJ_PARAMS;
                            if (ryzenadj_highlight == original_highlight) {
                                // We've looped through all options, stop here
                                break;
                            }
                        } while (!ryzenadj_params[ryzenadj_highlight].adjustable || ryzenadj_params[ryzenadj_highlight].name[0] == '\0');
                    } else {
                        if (highlight < visible_count - 1)
                            ++highlight;
                    }
                    break;
                case KEY_LEFT:
                case KEY_RIGHT:
                    if (active_window == 1 && ryzenadj_params[ryzenadj_highlight].adjustable) {
                        const char* param = ryzenadj_params[ryzenadj_highlight].parameter;
                        if (param[0] != '\0') {
                            execute_ryzenadj_command(param, ch == KEY_LEFT ? -1000 : 1000);
                            draw_ryzenadj_window(ryzenadj_win);
                        }
                    }
                    break;
                case '\t': // Tab key
                    active_window = 1 - active_window; // Toggle between 0 and 1
                    break;
                case 10:  // Enter key
                    if (active_window == 0) {
                        char path[256];
                        sprintf(path, "/sys/devices/system/cpu/cpu%d/online", visible_cpus[highlight]);
                        int fd = open(path, O_RDWR);
                        if (fd != -1) {
                            char current_state;
                            if (read(fd, &current_state, 1) == 1) {
                                lseek(fd, 0, SEEK_SET);
                                char new_state = (current_state == '1') ? '0' : '1';
                                if (write(fd, &new_state, 1) == 1) {
                                    // Successfully toggled CPU state
                                } else {
                                    // Handle write error
                                }
                            } else {
                                // Handle read error
                            }
                            close(fd);
                        }
                        // Force an immediate redraw of the CPU menu
                        draw_menu(menu_win, highlight, visible_cpus, visible_count);
                        wrefresh(menu_win);
                    }
                    break;
                case 'r': // Reset user modifications
                    for (int i = 0; i < MAX_RYZENADJ_PARAMS && ryzenadj_params[i].name[0] != '\0'; i++) {
                        ryzenadj_params[i].user_modified = 0;
                    }
                    parse_ryzenadj_output();
                    for (int i = 0; i < MAX_RYZENADJ_PARAMS && ryzenadj_params[i].name[0] != '\0'; i++) {
                        update_single_parameter(i);
                    }
                    break;
                case 'q':
                    goto cleanup;
            }
        }

        // Check if 2 seconds have passed since last update AND last user action
        time_t current_time = time(NULL);
        if (difftime(current_time, last_update) >= 2 && difftime(current_time, last_user_action) >= 2) {
            // Update display for all parameters
            for (int i = 0; i < MAX_RYZENADJ_PARAMS && ryzenadj_params[i].name[0] != '\0'; i++) {
                update_single_parameter(i);
            }
            last_update = current_time;
        }
    }

cleanup:
    // Clean up
    endwin();
    return 0;
}
