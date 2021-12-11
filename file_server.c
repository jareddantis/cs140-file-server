#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>

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
    printf("[%s] %s: %s\n", time_str, thread_name, msg);
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
    fprintf(stderr, "[%s] %s: %s\n", time_str, thread_name, msg);
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
    char cmdline[108];
    pthread_t thread;

    // Loop forever
    while (1) {
        // Read user input
        scanf("%s", cmdline);

        // Create log line with timestamp
        char *timestamp = get_time();
        char *log_line = malloc(strlen(timestamp) + strlen(cmdline) + 2);
        sprintf(log_line, "[%s] %s\n", timestamp, cmdline);

        // Since the master thread is the only thread that can write to the log file,
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
 * @fn int *worker_thread(char *cmdline)
 * @brief Worker thread that handles a single user request.
 *        This thread will receive a request from the master thread,
 *        parse the request, and handle the request accordingly.
 */
int *worker_thread(char *cmdline) {}

/**
 * The main function will do only one thing: spawn the master thread.
 */
int main(int argc, char *argv[]) {
    pthread_t master;
    printf("Starting file server...\n");

    // Create master thread
    pthread_create(&master, NULL, master_thread, "master");

    // Wait for thread to finish
    pthread_join(master, NULL);

    // Exit
    printf("Exiting file server...\n");
    return 0;
}
