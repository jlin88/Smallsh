// Name: Justin Lin
// Class: CS344
// Program: smallsh
// Date: 2/20/20

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>

// Constants from specifications
#define NUM_ARGS 512
#define INPUT_LEN 2048
#define PIDS_SIZE 100

// Struct to keep argument flags
struct Flags {
    bool background;
    bool input;
    bool output;
    int numArgs;
    int inputIndex;
    int outputIndex;
};

// Struct to keep track of child PIDS
struct Pids {
    int childPID[PIDS_SIZE];
    int* numPids;
    int* exitStatus;
    int* fgExitStatus;
};

// Atomic Global Variable for catchSIGTSTP suggested by TA
// Value: 1 for allowing background processes, 0 for not
// Source: https://stackoverflow.com/questions/24931456/how-does-sig-atomic-t-actually-work
volatile sig_atomic_t allowBackground = 1;
// Global structs for signal handlers
struct sigaction sigInt = {0};
struct sigaction sigTstp = {0};


// Forward declarations so declaration order doesn't matter
void getInput(char** args, struct Flags* flags);
void parseInput(char** args, char* input, struct Flags* flags);
char* replacePID(char* input);
void executeCMD(char** args, struct Flags* flags, struct Pids* pids);
void redirectFile(char** args, struct Flags* flags);
void shiftPids(struct Pids* pids, int index);
void resetFlags(struct Flags* flags);
// Built-in shell commands
void exitCMD(char** args, struct Flags* flags, struct Pids* pids);
void cdCMD(char* directory);
void statusCMD(int exitMethod);
// Signal function
void catchSIGTSTP(int signo);


// Functions definitions

// Gets user input that is not a comment and splits each word into array passed
// in. Returns the number of arguments (words) user passed in.
void getInput(char** args, struct Flags* flags) {
    // Get input of the right length that isn't a comment
    int numChars = 0;
    size_t bufferSize = 0;
    char* input = NULL;

    // Loop input until we get something valid to parse
    do {
        printf(":");
        fflush(stdout);
        // Get input from stdin
        //fgets(input, INPUT_LEN, stdin);
        numChars = getline(&input, &bufferSize, stdin);
        // Make sure getline didn't get interrupted. If it did, clear stdin.
        if (numChars == -1)
            clearerr(stdin);
    } while ((input[0] == '#') || (strlen(input) < 1) || (strlen(input) > INPUT_LEN) || input[0] == '\n');

    // Remove newline char if it exists
    if (input[strlen(input)-1] == '\n') {
        input[strlen(input)-1] = '\0';
    }

    // Check to see if we need to expand "$$" to actual PID
    char *expanded = NULL;
    expanded = replacePID(input);
    strcpy(input, expanded);

    // Parse input to put everything into arg array and set relevant flags
    parseInput(args, input, flags);

    // Free previously dynamically allocated memory from expansion
    free(expanded);
}

// Parses the user input and sets the relevant flags if necessary for execution
// of smallsh
void parseInput(char** args, char* input, struct Flags* flags) {
    int numArgs = 0;

    // Parse input and put everything into array
    char* token = strtok(input, " ");
    while (token != NULL) {
        // Set flags so we know to redirect stdin
        if (strcmp(token, "<") == 0) {
            flags->input = true;
            // Index of file to redirect stdin to
            flags->inputIndex = numArgs+1;
        // Set flags so we know to redirect stdout
        } else if (strcmp(token, ">") == 0) {
            flags->output = true;
            // Index of file to redirect stdout to
            flags->outputIndex = numArgs+1;
        }

        // Put each pointer to word into args array
        // https://www.geeksforgeeks.org/strdup-strdndup-functions-c/
        //strcpy(args[numArgs], token);
        args[numArgs] = strdup(token);
        numArgs++;

        // Get next word
        token = strtok(NULL, " ");
    }

    // Check if foreground/background process
    if (strcmp(args[numArgs-1], "&") == 0) {
        // Replace '&' with NULL
        args[numArgs-1] = NULL;
        flags->background = true;
    // If last argument is not &, add a NULL to array for execvp
    } else {
        args[numArgs] = NULL;
        numArgs++;
    }

    // Save number of arguments so we can reference later
    flags->numArgs = numArgs;
}

// Expands all "$$" sequences to be the PID of shell
char* replacePID(char* input) {
    char *buffer = NULL;
    buffer = strdup(input);

    // Dynamically allocate memory so we can return string with expanded PIDs
    char *newString = malloc(INPUT_LEN * sizeof(char));
    memset(newString, '\0', INPUT_LEN);

    // Get length of pid
    char pid[INPUT_LEN];
    sprintf(pid, "%d", getpid());

    // Index for newString
    int j = 0;
    // Copy buffer into newString expanding the "$$" to shell PID
    for (int i = 0; i < strlen(buffer); i++) {
        // "$$" found so concatenate PID and increment j index
        if ((buffer[i] == '$') && (i+1 < strlen(buffer)) && (buffer[i+1] == '$')) {
            strcat(newString, pid);
            j = j + strlen(pid);
            i++;
        // Copy everything else
        } else {
            newString[j] = buffer[i];
            j++;
        }
    }

    // Free strdup memory
    free(buffer);

    return newString;
}

