#ifndef LOGGER_H
#define LOGGER_H

/**
 * @function log_activity: Append an activity record to the specified log file.
 *
 * @param filename: Path to the log file.
 * @param function_code: Code representing the performed action (1: Login, 2: Post, 3: Logout, 4: Exit).
 * @param user_input: The input string associated with the operation.
 * @param is_success: Operation status (1 for success (+OK), 0 for failure (-ERR)).
 *
 * @return: 0 if log written successfully, or -1 if file opening failed.
 **/
int log_activity(const char *filename, int function_code, const char *user_input, int is_success);

#endif
