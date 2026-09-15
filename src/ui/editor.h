#ifndef MIGA80_UI_EDITOR_H
#define MIGA80_UI_EDITOR_H

#include <stddef.h>

#define MIGA80_EDITOR_NO_ANCHOR ((size_t)-1)

enum Miga80EditorStatus {
    MIGA80_EDITOR_OK = 0,
    MIGA80_EDITOR_INVALID_ARGUMENT,
    MIGA80_EDITOR_INVALID_CHARACTER,
    MIGA80_EDITOR_CAPACITY,
    MIGA80_EDITOR_CLIPBOARD_CAPACITY
};

enum Miga80EditorDirection {
    MIGA80_EDITOR_LEFT = 0,
    MIGA80_EDITOR_RIGHT,
    MIGA80_EDITOR_UP,
    MIGA80_EDITOR_DOWN
};

struct Miga80EditorDocument {
    char *text;
    size_t capacity;
    size_t length;
    size_t cursor;
    size_t anchor;
    size_t preferred_column;
    int preferred_column_valid;
    size_t first_visible_line;
    size_t first_visible_column;
    char *clipboard;
    size_t clipboard_capacity;
    size_t clipboard_length;
    int dirty;
};

enum Miga80EditorCommand {
    MIGA80_EDITOR_COMMAND_NONE = 0,
    MIGA80_EDITOR_COMMAND_TEXT,
    MIGA80_EDITOR_COMMAND_ENTER,
    MIGA80_EDITOR_COMMAND_TAB,
    MIGA80_EDITOR_COMMAND_BACKSPACE,
    MIGA80_EDITOR_COMMAND_DELETE,
    MIGA80_EDITOR_COMMAND_LEFT,
    MIGA80_EDITOR_COMMAND_RIGHT,
    MIGA80_EDITOR_COMMAND_UP,
    MIGA80_EDITOR_COMMAND_DOWN,
    MIGA80_EDITOR_COMMAND_COPY,
    MIGA80_EDITOR_COMMAND_CUT,
    MIGA80_EDITOR_COMMAND_PASTE,
    MIGA80_EDITOR_COMMAND_OPEN,
    MIGA80_EDITOR_COMMAND_SAVE,
    MIGA80_EDITOR_COMMAND_SAVE_AS,
    MIGA80_EDITOR_COMMAND_RUN,
    MIGA80_EDITOR_COMMAND_ESCAPE,
    MIGA80_EDITOR_COMMAND_QUIT
};

enum {
    MIGA80_EDITOR_KEY_SHIFT = 1U << 0,
    MIGA80_EDITOR_KEY_CONTROL = 1U << 1,
    MIGA80_EDITOR_KEY_REPEAT = 1U << 2,
    MIGA80_EDITOR_KEY_ALT = 1U << 3,
    MIGA80_EDITOR_KEY_COMMAND = 1U << 4
};

int miga80_editor_init(struct Miga80EditorDocument *document,
                       char *text, size_t capacity, size_t length,
                       char *clipboard, size_t clipboard_capacity);
enum Miga80EditorStatus miga80_editor_set_text(
    struct Miga80EditorDocument *document, const char *text, size_t length);
enum Miga80EditorStatus miga80_editor_normalize_text(
    char *destination, size_t capacity, const char *source,
    size_t source_length, size_t *destination_length);

int miga80_editor_has_selection(const struct Miga80EditorDocument *document);
void miga80_editor_selection(const struct Miga80EditorDocument *document,
                             size_t *start, size_t *end);
enum Miga80EditorStatus miga80_editor_insert(
    struct Miga80EditorDocument *document, const char *text, size_t length);
void miga80_editor_backspace(struct Miga80EditorDocument *document);
void miga80_editor_delete(struct Miga80EditorDocument *document);
enum Miga80EditorStatus miga80_editor_copy(
    struct Miga80EditorDocument *document);
enum Miga80EditorStatus miga80_editor_cut(
    struct Miga80EditorDocument *document);
enum Miga80EditorStatus miga80_editor_paste(
    struct Miga80EditorDocument *document);
void miga80_editor_move(struct Miga80EditorDocument *document,
                        enum Miga80EditorDirection direction, int selecting);
void miga80_editor_ensure_cursor_visible(
    struct Miga80EditorDocument *document, size_t rows, size_t columns);
void miga80_editor_cursor_position(
    const struct Miga80EditorDocument *document, size_t *line, size_t *column);

enum Miga80EditorCommand miga80_editor_decode_key(
    unsigned int raw_code, unsigned int qualifiers,
    const char *translated, size_t translated_length);

#endif
