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
 *        If the file exists, append the following to read.txt:
 *           <cmdline>: FILE EMPTY\n
 *        and empty the contents of the file.
 *        If the file does not exist, append the following to read.txt:
 *           <cmdline>: FILE ALREADY EMPTY\n
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param cmdline Command line used to call the function.
 */
void empty_file(char *file_path, char *cmdline) {}

int main(int argc, char *argv[]) {
    return 0;
}
