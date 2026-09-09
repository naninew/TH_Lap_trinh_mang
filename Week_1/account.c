#include <stdio.h>
#include <string.h>
#include "account.h"

int load_accounts(const char *filename, Account accounts[], int max_count) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        return -1;
    }
    int count = 0;
    while (count < max_count &&
           fscanf(fp, USERNAME_FORMAT " %d", accounts[count].username, &accounts[count].status) == 2) {
        count++;
    }
    fclose(fp);
    return count;
}

int find_account(Account accounts[], int count, const char *username) {
    for (int i = 0; i < count; i++) {
        if (strcmp(accounts[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}
