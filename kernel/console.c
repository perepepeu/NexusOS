#include <stdint.h>
#include <console.h>
#include <io.h>

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
static bool input_caret;
static uint64_t caret_x, caret_y;
static char caret_character;

static uint32_t channel(uint8_t value, uint8_t size, uint8_t shift) {
    return ((uint32_t)value * ((1u << size) - 1) / 255) << shift;
}

static uint32_t color(uint8_t red, uint8_t green, uint8_t blue) {
    return channel(red, screen->red_mask_size, screen->red_mask_shift)
        | channel(green, screen->green_mask_size, screen->green_mask_shift)
        | channel(blue, screen->blue_mask_size, screen->blue_mask_shift);
}

static void pixel(uint64_t x, uint64_t y, uint32_t value) {
    if (!screen || x >= screen->width || y >= screen->height) return;
    volatile uint32_t *row = (volatile uint32_t *)
        ((volatile uint8_t *)screen->address + y * screen->pitch);
    row[x] = value;
}

static void clear(void) {
    input_caret = false;
    for (uint64_t y = 0; y < screen->height; ++y) {
        for (uint64_t x = 0; x < screen->width; ++x) {
            pixel(x, y, background);
        }
    }
    cursor_x = MARGIN;
    cursor_y = MARGIN;
}

static void console_clear_unlocked(void) {
    if (screen) clear();
}

static bool valid_channel(uint8_t size, uint8_t shift) {
    return size > 0 && size <= 8 && shift + size <= 32;
}

static bool console_init_unlocked(struct limine_framebuffer_response *response) {
    screen = 0;
    if (!response || !response->framebuffers) {
        return false;
    }
    for (uint64_t i = 0; i < response->framebuffer_count; ++i) {
        struct limine_framebuffer *fb = response->framebuffers[i];
        if (!fb || !fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB
                || fb->bpp != 32 || fb->pitch % 4 != 0
                || fb->width > UINT64_MAX / 4 || fb->pitch < fb->width * 4
                || fb->height == 0 || fb->pitch > UINT64_MAX / fb->height
                || (uintptr_t)fb->address > UINTPTR_MAX - fb->pitch * fb->height
                || fb->width < 2 * MARGIN + CELL_WIDTH
                || fb->height < 2 * MARGIN + CELL_HEIGHT
                || !valid_channel(fb->red_mask_size, fb->red_mask_shift)
                || !valid_channel(fb->green_mask_size, fb->green_mask_shift)
                || !valid_channel(fb->blue_mask_size, fb->blue_mask_shift)) {
            continue;
        }
        uint32_t red = ((1u << fb->red_mask_size) - 1) << fb->red_mask_shift;
        uint32_t green = ((1u << fb->green_mask_size) - 1) << fb->green_mask_shift;
        uint32_t blue = ((1u << fb->blue_mask_size) - 1) << fb->blue_mask_shift;
        if ((red & green) || (red & blue) || (green & blue)) continue;
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
        case '>': return row < 4 ? (uint8_t)(1u << (4 - row)) : (uint8_t)(1u << (row - 2));
        case '<': return row < 4 ? (uint8_t)(1u << row) : (uint8_t)(1u << (6 - row));
        case '!': return row < 4 || row == 6 ? 4 : 0;
        case '?': { static const uint8_t question[7] = {14,17,1,2,4,0,4}; return question[row]; }
        case ',': return row == 5 ? 4 : row == 6 ? 8 : 0;
        case '\'': return row < 2 ? 4 : 0;
        case '"': return row < 2 ? 10 : 0;
        case '`': return row == 0 ? 8 : row == 1 ? 4 : 0;
        case '\\': return (uint8_t)(1u << (4 - row * 4 / 6));
        case '|': return 4;
        case '+': return row == 3 ? 31 : row > 0 && row < 6 ? 4 : 0;
        case '#': return row == 2 || row == 4 ? 31 : 10;
        case '^': return row == 0 ? 4 : row == 1 ? 10 : 0;
        case '~': return row == 2 ? 9 : row == 3 ? 22 : 0;
        case '*': return row == 2 || row == 4 ? 10 : row == 3 ? 4 : 0;
        case '{': return row == 0 || row == 6 ? 6 : row == 3 ? 8 : 4;
        case '}': return row == 0 || row == 6 ? 12 : row == 3 ? 2 : 4;
        case '$': { static const uint8_t dollar[7] = {4,15,20,14,5,30,4}; return dollar[row]; }
        case '@': { static const uint8_t at[7] = {14,17,23,21,23,16,14}; return at[row]; }
        case '&': { static const uint8_t amp[7] = {12,18,20,8,21,18,13}; return amp[row]; }
        default: {
            static const uint8_t unknown[7] = {14,17,1,2,4,0,4};
            return unknown[row];
        }
    }
}

