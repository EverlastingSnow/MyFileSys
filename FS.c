#include "FS.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 定义全局变量
unsigned char* myVHead;  // 虚拟磁盘头指针
USEROPEN openFileList[MAXOPENFILE];
int curFd;                   // 指向当前文件描述符编号
unsigned char* startPos;     // 虚拟磁盘数据区起始位置
unsigned char buffer[SIZE];  // 用于读写数据块的缓冲区

void my_copy_fcb(int fd, FCB* fcbptr, int flag) {
  // 拷贝FCB到内存或将内存的FCB存入磁盘
  // 0: 拷贝到内存，1: 拷贝到磁盘
  if (flag == 0) {
    memcpy(&openFileList[fd], fcbptr, sizeof(FCB));
  } else {
    memcpy(fcbptr, &openFileList[fd], sizeof(FCB));
  }
}

void my_format() {
  // fat表格式化
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  FAT* fat2 = (FAT*)(myVHead + BLOCKSIZE * 3);
  for (int i = 0; i < 6; ++i) {
    fat1[i].id = END;
    fat2[i].id = END;
  }
  for (int i = 6; i < 1000; ++i) {
    fat1[i].id = FREE;
    fat2[i].id = FREE;
  }

  // 引导块格式化
  BLOCK0* block0 = (BLOCK0*)myVHead;

  strcpy(block0->name, SYSFILENAME);
  strcpy(block0->info,
         "A FAT file system\n Block size: 1KB\nBlock nums: 1000\nBlock0: "
         "0\nFAT1: 1\nFAT2: 3\nRoot: 5\nData: 7\n");
  block0->root = 5;
  block0->startBlock = myVHead + BLOCKSIZE * (5 + ROOTBLOCKNUM);

  // 根目录当前目录格式化
  FCB* root = (FCB*)(myVHead + BLOCKSIZE * 5);
  strcpy(root->filename, ".");
  strcpy(root->exname, "d");
  root->attribute = ATT_DIR;

  time_t rawTime = time(NULL);
  struct tm* time = localtime(&rawTime);
  root->time = time->tm_hour * 2048 + time->tm_min * 32 + time->tm_sec / 2;
  root->date =
      (time->tm_year - 100) * 512 + (time->tm_mon + 1) * 32 + (time->tm_mday);
  root->firstBlock = 5;
  root->length = 2 * sizeof(FCB);
  // printf("%lu\n", sizeof(FCB));
  root->free = 1;

  // 根目录上级目录格式化
  FCB* root2 = (FCB*)(root + 1);
  memcpy(root2, root, sizeof(FCB));
  strcpy(root2->filename, "..");

  // 剩余块格式化
  for (int i = 2; i < BLOCKSIZE / sizeof(FCB); ++i) {
    root2++;
    root2->free = 0;
    strcpy(root2->filename, "");
  }

  FILE* fp = fopen(SYSFILENAME, "wb");
  if (fp == NULL) {
    printf("Error: Cannot open virtual disk file.\n");
    exit(1);
  }
  fwrite(myVHead, SIZE, 1, fp);
  fclose(fp);
}

void my_startsys() {
  // 初始化虚拟磁盘
  myVHead = (unsigned char*)malloc(SIZE);
  FILE* file;
  if ((file = fopen(SYSFILENAME, "r")) != NULL) {
    fread(buffer, SIZE, 1, file);
    fclose(file);
    if (memcmp(buffer, SYSFILENAME, sizeof(SYSFILENAME)) == 0) {
      memcpy(myVHead, buffer, SIZE);
      printf("Virtual disk loaded successfully.\n");
    } else {
      printf("Error: Invalid virtual disk format, rebuild it.\n");
      my_format();
      memcpy(buffer, myVHead, SIZE);
    }
  } else {
    printf("Error: Virtual disk not found, creating a new one.\n");
    my_format();
    memcpy(buffer, myVHead, SIZE);
  }

  // 初始化根目录
  FCB* root = (FCB*)(myVHead + BLOCKSIZE * 5);
  my_copy_fcb(0, root, 0);
  strcpy(openFileList[0].dir, "/root/");
  openFileList[0].fcbState = 0;
  openFileList[0].pos = 0;
  openFileList[0].free = 1;
  // root目录存在第5块
  openFileList[0].faDirBlock = 5;
  openFileList[0].fcbOffset = 0;

  startPos = ((BLOCK0*) myVHead)->startBlock;
}

