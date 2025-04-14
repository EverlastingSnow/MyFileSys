#include "FS.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

unsigned char* myVHead; // 虚拟磁盘头指针
USEROPEN openFileList[MAXOPENFILE];
int curFd; //指向当前文件描述符编号
//pwd可由openFileList[curFd].dir得到
unsigned char * startPos;//虚拟磁盘数据区起始位置
unsigned char buffer[SIZE]; // 用于读写数据块的缓冲区

void my_copy_fcb(int fd, FCB* fcbptr, int flag){
    //拷贝FCB到内存或将内存的FCB存入磁盘
    //0: 拷贝到内存，1: 拷贝到磁盘
    if (flag == 0){
        memcpy(&openFileList[fd], fcbptr, sizeof(FCB));
    }else {
        memcpy(fcbptr, &openFileList[fd], sizeof(FCB));
    }
}
void my_format(){
    //fat表格式化
    FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
    FAT* fat2 = (FAT*)(myVHead + BLOCKSIZE * 3);
    for (int i = 0;i < 6; ++i){
        fat1[i].id = END;
        fat2[i].id = END;
    }
    for (int i = 6;i < 1000; ++i){
        fat1[i].id = FREE;
        fat2[i].id = FREE;
    }

    //引导块格式化
    BLOCK0 * block0 = (BLOCK0*)myVHead;
    
    strcpy(block0->name, SYSFILENAME);
    strcpy(block0->info, "A FAT file system\n Block size: 1KB\nBlock nums: 1000\nBlock0: 0\nFAT1: 1\nFAT2: 3\nRoot: 5\nData: 7\n");
    block0->root = 5;
    block0->startBlock = myVHead + BLOCKSIZE * (5 + ROOTBLOCKNUM);

    //根目录当前目录格式化
    FCB* root = (FCB*)(myVHead + BLOCKSIZE * 5);
    strcpy(root->filename, ".");
    strcpy(root->exname, "d");
    root->attribute = ATT_DIR;

    time_t rawTime = time(NULL);
    struct tm *time = localtime(&rawTime);
    root->time = time->tm_hour * 2048 + time->tm_min * 32 + time->tm_sec / 2;
    root->date = (time->tm_year - 100) * 512 + (time->tm_mon + 1) * 32 + (time->tm_mday);
    root->firstBlock = 1;
    root->length = sizeof(FCB);
    root->free = 1;

    //根目录上级目录格式化
    FCB* root2 = (FCB*)(root + 1);
    strcpy(root->filename, "..");
    strcpy(root->exname, "d");
    root->attribute = ATT_DIR;

    //剩余块格式化
    for (int i = 2;i < BLOCKSIZE / sizeof(FCB); ++i){
        root2++;
        root2->free = 0;
        strcpy(root2->filename, "");
    }

    FILE *fp = fopen(SYSFILENAME, "wb");
    if (fp == NULL){
        printf("Error: Cannot open virtual disk file.\n");
        exit(1);
    }
    fwrite(myVHead, SIZE, 1, fp);
    fclose(fp);
}
void my_startsys(){
    //初始化虚拟磁盘
    myVHead = (unsigned char*)malloc(SIZE);
    FILE *file; 
    if ((file = fopen(SYSFILENAME , "r")) != NULL){
        fread(buffer, SIZE, 1, file);
        fclose(file);
        if (memcmp(buffer, SYSFILENAME, sizeof(SYSFILENAME)) == 0) {
            memcpy(myVHead, buffer, SIZE);
            printf("Virtual disk loaded successfully.\n");    
        }else{
            printf("Error: Invalid virtual disk format, rebuild it.\n");
            my_format();
            memcpy(buffer, myVHead, SIZE);
        }
    }else{
        printf("Error: Virtual disk not found, creating a new one.\n");
        my_format();
        memcpy(buffer, myVHead, SIZE);
    }

    //初始化根目录
    FCB* root = (FCB*)(myVHead + BLOCKSIZE * 5);
    my_copy_fcb(0, root, 0);
    strcpy(openFileList[0].dir, "/root");
    openFileList[0].fcbState = 0;
    openFileList[0].pos = 0;
    openFileList[0].free = 1;
    //root目录存在第5块
    openFileList[0].faDirBlock = 5;
    openFileList[0].fcbOffset = 0;
}
void my_exitsys(){
    for (int i = 0;i < MAXOPENFILE; ++i){
        if (openFileList[i].free == 1){
            my_close(i);
        }
    }
    FILE *fp = fopen(SYSFILENAME, "wb");
    fwrite(myVHead, SIZE, 1, fp);
    fclose(fp);
}