// Exit command kills all other processes/jobs and terminates the shell
void exitCMD(char** args, struct Flags* flags, struct Pids* pids) {
    printf("Cleaning up children...\n");
    fflush(stdout);

    // Cleanup remaining child processes
    for (int i = 0; i < *(pids->numPids); i++) {
        kill(pids->childPID[i], SIGKILL);
    }

    // Free each pointer in args
    for (int i = 0; i < flags->numArgs; i++) {
        free(args[i]);
        args[i] = NULL;
    }

    printf("Children have been...terminated\nNow Exiting...Goodbye!\n");
    fflush(stdout);

    // Exit shell
    exit(0);
}

// Changes the working directory of the shell. When cd is invoked with no arguments,
// will change the working directory to the directory specified in the HOME environment
// variable. If an argument is provided, will change working directory to the path
// specified.
void cdCMD(char* directory) {
    // If NULL argument then user did not pass in argument so we change directory
    // to path specified in HOME environment var
    if (directory == NULL) {
        chdir(getenv("HOME"));
    // Change working directory to path argument
    } else {
        chdir(directory);
    }
}

// Checks status of last foreground process run by shell and prints result
void statusCMD(int exitMethod) {
    // Check if exited normally
    if (WIFEXITED(exitMethod)) {
        printf("exit value %d\n", WEXITSTATUS(exitMethod));
        fflush(stdout);
    // Check if exited from signal
    } else if (WIFSIGNALED(exitMethod)) {
        printf("terminated by signal %d\n", WTERMSIG(exitMethod));
        fflush(stdout);
    }
}

// Helper function to shift pids of completed child processes so there are no
// holes left in pids array
void shiftPids(struct Pids* pids, int index) {
    for (int i = index; i < *(pids->numPids); i++) {
        pids->childPID[i] = pids->childPID[i+1];
    }
    *(pids->numPids) -= 1;
}

// Helper function to reset the flags struct
void resetFlags(struct Flags* flags) {
    flags->background = false;
    flags->input = false;
    flags->output = false;
    flags->numArgs = 0;
    flags->inputIndex = 0;
    flags->outputIndex = 0;
}

// Runs the smallsh program. Will check for built in commands first. If input
// is not a built in command, then will fork off a child process to execute the
// command. If the command is not valid, will exit.
void executeCMD(char** args, struct Flags* flags, struct Pids* pids) {
    pid_t spawnPID = -5;
    pid_t childPID = -5;

    // Check for built in command first
    if (strcmp(args[0], "exit") == 0) {
        exitCMD(args, flags, pids);
    // Change directory
    } else if (strcmp(args[0], "cd") == 0) {
        cdCMD(args[1]);
    // Status
    } else if (strcmp(args[0], "status") == 0) {
        statusCMD(*(pids->fgExitStatus));
    // Not built in command, so fork off child process to execute command
    } else {
        spawnPID = fork();

        // Switch statement so parent/child take different actions
        switch (spawnPID) {
            // Error forking process
            case -1: {
                perror("fork()");
                exit(1);
                break;
            }
            // Child process
            case 0: {
                // Allow child process to be terminated by SIGINT signal
                sigInt.sa_handler = SIG_DFL;
                sigaction(SIGINT, &sigInt, NULL);

                // Check if we need to redirect input/output. If process is background
                // process and I/O not redirected, /dev/null will be used
                redirectFile(args, flags);

                // Execute command
                if (execvp(*args, args) < 0) {
                    // If we enter if statement, exec failed so print error and exit child process
                    perror("Exec failure");
                    exit(1);
                }
                break;
            }
            // Parent process
            default: {
                // Run in background so we store childPID and continue running
                if (flags->background && allowBackground) {
                    printf("background pid is %d\n", spawnPID);
                    fflush(stdout);

                    // Store PID of child
                    pids->childPID[*(pids->numPids)] = spawnPID;
                    *(pids->numPids) += 1;
                // Run in foreground so we wait for completion
                } else {
                    childPID = waitpid(spawnPID, pids->fgExitStatus, 0);

                    // Check if interrupted by signal
                    if (WIFSIGNALED(*(pids->fgExitStatus))) {
                            printf("terminated by signal %d\n", WTERMSIG(*(pids->fgExitStatus)));
                            fflush(stdout);
                    }
                }

                // Loop through all child processes that have not been terminated
                for (int i = 0; i < *(pids->numPids); i++) {
                    // Check if any child processes have completed
                    childPID = waitpid(pids->childPID[i], pids->exitStatus, WNOHANG);

                    // If a child process finished, get exit/signal status
                    if (childPID != 0) {
                        if (WIFEXITED(*(pids->exitStatus))) {
                            printf("background pid %d is done: exit value %d\n", childPID, WEXITSTATUS(*(pids->exitStatus)));
                            fflush(stdout);
                        } else if (WIFSIGNALED(*(pids->exitStatus))) {
                            printf("background pid %d is done: terminated by signal %d\n", childPID, WTERMSIG(*(pids->exitStatus)));
                            fflush(stdout);
                        }
                        // Remove PID of completed child process
                        shiftPids(pids, i);
                    }
                }
                break;
            }
        }
    }
}