void my_exitsys() {
  // for (int i = 0; i < MAXOPENFILE; ++i) {
  //   if (openFileList[i].free == 1) {
  //     my_close(i);
  //   }
  // }
  while (curFd){
    my_close(curFd);
  }
  FILE* fp = fopen(SYSFILENAME, "wb");
  fwrite(myVHead, SIZE, 1, fp);
  fclose(fp);
}

int getFreeUserOpen() {
  for (int i = 0; i < MAXOPENFILE; ++i) {
    if (openFileList[i].free == 0) {
      openFileList[i].free = 1;
      return i;
    }
  }
  return -1;
}

int my_open(char* fileName) {
  // 读当前目录FCB
  char buf[MAXOPENFILE * BLOCKSIZE];
  openFileList[curFd].pos = 0;
  if (do_read(curFd, openFileList[curFd].length, buf) < 0) {
    printf("Error: Failed to read current directory.\n");
    return -1;
  }
  // 遍历当前目录FCB，找到目标文件
  FCB* fcbptr = (FCB*)buf;
  int fcbID = -1;
  int isDir = 0;
  for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i,++fcbptr) {
    size_t len = strlen(fcbptr->filename) + strlen(fcbptr->exname) + 2;     
    char *tmp = malloc(sizeof(char) * len);
    strcpy(tmp, fcbptr->filename);
    strcat(tmp, ".");
    strcat(tmp, fcbptr->exname);
    if (strcmp(tmp, fileName) == 0 &&
        fcbptr->attribute == ATT_FILE ||
        strcmp(fcbptr->filename, fileName) == 0 &&
        strcmp(fcbptr->exname, "") == 0 &&
        fcbptr->attribute == ATT_FILE) {
      // 找到目标文件
      fcbID = i;
      break;
    }
    if (strcmp(fcbptr->filename, fileName) == 0 &&
        fcbptr->attribute == ATT_DIR) {
      isDir = 1;
    }
  }
  if (fcbID == -1) {
    if (isDir == 1) {
      printf("Error: Cannot open a directory.\n");
    } else {
      printf("Error: File not found.\n");
    }
    return -1;
  }
  int fd = getFreeUserOpen();
  if (fd == -1) {
    printf("Error: No free file descriptor.\n");
    return -1;
  }
  my_copy_fcb(fd, fcbptr, 0);
  strcat(strcpy(openFileList[fd].dir, openFileList[curFd].dir), fileName);
  // 记录父目录
  openFileList[fd].faDirBlock = openFileList[curFd].firstBlock;
  openFileList[fd].fcbOffset = fcbID;

  openFileList[fd].fcbState = 0;
  openFileList[fd].pos = 0;
  openFileList[fd].free = 1;
  curFd = fd;
  return 0;
}

int my_close(int fd) {
  // fd合法性判断
  if (fd < 0 || fd >= MAXOPENFILE) {
    if (fd < 0)
      printf("Fd is negative!\n");
    else
      printf("Fd is larger than MAX!\n");
    return -1;
  }
  // 获取父目录fd
  int fatherFd = -1;
  for (int i = 0; i < MAXOPENFILE; ++i) {
    if (openFileList[i].firstBlock == openFileList[fd].faDirBlock) {
      fatherFd = i;
      break;
    }
  }
  if (fatherFd == -1) {
    printf("Error: Father directory not found.\n");
    return -1;
  }
  // 写入修改的FCB
  char buf[MAXOPENFILE * BLOCKSIZE];
  if (openFileList[fd].fcbState == 1) {
    if (do_read(fatherFd, openFileList[fatherFd].length, buf) < 0) {
      printf("Error: Failed to read father directory.\n");
      return -1;
    }
    // 获取父目录中当前文件的FCB
    FCB* fcbptr = (FCB*)(buf + openFileList[fd].fcbOffset * sizeof(FCB));
    // 修改FCB为当前状态
    my_copy_fcb(fd, fcbptr, 1);
    openFileList[fatherFd].pos = openFileList[fd].fcbOffset * sizeof(FCB);
    // 写入磁盘
    if (do_write(fatherFd, (char*)fcbptr, sizeof(FCB), OW) < 0) {
      printf("Error: Failed to write father directory.\n");
      return -1;
    }
  }

  // 如果是目录，则将当前目录设为父目录
  // if (openFileList[fd].attribute == ATT_DIR) {
    curFd = fatherFd;
  // }
  // 释放打开的文件表
  memset(&openFileList[fd], 0, sizeof(USEROPEN));

  return fatherFd;
}

