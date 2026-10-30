#ifndef WIN32_HOST_H
#define WIN32_HOST_H
void host_test_pump(unsigned ms);
void host_test_click(int x,int y);
void host_test_quick_click(int x,int y);
int host_test_can_maximize(void);
void host_test_resize_client(int width,int height);
void host_save_frame(const char *path);
#endif
