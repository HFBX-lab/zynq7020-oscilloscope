/* The only Windows-specific layer. UI + data model remain portable C. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scope_ui.h"
#include "win32_host.h"

static ScopeUI ui;
static HWND window;
static lv_indev_t *pointer;
static lv_point_t mouse;
static bool pressed, exiting;
static ScopeLanguage window_language=SCOPE_ZH_CN;
typedef struct {lv_point_t point;lv_indev_state_t state;} PointerEvent;
static PointerEvent pointer_events[64];
static unsigned pointer_head,pointer_tail;
static uint32_t framebuffer[SCOPE_WIDTH*SCOPE_HEIGHT];
static uint32_t drawbuffer[SCOPE_WIDTH*60];
static BITMAPINFO bitmap;
static HDC paint_buffer;
static HBITMAP paint_bitmap;
static HGDIOBJ paint_previous;
static int paint_width,paint_height;

static void queue_pointer(void) {
    unsigned next=(pointer_tail+1)%64;
    if(next==pointer_head)pointer_head=(pointer_head+1)%64;
    pointer_events[pointer_tail]=(PointerEvent){mouse,pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED};
    pointer_tail=next;
}

static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {
    int width=area->x2-area->x1+1;
    for(int y=area->y1;y<=area->y2;y++) {
        memcpy(&framebuffer[y*SCOPE_WIDTH+area->x1],pixels,(size_t)width*4);
        pixels+=width*4;
    }
    lv_display_flush_ready(display);
    if(lv_display_flush_is_last(display) && window) InvalidateRect(window,NULL,FALSE);
}
static void input_read(lv_indev_t *indev,lv_indev_data_t *data) {
    (void)indev;
    if(pointer_head!=pointer_tail) {
        PointerEvent event=pointer_events[pointer_head];
        pointer_head=(pointer_head+1)%64;
        data->point=event.point;data->state=event.state;
    } else {
        data->point=mouse;data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    }
    data->continue_reading=pointer_head!=pointer_tail;
}
static RECT viewport(int width,int height) {
    RECT r={0};
    if(width<=0 || height<=0)return r;
    int w=width,h=height;
    if((int64_t)width*SCOPE_HEIGHT>(int64_t)height*SCOPE_WIDTH)
        w=(int)((int64_t)height*SCOPE_WIDTH/SCOPE_HEIGHT);
    else h=(int)((int64_t)width*SCOPE_HEIGHT/SCOPE_WIDTH);
    r.left=(width-w)/2;r.top=(height-h)/2;r.right=r.left+w;r.bottom=r.top+h;
    return r;
}
static bool map_pointer(HWND hwnd,int x,int y,bool clamp,lv_point_t *point) {
    RECT client;GetClientRect(hwnd,&client);
    RECT r=viewport(client.right,client.bottom);
    int w=r.right-r.left,h=r.bottom-r.top;
    if(w<=0 || h<=0)return false;
    if(!clamp && (x<r.left || x>=r.right || y<r.top || y>=r.bottom))return false;
    if(x<r.left)x=r.left;
    if(x>=r.right)x=r.right-1;
    if(y<r.top)y=r.top;
    if(y>=r.bottom)y=r.bottom-1;
    point->x=(int)((int64_t)(x-r.left)*SCOPE_WIDTH/w);
    point->y=(int)((int64_t)(y-r.top)*SCOPE_HEIGHT/h);
    return true;
}
static void release_paint_buffer(void) {
    if(!paint_buffer)return;
    SelectObject(paint_buffer,paint_previous);
    DeleteObject(paint_bitmap);DeleteDC(paint_buffer);
    paint_buffer=NULL;paint_bitmap=NULL;paint_previous=NULL;
    paint_width=paint_height=0;
}
static void paint_frame(HDC dc,int width,int height) {
    if(width<=0 || height<=0)return;
    if(!paint_buffer || paint_width!=width || paint_height!=height) {
        release_paint_buffer();
        paint_buffer=CreateCompatibleDC(dc);
        if(!paint_buffer)return;
        paint_bitmap=CreateCompatibleBitmap(dc,width,height);
        if(!paint_bitmap){DeleteDC(paint_buffer);paint_buffer=NULL;return;}
        paint_previous=SelectObject(paint_buffer,paint_bitmap);
        paint_width=width;paint_height=height;
    }
    RECT client={0,0,width,height};
    FillRect(paint_buffer,&client,(HBRUSH)GetStockObject(BLACK_BRUSH));
    RECT r=viewport(width,height);
    if(r.right<=r.left || r.bottom<=r.top)return;
    SetStretchBltMode(paint_buffer,HALFTONE);
    SetBrushOrgEx(paint_buffer,0,0,NULL);
    StretchDIBits(paint_buffer,r.left,r.top,r.right-r.left,r.bottom-r.top,
                  0,0,SCOPE_WIDTH,SCOPE_HEIGHT,framebuffer,&bitmap,DIB_RGB_COLORS,SRCCOPY);
    BitBlt(dc,0,0,width,height,paint_buffer,0,0,SRCCOPY);
}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        RECT client;GetClientRect(hwnd,&client);
        paint_frame(dc,client.right,client.bottom);
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_LBUTTONDOWN:
        if(!map_pointer(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp),false,&mouse))return 0;
        SetCapture(hwnd); pressed=true;
        queue_pointer();return 0;
    case WM_MOUSEMOVE:
        if(!map_pointer(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp),pressed,&mouse))
            mouse=(lv_point_t){-1,-1};
        return 0;
    case WM_LBUTTONUP:
        if(!pressed)return 0;
        map_pointer(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp),true,&mouse);pressed=false;
        queue_pointer();ReleaseCapture();return 0;
    case WM_CAPTURECHANGED: case WM_KILLFOCUS:
        if(pressed){pressed=false;queue_pointer();}return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: release_paint_buffer();exiting=true; PostQuitMessage(0); return 0;
    default: return DefWindowProcW(hwnd,msg,wp,lp);
    }
}
static void pump(unsigned ms) {
    for(unsigned elapsed=0;elapsed<ms;elapsed+=5) {
        MSG msg;
        while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT){exiting=true;break;}
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        unsigned step=ms-elapsed<5?ms-elapsed:5;
        lv_tick_inc(step); scope_ui_step(&ui,(double)step/1000); lv_timer_handler();
        if(window && window_language!=ui.language) {
            window_language=ui.language;
            SetWindowTextW(window,ui.language==SCOPE_ZH_CN?
                L"数字示波器 · 演示模式":L"Oscilloscope · Demo Mode");
        }
    }
}
void host_test_pump(unsigned ms) {pump(ms);}
int host_test_can_maximize(void) {
    LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
    return (style&WS_MAXIMIZEBOX)!=0 && (style&WS_THICKFRAME)!=0;
}
void host_test_resize_client(int width,int height) {
    RECT area={0,0,width,height};
    AdjustWindowRect(&area,(DWORD)GetWindowLongPtrW(window,GWL_STYLE),FALSE);
    SetWindowPos(window,NULL,0,0,area.right-area.left,area.bottom-area.top,
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    pump(120);
}
void host_test_click(int x,int y) {
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(x,y));
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
    pump(50);
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y)); pump(50);
}
void host_test_quick_click(int x,int y) {
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(x,y));
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    pump(100);
}
void host_save_frame(const char *path) {
    lv_refr_now(NULL);
    FILE *out=fopen(path,"wb"); if(!out){perror(path);exit(2);}
    BITMAPFILEHEADER h={0};
    h.bfType=0x4d42; h.bfOffBits=sizeof(h)+sizeof(BITMAPINFOHEADER);
    h.bfSize=h.bfOffBits+sizeof(framebuffer);
    fwrite(&h,sizeof(h),1,out); fwrite(&bitmap.bmiHeader,sizeof(BITMAPINFOHEADER),1,out);
    fwrite(framebuffer,sizeof(framebuffer),1,out); fclose(out);
}
int ui_smoke_run(ScopeUI *ui);
int main(int argc,char **argv) {
    bool test=argc>1 && strcmp(argv[1],"--self-test")==0;
    bool snapshot=argc>1 && strcmp(argv[1],"--snapshot")==0;
    if(test) {
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0,_CALL_REPORTFAULT|_WRITE_ABORT_MSG);
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    }
    SetProcessDPIAware();
    bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth=SCOPE_WIDTH; bitmap.bmiHeader.biHeight=-SCOPE_HEIGHT;
    bitmap.bmiHeader.biPlanes=1; bitmap.bmiHeader.biBitCount=32; bitmap.bmiHeader.biCompression=BI_RGB;
    WNDCLASSW cls={0}; cls.lpfnWndProc=window_proc; cls.hInstance=GetModuleHandleW(NULL);
    cls.lpszClassName=L"SP2ScopeLVGL"; cls.hCursor=LoadCursor(NULL,IDC_ARROW);
    if(!RegisterClassW(&cls))return 2;
    DWORD style=WS_OVERLAPPEDWINDOW;
    RECT rect={0,0,SCOPE_WIDTH,SCOPE_HEIGHT}; AdjustWindowRect(&rect,style,FALSE);
    window=CreateWindowW(cls.lpszClassName,L"数字示波器 · 演示模式",style,
        CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,NULL,NULL,cls.hInstance,NULL);
    if(!window)return 2;
    lv_init();
    lv_display_t *display=lv_display_create(SCOPE_WIDTH,SCOPE_HEIGHT);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,drawbuffer,NULL,sizeof(drawbuffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer,input_read);
    scope_ui_create(&ui);
    if(test){int result=ui_smoke_run(&ui);DestroyWindow(window);lv_deinit();return result;}
    if(snapshot){pump(120);host_save_frame(argc>2?argv[2]:"artifacts/preview.bmp");DestroyWindow(window);lv_deinit();return 0;}
    ShowWindow(window,SW_SHOW); UpdateWindow(window);
    DWORD last=GetTickCount();
    while(!exiting) {
        DWORD now=GetTickCount(); unsigned dt=now-last;
        if(dt>=5){if(dt>100)dt=100; pump(dt);last=now;}
        Sleep(5);
    }
    lv_deinit();return 0;
}
