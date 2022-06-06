file_server
===========

submitted by DANTIS, Aurel Jared C. (student number ***REMOVED***)
in partial fulfillment of the requirements of the CS 140 course
for the first semester of Academic Year 2021-2022


# Compiling the file server

Ensure you have a working GCC compiler installed on your system, accessible through your $PATH.

Compile the server by 

```
gcc -o file_server -pthread file_server.c
```


# Running the file server

After compiling the file server, run it using

```
./file_server
```

The file server also accepts the following optional flags:

- `-i`: Instant mode. The server will skip the sleep periods mandated in the project specification.
- `-j`: Join mode. The server will join all worker threads, which essentially makes the file server blocking.
- `-v`: Verbose mode. The server will print logs to stdout and stderr.

Using a flag is as simple as `./file_server [flag] [flag2]...`. By default, the functionalities controlled by these flags are disabled.

Combining multiple flags into one argument is not supported. For example, `./file_server -ijv` is not supported; instead, use `./file_server -i -j -v`. The server will print a small help message and exit if it encounters an invalid flag.


# Colorized log output

Running the file server with the `-v` flag will print colorized log output, using ANSI escape sequences. It is recommended to use a terminal emulator with support for these sequences, as there is no way to disable colorization.

Users who do not use the `-v` flag will not see any logging output.