int my_read(int fd) {
  if (fd < 0 || fd >= MAXOPENFILE) {
    printf("File not exist!\n");
    return -1;
  }
  openFileList[fd].pos = 0;
  char buf[MAXOPENFILE * BLOCKSIZE] = "";
  do_read(fd, openFileList[fd].length, buf);
  printf("%s\n", buf);
  return 0;
}

int do_read(int fd, int len, char* text) {
  // 获取文件磁盘块位置
  int offset = openFileList[fd].pos;
  int blockID = openFileList[fd].firstBlock;
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  FAT* fatPtr = fat1 + blockID;
  while (offset >= BLOCKSIZE) {
    offset -= BLOCKSIZE;
    blockID = fatPtr->id;
    if (blockID == END) {
      printf("Error: End of file reached.\n");
      return -1;
    }
    fatPtr = fat1 + blockID;
  }
  // 将数据读入缓冲区
  unsigned char* buf = (unsigned char*)malloc(BLOCKSIZE);
  if (buf == NULL) {
    printf("Memory allocation failed.\n");
    return -1;
  }
  unsigned char* blockPtr = (myVHead + BLOCKSIZE * blockID);
  memcpy(buf, blockPtr, BLOCKSIZE);

  // 读取内容
  int leftToRead = len;
  while (leftToRead > 0) {
    if (offset + leftToRead <= BLOCKSIZE) {
      memcpy(text, buf + offset, leftToRead);
      text += leftToRead;
      openFileList[fd].pos += leftToRead;
      leftToRead = 0;
    } else {
      memcpy(text, buf + offset, BLOCKSIZE - offset);
      text += BLOCKSIZE - offset;
      leftToRead -= BLOCKSIZE - offset;

      blockID = fatPtr->id;
      if (blockID == END) {
        printf("Error: End of file reached.\n");
        return -1;
      }
      fatPtr = fat1 + blockID;
      blockPtr = (myVHead + BLOCKSIZE * blockID);
      memcpy(buf, blockPtr, BLOCKSIZE);
      offset = 0;
    }
  }
  free(buf);
  return len - leftToRead;
}

int getFreeBlock() {
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  for (int i = 0; i < SIZE / BLOCKSIZE; ++i) {
    if (fat1[i].id == FREE) {
      return i;
    }
  }
  return -1;
}

int my_write(int fd) {
  if (fd < 0 || fd >= MAXOPENFILE) {
    printf("Error: Invalid file descriptor.\n");
    return -1;
  }
  int wStyle;
  while (1) {
    printf("Enter write style: (0)truncate (1)overwrite (2):append ");
    scanf("%d", &wStyle);
    if (wStyle == TW || wStyle == OW || wStyle == AW) {
      break;
    } else {
      printf("Invalid input, please try again.\n");
    }
  }
  char text[MAXOPENFILE * BLOCKSIZE] = "";
  char line[MAXOPENFILE * BLOCKSIZE] = "";
  printf(
      "Please enter the file content, if finished please input \":wq\" in a "
      "new line with Carriage Return:\n");
  while (fgets(line, MAXOPENFILE * BLOCKSIZE, stdin)) {
    if (strcmp(line, ":wq\n") == 0) {
      break;
    }
    line[strlen(line)] = '\n';
    strcat(text, line);
  }

  text[strlen(text)] = '\0';
  strcpy(line, text + 2);
  line[strlen(line) - 2] = '\0';
  do_write(fd, line, strlen(line) + 1, wStyle);
  openFileList[fd].fcbState = 1;
  return 0;
}

