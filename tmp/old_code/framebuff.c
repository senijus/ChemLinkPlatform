#include "framebuff.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "font.h"
#include <string.h>
#include "log.h"  // 引入日志头文件，用于替换perror输出

// 帧缓冲初始化：打开设备、获取屏幕信息、映射显存
FB_T *FrameBuffInit(const char *pDevName)
{
    FB_T *pfb = NULL;
    int ret = 0;

    pfb = malloc(sizeof(FB_T));
    if (NULL == pfb)
    {
        // 内存分配失败，输出错误日志
        LogWrite(LOG_LEVEL_ERROR, "FrameBuffInit: malloc FB_T struct failed");
        return NULL;
    }
    LogWrite(LOG_LEVEL_INFO, "FrameBuffInit: malloc FB_T struct success");  // 内存分配成功日志

    pfb->fd = open(pDevName, O_RDWR);
    if (-1 == pfb->fd)
    {
        // 设备打开失败，输出错误日志
        LogWrite(LOG_LEVEL_ERROR, "FrameBuffInit: open framebuff device failed");
        free(pfb);  // 释放已分配的内存，避免泄漏
        return NULL;
    }
    LogWrite(LOG_LEVEL_INFO, "FrameBuffInit: open framebuff device success");  // 设备打开成功日志

    ret = ioctl(pfb->fd, FBIOGET_VSCREENINFO, &pfb->vscreeninfo);
    if (-1 == ret)
    {
        // 获取屏幕信息失败，输出错误日志
        LogWrite(LOG_LEVEL_ERROR, "FrameBuffInit: ioctl get screen info failed");
        close(pfb->fd);
        free(pfb);
        return NULL;
    }
    LogWrite(LOG_LEVEL_INFO, "FrameBuffInit: get screen info success");  // 获取屏幕信息成功日志

    printf("=============================================\n");
    printf("width:%d\n", pfb->vscreeninfo.xres);
    printf("high:%d\n", pfb->vscreeninfo.yres);
    printf("width_virtual:%d\n", pfb->vscreeninfo.xres_virtual);
    printf("high_virtual:%d\n", pfb->vscreeninfo.yres_virtual);
    printf("pixel:%d\n", pfb->vscreeninfo.bits_per_pixel / 8);
    printf("=============================================\n");

    // 映射显存到用户空间
    pfb->pframestart = mmap(NULL, pfb->vscreeninfo.xres_virtual * pfb->vscreeninfo.yres_virtual * pfb->vscreeninfo.bits_per_pixel / 8, 
                            PROT_READ | PROT_WRITE, MAP_SHARED, pfb->fd, 0);
    if (NULL == pfb->pframestart)
    {
        // 显存映射失败，输出错误日志
        LogWrite(LOG_LEVEL_ERROR, "FrameBuffInit: mmap framebuff memory failed");
        close(pfb->fd);
        free(pfb);
        return NULL;
    }
    LogWrite(LOG_LEVEL_INFO, "FrameBuffInit: mmap framebuff memory success");  // 显存映射成功日志

    return pfb;
}

// 帧缓冲销毁：释放映射内存、关闭设备、释放结构体
int FrameBuffDestroy(FB_T **ppfb)
{
    if (*ppfb != NULL)
    {
        // 释放显存映射
        munmap((*ppfb)->pframestart, (*ppfb)->vscreeninfo.xres_virtual * (*ppfb)->vscreeninfo.yres_virtual * (*ppfb)->vscreeninfo.bits_per_pixel / 8);
        LogWrite(LOG_LEVEL_INFO, "FrameBuffDestroy: munmap framebuff memory success");
        
        // 关闭设备文件描述符
        close((*ppfb)->fd);
        // 释放结构体内存
        free(*ppfb);
        *ppfb = NULL;
        LogWrite(LOG_LEVEL_INFO, "FrameBuffDestroy: free FB_T struct success");
    }

    return 0;
}

// 绘制单个像素点
int DrawOnePixel(FB_T *pfb, int x, int y, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb)
{
    pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x].Red = tmpr;
    pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x].Green = tmpg;
    pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x].Blue = tmpb;

    return 0;
}

