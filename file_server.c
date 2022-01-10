#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

/**
 * Debug flags
 */
#define LOG_TO_CONSOLE  1          // Set to 1 to print logs to console, or to 0 to print to a log file.

/**
 * ANSI color codes for colored output.
 * See print_log() and print_log().
 * Adapted from https://stackoverflow.com/a/3219471/3350320.
 */
#define ANSI_RED        "\x1b[31m"
#define ANSI_GREEN      "\x1b[32m"
#define ANSI_YELLOW     "\x1b[33m"
#define ANSI_CYAN       "\x1b[36m"
#define ANSI_RESET      "\x1b[0m"

/**
 * Paths to files written to by the server.
 */
#define READ_FILE       "read.txt"
#define EMPTY_FILE      "empty.txt"
#define COMMANDS_FILE   "commands.txt"
#define LOG_FILE        "log.txt"

/**
 * Constants to denote request types, for convenience.
 * See determine_request().
 */
#define REQUEST_INVALID 0
#define REQUEST_READ    1
#define REQUEST_WRITE   2
#define REQUEST_EMPTY   3

/**
 * Buffer size for reading from files.
 */
#define READ_BUF_SIZE   1024

/**
 * We can't return values from threads, but we *can* pass a pointer to
 * a preallocated return value variable to them. Thus we make use of a
 * struct for bundling thread arguments and return values into a neat
 * little package.
 */
typedef struct thread_parcel ThreadParcel;
struct thread_parcel {
    char *cmdline;
    int return_value;
};

/**
 * To avoid race conditions with file accesses,
 * we keep track of open files in a linked list of path-semaphore objects.
 */
#define WAIT_DEALLOC   3
typedef struct open_file_obj OpenFile;
typedef struct open_file_node OpenFileNode;
struct open_file_obj {
    char *file_path;
    sem_t lock;
};
struct open_file_node {
    OpenFile *file;
    OpenFileNode *next;
    int ticks_before_dealloc;     // When this reaches 0, the node is deallocated.
};
sem_t open_files_lock;
sem_t log_file_lock;
OpenFileNode *open_files = NULL;

/*****************************
 *      Helper functions     *
 *****************************/

/**
 * @fn int determine_request(char *cmdline)
 * @brief Determines the type of request from the command line.
 * 
 * @param cmdline The command line from the client.
 * @return The type of request.
 */
int determine_request(char *cmdline) {
    // Check if the command line is empty.
    if (strlen(cmdline) == 0)
        return REQUEST_INVALID;

    if (strncmp(cmdline, "read", 4) == 0)
        return REQUEST_READ;
    else if (strncmp(cmdline, "write", 5) == 0)
        return REQUEST_WRITE;
    else if (strncmp(cmdline, "empty", 5) == 0)
        return REQUEST_EMPTY;

    return REQUEST_INVALID;
}

/**
 * @fn void deallocate_old_files(char *just_closed)
 * @brief Deallocates old files from the open file list.
 *        "Old" files are those that have been open for more than
 *        WAIT_DEALLOC ticks, where each tick is marked by a call to close_file().
 * 
 * @param just_closed The path of the file that was just closed, which we exclude from ticking.
 */
void deallocate_old_files(char *just_closed) {
    OpenFileNode *curr = open_files;
    OpenFileNode *prev = NULL;
    int is_first = 1;

    sem_wait(&open_files_lock);
    while (curr != NULL) {
        // Skip the file that was just closed
        if (strcmp(curr->file->file_path, just_closed) == 0) {
            prev = curr;
            curr = curr->next;
            continue;
        }

        // Tick the node's dealloc timer. If it reaches 0, deallocate it.
        curr->ticks_before_dealloc--;
        if (curr->ticks_before_dealloc == 0) {
            prev = curr;
            curr = curr->next;
            free(prev->file);
            free(prev);

            if (is_first)
                open_files = curr;
        } else {
            // Move to the next node
            curr = curr->next;
            is_first = 0;
        }
    }
    sem_post(&open_files_lock);
}

