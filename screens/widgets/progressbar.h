uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 11) | (g << 5) | b;
}


#define fg_color RGB(0, 255, 0) 
#define bg_color RGB(20, 80, 10)


struct ProgressBardata
{
    int x, y, width, height;
    uint16_t fg_color, bg_color;
    void *data; // Pointer to ProgressBarData
    void (*draw)(Widget *self, uint16_t *fb);
        
};
