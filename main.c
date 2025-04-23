#include <stdio.h>
#include <string.h>

#include "FS.h"

int getCommandID(char* command) {
  if (strcmp(command, "cd") == 0) return CD;
  if (strcmp(command, "mkdir") == 0) return MKDIR;
  if (strcmp(command, "rmdir") == 0) return RMDIR;
  if (strcmp(command, "ls") == 0) return LS;
  if (strcmp(command, "touch") == 0) return TOUCH;
  if (strcmp(command, "rm") == 0) return RM;
  if (strcmp(command, "open") == 0) return OPEN;
  if (strcmp(command, "close") == 0) return CLOSE;
  if (strcmp(command, "write") == 0) return WRITE;
  if (strcmp(command, "read") == 0) return READ;
  if (strcmp(command, "exit") == 0) return EXIT;
  if (strcmp(command, "help") == 0) return HELP;
  return -1;
}
int main() {
  printf("Hello, from MyFileSys!\n");
  char command[100];
  char arg1[100];
  char arg2[100];
  int fd;
  my_startsys();

  while (1) {
    printf("\033[0;32mroot@localhost\033[0m:");
    printf("\033[0;34m%s\033[0m", openFileList[curFd].dir);
    printf("$");

    fgets(command, 100, stdin);
    char* sp;
    sp = strtok(command, " \n");
    // 执行命令
    switch (getCommandID(sp)) {
      case CD:
        break;
      case MKDIR:
        break;
      case RMDIR:
        break;
      case LS:
        break;
      case TOUCH:
        break;
      case RM:
        break;
      case OPEN:
        sp = strtok(NULL, " ");
        if (sp != NULL) {
          my_open(sp);
        } else {
          printf("Please a enter filename\n");
        }
        break;
      case CLOSE:
        sp = strtok(NULL, " ");
        if (sp == NULL) {
          if (openFileList[curFd].attribute == ATT_DIR) {
            printf("Can't close a directory.\n");
          } else {
            my_close(curFd);
          }
        } else {
          int succ = 0;
          for (int i = 0; i < MAXOPENFILE; ++i) {
            if (strcmp(openFileList[i].filename, sp) == 0) {
              my_close(i);
              succ = 1;
              break;
            }
          }
          if (!succ) {
            printf("File not found!\n");
          }
        }
        break;
      case WRITE:
        if (openFileList[curFd].attribute == ATT_FILE) {
          my_write(curFd);
        } else {
          printf("Please write a file!");
        }
        break;
      case READ:
        if (openFileList[curFd].attribute == ATT_FILE) {
          my_read(curFd);
        } else {
          printf("Please open the file first!");
        }
        break;
      case EXIT:
        my_exitsys();
        printf("Exit MyFileSys successfully.\n");
        return 0;
      case HELP:
        printf(
            "Available commands:\ncd dirname\nmkdir dirname\nrmdir "
            "dirname\nls\ntouch filename\nrm filename\nopen "
            "filename\nclose\nwrite\nread\nexit\nhelp\n");
        break;
      default:
        printf("Unknown command. Type 'help' for a list of commands.\n");
    }
  }

  my_exitsys();
  return 0;
}