/**
 * @fn char *get_time()
 * @brief Create a string with the current timestamp.
 * @return A string with the current time in the ctime format "Www Mmm dd hh:mm:ss yyyy"
 */
char *get_time() {
    time_t rawtime = time(0);
    char *time_str = strtok(ctime(&rawtime), "\n");
    return time_str;
}

/**
 * @fn void print_log(char *msg)
 * @brief Print a timestamped message to stdout.
 * 
 * @param caller The name of the function or thread that is printing the message.
 * @param msg The message to print.
 * @param is_error Set to a non-zero value if the message is an error message.
 */
void print_log(char *caller, char *msg, int is_error) {
    char *log_line, *time_str = get_time();
    FILE *log_file;
    ssize_t msg_len;
    
    // Always print errors to console
    if (is_error)
        fprintf(stderr, ANSI_YELLOW "[%s] " ANSI_RED "[ERR] " ANSI_CYAN "%s: " ANSI_RESET "%s\n", time_str, caller, msg);

    if (LOG_TO_CONSOLE) {
        if (is_error == 0)
            printf(ANSI_YELLOW "[%s] " ANSI_GREEN "[LOG] " ANSI_CYAN "%s: " ANSI_RESET "%s\n", time_str, caller, msg);
    } else {
        sem_wait(&log_file_lock);
        log_file = fopen(LOG_FILE, "a");

        if (is_error)
            fprintf(log_file, "[%s] [ERR] %s: %s\n", time_str, caller, msg);
        else
            fprintf(log_file, "[%s] [LOG] %s: %s\n", time_str, caller, msg);

        fclose(log_file);
        sem_post(&log_file_lock);
    }
}

/*****************************
 *      Command handlers     *
 *****************************/

/**
 * @fn void open_file(char *file_path)
 * @brief Marks a file path as currently open, and waits for a lock on it.
 * @param file_path The path of the file to open.
 * @return The semaphore of the file that was just opened.
 */
sem_t *open_file(char *file_path) {
    OpenFileNode *node;
    OpenFile *file;

    // Check if the file is already open
    node = open_files;
    while (node != NULL) {
        if (strcmp(node->file->file_path, file_path) == 0) {
            // File is already open, so just wait for the lock
            sem_wait(&node->file->lock);
            
            // Reset deallocation timer
            node->ticks_before_dealloc = WAIT_DEALLOC;
            return &node->file->lock;
        }
        node = node->next;
    }

    // File is not open, so create a new node and add it to the start of the list
    sem_wait(&open_files_lock);
    file = malloc(sizeof(OpenFile));
    file->file_path = file_path;
    sem_init(&file->lock, 0, 1);
    node = malloc(sizeof(OpenFileNode));
    node->file = file;
    node->next = open_files;
    node->ticks_before_dealloc = WAIT_DEALLOC;
    open_files = node;
    sem_post(&open_files_lock);
    return &file->lock;
}

/**
 * @fn void close_file(char *file_path)
 * @brief Marks a file path as no longer open, and releases the lock on it.
 * @param lock The semaphore of the file to close.
 * @param file_path The path of the file to close.
 */
void close_file(sem_t *lock, char *file_path) {
    OpenFileNode *node;
    char *log_line;
    
    // Unlock semaphore
    sem_post(lock);

    // Tick all deallocation timers
    deallocate_old_files(file_path);
}

/**
 * @fn int write_file(char *file_path, char *text)
 * @brief Write text to a file located at *file_path.
 *        If the file already exists, append text to it.
 *        If the file does not exist, create it and write text to it.
 *        If for_user is non-zero, we sleep for 25ms * number of characters in text.
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param text Text to write to the file.
 * @param for_user Set to a non-zero value if we are writing to a user-specified file.
 * @return 0 on success, -1 on failure.
 */
