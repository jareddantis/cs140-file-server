#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

/**
 * Paths to files written to by the server.
 */
#define READ_FILE       "read.txt"
#define EMPTY_FILE      "empty.txt"
#define COMMANDS_FILE   "commands.txt"

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
 * struct for packaging thread arguments and return values into a neat
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
typedef struct open_file_obj OpenFile;
typedef struct open_file_node OpenFileNode;
struct open_file_obj {
    char *file_path;
    sem_t lock;
};
struct open_file_node {
    OpenFile *file;
    OpenFileNode *next;
};
sem_t open_files_lock;
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

/*****************************
 *      Command handlers     *
 *****************************/

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
 * @fn int write_file(char *file_path, char *text)
 * @brief Write text to a file located at *file_path.
 *        If the file already exists, append text to it.
 *        If the file does not exist, create it and write text to it.
 * 
 * @param file_path Path to the file, consisting of at most 50 characters.
 * @param text Text to write to the file.
 * @return 0 on success, -1 on failure.
 */
int write_file(char *file_path, char *text) {
    FILE *file;
    char *log_line;
    int wait_us = 25000;

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
    wait_us *= strlen(text);
    usleep(wait_us);

    // Close the file
    fclose(file);
    close_file(file_path);
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

    // Open destination first
    open_file(dest_path);
    dest = fopen(dest_path, "a");
    if (dest == NULL) {
        // Could not open file. Print error.
        // 50 chars for the file path, 34 chars for the format string.
        log_line = malloc(85);
        sprintf(log_line, "Cannot open file \"%s\" for appending.", dest_path);
        print_err("read_file", log_line);
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
    open_file(src_path);
    src = fopen(src_path, "r");
    if (src != NULL) {
        // Append the command line to dest
        if (cmdline != NULL)
            fprintf(dest, "%s: ", cmdline);

        // Append source content to dest in chunks of READ_BUF_SIZE
        while ((read_size = fread(buf, 1, READ_BUF_SIZE, src)) > 0)
            fwrite(buf, 1, read_size, dest);
    } else {
        // Could not open file. Print error.
        // 50 chars for the file path, 32 chars for the format string.
        log_line = malloc(83);
        sprintf(log_line, "Cannot open file \"%s\" for reading.", src_path);
        print_err("read_file", log_line);
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
    FILE *empty, *file;
    char *log_line;
	int ret, wait_s = 7 + (rand() % 4);      // Returns a pseudo-random integer between 7 and 10, inclusive

    // Open EMPTY_FILE
    open_file(EMPTY_FILE);
    empty = fopen(EMPTY_FILE, "a");
    if (empty == NULL) {
        // Could not open file. Print error.
        // 50 chars for the file path, 34 chars for the format string.
        log_line = malloc(85);
        sprintf(log_line, "Cannot open file \"%s\" for appending.", EMPTY_FILE);
        print_err("read_file", log_line);
        free(log_line);
        return -1;
    }

    // Check if file exists
    if (access(file_path, F_OK) != 0) {
        // File does not exist. Print FILE DNE to EMPTY_FILE.
        if (cmdline != NULL)
            fprintf(empty, "%s: FILE ALREADY EMPTY\n", cmdline);
    } else {
        // File exists. Append the command line to EMPTY_FILE.
        if (cmdline != NULL)
            fprintf(empty, "%s: ", cmdline);

        // Open file
        open_file(file_path);
        file = fopen(file_path, "w");
        if (file == NULL) {
            // Could not open file. Print error.
            // 50 chars for the file path, 33 chars for the format string.
            log_line = malloc(84);
            sprintf(log_line, "Cannot open file \"%s\" for emptying.", file_path);
            print_err("read_file", log_line);
            free(log_line);
            return -1;
        }

        // Since we opened the file with the "w" flag,
        // the system empties the file for us if it already exists,
        // and thus all that is left to do is close it.
        fclose(file);
        close_file(file_path);
    }

    return 0;
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
    int request_type, text_len;

    // Get cmdline from parcel
    cmdline = parcel->cmdline;

    // Check what type of request the client sent.
    request_type = determine_request(cmdline);
    if (request_type == REQUEST_INVALID) {
        print_err("worker", "Invalid command.");
        parcel->return_value = -1;
    }

    // All valid command lines contain the file path as the second argument.
    // Extract this from the command line.
    cmd = strtok(cmdline, " ");
    file_path = strtok(cmdline, " ");
    if (file_path == NULL) {
        print_err("worker", "Missing file path");
        parcel->return_value = -1;
    }

    // Optionally, the command line may contain free text as the third argument.
    // Check if this argument is present using strlen and extract it.
    if (strlen(cmdline) > strlen(cmd) + strlen(file_path)) {
        // Make sure we're writing to a file.
        if (request_type != REQUEST_WRITE) {
            print_err("worker", "Free text argument only valid for write requests.");
            parcel->return_value = -1;
        }

        // How long is the free text?
        text_len = strlen(cmdline) - (strlen(cmd) + strlen(file_path));
        if (text_len > 50) {
            print_err("worker", "Free text argument is longer than 50 characters");
            parcel->return_value = -1;
        }

        // Extract the free text using strncpy.
        strncpy(text, cmdline + strlen(cmd) + strlen(file_path), text_len);
        text[text_len] = '\0';
    }

    // Now that we have the file path and the free text,
    // we can now handle the request.
    switch (request_type) {
        case REQUEST_READ:
            parcel->return_value = read_file(file_path, READ_FILE, cmdline);
        case REQUEST_WRITE:
            parcel->return_value = write_file(file_path, text);
        case REQUEST_EMPTY:
            parcel->return_value = empty_file(file_path, cmdline);
        default:
            parcel->return_value = -1;
    }
}

/**
 * @fn void *master_thread()
 * @brief Master thread that handles all user requests.
 *        This thread will continuously receive user requests from stdin
 *        and spawn worker threads to handle said requests accordingly,
 *        appending each command to a file named <COMMANDS_FILE> along with
 *        the timestamp of the command.
 */
void *master_thread() {
    // The longest command name is 5 characters,
    // and the file path and text are both at most 50 characters,
    // therefore including whitespace each command line is at most 107 characters.
    // This leaves us with a total of 108, including the NULL terminator.
    char *timestamp, *log_line, cmdline[108];
    ThreadParcel *parcel;
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
        write_file(COMMANDS_FILE, log_line);
        free(log_line);

        // Create a new thread to handle the request
        parcel = malloc(sizeof(ThreadParcel));
        parcel->cmdline = cmdline;
        parcel->return_value = 0;
        if (pthread_create(&thread, NULL, worker_thread, parcel) != 0)
            print_err("master", "Could not create worker thread.");

        // Wait for the thread to finish
        pthread_join(thread, NULL);
        if (parcel->return_value != 0)
            print_err("master", "Worker thread returned an error.");
        free(parcel);
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

    // Seed RNG
	srand(time(0));

    // Create master thread
    printf("Starting file server...\n");
    pthread_create(&master, NULL, master_thread, "master");

    // Wait for thread to finish
    pthread_join(master, NULL);

    // Exit
    printf("Exiting file server...\n");
    return 0;
}
