#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

/**
 * Debug flags (see main())
 */
int log_to_console = 0;
int skip_sleep = 0;

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

/**
 * Constants to denote request types, for convenience.
 * See worker_thread().
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
 * Implementation of a FIFO locking system for the server.
 * Threads can request a ticket, and the server will assign them the next
 * available ticket number. If another ticket is being served currently,
 * the thread will be blocked until the current ticket is finished.
 * See ticket_enqueue() and ticket_dequeue().
 */
typedef struct {
    pthread_cond_t queue;
    pthread_mutex_t lock;
    unsigned int curr, waiting;
} queue_lock;

/**
 * To avoid race conditions with file accesses,
 * we keep track of open files in a linked list of path-semaphore objects.
 */
typedef struct file_t_struct file_t;
struct file_t_struct {
    char *path;
    file_t *next;
    queue_lock *lock;
};
file_t *open_files = NULL;
queue_lock *open_files_lock = NULL;

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
int determine_request(char *cmd) {
    // Check if the command line is empty.
    if (strcmp(cmd, "read") == 0)
        return REQUEST_READ;
    else if (strcmp(cmd, "write") == 0)
        return REQUEST_WRITE;
    else if (strcmp(cmd, "empty") == 0)
        return REQUEST_EMPTY;

    return REQUEST_INVALID;
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
 *        This is a variadic function, meant to be called with printf-style arguments.
 *        A sample call would be
 *            print_log(0, "main", "Hello, world! %d", 42);
 *        which would print the following to stdout:
 *            [2020-01-01 01:01:01] [LOG] main: Hello, world! 42
 * 
 * @param is_error Set to a non-zero value if the message is an error message.
 * @param caller The name of the function or thread that is printing the message.
 * @param msg The format string of the log message.
 * @param ... The arguments to the format string.
 */
void print_log(int is_error, char *caller, char *msg, ...) {
    char *time_str = get_time();
    ssize_t msg_len;
    va_list args;

    // Don't do anything if logging is not enabled
    if (log_to_console == 0)
        return;
    
    // Print the timestamp, log type, and caller name
    if (is_error)
        fprintf(stderr, ANSI_YELLOW "[%s] " ANSI_RED "[ERR] " ANSI_CYAN "%s: " ANSI_RESET, time_str, caller);
    else
        printf(ANSI_YELLOW "[%s] " ANSI_GREEN "[LOG] " ANSI_CYAN "%s: " ANSI_RESET, time_str, caller);
    
    // Enable access to variadic arguments
    va_start(args, msg);

    // Print the log message itself along with a newline
    if (is_error) {
        vfprintf(stderr, msg, args);
        fprintf(stderr, "\n");
    } else {
        vprintf(msg, args);
        printf("\n");
    }
}

/**
 * @fn void ticket_init(queue_lock *lock)
 * @brief Initialize a ticket queue lock.
 * @param lock The lock to initialize.
 */
void ticket_init(queue_lock *lock) {
    pthread_cond_init(&lock->queue, NULL);
    pthread_mutex_init(&lock->lock, NULL);
    lock->curr = 0;
    lock->waiting = 0;
}

/**
 * @fn void ticket_enqueue(queue_lock *lock)
 * @brief Place the calling function into a FIFO queue of waiting threads,
 *        managed by the given queue_lock (condition variable and mutex).
 *        Adapted from https://stackoverflow.com/a/3050871/3350320
 * @param lock The queue_lock to use.
 */
void ticket_enqueue(queue_lock *lock) {
    unsigned int ticket;

    pthread_mutex_lock(&lock->lock);
    ticket = lock->waiting++;
    while (ticket != lock->curr)
        pthread_cond_wait(&lock->queue, &lock->lock);
    pthread_mutex_unlock(&lock->lock);
}

/**
 * @fn void ticket_dequeue(queue_lock *lock)
 * @brief Increments the current ticket in the queue lock and wakes up the thread
 *        holding the new ticket value.
 *        Adapted from https://stackoverflow.com/a/3050871/3350320
 */
void ticket_dequeue(queue_lock *lock) {
    pthread_mutex_lock(&lock->lock);
    lock->curr++;
    pthread_cond_broadcast(&lock->queue);
    pthread_mutex_unlock(&lock->lock);
}

/*****************************
 *      Command handlers     *
 *****************************/

/**
 * @fn void enqueue(char *file_path)
 * @brief Marks a file path as currently open, and waits for a lock on it.
 * @param file_path The path of the file to open.
 */
void *enqueue(char *file_path) {
    file_t *file = open_files;
    unsigned long ticket;

    // Get ticket for modifying open_files
    print_log(0, "enqueue", "Received request to lock file %s", file_path);
    ticket_enqueue(open_files_lock);

    // Check if the file is already open
    while (file != NULL) {
        if (strcmp(file->path, file_path) == 0) {
            // File has already been opened, so wait for it to be closed
            print_log(0, "enqueue", "File %s has been opened before, acquiring ticket.", file_path);
            goto acquire;
        }
        file = file->next;
    }

    // No files are currently open, so we can initialize the list
    print_log(0, "enqueue", "File %s has not been opened before, creating new file node.", file_path);
    file = malloc(sizeof(file_t));
    file->lock = malloc(sizeof(queue_lock));
    file->path = file_path;
    file->next = NULL;
    open_files = file;
    ticket_init(file->lock);

acquire:
    ticket_enqueue(file->lock);
    ticket_dequeue(open_files_lock);
}

/**
 * @fn void dequeue(char *file_path)
 * @brief Marks a file path as no longer open, and releases the lock on it.
 * @param file_path The path of the file to close.
 */
void dequeue(char *file_path) {
    file_t *prev, *curr, *file = open_files;
    ThreadParcel *next;

    // Check if the file is open
    ticket_enqueue(open_files_lock);
    print_log(0, "dequeue", "Received request to unlock file %s", file_path);
    while (file != NULL) {
        if (strcmp(file->path, file_path) == 0) {
            // File is open, is the queue empty?
            print_log(0, "dequeue", "File %s is open, serving next ticket.", file_path);
            ticket_dequeue(file->lock);
            break;
        }
        file = file->next;
    }
    ticket_dequeue(open_files_lock);
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
    int wait_us = 25000;

    // Open the file
    file = fopen(file_path, "a");
    if (file == NULL) {
        // Could not open file. Print error.
        print_log(1, "write_file", "Cannot open file \"%s\" for writing.", file_path);
        return -1;
    }

    // Write the text to the file
    fprintf(file, "%s\n", text);

    // Project requirement: Wait 25ms per character written
    if (for_user && skip_sleep == 0) {
        wait_us *= strlen(text);
        print_log(0, "write_file", "%d characters written to \"%s\". Sleeping for %d ms...", strlen(text), file_path, wait_us / 1000);
        usleep(wait_us);
    } else {
        print_log(0, "write_file", "%d characters written to \"%s\".", strlen(text), file_path);
    }

    // Close the file
    fclose(file);
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
 * @param cmdline Command line used to call the function. NULL if called from empty_file().
 * @return 0 on success, -1 on failure.
 */
int read_file(char *src_path, char *dest_path, char *cmdline) {
    FILE *src, *dest;
    char buf[READ_BUF_SIZE];
    size_t read_size;
    int return_value = 0;

    // Check that we are not reading content into the same file
    if (strcmp(src_path, dest_path) == 0) {
        print_log(1, "read_file", "Cannot read file \"%s\" into itself.", src_path);
        return -1;
    }

    // We already hold a lock on the source file, so we need to acquire
    // a lock on the destination file.
    // To do this, we enqueue a dummy parcel for the destination file
    // and wait for its corresponding lock.
    print_log(0, "read_file", "Attempting to acquire lock on destination file \"%s\".", dest_path);
    enqueue(dest_path);
    print_log(0, "read_file", "Acquired lock on destination file \"%s\".", dest_path);

    // Open destination now that we hold a lock on it.
    dest = fopen(dest_path, "a");
    if (dest == NULL) {
        // Could not open file. Print error.
        print_log(1, "read_file", "Cannot open file \"%s\" for appending.", dest_path);
        return_value = -1;
        goto cleanup;
    }

    // Check if file exists
    if (access(src_path, F_OK) != 0) {
        // File does not exist. Print FILE DNE to READ_FILE.
        if (cmdline != NULL)
            fprintf(dest, "%s: FILE DNE\n", cmdline);
        else
            fprintf(dest, "%s: FILE ALREADY EMPTY\n", cmdline);
        print_log(1, "read_file", "File \"%s\" does not exist.", src_path);
        return_value = -1;
        goto cleanup;
    }

    // Open source.
    src = fopen(src_path, "r");
    if (src != NULL) {
        // Append the command line to dest
        if (cmdline != NULL)
            fprintf(dest, "%s: ", cmdline);

        // Append source content to dest in chunks of READ_BUF_SIZE
        while ((read_size = fread(buf, 1, READ_BUF_SIZE, src)) > 0)
            fwrite(buf, 1, read_size, dest);
        fprintf(dest, "\n");

        // Close source and dest
        fclose(src);
        fclose(dest);
        print_log(0, "read_file", "Successfully read file \"%s\" into \"%s\".", src_path, dest_path);
    } else {
        // Could not open file. Print error.
        print_log(1, "read_file", "Cannot open file \"%s\" for reading.", src_path);
        return_value = -1;
    }

cleanup:
    // Dequeue this thread from the destination file's queue.
    dequeue(dest_path);

    return return_value;
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
        // File exists. Open it to empty.
        file = fopen(file_path, "w");
        if (file == NULL) {
            // Could not open file. Print error.
            print_log(1, "empty_file", "Cannot open file \"%s\" for emptying.", file_path);
            return -1;
        }

        // Since we opened the file with the "w" flag,
        // the system empties the file for us if it already exists,
        // and thus all that is left to do is close it.
        fclose(file);

        // Project requirement: wait for a random amount of time
        // between 7 to 10 sec, inclusive
        if (skip_sleep == 0) {
            print_log(0, "empty_file", "%s emptied. Sleeping for %d seconds...", file_path, wait_s);
            sleep(wait_s);
        } else {
            print_log(0, "empty_file", "%s emptied.", file_path);
        }
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
        print_log(1, "cleanup", "Worker thread returned an error.");
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
    ThreadParcel *dummy, *parcel = (ThreadParcel *)arg;
    char *cmdline, *cmd, *file_path, text[51];
    int request_type, preceding_len, text_len;
    int wait_s, wait_prob = rand() % 100;

    // Get cmdline from parcel
    cmdline = malloc(strlen(parcel->cmdline) + 1);
    strcpy(cmdline, parcel->cmdline);

    // All valid command lines contain the command name as the first arg
    // and a file path as the second argument.
    // Extract them from the command line.
    cmd = strtok(cmdline, " ");
    file_path = strtok(NULL, " ");
    if (file_path == NULL) {
        print_log(1, "worker", "Missing file path.");
        parcel->return_value = -1;
        thread_cleanup(parcel);
        return NULL;
    }

    // Check what type of request the client sent.
    request_type = determine_request(cmd);
    if (request_type == REQUEST_INVALID) {
        print_log(1, "worker", "Invalid command.");
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
            print_log(1, "worker", "Free text argument only valid for write requests.");
            parcel->return_value = -1;
            thread_cleanup(parcel);
            return NULL;
        }

        // How long is the free text?
        text_len = strlen(parcel->cmdline) - preceding_len;
        if (text_len > 50) {
            print_log(1, "worker", "Free text argument is longer than 50 characters.");
            parcel->return_value = -1;
            thread_cleanup(parcel);
            return NULL;
        }

        // Extract the free text using strncpy.
        strncpy(text, parcel->cmdline + preceding_len, text_len);
        text[text_len] = '\0';
    }

    // Project requirement: sleep for 1 second 80% of the time, and 6 seconds 20% of the time
    if (skip_sleep == 0) {
        if (wait_prob < 80)
            wait_s = 1;
        else
            wait_s = 6;
        print_log(0, "worker", "Sleeping for %d sec...", wait_s);
        sleep(wait_s);
    }

    // Initialize mutex and add this thread to the file queue.
    print_log(0, "worker", "Attempting to acquire lock for file \"%s\".", file_path);
    enqueue(file_path);

    // Handle the request once the lock is free.
    print_log(0, "worker", "Acquired lock for file \"%s\", now performing operation \"%s\".", file_path, cmd);
    switch (request_type) {
        case REQUEST_READ:
            parcel->return_value = read_file(file_path, READ_FILE, parcel->cmdline);
            break;
        case REQUEST_WRITE:
            parcel->return_value = write_file(file_path, text, 1);
            break;
        case REQUEST_EMPTY:
            // To avoid deadlocks, we can first read the file contents
            // before emptying it, instead of having empty_file call
            // read_file from within the same thread.
            parcel->return_value = read_file(file_path, EMPTY_FILE, NULL);
            if (parcel->return_value == 0)
                parcel->return_value = empty_file(file_path, parcel->cmdline);
            break;
        default:
            print_log(1, "worker", "Invalid request type.");
            parcel->return_value = -1;
    }

    // Dequeue the file and destroy the lock.
    print_log(0, "worker", "Releasing lock for file \"%s\"", file_path);
    dequeue(file_path);
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
        print_log(0, "master", "Received command: %s", cmdline);

        // Create log line with timestamp
        timestamp = get_time();
        log_line = malloc(strlen(timestamp) + strlen(cmdline) + 4);
        sprintf(log_line, "[%s] %s", timestamp, cmdline);
        write_file(COMMANDS_FILE, log_line, 0);
        free(log_line);

        // Create a new thread to handle the request
        parcel = malloc(sizeof(ThreadParcel));
        parcel->cmdline = cmdline;
        parcel->return_value = 0;
        print_log(0, "master", "Spawning new thread to handle request.");
        if (pthread_create(&thread, NULL, worker_thread, parcel) != 0)
            print_log(1, "master", "Could not create worker thread.");
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
    int arg, join_threads = 0;

    // Check if the user wants to join threads
    for (arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "-i") == 0 && skip_sleep == 0)
            skip_sleep = 1;
        else if (strcmp(argv[arg], "-j") == 0 && join_threads == 0)
            join_threads = 1;
        else if (strcmp(argv[arg], "-v") == 0 && log_to_console == 0)
            log_to_console = 1;
        else {
            printf("Usage: %s [-i] [-j] [-v]\n", argv[0]);
            printf("\t-i\tInstant mode: Skip spec-mandated sleeps.\n");
            printf("\t-j\tJoin mode: Join worker threads after they have finished, making the server blocking.\n");
            printf("\t\tBy default, threads are detached, so the server can keep accepting input\n");
            printf("\t\twhile the worker threads are running.\n");
            printf("\t-v\tVerbose mode: print logs to stdout. Off by default.\n");
            return 1;
        }
    }
    if (skip_sleep) print_log(0, "main", "Instant mode enabled.");
    if (join_threads) print_log(0, "main", "Join mode enabled.");
    if (log_to_console) print_log(0, "main", "Verbose mode enabled.");

    // Initialize ticketing lock on open_files
    open_files_lock = malloc(sizeof(queue_lock));
    ticket_init(open_files_lock);

    // Seed RNG
    srand(time(0));

    // Create master thread
    print_log(0, "main", "Starting file server...");
    pthread_create(&master, NULL, master_thread, (void*)&join_threads);

    // Wait for thread to finish
    pthread_join(master, NULL);

    // Destroy ticketing lock on open_files
    pthread_mutex_destroy(&open_files_lock->lock);
    pthread_cond_destroy(&open_files_lock->queue);
    free(open_files_lock);

    // Exit
    print_log(0, "main", "Exiting file server...");
    return 0;
}