// Helper function to perform I/O redirection checking and will redirect to
// specified files if they exist. If it is a background process and I/O files
// are not specified, /dev/null is used instead
void redirectFile(char** args, struct Flags* flags) {
    int file = NULL;

    // Check if we need to redirect input
    if (flags->input) {
        // Open file descriptor for read only
        file = open(args[flags->inputIndex], O_RDONLY);
        if (file == -1) {
            // Error checking
            perror("Error opening file for read");
            exit(1);
        }

        // Copies file descriptor
        if (dup2(file, 0) == -1) {
            // Error checking
            perror("Error redirecting stdin");
            exit(1);
        }

        // Close file
        close(file);

        // Remove redirection arguments from args list so we don't pass into exec
        args[flags->inputIndex] = NULL;
        args[flags->inputIndex-1] = NULL;
    }

    // Check if we need to redirect output
    if (flags->output) {
        // Open file descriptor for write only, truncated if it exists, otherwise
        // create if it doesnt.
        file = open(args[flags->outputIndex], O_CREAT | O_RDWR | O_TRUNC, 0644);
        if (file == -1) {
            // Error checking
            perror("Error opening file for write");
            exit(1);
        }

        // Copies file descriptor
        if (dup2(file, 1) == -1) {
            // Error checking
            perror("Error redirecting stdout");
            exit(1);
        }

        // Close file
        close(file);

        // Remove redirection arguments from args list so we don't pass into exec
        args[flags->outputIndex] = NULL;
        args[flags->outputIndex-1] = NULL;
    }

    // If background process and input/output redirection not specified, redirect
    // both to dev/null
    if (allowBackground && flags->background) {
        // Redirection for input
        if (!flags->input) {
            // Open /dev/null
            file = open("/dev/null", O_RDONLY);

            // Check opened correctly
            if (file == -1) {
                perror("open");
                exit(1);
            }

            // Redirect stdin to /dev/null
            if (dup2(file, 0) == -1) {
                perror("Error redirecting stdin");
                exit(1);
            }

            // Close file
            close(file);
        }

        // Redirection for output
        if (!flags->output) {
            // Open file descriptor for write only, truncated if it exists, otherwise
            // create if it doesnt.
            file = open("/dev/null", O_CREAT | O_RDWR | O_TRUNC, 0644);

            // Check opened correctly
            if (file == -1) {
                perror("Error opening file for write");
                exit(1);
            }

            // Copies file descriptor
            if (dup2(file, 1) == -1) {
                perror("Error redirecting stdout");
                exit(1);
            }

            // Close file
            close(file);
        }
    }
}

// Sighandler function that will toggle allowing background functions or not. Achieves
// this by changing a volatile atomic int (suggested by TA)
void catchSIGTSTP(int signo) {
    // Background processes are currently allowed so we need to turn it off
    if (allowBackground == 1) {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 49);
        allowBackground = 0;
    // Background processes are currently not allowed so we need to turn it on
    } else {
        char* message = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 29);
        allowBackground = 1;
    }
}

int main() {
    char* args[NUM_ARGS];
    // Struct to keep track of flags and information about arguments
    struct Flags flags = {false, false, false, 0, 0, 0};
    // Struct to keep track of child PIDs
    struct Pids pids;
    int zero = 0;
    int exitStatus = 0;
    int fgExitStatus = 0;

    // Init pids struct
    pids.numPids = &zero;
    pids.exitStatus = &exitStatus;
    pids.fgExitStatus = &fgExitStatus;

    // Init global sighandlers
    sigInt.sa_handler = SIG_IGN;
    sigfillset(&(sigInt.sa_mask));
    sigInt.sa_flags = 0;
    sigaction(SIGINT, &sigInt, NULL);

    sigTstp.sa_handler = catchSIGTSTP;
    sigfillset(&(sigTstp.sa_mask));
    sigTstp.sa_flags = 0;
    sigaction(SIGTSTP, &sigTstp, NULL);

    // Loop until user exits program
    while (true) {
        // Init pids to a sentinel value
        for (int i = 0; i < PIDS_SIZE; i++) {
            pids.childPID[i] = -1;
        }

        // Get user input and set flags in struct
        getInput(args, &flags);

        // Execute commands user entered
        executeCMD(args, &flags, &pids);

        // Free each pointer in args
        for (int i = 0; i < flags.numArgs; i++) {
            free(args[i]);
            args[i] = NULL;
        }
        memset(args, '\0', NUM_ARGS * sizeof(char*));

        // Reset arguments and flags
        resetFlags(&flags);
    }
    return 0;
}