int getFreeUserOpen(){
    for (int i = 0;i < MAXOPENFILE; ++i){
        if (openFileList[i].free == 0){
            openFileList[i].free = 1;
            return i;
        }
    }
    return -1;
}
int my_open(char* fileName){
    //读当前目录FCB
    char buf[MAXOPENFILE * BLOCKSIZE];
    openFileList[curFd].pos = 0;
    if (do_read(curFd, openFileList[curFd].length, buf) < 0){
        printf("Error: Failed to read current directory.\n");
        return -1;
    }
    //遍历当前目录FCB，找到目标文件
    FCB* fcbptr = (FCB*)buf;
    int fcbID = -1;int isDir = 0;
    for (int i = 0;i < openFileList[curFd].length / sizeof(FCB); ++i){
        if (strcmp(fcbptr->filename, fileName) == 0 && fcbptr->attribute == ATT_FILE){
            //找到目标文件
            fcbID = i;
            break;
        }
        if (strcmp(fcbptr->filename, fileName) == 0 && fcbptr->attribute == ATT_DIR){
            isDir = 1;
        }
    }
    if (fcbID == -1){
        if (isDir == 1){
            printf("Error: Cannot open a directory.\n");
        }else{
            printf("Error: File not found.\n");
        }
        return -1;
    }
    int fd = getFreeUserOpen();
    if (fd == -1){
        printf("Error: No free file descriptor.\n");
        return -1;
    }
    my_copy_fcb(fd, fcbptr, 0);
    strcat(strcpy(openFileList[fd].dir, openFileList[curFd].dir), fileName);
    //记录父目录
    openFileList[fd].faDirBlock = openFileList[curFd].firstBlock;
    openFileList[fd].fcbOffset = fcbID;

    openFileList[fd].fcbState = 0;
    openFileList[fd].pos = 0;
    openFileList[fd].free = 1;
    curFd = fd;
    return 0;
}
int my_close(int fd){
    //fd合法性判断
    if (fd < 0 || fd >= MAXOPENFILE){
        if (fd < 0)
            printf("Fd is negative!\n");
        else
            printf("Fd is larger than MAX!\n");
        return -1;
    }
    //获取父目录fd
    int fatherFd = -1;
    for (int i = 0;i < MAXOPENFILE; ++i){
        if (openFileList[i].firstBlock == openFileList[fd].faDirBlock){
            fatherFd = i;
            break;
        }
    }
    if (fatherFd == -1){
        printf("Error: Father directory not found.\n");
        return -1;
    }
    //写入修改的FCB
    char buf[MAXOPENFILE * BLOCKSIZE];
    if (openFileList[fd].fcbState == 1){
        if (do_read(fatherFd, openFileList[fatherFd].length, buf) < 0){
            printf("Error: Failed to read father directory.\n");
            return -1;
        }
        //获取父目录中当前文件的FCB
        FCB* fcbptr = (FCB*) (buf + openFileList[fd].fcbOffset * sizeof(FCB));
        //修改FCB为当前状态
        my_copy_fcb(fd, fcbptr, 1);
        openFileList[fatherFd].pos = openFileList[fd].fcbOffset * sizeof(FCB);
        //写入磁盘
        if (do_write(fatherFd, (char*)fcbptr, sizeof(FCB), OW) < 0){
            printf("Error: Failed to write father directory.\n");
            return -1;
        }
    }   

    //如果是目录，则将当前目录设为父目录
    if (openFileList[fd].attribute == ATT_DIR){
        curFd = fatherFd;
    }
    //释放打开的文件表
    memset(&openFileList[fd], 0, sizeof(USEROPEN));
    
    return fatherFd;
}