static void newline(void) {
    cursor_x = MARGIN;
    cursor_y += CELL_HEIGHT;
    uint64_t bottom = MARGIN + ((screen->height - 2 * MARGIN) / CELL_HEIGHT) * CELL_HEIGHT;
    if (cursor_y + CELL_HEIGHT > bottom) {
        for (uint64_t y = MARGIN; y < bottom; ++y) {
            for (uint64_t x = MARGIN; x < screen->width - MARGIN; ++x) {
                uint32_t value = background;
                if (y + CELL_HEIGHT < bottom) {
                    volatile uint32_t *row = (volatile uint32_t *)
                        ((volatile uint8_t *)screen->address + (y + CELL_HEIGHT) * screen->pitch);
                    value = row[x];
                }
                pixel(x, y, value);
            }
        }
        cursor_y -= CELL_HEIGHT;
    }
}

static void cell(uint64_t x_position, uint64_t y_position, char c) {
    for (unsigned int y = 0; y < CELL_HEIGHT; ++y) {
        uint8_t bits = y < 7 * SCALE ? glyph_row(c, y / SCALE) : 0;
        for (unsigned int x = 0; x < CELL_WIDTH; ++x) {
            bool set = x < 5 * SCALE && (bits & (1u << (4 - x / SCALE)));
            pixel(x_position + x, y_position + y, set ? foreground : background);
        }
    }
}

static void console_input_unlocked(const char *prompt, const char *line, unsigned int cursor) {
    if (!screen) return;
    uint64_t columns = (screen->width - 2 * MARGIN) / CELL_WIDTH;
    uint64_t prefix = 0, length = 0;
    while (prompt[prefix] && prefix + 1 < columns) ++prefix;
    while (line[length]) ++length;
    if (cursor > length) cursor = length;
    uint64_t available = columns - prefix - 1;
    uint64_t offset = available && cursor >= available ? cursor - available + 1 : 0;
    for (uint64_t i = 0; i < columns; ++i) cell(MARGIN + i * CELL_WIDTH, cursor_y, ' ');
    for (uint64_t i = 0; i < prefix; ++i) cell(MARGIN + i * CELL_WIDTH, cursor_y, prompt[i]);
    for (uint64_t i = 0; i < available && offset + i < length; ++i)
        cell(MARGIN + (prefix + i) * CELL_WIDTH, cursor_y, line[offset + i]);
    uint64_t caret = prefix + (available ? cursor - offset : 0);
    caret_x = MARGIN + caret * CELL_WIDTH;
    caret_y = cursor_y;
    caret_character = available && cursor < length ? line[cursor] : ' ';
    input_caret = true;
    for (unsigned int y = 15; y < 17; ++y)
        for (unsigned int x = 0; x < 5 * SCALE; ++x)
            pixel(MARGIN + caret * CELL_WIDTH + x, cursor_y + y, foreground);
    cursor_x = MARGIN + caret * CELL_WIDTH;
}

static void console_write_n_unlocked(const char *text, size_t bytes) {
    if (!screen) return;
    if (input_caret) {
        cell(caret_x, caret_y, caret_character);
        input_caret = false;
    }
    while (bytes--) {
        char c = *text++;
        if (!c) continue; /* NUL e um byte transferido, sem glifo no framebuffer. */
        if (c == '\n') { newline(); continue; }
        if (c == '\r') { cursor_x = MARGIN; continue; }
        if (c == '\b') {
            if (cursor_x > MARGIN) cursor_x -= CELL_WIDTH;
            else if (cursor_y > MARGIN) {
                cursor_y -= CELL_HEIGHT;
                cursor_x = MARGIN + ((screen->width - 2 * MARGIN) / CELL_WIDTH - 1) * CELL_WIDTH;
            } else continue;
            for (unsigned int y = 0; y < CELL_HEIGHT; ++y)
                for (unsigned int x = 0; x < CELL_WIDTH; ++x)
                    pixel(cursor_x + x, cursor_y + y, background);
            continue;
        }
        if (cursor_x + CELL_WIDTH > screen->width - MARGIN) newline();
        cell(cursor_x, cursor_y, c);
        cursor_x += CELL_WIDTH;
    }
}
bool console_init(struct limine_framebuffer_response *response) {
    uint64_t flags = irq_save(); bool ok = console_init_unlocked(response); irq_restore(flags); return ok;
}
void console_clear(void) {
    uint64_t flags = irq_save(); console_clear_unlocked(); irq_restore(flags);
}
void console_input(const char *prompt, const char *line, unsigned int cursor) {
    uint64_t flags = irq_save(); console_input_unlocked(prompt, line, cursor); irq_restore(flags);
}
void console_write(const char *text) {
    size_t bytes = 0; while (text[bytes]) ++bytes;
    console_write_n(text, bytes);
}
void console_write_n(const char *text, size_t bytes) {
    uint64_t flags = irq_save(); console_write_n_unlocked(text, bytes); irq_restore(flags);
}
