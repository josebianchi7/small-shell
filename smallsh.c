#include <signal.h>         // Signal/ messages
#include <stdlib.h>         // Memory management 
#include <stdio.h>          // Input/ output
#include <stdbool.h>        // Boolean values
#include <string.h>         // String functions
#include <sys/types.h>      // Process ID functions
#include <unistd.h>         // Process management/ file operations
#include <sys/wait.h>       // Process termination functions
#include <dirent.h>         // Directory management
#include <fcntl.h>          // File descriptor options

#define INPUT_LENGTH 2048
#define MAX_ARGS 512

/*
Program Name: SMALLSH
Author: Jose Bianchi
Description: Shell program called smallsh that implements a subset of 
features of well-known shells, such as bash. Shell program can:
    1. Provide prompt for running commands
    2. Handle blank lines and comments (lines beginning with #)
    3. Execute correctly comands exit, cd, and status 
    4. Execute other commands by using appropiate exec() function
    5. Support input and output redirection
    6. Support running commands in foreground and background 
    7. Custom handlers for SIGINT and SIGSTP
*/

/*
* Struct to parse user input into appropiate components from sample_parser.c.  
*   Source Code Title: sample_parser.c
*   Author: Nauman Chaudhry, Guillermo Tonsmann
*   Date: 17 May 2025
*   Availability: https://canvas.oregonstate.edu/courses/1999732/assignments/9997827
*/
struct command_line {
    // parsed argument string array
	char *argv[MAX_ARGS + 1];
    // argument count
	int argc;               
	char *input_file;           
	char *output_file;
	// background execution flag
    bool is_bg;                 
};

// PID node structure
struct pid_item {
    int pid_num;
    struct pid_item *next;
};

// Functions declarations:
struct command_line *parse_input();
void print_cmd(struct command_line *cmd);
void free_cmd_line(struct command_line *cmd);
int add_pid(struct pid_item *pid_head, int pid_val);
void end_children_free_list(struct pid_item *pid_head);
void check_bg_processes(struct pid_item *pid_head);
bool *fg_mode_ptr;
void handle_SIGTSTP(int signo);

