#include <stdint.h>
#include <console.h>

/* Fonte 5x7 propria: cada linha usa os cinco bits inferiores. */
static const uint8_t letters[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
};
static const uint8_t digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
};

enum { MARGIN = 24, SCALE = 2, CELL_WIDTH = 12, CELL_HEIGHT = 18 };
static struct limine_framebuffer *screen;
static uint64_t cursor_x, cursor_y;
static uint32_t background, foreground;

static uint32_t channel(uint8_t value, uint8_t size, uint8_t shift) {
    return ((uint32_t)value * ((1u << size) - 1) / 255) << shift;
}

static uint32_t color(uint8_t red, uint8_t green, uint8_t blue) {
    return channel(red, screen->red_mask_size, screen->red_mask_shift)
        | channel(green, screen->green_mask_size, screen->green_mask_shift)
        | channel(blue, screen->blue_mask_size, screen->blue_mask_shift);
}

static void pixel(uint64_t x, uint64_t y, uint32_t value) {
    volatile uint32_t *row = (volatile uint32_t *)
        ((volatile uint8_t *)screen->address + y * screen->pitch);
    row[x] = value;
}

static void clear(void) {
    for (uint64_t y = 0; y < screen->height; ++y) {
        for (uint64_t x = 0; x < screen->width; ++x) {
            pixel(x, y, background);
        }
    }
    cursor_x = MARGIN;
    cursor_y = MARGIN;
}

void console_clear(void) {
    if (screen) clear();
}

static bool valid_channel(uint8_t size, uint8_t shift) {
    return size > 0 && size <= 8 && shift + size <= 32;
}

bool console_init(struct limine_framebuffer_response *response) {
    if (!response || !response->framebuffers) {
        return false;
    }
    for (uint64_t i = 0; i < response->framebuffer_count; ++i) {
        struct limine_framebuffer *fb = response->framebuffers[i];
        if (!fb || !fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB
                || fb->bpp != 32 || fb->pitch % 4 != 0
                || fb->width > UINT64_MAX / 4 || fb->pitch < fb->width * 4
                || fb->height == 0 || fb->pitch > UINT64_MAX / fb->height
                || fb->width < 2 * MARGIN + CELL_WIDTH
                || fb->height < 2 * MARGIN + CELL_HEIGHT
                || !valid_channel(fb->red_mask_size, fb->red_mask_shift)
                || !valid_channel(fb->green_mask_size, fb->green_mask_shift)
                || !valid_channel(fb->blue_mask_size, fb->blue_mask_shift)) {
            continue;
        }
        screen = fb;
        background = color(16, 24, 32);
        foreground = color(220, 235, 245);
        clear();
        return true;
    }
    return false;
}

static uint8_t glyph_row(char c, unsigned int row) {
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'][row];
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    switch (c) {
        case ' ': return 0;
        case '.': return row == 6 ? 4 : 0;
        case ':': return row == 2 || row == 5 ? 4 : 0;
        case '-': return row == 3 ? 14 : 0;
        case '_': return row == 6 ? 31 : 0;
        case '/': return (uint8_t)(1u << (row * 4 / 6));
        case ';': return row == 2 || row == 5 ? 4 : row == 6 ? 8 : 0;
        case '[': return row == 0 || row == 6 ? 14 : 8;
        case ']': return row == 0 || row == 6 ? 14 : 2;
        case '(': return row == 0 || row == 6 ? 2 : row == 1 || row == 5 ? 4 : 8;
        case ')': return row == 0 || row == 6 ? 8 : row == 1 || row == 5 ? 4 : 2;
        case '=': return row == 2 || row == 4 ? 31 : 0;
        case '%': return row == 0 || row == 1 ? 25 : row == 5 || row == 6 ? 19 : (uint8_t)(1u << (row - 1));
        default: {
            static const uint8_t unknown[7] = {14,17,1,2,4,0,4};
            return unknown[row];
        }
    }
}

static void newline(void) {
    cursor_x = MARGIN;
    cursor_y += CELL_HEIGHT;
    /* Reinicia a tela ao esgotar as linhas; scrolling vira uma etapa futura. */
    if (cursor_y + CELL_HEIGHT > screen->height - MARGIN) clear();
}

void console_write(const char *text) {
    if (!screen) return;
    while (*text) {
        char c = *text++;
        if (c == '\n') { newline(); continue; }
        if (c == '\r') { cursor_x = MARGIN; continue; }
        if (cursor_x + CELL_WIDTH > screen->width - MARGIN) newline();
        for (unsigned int y = 0; y < CELL_HEIGHT; ++y) {
            uint8_t bits = y < 7 * SCALE ? glyph_row(c, y / SCALE) : 0;
            for (unsigned int x = 0; x < CELL_WIDTH; ++x) {
                bool set = x < 5 * SCALE && (bits & (1u << (4 - x / SCALE)));
                pixel(cursor_x + x, cursor_y + y, set ? foreground : background);
            }
        }
        cursor_x += CELL_WIDTH;
    }
}