int do_write(int fd, char* text, int len, char wStyle) {
  // 截断写
  if (wStyle == TW) {
    openFileList[fd].pos = 0;
    openFileList[fd].length = 0;
  } else {
    // 追加写
    if (wStyle == AW) {
      openFileList[fd].pos = openFileList[fd].length;
    }
    if (openFileList[fd].attribute == ATT_FILE) {
      if (openFileList[fd].length != 0) {
        openFileList[fd].pos = openFileList[fd].length - 1;  // 去掉末尾\0
      }
    }
  }

  // 获取文件磁盘块位置和偏移
  int blockID = openFileList[fd].firstBlock;
  int offset = openFileList[fd].pos;
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  FAT* fatPtr = fat1 + blockID;
  while (offset >= BLOCKSIZE) {
    blockID = fatPtr->id;
    if (blockID == END) {
      printf("Error: End of file reached.\n");
      return -1;
    }
    fatPtr = fat1 + blockID;
    offset -= BLOCKSIZE;
  }

  // 将数据写入磁盘块
  unsigned char* blockPtr = (unsigned char*)(myVHead + BLOCKSIZE * blockID);
  unsigned char* buf = (unsigned char*)malloc(BLOCKSIZE * MAXOPENFILE);
  if (buf == NULL) {
    printf("Memory allocation failed.\n");
    return -1;
  }
  int leftToWrite = len;
  while (leftToWrite > 0) {
    memcpy(buf, blockPtr, BLOCKSIZE);
    for (; offset < BLOCKSIZE; ++offset) {
      if (leftToWrite == 0) {
        break;
      }
      buf[offset] = text[len - leftToWrite];
      leftToWrite--;
    }
    memcpy(blockPtr, buf, BLOCKSIZE);

    // 找到下一个磁盘块，如果没有则申请
    if (offset == BLOCKSIZE && leftToWrite > 0) {
      offset = 0;
      blockID = fatPtr->id;
      if (blockID == END) {
        blockID = getFreeBlock();
        if (blockID == -1) {
          printf("Error: No free block available.\n");
          return -1;
        }
        blockPtr = (myVHead + BLOCKSIZE * blockID);
        fatPtr->id = blockID;
        fatPtr = fat1 + blockID;
        fatPtr->id = END;
      } else {
        blockPtr = (myVHead + BLOCKSIZE * blockID);
        fatPtr = fat1 + blockID;
      }
    }
  }

  openFileList[fd].pos += len;
  if (openFileList[fd].pos > openFileList[fd].length) {
    openFileList[fd].length = openFileList[fd].pos;
  }
  // printf(
  //     "fd = %d, openFileList[fd].length = %d, openFileList[fd].pos = %d, "
  //     "filename = "
  //     "%s\n",
  //     fd, openFileList[fd].length, openFileList[fd].pos, text);
  // 截断写一定要修改文件长度，覆盖写需要修改目录长度
  if (wStyle == TW || (wStyle == AW && openFileList[fd].attribute == ATT_DIR)) {
    offset = openFileList[fd].length;
    fatPtr = fat1 + openFileList[fd].firstBlock;
    while (offset >= BLOCKSIZE) {
      blockID = fatPtr->id;
      offset -= BLOCKSIZE;
      fatPtr = fat1 + blockID;
    }
    int id = fatPtr->id;
    fatPtr->id = END;
    fatPtr = fat1 + id;
    while (fatPtr->id != END) {
      id = fatPtr->id;
      fatPtr->id = FREE;
      fatPtr = fat1 + id;
    }
    fatPtr->id = FREE;
    // while (1) {
    //   // 不是最后一块，就先释放这块，再释放后面的
    //   if (fatPtr->id != END) {
    //     int id = fatPtr->id;
    //     fatPtr->id = FREE;
    //     fatPtr = fat1 + id;
    //   } else {
    //     fatPtr->id = FREE;
    //     break;
    //   }
    // }
    // // FAT表最后一块添加标记
    // fatPtr = fat1 + blockID;
    // fatPtr->id = END;
  }
  // 备份fat1到fat2
  memcpy((FAT*)(myVHead + BLOCKSIZE * 3), (FAT*)(myVHead + BLOCKSIZE * 1),
         BLOCKSIZE * 2);
  return len - leftToWrite;
}