int main(void) {
    struct command_line *curr_command;
    // Create list to track child PIDs 
    struct pid_item *head_child_pid = malloc(sizeof(struct pid_item));
    head_child_pid->pid_num = -1;
    head_child_pid->next = NULL;
    int add_pid_result;
    // store last foreground exit value
    int last_fg_status = 0;
    int exit_by_signal = false;
    int cd_result;
    int child_status;
    int input_fd;
    int output_fd;
    bool fg_only_mode = false;
    fg_mode_ptr = &fg_only_mode;

    // Initialize and install signal handler to ignore SIGINT
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Initialize and install signal handler for SIGSTP 
    struct sigaction SIGSTP_action = {0};
    SIGSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGSTP_action.sa_mask);
    SIGSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGSTP_action, NULL);
    
    while(true) {
        // Get and parse user command input
        curr_command = parse_input();

        if (curr_command->argc == 0) {  
            printf("# that was a blank command line, this is a comment line\n");
            fflush(stdout);
        
        } else if (curr_command->argv[0][0] != '#') {

            if (strcmp(curr_command->argv[0], "exit") == 0) {
                free_cmd_line(curr_command);
                // Confirm each process terminated, free PID list memory
                end_children_free_list(head_child_pid);
                // Terminate self
                exit(0);
            
            } else if (strcmp(curr_command->argv[0], "cd") == 0) {
                char *curr_dir = getenv("PWD");
                // No addditional arguments, cd to HOME env variable
                if(curr_command->argc == 1) {
                    char *home_dir = getenv("HOME");
                    setenv("PWD", home_dir, 1);
                    cd_result = chdir(home_dir);
                } else {
                    char *new_path = curr_command->argv[1];
                    cd_result = chdir(new_path);
                    setenv("PWD", new_path, 1);
                }
                if (cd_result != 0) {
                    setenv("PWD", curr_dir, 1);
                    perror("cd failed.");
                    fflush(stderr);
                }
            
            } else if (strcmp(curr_command->argv[0], "status") == 0) {
                if (exit_by_signal) {
                    printf("terminated by signal %d\n", last_fg_status);
                } else {
                    printf("exit value %d\n", last_fg_status);
                }
                fflush(stdout);
            
            } else {
                // fork child to execute non-built in command (if able)
                pid_t spawn_pid = fork();
                switch (spawn_pid) {
                    case -1:
                        perror("fork() failed!");
                        fflush(stderr);
                        break;
                    case 0:
                        // Child
                        if (curr_command->is_bg) {
                            // Child background processes ignore SIGINT and SIGSTP
                            struct sigaction ignore_action = {0};
                            ignore_action.sa_handler = SIG_IGN;
                            sigaction(SIGINT, &ignore_action, NULL);

                            struct sigaction ignore_SIGTSTP = {0};
                            ignore_SIGTSTP.sa_handler = SIG_IGN;
                            sigaction(SIGTSTP, &ignore_SIGTSTP, NULL);

                        } else {
                            // Child foreground process follow default SIGINT
                            struct sigaction default_action = {0};
                            default_action.sa_handler = SIG_DFL;
                            sigaction(SIGINT, &default_action, NULL);
                        }

                        // Redirect input and/ or output if specified and able
                        if (curr_command->input_file != NULL) {
                            input_fd = open(curr_command->input_file, O_RDONLY);
                            if (input_fd == -1) {
                                fprintf(stderr, "cannot open %s for input\n", curr_command->input_file);
                                fflush(stderr);
                                exit(1);
                            }
                            dup2(input_fd, STDIN_FILENO);
                            close(input_fd);

                        } else if (curr_command->is_bg && curr_command->input_file == NULL) {
                            // Redirect background process stdin if not redirected
                            input_fd = open("/dev/null", O_WRONLY);
                            dup2(input_fd, STDIN_FILENO);
                            close(input_fd);
                        }

                        if (curr_command->output_file != NULL) {
                            output_fd = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if (output_fd == -1) {
                                fprintf(stderr, "cannot open %s for output\n", curr_command->output_file);
                                fflush(stderr);
                                exit(1);
                            }
                            dup2(output_fd, STDOUT_FILENO);
                            close(output_fd);

                        } else if (curr_command->is_bg && curr_command->output_file == NULL) {
                            // Redirect background process stdout if not redirected
                            output_fd = open("/dev/null", O_WRONLY);
                            dup2(output_fd, STDOUT_FILENO);
                            close(output_fd);
                        }
                        // Search for executable file to execute command program replacing current program 
                        execvp(curr_command->argv[0], curr_command->argv);
                        // If execvp fails, errno is set, report failure, terminate self (child)
                        printf("%s: no such file or directory\n", curr_command->argv[0]);
                        fflush(stdout);
                        exit(1);
                        break;

                    default:
                        // Parent (only waits for foreground process, track bg PIDs)
                        if (curr_command->is_bg) {
                            add_pid_result = add_pid(head_child_pid, spawn_pid);
                            if (add_pid_result != 0) {
                                printf("background pid %d was not added to job list\n", spawn_pid);
                                fflush(stdout);
                            }
                            printf("background pid is %d\n", spawn_pid);
                            fflush(stdout);
                            
                        } else {
                            waitpid(spawn_pid, &child_status, 0);
                            if (WIFEXITED(child_status)) {
                                // Normal exit
                                last_fg_status = WEXITSTATUS(child_status);
                                exit_by_signal = false;
                            } else {
                                // Abnormal exit
                                last_fg_status = WTERMSIG(child_status);
                                exit_by_signal = true;
                                fprintf(stderr, "terminated by signal %d\n", last_fg_status);
                                fflush(stdout);
                            }
                        }
                }
            }              
        } 
        // Reset and check if any background processes completed
        free_cmd_line(curr_command);
        check_bg_processes(head_child_pid); 
    }
    return 0;
}

/*
* Function to parse user input into command_line struct modified from sample_parser.c. 
*   Base Code Title: sample_parser.c
*   Author: Nauman Chaudhry, Guillermo Tonsmann
*   Date: 17 May 2025
*   Availability: https://canvas.oregonstate.edu/courses/1999732/assignments/9997827
*/
struct command_line *parse_input() {
    char user_input[INPUT_LENGTH];
    // Allocate memory for one command_line struct
    struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));
    
    // Get input
    printf(": ");
    fflush(stdout);
    fgets(user_input, INPUT_LENGTH, stdin);

    // Tokenize input, delimiters inlcude space and new line
    char *token = strtok(user_input, " \n");
    char *last_token = NULL;

	while(token){
        last_token = token;
		if(strcmp(token,"<") == 0){
		    curr_command->input_file = strdup(strtok(NULL," \n"));
		} else if(strcmp(token,">") == 0){
			curr_command->output_file = strdup(strtok(NULL," \n"));
		} else{
            // Increment argument count and store current string as argument in incremented index
			curr_command->argv[curr_command->argc++] = strdup(token);
		}
		token=strtok(NULL," \n");
	}
    curr_command->argv[curr_command->argc] = NULL;

    // Check if last word is "&" for background flag
    if (last_token && (strcmp(last_token, "&") == 0)) {
        // Remove "&" from array
        free(curr_command->argv[curr_command->argc - 1]);
        curr_command->argc--;
        curr_command->argv[curr_command->argc] = NULL;
        // If not foreground only mode, turn on background flag
        if (!*fg_mode_ptr) {
            curr_command->is_bg = true;
        } else {
            curr_command->is_bg = false;
        }
    }
    return curr_command;
};

