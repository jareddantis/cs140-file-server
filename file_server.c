#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <pthread.h>

/**
 * @fn void write_file(char *file_path, char *text)
 * @brief Write text to a file located at *file_path.
 *        If the file already exists, append text to it.
 *        If the file does not exist, create it and write text to it.
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param text Text to write to the file.
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
 * @fn void master_thread()
 * @brief Master thread that handles all user requests.
 *        This thread will continuously receive user requests from stdin
 *        and spawn worker threads to handle said requests accordingly,
 *        appending each command to a file named commands.txt along with
 *        the timestamp of the command.
 */
void master_thread() {}

/**
 * @fn void worker_thread(char *cmdline)
 * @brief Worker thread that handles a single user request.
 *        This thread will receive a request from the master thread,
 *        parse the request, and handle the request accordingly.
 */
void worker_thread(char *cmdline) {}

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
