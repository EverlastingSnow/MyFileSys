// CONST.h
#define BLOCKSIZE 1024 //块大小
#define SIZE 1024000 //总大小
#define END 65535 //文件结束符
#define FREE 0 //空闲块
#define ROOTBLOCKNUM 2 //根目录占用块数
#define MAXOPENFILE 10 //最大打开文件数
#define SYSFILENAME "MYFILESYS" //文件系统名

#define ATT_DIR 0 //attribute 目录 0
#define ATT_FILE 1 //attribute 文件 1

//操作编号
#define CD 0 
#define RMDIR 1
#define MKDIR 2
#define LS 3
#define TOUCH 4
#define RM 5
#define OPEN 6
#define CLOSE 7
#define WRITE 8
#define READ 9
#define EXIT 10
#define HELP 11

#define TW 0 //截断写（Truncate Write）
#define OW 1 //覆盖写（Overwrite）
#define AW 2 //追加写（Append Write）