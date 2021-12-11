#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

/**
 * Constants to denote request types, for convenience.
 * See determine_request().
 */
#define REQUEST_INVALID 0
#define REQUEST_READ    1
#define REQUEST_WRITE   2
#define REQUEST_EMPTY   3

/**
 * To avoid race conditions with file accesses,
 * we keep track of open files in a linked list of path-semaphore objects.
 */
OpenFileNode *open_files = NULL;
sem_t open_files_lock;
typedef struct OpenFile {
    char *file_path;
    sem_t lock;
} OpenFile;
typedef struct OpenFileNode {
    OpenFile *file;
    OpenFileNode *next;
} OpenFileNode;

/**
 * @fn void open_file(char *file_path)
 * @brief Marks a file path as currently open, and waits for a lock on it.
 * @param file_path The path of the file to open.
 */
void open_file(char *file_path) {
    OpenFileNode *node;
    OpenFile *file;

    // Check if the file is already open
    node = open_files;
    while (node != NULL) {
        if (strcmp(node->file->file_path, file_path) == 0) {
            // File is already open, so just wait for the lock
            sem_wait(&node->file->lock);
            return;
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
    open_files = node;
    sem_post(&open_files_lock);
}

/**
 * @fn void close_file(char *file_path)
 * @brief Marks a file path as no longer open, and releases the lock on it.
 * @param file_path The path of the file to close.
 */
void close_file(char *file_path) {
    OpenFileNode *node;
    char *log_line;
    
    node = open_files;
    while (node != NULL) {
        if (strcmp(node->file->file_path, file_path) == 0) {
            // Release the lock
            sem_post(&node->file->lock);
            return;
        }
        node = node->next;
    }

    // File not found. Print error.
    // 50 chars for the file path, 30 chars for the format string.
    log_line = malloc(81);
    sprintf(log_line, "Cannot close unopened file \"%s\".", file_path);
    print_err("close_file", log_line);
    free(log_line);
}

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
 * @fn char *get_time()
 * @brief Create a string with the current timestamp.
 * @return A string with the current time in the ctime format "Www Mmm dd hh:mm:ss yyyy"
 */
char *get_time() {
    time_t rawtime = time(0);
    char *time_str = ctime(&rawtime);
    return time_str;
}

/**
 * @fn void print_log(char *msg)
 * @brief Print a timestamped message to stdout.
 * 
 * @param caller The name of the function or thread that is printing the message.
 * @param msg The message to print.
 */
void print_log(char *caller, char *msg) {
    char *time_str = get_time();
    printf("[%s][LOG] %s: %s\n", time_str, caller, msg);
}

/**
 * @fn void print_err(char *msg)
 * @brief Print a timestamped error message to stderr.'
 * 
 * @param caller The name of the function or thread that is printing the message.
 * @param msg The error message to print.
 */
void print_err(char *caller, char *msg) {
    char *time_str = get_time();
    fprintf(stderr, "[%s][ERR] %s: %s\n", time_str, caller, msg);
}

/**
 * @fn void write_file(char *file_path, char *text)
 * @brief Write text to a file located at *file_path.
 *        If the file already exists, append text to it.
 *        If the file does not exist, create it and write text to it.
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param text Text to write to the file.
 */
void write_file(char *file_path, char *text) {
    FILE *file;
    char *log_line;
    int wait_us;

    // Lock the file path, or wait if it's locked by another thread
    open_file(file_path);

    // Open the file
    file = fopen(file_path, "a");
    if (file == NULL) {
        // Could not open file. Print error.
        // 50 chars for the file path, 32 chars for the format string.
        log_line = malloc(83);
        sprintf(log_line, "Cannot open file \"%s\" for writing.", file_path);
        print_err("write_file", log_line);
        free(log_line);
        return;
    }

    // Write the text to the file
    fprintf(file, "%s\n", text);

    // Project requirement: Wait 25ms per character written
    wait_us = strlen(text) * 25 * 1000;
    usleep(wait_us);

    // Close the file
    fclose(file);
    close_file(file_path);
}

/**
 * @fn void read_file(char *file_path, char *cmdline)
 * @brief Read text from a file located at *file_path.
 *        If the file exists, append the following to read.txt:
 *            <cmdline>: <contents>\n
 *        If the file does not exist, append the following to read.txt:
 *            <cmdline>: FILE DNE\n
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param cmdline Command line used to call the function.
 */
void read_file(char *file_path, char *cmdline) {}

/**
 * @fn void empty_file(char *file_path, char *cmdline)
 * @brief Empty the contents of a file located at *file_path.
 *        If the file exists, append the following to empty.txt:
 *           <cmdline>: FILE EMPTY\n
 *        and empty the contents of the file.
 *        If the file does not exist, append the following to empty.txt:
 *           <cmdline>: FILE ALREADY EMPTY\n
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param cmdline Command line used to call the function.
 */
void empty_file(char *file_path, char *cmdline) {}

/**
 * @fn int *worker_thread(char *cmdline)
 * @brief Worker thread that handles a single user request.
 *        This thread will receive a request from the master thread,
 *        parse the request, and handle the request accordingly.
 */
int *worker_thread(char *cmdline) {
    char *cmd, *file_path, text[51];
    int request_type, text_len;

    // Check what type of request the client sent.
    request_type = determine_request(cmdline);
    if (request_type == REQUEST_INVALID) {
        print_err("worker", "Invalid command.");
        return 1;
    }

    // All valid command lines contain the file path as the second argument.
    // Extract this from the command line.
    cmd = strtok(cmdline, " ");
    file_path = strtok(cmdline, " ");
    if (file_path == NULL) {
        print_err("worker", "Missing file path");
        return 1;
    }

    // Optionally, the command line may contain free text as the third argument.
    // Check if this argument is present using strlen and extract it.
    if (strlen(cmdline) > strlen(cmd) + strlen(file_path)) {
        // Make sure we're writing to a file.
        if (request_type != REQUEST_WRITE) {
            print_err("worker", "Free text argument only valid for write requests.");
            return 1;
        }

        // How long is the free text?
        text_len = strlen(cmdline) - (strlen(cmd) + strlen(file_path));
        if (text_len > 50) {
            print_err("worker", "Free text argument is longer than 50 characters");
            return 1;
        }

        // Extract the free text using strncpy.
        strncpy(text, cmdline + strlen(cmd) + strlen(file_path), text_len);
        text[text_len] = '\0';
    }

    // Now that we have the file path and the free text,
    // we can now handle the request.
    switch (request_type) {
        case REQUEST_READ:
            read_file(file_path, cmdline);
            break;
        case REQUEST_WRITE:
            write_file(file_path, text);
            break;
        case REQUEST_EMPTY:
            empty_file(file_path, cmdline);
            break;
    }

    return 0;
}

/**
 * @fn void *master_thread()
 * @brief Master thread that handles all user requests.
 *        This thread will continuously receive user requests from stdin
 *        and spawn worker threads to handle said requests accordingly,
 *        appending each command to a file named commands.txt along with
 *        the timestamp of the command.
 */
void *master_thread() {
    // The longest command name is 5 characters,
    // and the file path and text are both at most 50 characters,
    // therefore including whitespace each command line is at most 107 characters.
    // This leaves us with a total of 108, including the NULL terminator.
    char *timestamp, *log_line, cmdline[108];
    pthread_t thread;

    // Loop forever
    while (1) {
        // Read user input
        printf("> ");
        scanf("%s", cmdline);

        // Create log line with timestamp
        timestamp = get_time();
        log_line = malloc(strlen(timestamp) + strlen(cmdline) + 2);
        sprintf(log_line, "[%s] %s\n", timestamp, cmdline);

        // Since the master thread is the only thread that can write to the log file,
        // and that this thread only dies when the whole server dies,
        // we do not have to worry about locking it.
        write_file("commands.txt", log_line);
        free(log_line);

        // Create a new thread to handle the request
        if (pthread_create(&thread, NULL, worker_thread, cmdline) != 0) {
            print_err("master", "Error in worker thread.");
        }

        // Wait for the thread to finish
        pthread_join(thread, NULL);
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

    // Initialize lock on open_files
    sem_init(&open_files_lock, 0, 1);

    // Create master thread
    printf("Starting file server...\n");
    pthread_create(&master, NULL, master_thread, "master");

    // Wait for thread to finish
    pthread_join(master, NULL);

    // Exit
    printf("Exiting file server...\n");
    return 0;
}