void my_cd(char* dirName) {
  // 检查当前目录是否为根目录
  // printf("dirName: %s, curFd: %d\n", dirName, curFd);
  if (strcmp(dirName, ".") == 0){
    return;
  }
  if (strcmp(dirName, "..") == 0) {
    if (strcmp(openFileList[curFd].dir, "/") == 0) {
      printf("Already in root directory.\n");
      return;
    }
    // 找到父目录
    int fatherFd = -1;
    for (int i = 0; i < MAXOPENFILE; ++i) {
      if (openFileList[i].firstBlock == openFileList[curFd].faDirBlock) {
        fatherFd = i;
        break;
      }
    }
    if (fatherFd == -1) {
      printf("Error: Parent directory not found.\n");
      return;
    }else{
      my_close(curFd);
    }
    return;
  }

  // 在当前目录中查找目标目录
  char buf[BLOCKSIZE];
  openFileList[curFd].pos = 0;
  if (do_read(curFd, openFileList[curFd].length, buf) < 0) {
    printf("Error: Failed to read current directory.\n");
    return;
  }

  FCB* fcbptr = (FCB*)buf;
  int found = 0;
  for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i) {
    // printf("name = %s\n", fcbptr->filename);
    // printf("dirName = %s\n", dirName);
    if (strcmp(fcbptr->filename, dirName) == 0 &&
        fcbptr->attribute == ATT_DIR) {
      found = 1;
      break;
    }
    fcbptr++;
  }

  if (!found) {
    printf("Error: Directory not found.\n");
    return;
  }

  // 打开目标目录
  int fd = getFreeUserOpen();
  if (fd == -1) {
    printf("Error: No free file descriptor.\n");
    return;
  }

  my_copy_fcb(fd, fcbptr, 0);
  strcat(strcpy(openFileList[fd].dir, openFileList[curFd].dir), dirName);
  strcat(openFileList[fd].dir, "/");
  openFileList[fd].faDirBlock = openFileList[curFd].firstBlock;
  openFileList[fd].fcbOffset = (fcbptr - (FCB*)buf);
  openFileList[fd].fcbState = 0;
  openFileList[fd].pos = 0;
  openFileList[fd].free = 1;
  curFd = fd;
}

void my_mkdir(char* dirName) {
  // 检查目录名长度
  if (strlen(dirName) > 8) {
    printf("Error: Directory name too long.\n");
    return;
  }

  // 检查目录名是否包含扩展名
  char* fname = strtok(dirName, ".");
  char* exname = strtok(NULL, ".");
  if (exname != NULL) {
    printf("Error: Directory name can not include extension name.\n");
    return;
  }

  // 检查当前目录是否已满
  char buf[MAXOPENFILE * BLOCKSIZE];
  openFileList[curFd].pos = 0;
  int fileLen = do_read(curFd, openFileList[curFd].length, buf);
  if (fileLen < 0) {
    printf("Error: Failed to read current directory.\n");
    return;
  }

  // 检查目录是否已存在
  FCB* fcbPtr = (FCB*)buf;
  for (int i = 0; i < fileLen / sizeof(FCB); ++i) {
    if (strcmp(fcbPtr[i].filename, dirName) == 0 &&
        fcbPtr[i].attribute == ATT_DIR) {
      printf("Error: Directory already exists.\n");
      return;
    }
  }

  // 申请一个空闲的打开目录表项
  int newFd = getFreeUserOpen();
  if (newFd == -1) {
    printf("File number has reached the limit\n");
    return;
  }

  // 分配新的磁盘块
  int newBlock = getFreeBlock();
  if (newBlock == -1) {
    printf("Error: No free blocks available.\n");
    openFileList[newFd].free = 0;
    return;
  }

  // 更新FAT表
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  fat1[newBlock].id = END;
  FAT* fat2 = (FAT*)(myVHead + BLOCKSIZE * 3);
  fat2[newBlock].id = END;

  // 查找空闲FCB，找不到则扩容
  fcbPtr = (FCB*)buf;
  int freeFCB = 0;
  for (freeFCB = 0; freeFCB < fileLen / sizeof(FCB); freeFCB++, fcbPtr++) {
    if (fcbPtr->free == 0) {
      break;
    }
  }
  // printf("freeFCB = %d, fileLen = %d\n", freeFCB, fileLen);

  // 初始化新目录的FCB
  FCB* newDir = (FCB*)malloc(sizeof(FCB));
  strcpy(newDir->filename, dirName);
  strcpy(newDir->exname, "d");
  newDir->attribute = ATT_DIR;
  time_t rawTime = time(NULL);
  struct tm* time = localtime(&rawTime);
  newDir->time = time->tm_hour * 2048 + time->tm_min * 32 + time->tm_sec / 2;
  newDir->date =
      (time->tm_year - 100) * 512 + (time->tm_mon + 1) * 32 + time->tm_mday;
  newDir->firstBlock = newBlock;
  newDir->length = 2 * sizeof(FCB);  // "." 和 ".." 条目
  newDir->free = 1;

  // 写入修改后的FCB
  openFileList[curFd].pos = freeFCB * sizeof(FCB);
  openFileList[curFd].fcbState = 1;
  if (do_write(curFd, (char*)newDir, sizeof(FCB), OW) < 0) {
    printf("Error: Failed to write directory entry.\n");
    return;
  }

  my_copy_fcb(newFd, newDir, 0);
  strcpy(openFileList[newFd].filename, dirName);
  strcpy(openFileList[newFd].exname, "d");
  openFileList[newFd].faDirBlock = openFileList[curFd].firstBlock;
  openFileList[newFd].fcbOffset = freeFCB;
  strcat(
      strcat(strcpy(openFileList[newFd].dir, (char*)(openFileList[curFd].dir)),
             "/"),
      dirName);
  openFileList[newFd].pos = 0;
  openFileList[newFd].fcbState = 0;
  openFileList[newFd].free = 1;

  // 初始化新目录的内容
  // 创建 "." 条目
  strcpy(newDir->filename, ".");
  do_write(newFd, (char*)newDir, sizeof(FCB), OW);
  // 创建 ".." 条目
  strcpy(newDir->filename, "..");
  newDir->firstBlock = openFileList[curFd].firstBlock;
  newDir->length = openFileList[curFd].length;
  newDir->date = openFileList[curFd].date;
  newDir->time = openFileList[curFd].time;
  do_write(newFd, (char*)newDir, sizeof(FCB), OW);

  // {
  //   char* tmpBuf = (char*)malloc(BLOCKSIZE * MAXOPENFILE);
  //   openFileList[newFd].pos = 0;
  //   int fileLen = do_read(newFd, openFileList[newFd].length, tmpBuf);
  //   printf("fileLen = %d %s\n", fileLen, tmpBuf);
  //   FCB* fcbPtr = (FCB*)tmpBuf;
  //   for (int i = 0; i < fileLen / sizeof(FCB); ++i) {
  //     printf("name = %s\n", fcbPtr[i].filename);
  //   }
  // }

  my_close(newFd);
  free(newDir);

  // 更新父目录的fcb (更新 "." 条目)
  fcbPtr = (FCB*)buf;
  fcbPtr->length = openFileList[curFd].length;
  openFileList[curFd].pos = 0;
  do_write(curFd, (char*)fcbPtr, sizeof(FCB), OW);
  openFileList[curFd].fcbState = 1;
}