int write_file(char *file_path, char *text, int for_user) {
    FILE *file;
    char *log_line;
    int wait_us = 25000;
    sem_t *lock;

    // Lock the file path, or wait if it's locked by another thread
    lock = open_file(file_path);

    // Open the file
    file = fopen(file_path, "a");
    if (file == NULL) {
        // Could not open file. Print error.
        // 50 chars for the file path, 32 chars for the format string.
        log_line = malloc(83);
        sprintf(log_line, "Cannot open file \"%s\" for writing.", file_path);
        print_log("write_file", log_line, 1);
        free(log_line);
        return -1;
    }

    // Write the text to the file
    fprintf(file, "%s\n", text);

    // Project requirement: Wait 25ms per character written
    if (for_user) {
        wait_us *= strlen(text);
        usleep(wait_us);
    }

    // Close the file
    fclose(file);
    close_file(lock, file_path);
    return 0;
}

/**
 * @fn int read_file(char *file_path, char *cmdline)
 * @brief Append text from a file located at *file_path to <READ_FILE>.
 *        If the file exists, append the following to <READ_FILE>:
 *            <cmdline>: <contents>\n
 *        If the file does not exist, append the following to <READ_FILE>:
 *            <cmdline>: FILE DNE\n
 *        If cmdline is NULL, then this function is assumed to be called as part of another
 *        thread's operations, and should therefore not print cmdline and FILE DNE.
 * 
 * @param src_path Path to the source file, consisting of at most 50 characters.
 * @param dest_path Path to the destination file, consisting of at most 50 characters.
 * @param cmdline Command line used to call the function. NULL if called from a thread.
 * @return 0 on success, -1 on failure.
 */
int read_file(char *src_path, char *dest_path, char *cmdline) {
    FILE *src, *dest;
    char *log_line, buf[READ_BUF_SIZE];
    size_t read_size;
    sem_t *src_lock, *dest_lock;

    // Open destination first
    dest_lock = open_file(dest_path);
    dest = fopen(dest_path, "a");
    if (dest == NULL) {
        // Could not open file. Print error.
        // 50 chars for the file path, 34 chars for the format string.
        log_line = malloc(85);
        sprintf(log_line, "Cannot open file \"%s\" for appending.", dest_path);
        print_log("read_file", log_line, 1);
        free(log_line);
        return -1;
    }

    // Check if file exists
    if (access(src_path, F_OK) != 0) {
        // File does not exist. Print FILE DNE to READ_FILE.
        if (cmdline != NULL)
            fprintf(dest, "%s: FILE DNE\n", cmdline);
    }

    // Open source
    src_lock = open_file(src_path);
    src = fopen(src_path, "r");
    if (src != NULL) {
        // Append the command line to dest
        if (cmdline != NULL)
            fprintf(dest, "%s: ", cmdline);

        // Append source content to dest in chunks of READ_BUF_SIZE
        while ((read_size = fread(buf, 1, READ_BUF_SIZE, src)) > 0)
            fwrite(buf, 1, read_size, dest);

        // Close source and dest
        fclose(src);
        fclose(dest);
        close_file(src_lock, src_path);
        close_file(dest_lock, dest_path);
    } else {
        // Could not open file. Print error.
        // 50 chars for the file path, 32 chars for the format string.
        log_line = malloc(83);
        sprintf(log_line, "Cannot open file \"%s\" for reading.", src_path);
        print_log("read_file", log_line, 1);
        free(log_line);
        return -1;
    }

    return 0;
}

/**
 * @fn int empty_file(char *file_path, char *cmdline)
 * @brief Empty the contents of a file located at *file_path into <EMPTY_FILE>.
 *        If the file exists, append the following to <EMPTY_FILE>:
 *           <cmdline>: FILE EMPTY\n
 *        and empty the contents of the file.
 *        If the file does not exist, append the following to <EMPTY_FILE>:
 *           <cmdline>: FILE ALREADY EMPTY\n
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param cmdline Command line used to call the function.
 * @return 0 on success, -1 on failure.
 */
