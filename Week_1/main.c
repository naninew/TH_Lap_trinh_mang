#include "account.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSSV "20235818"
#define ACCOUNT_FILE "account.txt"
#define MAX_FILENAME_LEN 64
#define MAX_MESSAGE_LEN 256

static char log_file[MAX_FILENAME_LEN];
static Account accounts[MAX_ACCOUNTS];
static int account_count;
static int logged_in = 0;
static char current_user[MAX_USERNAME];

/**
 * @function print_menu: Display the application main menu options to the console.
 *
 * @return: None.
 **/
void print_menu(void) {
  printf("1. Log in\n");
  printf("2. Post message\n");
  printf("3. Logout\n");
  printf("4. Exit\n");
  printf("Choice: ");
}

/**
 * @function handle_login: Process user login and validate account credentials.
 *
 * @return: None.
 **/
void handle_login(void) {
  char username[MAX_USERNAME];
  printf("Username: ");
  scanf(USERNAME_FORMAT, username);

  if (logged_in) {
    printf("You have already logged in\n");
    log_activity(log_file, 1, username, 0);
    return;
  }

  int idx = find_account(accounts, account_count, username);
  if (idx == -1) {
    printf("Account is not exist\n");
    log_activity(log_file, 1, username, 0);
    return;
  }
  if (accounts[idx].status == 0) {
    printf("Account is banned\n");
    log_activity(log_file, 1, username, 0);
    return;
  }

  logged_in = 1;
  strcpy(current_user, username);
  printf("Hello %s\n", username);
  log_activity(log_file, 1, username, 1);
}

/**
 * @function handle_post: Process message posting for an authenticated user.
 *
 * @return: None.
 **/
void handle_post(void) {
  char message[MAX_MESSAGE_LEN];
  printf("Post message: ");
  getchar();
  if (fgets(message, sizeof(message), stdin) != NULL) {
    message[strcspn(message, "\n")] = '\0';
  } else {
    message[0] = '\0';
  }

  if (!logged_in) {
    printf("You have not logged in.\n");
    log_activity(log_file, 2, message, 0);
    return;
  }

  printf("Successful post\n");
  log_activity(log_file, 2, message, 1);
}

/**
 * @function handle_logout: Log out the currently authenticated user.
 *
 * @return: None.
 **/
void handle_logout(void) {
  if (!logged_in) {
    printf("You have not logged in.\n");
    log_activity(log_file, 3, "", 0);
    return;
  }

  logged_in = 0;
  current_user[0] = '\0';
  printf("Successful log out\n");
  log_activity(log_file, 3, "", 1);
}

/**
 * @function main: Application entry point managing the main control loop.
 *
 * @return: 0 if program finishes successfully.
 *          1 if account data file cannot be opened.
 **/
int main(void) {
  snprintf(log_file, sizeof(log_file), "log_%s.txt", MSSV);

  account_count = load_accounts(ACCOUNT_FILE, accounts, MAX_ACCOUNTS);
  if (account_count < 0) {
    printf("Cannot open %s\n", ACCOUNT_FILE);
    return 1;
  }

  int choice;
  while (1) {
    print_menu();
    if (scanf("%d", &choice) != 1) {
      break;
    }
    switch (choice) {
    case 1:
      handle_login();
      break;
    case 2:
      handle_post();
      break;
    case 3:
      handle_logout();
      break;
    case 4:
      log_activity(log_file, 4, "", 1);
      return 0;
    default:
      printf("Invalid choice\n");
    }
  }
  return 0;
}
