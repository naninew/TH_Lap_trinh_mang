#ifndef ACCOUNT_H
#define ACCOUNT_H

#define MAX_ACCOUNTS 100
#define MAX_USERNAME 50
#define USERNAME_FORMAT "%49s"

typedef struct {
    char username[MAX_USERNAME];
    int status;
} Account;

/**
 * @function load_accounts: Read account information from file and store into array.
 *
 * @param filename: Path to the account data file.
 * @param accounts: Array of Account structures to store accounts.
 * @param max_count: Maximum number of accounts to load.
 *
 * @return: Total number of accounts loaded, or -1 if file opening failed.
 **/
int load_accounts(const char *filename, Account accounts[], int max_count);

/**
 * @function find_account: Search for an account in the accounts array by username.
 *
 * @param accounts: Array of Account structures to search.
 * @param count: Number of accounts in the array.
 * @param username: Target username string to find.
 *
 * @return: Index of account if found, or -1 if account does not exist.
 **/
int find_account(Account accounts[], int count, const char *username);

#endif