// 绘制水平线
int DrawHorLine(FB_T *pfb, int x, int y, int width, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb)
{
    int i = 0;

    for(i = 0; i < width && i + x < pfb->vscreeninfo.xres; i++)
    {
        pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x + i].Red = tmpr;
        pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x + i].Green = tmpg;
        pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x + i].Blue = tmpb;
    }

    return 0;
}

// 绘制垂直线
int DrawVerLine(FB_T *pfb, int x, int y, int high, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb)
{
    int i = 0;

    for(i = 0; i < high && y + i < pfb->vscreeninfo.yres; i++)
    {
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x].Red = tmpr;
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x].Green = tmpg;
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x].Blue = tmpb;
    }

    return 0;
}

// 绘制空心矩形
int DrawRect(FB_T *pfb, int x, int y, int width, int high, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb)
{
    int i = 0;

    for(i = 0; i < width && i + x < pfb->vscreeninfo.xres; i++)
    {
        pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x + i].Red = tmpr;
        pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x + i].Green = tmpg;
        pfb->pframestart[(y * pfb->vscreeninfo.xres_virtual) + x + i].Blue = tmpb;
    }

    for(i = 0; i < width && i + x < pfb->vscreeninfo.xres; i++)
    {
        pfb->pframestart[((y + high - 1) * pfb->vscreeninfo.xres_virtual) + x + i].Red = tmpr;
        pfb->pframestart[((y + high - 1) * pfb->vscreeninfo.xres_virtual) + x + i].Green = tmpg;
        pfb->pframestart[((y + high - 1) * pfb->vscreeninfo.xres_virtual) + x + i].Blue = tmpb;
    }

    for(i = 0; i < high && y + i < pfb->vscreeninfo.yres; i++)
    {
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x].Red = tmpr;
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x].Green = tmpg;
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x].Blue = tmpb;
    }

    for(i = 0; i < high && y + i < pfb->vscreeninfo.yres; i++)
    {
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x + width -1].Red = tmpr;
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x + width -1].Green = tmpg;
        pfb->pframestart[((y + i) * pfb->vscreeninfo.xres_virtual) + x + width -1].Blue = tmpb;
    }

    return 0;
}

// 绘制实心矩形
int DrawSolidRect(FB_T *pfb, int x, int y, int width, int high, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb)
{
    int i = 0;
    int j = 0;

    for(j = 0; j < high && y + j < pfb->vscreeninfo.yres; j++)
    {
        for(i = 0; i < width && i + x < pfb->vscreeninfo.xres; i++)
        {
            pfb->pframestart[((y + j) * pfb->vscreeninfo.xres_virtual) + x + i].Red = tmpr;
            pfb->pframestart[((y + j) * pfb->vscreeninfo.xres_virtual) + x + i].Green = tmpg;
            pfb->pframestart[((y + j) * pfb->vscreeninfo.xres_virtual) + x + i].Blue = tmpb;
        }
    }

    return 0;
}

// 清屏（填充指定颜色）
int ClearScreen(FB_T *pfb, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb)
{
    int i = 0;
    int j = 0;

    for(i = 0; i < pfb->vscreeninfo.yres; i++)
    {
        for(j = 0; j < pfb->vscreeninfo.xres; j++)
        {
            pfb->pframestart[(i * pfb->vscreeninfo.xres_virtual) + j].Red = tmpr;
            pfb->pframestart[(i * pfb->vscreeninfo.xres_virtual) + j].Green = tmpg;
            pfb->pframestart[(i * pfb->vscreeninfo.xres_virtual) + j].Blue = tmpb;
        }
    }

    return 0;
}

