#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include "cachelab.h"

//定义地址长度
#define BUFF_SIZE 1000
#define ADDR_LENGTH 64

//定义地址类型为unsigned long long int
typedef unsigned long long int addrType;

//定义cache行
typedef struct CacheLine {
    char valid;         //有效位
    addrType tag;       //标记位
    int lru;            //lru计数器
} CacheLine;

//定义cache组
typedef struct {
    CacheLine *lines;   //cache行
} CacheGroup;

//定义cache空间
typedef struct 
{
    CacheGroup *groups; //cache组
} Cache;

int s = 0;                  //组索引位数
int b = 0;                  //块位数
int E = 0;                  //组内cache行数
int verbosity = 0;
char* trace_file = NULL;

int S;                      //组数
int B;                      //块大小

int miss_count = 0;         //缺失次数
int hit_count = 0;          //命中次数
int eviction_count = 0;     //替换次数
addrType lru_count = 1;     //lru计数器

Cache cache;                //cache
addrType indexMark;         //组索引位
addrType tagMask;           //标记位

int cacheState = -1;

/*
    初始化cache
    创建cache行
    把所有cache行清空
*/
void initCache() {
    if(s < 0) {
        printf("s error\n!");
        exit(0);
    }
    cache.groups = (CacheGroup*)malloc(S*sizeof(CacheGroup));   //动态申请cache内存空间
    if(!cache.groups) {
        printf("memory error!\n");
        exit(0);
    }
    for(int i = 0;i < S;i++) {
        cache.groups[i].lines = (CacheLine*)malloc(E*sizeof(CacheLine));    //动态申请cache行的内存空间
        if(!cache.groups) {
            printf("memory error!\n");
            exit(0);
        }
        //设置cache的组索引位，标记位，有效位
        for(int j = 0;j < E;j++) {
            cache.groups[i].lines[j].valid = 0;
            cache.groups[i].lines[j].tag = 0;
            cache.groups[i].lines[j].lru = 0;
        }
    }
}

/*
    释放cache内存空间
*/
void freeCache() {
    for(int i = 0;i < S;i++) {
        free(cache.groups[i].lines);
    }
    free(cache.groups);
}

/*
    如果该数据在cache中，hit_count++;
    如果不在cache中，将其放入cache，miss_count++;
    如果某个cache行被替换，eviction_count++;
*/
void visit(addrType addr) {
    int i, j;
    int full = 1;           //cache是否已满的标志
    int state;          //组内替换位
    int minLru;         //最近最少使用的cache行的lru位
    //判断组内是否已存在该数据
    for(i = 0;i < E;i++) {
        if(cache.groups[indexMark].lines[i].valid == 1 && cache.groups[indexMark].lines[i].tag == tagMask) {
            hit_count++;
            cache.groups[indexMark].lines[i].lru = lru_count;
            //将其他cache行的lru减1
            for(j = 0;j < E;j++) {
                if(j != i)
                    cache.groups[indexMark].lines[j].lru--;
            }
            cacheState = 0;         //表示cache命中
            return;
        }
    }
    //未找到，判断组内cache是否已满
    miss_count++;
    for(i = 0;i < E;i++) {
        if(cache.groups[indexMark].lines[i].valid == 0) {
            full = 0;
            break;
        }
    }
    //如果组内cache未满，将数据装入cache空行中
    if(full == 0) {
        cache.groups[indexMark].lines[i].valid = 1;
        cache.groups[indexMark].lines[i].tag = tagMask;
        cache.groups[indexMark].lines[i].lru = lru_count;
        //将其他cache行的lru减1
        for(j = 0;j < E;j++) {
            if(j != i)
                cache.groups[indexMark].lines[j].lru--;
        }
        cacheState = 1;     //表示为命中，Cache有空行
    }
    //如果组内cache已满，计算出最近最少使用的cache行，即lru最小的cache行
    else {
        eviction_count++;
        state = 0;
        minLru = cache.groups[indexMark].lines[0].lru;
        for(j = 0;j < E;j++) {
            if(minLru > cache.groups[indexMark].lines[j].lru)
            {
                minLru = cache.groups[indexMark].lines[j].lru;
                state = j;
            }
        }
        //找到最仅最少使用的cache行后进行替换
        cache.groups[indexMark].lines[state].valid = 1;
        cache.groups[indexMark].lines[state].tag = tagMask;
        cache.groups[indexMark].lines[state].lru = lru_count;
        //将其他cache行的lru减1
        for(j = 0;j < E;j++) {
            if(j != state) 
                cache.groups[indexMark].lines[j].lru--;
        }
        cacheState = 2;         //表示Cache为命中，没有空闲行，使用LRU算法进行替换
    }
}

/*
    读取trace文件
    执行trace文件中的操作
*/
void traceFile(char* tracePath) {
    char buff[BUFF_SIZE];
    addrType addr = 0;
    tagMask = 0;
    unsigned int length = 0;
    FILE* traceFp = fopen(tracePath, "r");
    //文件打开错误
    if(!traceFp) {
        fprintf(stderr, "%s: %s\n", tracePath, strerror(errno));
        exit(1);
    }
    //依次读取每一行
    while (fgets(buff, BUFF_SIZE, traceFp) != NULL) {
        if(buff[1] == 'S' || buff[1] == 'L' || buff[1] == 'M') {
            sscanf(buff+3, "%llx, %u", &addr, &length);
            indexMark = (addr >> b & ((1 << s)-1));     //索引
            tagMask = (addr >> b) >> s;
            //访问cache
            visit(addr);
            if(verbosity) {
                printf("%c %llx,%u ", buff[1], addr, length);
                if(cacheState == 0)
                    printf("hit ");
                if(cacheState == 1)
                    printf("miss ");
                if(cacheState == 2)
                    printf("miss eviction ");
            }
            
            if(buff[1] == 'M') {
                visit(addr);
                if(verbosity) {
                    if(cacheState == 0)
                        printf("hit");
                    if(cacheState == 1)
                        printf("miss");
                    if(cacheState == 2)
                    printf("miss eviction");
                }
            }
                
            if(verbosity)
                printf("\n");

        }
    }
    fclose(traceFp);
}

//显示帮助信息
void printInfo(char* argv[]) {
    printf("Command：%s [-hv] -s <sum> -b <num> -t <file>\n",argv[0]);
    printf("opt: \n");
    printf("    -h          help\n");
    printf("    -v          display trace\n");
    printf("    -s <num>    number of group bits\n");
    printf("    -E <num>    number of group\n");
    printf("    -b <num>    number of block bits\n");
    printf("    -t <num>    path of trace file\n");
    exit(0);
}


int main(int argc, char* argv[])
{
    char c;
    while((c = getopt(argc, argv, "s:E:b:t:vh")) != -1) {
        switch (c) {
        case 'v':
            verbosity = 1;
            break;
        case 'h':
            printInfo(argv);
            exit(0);
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            trace_file = optarg;
            break;
        default:
            printInfo(argv);
            exit(1);
        }
    }
    //确定所有参数都被指定
    if(s == 0 || E == 0 || b == 0 || trace_file == NULL) {
        printf("%s: Missing required command line argument\n", argv[0]);
        printInfo(argv);
        exit(1);
    }
    S = 1 << s;                 //获得组数
    B = 1 << b;                 //获得块大小

    initCache();                //初始化cache
    traceFile(trace_file);      //读取trace文件
    freeCache();                //释放cache内存空间
    printSummary(hit_count, miss_count, eviction_count);    
    return 0;
}
