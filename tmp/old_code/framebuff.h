#ifndef __FRAMEBUFF_H__
#define __FRAMEBUFF_H__

#include <linux/fb.h>

/* 每个像素点颜色信息 ARGB888 小端存储 */
typedef struct rgb {
    unsigned char Blue;
    unsigned char Green;
    unsigned char Red;
    unsigned char Reserved;
}RGB_T;

/* Framebuff 句柄类型 */
typedef struct framebuff {
    int fd;                                     //文件描述符
    struct fb_var_screeninfo vscreeninfo;       //屏幕信息
    RGB_T *pframestart;                         //帧缓存起始地址
}FB_T;

extern FB_T *FrameBuffInit(const char *pDevName);
extern int FrameBuffDestroy(FB_T **ppfb);
extern int DrawOnePixel(FB_T *pfb, int x, int y, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb);
extern int DrawHorLine(FB_T *pfb, int x, int y, int width, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb);
extern int DrawVerLine(FB_T *pfb, int x, int y, int high, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb);
extern int DrawRect(FB_T *pfb, int x, int y, int width, int high, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb);
extern int DrawSolidRect(FB_T *pfb, int x, int y, int width, int high, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb);
extern int ClearScreen(FB_T *pfb, unsigned char tmpr, unsigned char tmpg, unsigned char tmpb);
extern int DrawOneAscii(FB_T *pfb, int x, int y, unsigned char ch, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb);
extern int DrawString(FB_T *pfb, int x, int y, const char *pstr, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb);

extern int DrawOneHanZi(FB_T *pfb, int x, int y, const char *pstr, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb);

extern int DrawHanZiString(FB_T *pfb, int x, int y, const char *pstr, 
                                          unsigned char fr, unsigned char fg, unsigned char fb,
                                          unsigned char br, unsigned char bg, unsigned char bb);
extern int DrawBmp(FB_T *pfb, int x, int y, char *pbmppath);


#endif