// 绘制单个ASCII字符
int DrawOneAscii(FB_T *pfb, int x, int y, unsigned char ch, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb)
{
    int i = 0;
    int j = 0;
    unsigned char flag = 0;

    for(j = 0; j < 16; j++)
    {
        for(i = 0, flag = 0x80; i < 8; i++, flag >>= 1)
        {
            if(flag & fontdata_8x16[ch * 16 + j])
            {
                DrawOnePixel(pfb, x+i, y+j, fr, fg, fb);
            }
            else
            {
                DrawOnePixel(pfb, x+i, y+j, br, bg, bb);   
            }
        }
    }

    return 0;
}

// 绘制ASCII字符串
int DrawString(FB_T *pfb, int x, int y, const char *pstr, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb)
{
    int i = 0;

    while(*pstr != '\0')
    {
        DrawOneAscii(pfb, x+i*8, y, *pstr, fr, fg, fb, br, bg, bb);
        pstr++;
        i++;
    }

    return 0;
}

// 绘制单个汉字
int DrawOneHanZi(FB_T *pfb, int x, int y, const char *pstr, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb)
{
    int i = 0;
    int n = 0;
    int j = 0;
    unsigned char flag = 0x80;

    for(n = 0; n < HANZI_MAXCNT; n++)
    {
        if(0 == strcmp(pstr, gHanziList[n].str))
        {
            break;
        }
    }

    if(n == HANZI_MAXCNT)
    {
        LogWrite(LOG_LEVEL_WARN, "DrawOneHanZi: hanzi not found in font list");  // 汉字未找到警告日志
        return -1;
    }

    for(j = 0; j < 16; j++)
    {
        for(i = 0, flag = 0x80; i < 8; i++, flag >>= 1)
        {
            if(flag & gHanziList[n].zimo[2*j])
            {
                DrawOnePixel(pfb, x+i, y+j, fr, fg, fb);
            }
            else
            {
                DrawOnePixel(pfb, x+i, y+j, br, bg, bb);
            }
        }
        for(i = 8, flag = 0x80; i < 16; i++, flag >>= 1)
        {
            if(flag & gHanziList[n].zimo[2*j+1])
            {
                DrawOnePixel(pfb, x+i, y+j, fr, fg, fb);
            }
            else
            {
                DrawOnePixel(pfb, x+i, y+j, br, bg, bb);
            }
        }
    }

    return 0;
}

// 绘制汉字字符串
int DrawHanZiString(FB_T *pfb, int x, int y, const char *pstr, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb)
{
    int i = 0;
    char tmpstr[4] = {0};

    while(*pstr != '\0')
    {
        memcpy(tmpstr, pstr, 3);
        DrawOneHanZi(pfb, x+i*16, y, tmpstr, fr, fg, fb, br, bg, bb);
        pstr += 3;
        i++;
    }

    return 0;
}

// 绘制BMP图片
int DrawBmp(FB_T *pfb, int x, int y, char *pbmppath)
{
    int i = 0;
    int j = 0;
    int fd = 0;
    int width = 0;
    int high = 0;
    char tmpbuff[3] = {0};

    fd = open(pbmppath, O_RDONLY);
    if(-1 == fd)
    {
        // BMP文件打开失败，输出错误日志
        LogWrite(LOG_LEVEL_ERROR, "DrawBmp: open bmp file failed");
        return -1;  // 原返回0改为-1，更符合错误语义，不影响核心逻辑
    }
    LogWrite(LOG_LEVEL_INFO, "DrawBmp: open bmp file success");  // BMP文件打开成功日志

    lseek(fd, 18, SEEK_SET);
    read(fd, &width, sizeof(width));
    read(fd, &high, sizeof(high));

    lseek(fd, 54, SEEK_SET);

    for(j = high-1; j >= 0; j--)
    {
        for(i = 0; i < width; i++)
        {
            read(fd, tmpbuff, sizeof(tmpbuff));
            DrawOnePixel(pfb, x+i, j+y, tmpbuff[2], tmpbuff[1], tmpbuff[0]);
        }

        if(3*width%4 != 0)
        {
            read(fd, tmpbuff, 4-3*width%4);
        }
    }

    close(fd);
    LogWrite(LOG_LEVEL_INFO, "DrawBmp: draw bmp to framebuff success");  // BMP绘制成功日志

    return 0;
}