int empty_file(char *file_path, char *cmdline) {
    FILE *file;
    char *log_line;
    sem_t *file_lock;
	int ret, wait_s = 7 + (rand() % 4);      // Returns a pseudo-random integer between 7 and 10, inclusive

    // Check if file exists
    if (access(file_path, F_OK) != 0) {
        // File does not exist. Print FILE DNE to EMPTY_FILE.
        if (cmdline != NULL) {
            log_line = malloc(strlen(cmdline) + 23);
            sprintf(log_line, "%s: FILE ALREADY EMPTY\n", cmdline);
            write_file(EMPTY_FILE, log_line, 0);
            free(log_line);
        }
    } else {
        // File exists. Append the command line and file contents to EMPTY_FILE.
        if (cmdline != NULL)
            read_file(file_path, EMPTY_FILE, cmdline);

        // Open file to empty
        file_lock = open_file(file_path);
        file = fopen(file_path, "w");
        if (file == NULL) {
            // Could not open file. Print error.
            // 50 chars for the file path, 33 chars for the format string.
            log_line = malloc(84);
            sprintf(log_line, "Cannot open file \"%s\" for emptying.", file_path);
            print_log("empty_file", log_line, 1);
            free(log_line);
            return -1;
        }

        // Since we opened the file with the "w" flag,
        // the system empties the file for us if it already exists,
        // and thus all that is left to do is close it.
        fclose(file);
        close_file(file_lock, file_path);
    }

    return 0;
}

/**
 * @fn void *thread_cleanup(ThreadParcel *parcel)
 * @brief Clean up the thread, printing errors if any.
 * @param parcel ThreadParcel of the thread to clean up.
 */
void *thread_cleanup(ThreadParcel *parcel) {
    if (parcel->return_value != 0)
        print_log("cleanup", "Worker thread returned an error.", 1);
    free(parcel);
    return NULL;
}

/*****************************
 *       Thread def'ns       *
 *****************************/

/**
 * @fn void *worker_thread(void *arg)
 * @brief Worker thread that handles a single user request.
 *        This thread will receive a request from the master thread,
 *        parse the request, and handle the request accordingly.
 * 
 * @return 0 on success, -1 on failure.
 */
void *worker_thread(void *arg) {
    ThreadParcel *parcel = (ThreadParcel *)arg;
    char *cmdline, *cmd, *file_path, text[51];
    int request_type, preceding_len, text_len;

    // Get cmdline from parcel
    cmdline = malloc(strlen(parcel->cmdline) + 1);
    strcpy(cmdline, parcel->cmdline);

    // Check what type of request the client sent.
    request_type = determine_request(cmdline);
    if (request_type == REQUEST_INVALID) {
        print_log("worker", "Invalid command.", 1);
        parcel->return_value = -1;
        thread_cleanup(parcel);
        return NULL;
    }

    // All valid command lines contain the file path as the second argument.
    // Extract this from the command line.
    cmd = strtok(cmdline, " ");
    file_path = strtok(NULL, " ");
    if (file_path == NULL) {
        print_log("worker", "Missing file path.", 1);
        parcel->return_value = -1;
        thread_cleanup(parcel);
        return NULL;
    }

    // Optionally, the command line may contain free text as the third argument.
    // Check if this argument is present using strlen and extract it.
    preceding_len = strlen(cmd) + strlen(file_path) + 2;
    if (strlen(parcel->cmdline) > preceding_len) {
        // Make sure we're writing to a file.
        if (request_type != REQUEST_WRITE) {
            print_log("worker", "Free text argument only valid for write requests.", 1);
            parcel->return_value = -1;
            thread_cleanup(parcel);
            return NULL;
        }

        // How long is the free text?
        text_len = strlen(parcel->cmdline) - preceding_len;
        if (text_len > 50) {
            print_log("worker", "Free text argument is longer than 50 characters.", 1);
            parcel->return_value = -1;
            thread_cleanup(parcel);
            return NULL;
        }

        // Extract the free text using strncpy.
        strncpy(text, parcel->cmdline + preceding_len, text_len);
        text[text_len] = '\0';
    }

    // Now that we have the file path and the free text,
    // we can now handle the request.
    switch (request_type) {
        case REQUEST_READ:
            parcel->return_value = read_file(file_path, READ_FILE, parcel->cmdline);
            break;
        case REQUEST_WRITE:
            parcel->return_value = write_file(file_path, text, 1);
            break;
        case REQUEST_EMPTY:
            parcel->return_value = empty_file(file_path, parcel->cmdline);
            break;
        default:
            parcel->return_value = -1;
    }

    thread_cleanup(parcel);
}