/*
* Function: print_cmd()
*   Prints data members of command_line struct argument (used for testing).
*   :param struct command_line *cmd: structure for command line input.
*/
void print_cmd(struct command_line *cmd) {
    printf("Command structure: \n");
    fflush(stdout);
    printf("total arguments: %d\n", cmd->argc);
    fflush(stdout);
    for (int i = 0; i < cmd->argc; i++) {
        printf("argument %d: %s\n", i, cmd->argv[i]);
        fflush(stdout);
    }
    printf("input file: %s\n", cmd->input_file);
    fflush(stdout);
    printf("output file: %s\n", cmd->output_file);
    fflush(stdout);
    printf("background function: %s\n", cmd->is_bg ? "true" : "false");
    fflush(stdout); 
}

/*
* Function: free_cmd_line()
*   Frees memory allocated in instance of struct command_line
*   :param struct command_line *cmd: structure for command line input. 
*/
void free_cmd_line(struct command_line *cmd) {
    if (cmd->input_file) {
        free(cmd->input_file);
    }
    if (cmd->output_file) {
        free(cmd->output_file);
    }
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd);
}

/*
* Function: add_pid()
*   Adds process id to linked list to track processes started from main().
*   :param struct pid_item *pid_head: head of current pid linked list
*   :param int pid_val: process id
*   :return int: success/ failure indicator
*/
int add_pid(struct pid_item *pid_head, int pid_val) {
    // Append to list if nonempty
    struct pid_item *curr = pid_head;
    while (curr->next != NULL) {
        curr = curr->next;
    }
    struct pid_item *new_pid = malloc(sizeof(struct pid_item));
    if (!new_pid) {
        printf("Failed to allocate memory for pid.\n");
        fflush(stdout);
        return 1;
    }
    new_pid->pid_num = pid_val;
    new_pid->next = NULL;
    curr->next = new_pid;

    return 0;
}

/*
* Function: end_children_free_list()
*   Kills any other processes or jobs that smallsh has started.
*   Frees memory in pid linked list.
*   :param struct pid_item *pid_head: head of current pid linked list 
*/
void end_children_free_list(struct pid_item *pid_head) {
    struct pid_item *curr_pid = pid_head->next;
    while (curr_pid != NULL) {
        // Attempt to kill process with current PID
        kill(curr_pid->pid_num, SIGTERM);
        curr_pid = curr_pid->next;
    }
    sleep(1);
    curr_pid = pid_head->next;
    // Verify processes were killed with WNOHANG for quick check
    while (curr_pid != NULL) {
        kill(curr_pid->pid_num, SIGTERM);
        if (waitpid(curr_pid->pid_num, NULL, WNOHANG) == 0) {
            kill(curr_pid->pid_num, SIGKILL);
            waitpid(curr_pid->pid_num, NULL, 0);
        }
        curr_pid = curr_pid->next;
    }
    // Free list memory
    while (pid_head != NULL) {
        curr_pid = pid_head;
        pid_head = pid_head->next;
        free(curr_pid);
    }
}

/*
* Function: check_bg_processes
*   Quick check using WNOHANG if any background processes have terminated.
*   Removes completed process PIDs from list.
*   :param struct pid_item *pid_head: head of current pid linked list
*/
void check_bg_processes(struct pid_item *pid_head) {
    struct pid_item *prev_pid = pid_head;
    struct pid_item *curr_pid = pid_head->next;
    while (curr_pid != NULL) {
        int child_status;
        pid_t exit_result = waitpid(curr_pid->pid_num, &child_status, WNOHANG);
        if (exit_result > 0) {
            if (WIFEXITED(child_status)) {
                printf("background pid %d is done: exit value %d\n", curr_pid->pid_num, WEXITSTATUS(child_status));
            } else if (WIFSIGNALED(child_status)) {
                printf("background pid %d is done: terminated by signal %d\n", curr_pid->pid_num, WTERMSIG(child_status));
            }
            fflush(stdout);
            // Remove terminated PID from list
            prev_pid->next = curr_pid->next;
            free(curr_pid);
            curr_pid = prev_pid->next;
        } else {
            prev_pid = curr_pid;
            curr_pid = curr_pid->next;
        }  
    }
}

/*
* Function: handle_SIGTSTP()
*   signal handler function for SIGSTP to switch foreground only mode.
*   :param int signo: signal number causing function call
*/
void handle_SIGTSTP(int signo) {
    if (*fg_mode_ptr) {
        char *fg_mode_msg = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, fg_mode_msg, strlen(fg_mode_msg));
        *fg_mode_ptr = false;
    } else {
        char *fg_mode_msg = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, fg_mode_msg, strlen(fg_mode_msg));
        *fg_mode_ptr = true;
    }
}