int my_read(int fd){
    if(fd < 0 || fd >= MAXOPENFILE){
        printf("File not exist!\n");
        return -1;
    }
    openFileList[fd].pos = 0;
    char buf[MAXOPENFILE * BLOCKSIZE] = "";
    do_read(fd, openFileList[fd].length, buf);
    printf("%s\n", buf);
    return 0;
}
int do_read(int fd, int len, char *text){
    //获取文件磁盘块位置
    int offset = openFileList[fd].pos;
    int blockID = openFileList[fd].firstBlock;
    FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
    FAT* fatPtr = fat1 + blockID;
    while (offset >= BLOCKSIZE){
        offset -= BLOCKSIZE;
        blockID = fatPtr->id;
        if (blockID == END){
            printf("Error: End of file reached.\n");
            return -1;
        }
        fatPtr = fat1 + blockID;
    }
    //将数据读入缓冲区
    unsigned char* buf = (unsigned char * ) malloc(BLOCKSIZE);
    if (buf == NULL){
        printf("Memory allocation failed.\n");
        return -1;
    }
    unsigned char *blockPtr = (myVHead + BLOCKSIZE * blockID);
    memcpy(buf, blockPtr, BLOCKSIZE);

    //读取内容
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

int getFreeBlock(){
    FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
    for (int i = 0; i < SIZE / BLOCKSIZE; ++i){
        if (fat1[i].id == FREE){
            return i;
        }
    }
    return -1;
}

int my_write(int fd){
    if (fd < 0 || fd >= MAXOPENFILE){
        printf("Error: Invalid file descriptor.\n");
        return -1;
    }
    int wStyle;
    while(1){
        printf("Enter write style: (0)truncate (1)overwrite (2):append ");
        scanf("%d", &wStyle);
        if (wStyle == TW || wStyle == OW || wStyle == AW){
            break;
        }else{
            printf("Invalid input, please try again.\n");
        }
    }
    char text[MAXOPENFILE * BLOCKSIZE] = "";
    char line[MAXOPENFILE * BLOCKSIZE] = "";
    printf("Please enter the file content, if finished please input \":wq\" in a new line:\n");
    while (fgets(line, MAXOPENFILE * BLOCKSIZE, stdin)) {
        if (strcmp(line, ":wq") == 0) {
            break;
        }
        line[strlen(line)] = '\n';
        strcat(text, line);
    }

    text[strlen(text)] = '\0';
    do_write(fd, text, strlen(text) + 1, wStyle);
    openFileList[fd].fcbState = 1;
    return 0;
}
int do_write(int fd, char *text, int len, char wStyle){
    //截断写
    if (wStyle == TW){
        openFileList[fd].pos = 0;
        openFileList[fd].length = 0;
    }else{
        //追加写
        if (wStyle == AW){
            openFileList[fd].pos = openFileList[fd].length;
        }
        if (openFileList[fd].attribute == ATT_FILE){
            if(openFileList[fd].length != 0){
                openFileList[fd].pos = openFileList[fd].length -1; //去掉末尾\0
            }
        }
    }

    //获取文件磁盘块位置和偏移
    int blockID = openFileList[fd].firstBlock;
    int offset = openFileList[fd].pos;
    FAT* fat1 = (FAT*)(myVHead + BLOCKSIZE * 1);
    FAT* fatPtr = fat1 + blockID;
    while (offset >= BLOCKSIZE){
        blockID = fatPtr->id;
        if (blockID == END){
            printf("Error: End of file reached.\n");
            return -1;
        }
        fatPtr = fat1 + blockID;
        offset -= BLOCKSIZE;
    }

    //将数据写入磁盘块
    unsigned char* blockPtr = (unsigned char * )(myVHead + BLOCKSIZE * blockID);
    unsigned char* buf = (unsigned char * ) malloc(BLOCKSIZE * MAXOPENFILE);
    if (buf == NULL){
        printf("Memory allocation failed.\n");
        return -1;
    }
    int leftToWrite = len;
    while (leftToWrite > 0) {
        memcpy(buf, blockPtr, BLOCKSIZE);
        for (; offset < BLOCKSIZE; ++offset){
            if (leftToWrite == 0){
                break;
            }
            buf[offset] = text[len - leftToWrite];
            leftToWrite--;
        }
        memcpy(blockPtr, buf, BLOCKSIZE);

        //找到下一个磁盘块，如果没有则申请
        if (offset == BLOCKSIZE && leftToWrite > 0) {
            offset = 0;
            blockID = fatPtr->id;
            if (blockID == END){
                blockID = getFreeBlock();
                if (blockID == -1){
                    printf("Error: No free block available.\n");
                    return -1;
                }
                fatPtr->id = blockID;
                fatPtr = fat1 + blockID;
                fatPtr->id = END;
            }else{
                blockPtr = (myVHead + BLOCKSIZE * blockID);
                fatPtr = fat1 + blockID;
            }
        }
    }

    openFileList[fd].pos += len;
    if (openFileList[fd].pos > openFileList[fd].length){
        openFileList[fd].length = openFileList[fd].pos;
    }
    //截断写一定要修改文件长度，覆盖写需要修改目录长度
    if (wStyle == TW || (wStyle == AW && openFileList[fd].attribute == ATT_FILE)){
        offset = openFileList[fd].length;
        fatPtr = fat1 + openFileList[fd].firstBlock;
        while (offset >= BLOCKSIZE){
            blockID = fatPtr->id;
            offset -= BLOCKSIZE;
            fatPtr = fat1 + blockID;
        }
        int id = fatPtr->id;
        fatPtr->id = END;
        fatPtr = fat1 + id;
        while (fatPtr->id != END){
            id = fatPtr->id;
            fatPtr->id = FREE;
            fatPtr = fat1 + id;
        }
        fatPtr->id = FREE;
    }
    //备份fat1到fat2
    memcpy((FAT*)(myVHead + BLOCKSIZE * 3), (FAT*)(myVHead + BLOCKSIZE * 1), BLOCKSIZE * 2);
    return len - leftToWrite;
}