/**
 * @fn void *master_thread()
 * @brief Master thread that handles all user requests.
 *        This thread will continuously receive user requests from stdin
 *        and spawn worker threads to handle said requests accordingly,
 *        appending each command to a file named <COMMANDS_FILE> along with
 *        the timestamp of the command.
 */
void *master_thread(void* arg) {
    // The longest command name is 5 characters,
    // and the file path and text are both at most 50 characters,
    // therefore including whitespace each command line is at most 107 characters.
    // This leaves us with a total of 109, including the newline and a NULL terminator.
    char *timestamp, *log_line, cmdline[109];
    ThreadParcel *parcel;
    pthread_t thread;

    // Loop forever
    while (1) {
        // Read user input
        printf("> ");
        fgets(cmdline, 109, stdin);

        // Remove newline from input
        // https://stackoverflow.com/a/28462221/3350320
        cmdline[strcspn(cmdline, "\n")] = '\0';
        log_line = malloc(128);
        sprintf(log_line, "Received command: %s", cmdline);
        print_log("master", log_line, 0);
        free(log_line);

        // Create log line with timestamp
        timestamp = get_time();
        log_line = malloc(strlen(timestamp) + strlen(cmdline) + 2);
        sprintf(log_line, "[%s] %s", timestamp, cmdline);
        write_file(COMMANDS_FILE, log_line, 0);
        free(log_line);

        // Create a new thread to handle the request
        parcel = malloc(sizeof(ThreadParcel));
        parcel->cmdline = cmdline;
        parcel->return_value = 0;
        print_log("master", "Spawning new thread to handle request.", 0);
        if (pthread_create(&thread, NULL, worker_thread, parcel) != 0)
            print_log("master", "Could not create worker thread.", 1);
        else {
            if (*(int*)arg == 1)
                pthread_join(thread, NULL);
            else
                pthread_detach(thread);
        }
    }
}

/**
 * @fn int main(...)
 * @brief Main function.
 *        This function will initialize all semaphores
 *        and spawn the master thread.
 */
int main(int argc, char *argv[]) {
    pthread_t master;
    int join_threads = 0;

    // Check if the user wants to join threads
    if (argc > 1) {
        if (strcmp(argv[1], "-j") == 0) {
            join_threads = 1;
            print_log("main", "Thread joining enabled.", 0);
        } else {
            printf("Usage: %s [-j]\n", argv[0]);
            printf("\t-j\tJoin worker threads with the master thread after they have finished.\n");
            printf("\t\tBy default, threads are detached, so the server can keep accepting input\n\t\twhile the worker threads are running.\n");
            return 1;
        }
    }

    // Initialize lock on open_files and log file
    sem_init(&open_files_lock, 0, 1);
    sem_init(&log_file_lock, 0, 1);

    // Seed RNG
    srand(time(0));

    // Create master thread
    print_log("main", "Starting file server...", 0);
    pthread_create(&master, NULL, master_thread, (void*)&join_threads);

    // Wait for thread to finish
    pthread_join(master, NULL);

    // Exit
    print_log("main", "Exiting file server...", 0);
    return 0;
}