void my_rmdir(char* dirName) {
  // 检查是否是当前目录
  if (strcmp(dirName, ".") == 0) {
    printf("Error: Cannot remove current directory.\n");
    return;
  }

  // 检查是否是父目录
  if (strcmp(dirName, "..") == 0) {
    printf("Error: Cannot remove parent directory.\n");
    return;
  }

  // 在当前目录中查找目标目录
  char buf[BLOCKSIZE];
  openFileList[curFd].pos = 0;
  if (do_read(curFd, openFileList[curFd].length, buf) < 0) {
    printf("Error: Failed to read current directory.\n");
    return;
  }

  FCB* fcbptr = (FCB*)buf;
  int found = -1;
  for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i) {
    if (strcmp(fcbptr->filename, dirName) == 0 &&
        fcbptr->attribute == ATT_DIR) {
      found = i;
      break;
    }
    fcbptr++;
  }

  if (found == -1) {
    printf("Error: Directory not found.\n");
    return;
  }

  // 检查目录是否为空
  char dirBuf[BLOCKSIZE];
  FCB* dirFCB = (FCB*)(myVHead + BLOCKSIZE * fcbptr->firstBlock);
  if (dirFCB->length > 2 * sizeof(FCB)) {  // 除了 "." 和 ".." 还有其他条目
    printf("Error: Directory is not empty.\n");
    return;
  }

  // 释放目录占用的磁盘块
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  int blockID = fcbptr->firstBlock;
  while (blockID != END) {
    int nextBlock = fat1[blockID].id;
    fat1[blockID].id = FREE;
    blockID = nextBlock;
  }

  // 从当前目录中删除目录项
  memset(fcbptr, 0, sizeof(FCB));
  openFileList[curFd].pos = found * sizeof(FCB);
  if (do_write(curFd, (char*)fcbptr, sizeof(FCB), OW) < 0) {
    printf("Error: Failed to remove directory entry.\n");
    return;
  }

  // 备份FAT表
  memcpy((FAT*)(myVHead + BLOCKSIZE * 3), fat1, BLOCKSIZE * 2);
}

