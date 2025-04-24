// FS.h
#ifndef FS_H
#define FS_H

#include "CONST.h"

// FCB 32个字节
typedef struct FCB {
  char filename[8];           // 文件名
  char exname[3];             // 扩展名
  unsigned char attribute;    // 文件属性 0-目录 1-文件
  unsigned char reserved[5];  // 系统保留
  unsigned short time;        // 文件创建时间
  unsigned short date;        // 文件创建日期
  unsigned short firstBlock;  // 第一个数据块号
  unsigned int length;        // 文件长度
  char free;                  // 是否被占
} FCB;

// 用户打开表
typedef struct USEROPEN {
  char filename[8];           // 文件名
  char exname[3];             // 扩展名
  unsigned char attribute;    // 文件属性 0-目录 1-文件
  unsigned char reserved[5];  // 系统保留
  unsigned short time;        // 文件创建时间
  unsigned short date;        // 文件创建日期
  unsigned short firstBlock;  // 第一个数据块号
  unsigned int length;        // 文件长度
  char free;                  // 打开表项是否为空，0为空，1被占用

  int faDirBlock;  // 文件所在父目录的盘块号
  int fcbOffset;   // 文件所在父目录盘块的FCB偏移
  char dir[80];    // 文件所在pwd目录
  int pos;         // 读写指针位置
  char fcbState;   // FCB是否被修改，修改1，未修改0
} USEROPEN;

// 引导块
typedef struct BLOCK0 {
  char name[16];              // 文件系统名称
  char info[200];             // 磁盘信息
  unsigned short root;        // 根目录起始块号
  unsigned char* startBlock;  // 数据区起始块号
} BLOCK0;

// FAT表
typedef struct FAT {
  unsigned short
      id;  // FAT
           // 为FREE表示空闲，为END表示是某文件最后的磁盘块，为其他值是下一个磁盘块索引编号
} FAT;

extern unsigned char* myVHead;  // 虚拟磁盘头指针
extern USEROPEN openFileList[MAXOPENFILE];
extern int curFd;                   // 指向当前文件描述符编号
extern unsigned char* startPos;     // 虚拟磁盘数据区起始位置
extern unsigned char buffer[SIZE];  // 用于读写数据块的缓冲区

void my_startsys();  // 初始化文件系统
void my_format();    // 格式化文件系统
void my_cd(char* dirName);
void my_mkdir(char* dirName);
void my_rmdir(char* dirName);
void my_ls();
void my_touch(char* fileName);
void my_rm(char* fileName);
int my_open(char* fileName);
int my_close(int fd);

int my_write(int fd);
int do_write(int fd, char* text, int len, char wStyle);

int my_read(int fd);
int do_read(int fd, int len, char* text);

void my_copy_fcb(int fd, FCB* fcbptr, int flag);

int getFreeUserOpen();

int getFreeBlock();

void my_exitsys();

#endif