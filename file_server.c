#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>

#define REQUEST_INVALID 0
#define REQUEST_READ    1
#define REQUEST_WRITE   2
#define REQUEST_EMPTY   3

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
 * @param thread_name The name of the thread that is printing the message.
 * @param msg The message to print.
 */
void print_log(char *thread_name, char *msg) {
    char *time_str = get_time();
    printf("[%s][LOG] %s: %s\n", time_str, thread_name, msg);
}

/**
 * @fn void print_err(char *msg)
 * @brief Print a timestamped error message to stderr.'
 * 
 * @param thread_name The name of the thread that is printing the message.
 * @param msg The error message to print.
 */
void print_err(char *thread_name, char *msg) {
    char *time_str = get_time();
    fprintf(stderr, "[%s][ERR] %s: %s\n", time_str, thread_name, msg);
}

/**
 * @fn void write_file(char *file_path, char *text)
 * @brief Write text to a file located at *file_path.
 *        If the file already exists, append text to it.
 *        If the file does not exist, create it and write text to it.
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param text Text to write to the file, consisting of at most 50 characters.
 */
void write_file(char *file_path, char *text) {}

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
        scanf("%s", cmdline);

        // Create log line with timestamp
        timestamp = get_time();
        log_line = malloc(strlen(timestamp) + strlen(cmdline) + 2);
        sprintf(log_line, "[%s] %s\n", timestamp, cmdline);

        // Since the master thread is the only thread that can write to the log file,
        // and that this thread only dies when the whole server dies,
        // we do not have to worry about locking it.
        write_file("commands.txt", log_line);

        // Create a new thread to handle the request
        if (pthread_create(&thread, NULL, worker_thread, cmdline) != 0) {
            print_err("master", "Error in worker thread.");
        }

        // Wait for the thread to finish
        pthread_join(thread, NULL);
    }
}

/**
 * The main function will do only one thing: spawn the master thread.
 */
int main(int argc, char *argv[]) {
    pthread_t master;

    // Create master thread
    printf("Starting file server...\n");
    pthread_create(&master, NULL, master_thread, "master");

    // Wait for thread to finish
    pthread_join(master, NULL);

    // Exit
    printf("Exiting file server...\n");
    return 0;
}