void my_ls() {
  char buf[BLOCKSIZE];
  openFileList[curFd].pos = 0;
  if (do_read(curFd, openFileList[curFd].length, buf) < 0) {
    printf("Error: Failed to read current directory.\n");
    return;
  }

  FCB* fcbptr = (FCB*)buf;
  printf("Name\tType\tSize\tDate\tTime\n");
  printf("----------------------------------------\n");
  for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i) {
    if (fcbptr->free == 1) {
      printf("%s", fcbptr->filename);
      if (fcbptr->attribute == ATT_FILE && strcmp(fcbptr->exname, "") != 0) {
        printf(".%s", fcbptr->exname);
      }
      printf("\t");
      printf("%s\t", fcbptr->attribute == ATT_DIR ? "DIR" : "FILE");
      printf("%d\t", fcbptr->length);
      printf("%d/%d/%d\t", (fcbptr->date >> 9) + 2000,
             (fcbptr->date >> 5) & 0xf, fcbptr->date & 0x1f);
      printf("%d:%d:%d\n", fcbptr->time >> 11, (fcbptr->time >> 5) & 0x3f,
             (fcbptr->time & 0x1f) * 2);
    }
    fcbptr++;
  }
}

void my_touch(char* fileName) {
  // 检查文件名长度
  if(strcmp(fileName, "") == 0){
    printf("Please input valid file name");
    return;
  }
  if (strlen(fileName) > 8) {
    printf("Error: File name too long.\n");
    return;
  }


  if(openFileList[curFd].attribute == ATT_FILE){
    printf("Error: Can not create a file in a file");
    return;
  }

  // 检查当前目录是否已满
  char buf[BLOCKSIZE];
  openFileList[curFd].pos = 0;
  if (do_read(curFd, openFileList[curFd].length, buf) < 0) {
    printf("Error: Failed to read current directory.\n");
    return;
  }

  // 检查文件是否已存在
  FCB* fcbptr = (FCB*)buf;
  for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i) {
    if (fcbptr->free == 0) {
      fcbptr++;
      continue;
    }
    size_t len = strlen(fcbptr->filename) + strlen(fcbptr->exname) + 2;     
    char *tmp = malloc(sizeof(char) * len);
    strcpy(tmp, fcbptr->filename);
    strcat(tmp, ".");
    strcat(tmp, fcbptr->exname);
    if (strcmp(tmp, fileName) == 0) {
      free(tmp);
      printf("Error: File already exists.\n");
      return;
    }
    free(tmp);
    fcbptr++;
  }

  // 查找空闲FCB
  fcbptr = (FCB*)buf;
  // int freeFCB = -1;
  // for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i) {
  //   if (fcbptr->free == 0) {
  //     freeFCB = i;
  //     break;
  //   }
  //   fcbptr++;
  // }
  int freeFCB = 0;
  for (freeFCB = 0; freeFCB < openFileList[curFd].length / sizeof(FCB); freeFCB++, fcbptr++) {
    if (fcbptr->free == 0) {
      break;
    }
  }

  if (freeFCB >= BLOCKSIZE / sizeof(FCB)) {
    printf("Error: Current directory is full.\n");
    return;
  }

  int blockID = getFreeBlock();
  if (blockID == -1) {
    printf("Error: No free blocks available.\n");
    return;
  }
  // 更新FAT表
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  FAT* fat2 = (FAT*)(myVHead + BLOCKSIZE * 3);
  fat1[blockID].id = END;
  memcpy(fat2, fat1, BLOCKSIZE * 2);


  // 初始化新文件的FCB
  char *delim = strchr(fileName, '.');
  if (delim != NULL){
    *delim = '\0';
    strncpy(fcbptr->filename, fileName, sizeof(fcbptr->filename) - 1);
    strncpy(fcbptr->exname, delim + 1, sizeof(fcbptr->exname) - 1);
    fcbptr->filename[sizeof(fcbptr->filename) - 1] = '\0';
    fcbptr->exname[sizeof(fcbptr->exname) - 1] = '\0';
  }else {
    strncpy(fcbptr->filename, fileName, sizeof(fcbptr->filename) - 1);
    fcbptr->filename[sizeof(fcbptr->filename) - 1] = '\0';
    fcbptr->exname[0] = '\0';
  }
  fcbptr->attribute = ATT_FILE;
  time_t rawTime = time(NULL);
  struct tm* time = localtime(&rawTime);
  fcbptr->time = time->tm_hour * 2048 + time->tm_min * 32 + time->tm_sec / 2;
  fcbptr->date =
      (time->tm_year - 100) * 512 + (time->tm_mon + 1) * 32 + time->tm_mday;
  fcbptr->firstBlock = blockID;
  fcbptr->length = 0;
  fcbptr->free = 1;

  // 写入修改后的FCB
  openFileList[curFd].pos = freeFCB * sizeof(FCB);
  if (do_write(curFd, (char*)fcbptr, sizeof(FCB), OW) < 0) {
    printf("Error: Failed to write file entry.\n");
    return;
  }

  // 更新当前目录的FCB
  fcbptr = (FCB*)buf;
  fcbptr->length = openFileList[curFd].length;
  openFileList[curFd].pos = 0;
  if (do_write(curFd, (char*)fcbptr, sizeof(FCB), OW) < 0) {
    printf("Error: Failed to write current directory entry.\n");
    return;
  }
  openFileList[curFd].fcbState = 1;
}

void my_rm(char* fileName) {
  // 在当前目录中查找目标文件
  char buf[BLOCKSIZE];
  openFileList[curFd].pos = 0;
  if (do_read(curFd, openFileList[curFd].length, buf) < 0) {
    printf("Error: Failed to read current directory.\n");
    return;
  }

  FCB* fcbptr = (FCB*)buf;
  int found = -1;
  for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i) {
    size_t len = strlen(fcbptr->filename) + strlen(fcbptr->exname) + 2;
    char *tmp = malloc(sizeof(char) * len);
    strcpy(tmp, fcbptr->filename);
    strcat(tmp, ".");
    strcat(tmp, fcbptr->exname);
    if (strcmp(tmp, fileName) == 0 &&
        fcbptr->attribute == ATT_FILE ||
        strcmp(fcbptr->filename, fileName) == 0 &&
        strcmp(fcbptr->exname, "") == 0 &&
        fcbptr->attribute == ATT_FILE
      ) {
      found = i;
      free(tmp);
      break;
    }
    free(tmp);
    fcbptr++;
  }

  if (found == -1) {
    printf("Error: File not found.\n");
    return;
  }

  // 释放文件占用的磁盘块
  FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
  int blockID = fcbptr->firstBlock;
  while (blockID != END) {
    int nextBlock = fat1[blockID].id;
    fat1[blockID].id = FREE;
    blockID = nextBlock;
  }

  // 从当前目录中删除文件项
  memset(fcbptr, 0, sizeof(FCB));
  openFileList[curFd].pos = found * sizeof(FCB);
  if (do_write(curFd, (char*)fcbptr, sizeof(FCB), OW) < 0) {
    printf("Error: Failed to remove file entry.\n");
    return;
  }

  // 备份FAT表
  memcpy((FAT*)(myVHead + BLOCKSIZE * 3), fat1, BLOCKSIZE * 2);
}
void my_tree(int dep){
  if (openFileList[curFd].attribute == ATT_FILE){
    printf("Error: Can list tree in a file\n");
    return;
  }
  char buf[BLOCKSIZE];
  openFileList[curFd].pos = 0;
  if (do_read(curFd, openFileList[curFd].length, buf) < 0) {
    printf("Error: Failed to read current directory.\n");
    return;
  }
  FCB* fcbptr = (FCB*)buf;
  for (int i = 0; i < openFileList[curFd].length / sizeof(FCB); ++i, fcbptr++) {
    if (fcbptr->free == 1 && strcmp(fcbptr->filename, ".") != 0 &&
        strcmp(fcbptr->filename, "..") != 0) {
      for (int j = 0;j < dep; j++){
        printf("\t");
      }
      printf("|");
      for (int j = 0;j < 4; j++){
        printf("-");
      }
      if (strcmp(fcbptr->exname, "") != 0 && fcbptr->attribute == ATT_FILE){
        printf("%s.%s\n", fcbptr->filename, fcbptr->exname);
      }else{
        printf("%s\n", fcbptr->filename);
      }
      if (fcbptr->attribute == ATT_DIR){
        // 递归调用
        // int tmp = curFd;
        size_t len = strlen(fcbptr->filename) + 2 + strlen(fcbptr->exname);
        if (strcmp(fcbptr->exname, "") == 0 || fcbptr->attribute == ATT_DIR) len = strlen(fcbptr->filename) + 1;
        char *tmp = malloc(sizeof(char) * len);
        strcpy(tmp, fcbptr->filename);
        if (strcmp(fcbptr->exname, "") != 0 && fcbptr->attribute == ATT_FILE){
          strcat(tmp, ".");
          strcat(tmp, fcbptr->exname);
        }
        my_cd(tmp);
        my_tree(dep + 1);
        my_cd("..");
        // curFd = tmp;
      }
    }  
  }
}