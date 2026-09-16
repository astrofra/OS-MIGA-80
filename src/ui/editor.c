#include "ui/editor.h"

#include <string.h>

enum {
    EDITOR_RAWKEY_BACKSPACE = 0x41U,
    EDITOR_RAWKEY_TAB = 0x42U,
    EDITOR_RAWKEY_ENTER = 0x43U,
    EDITOR_RAWKEY_RETURN = 0x44U,
    EDITOR_RAWKEY_ESCAPE = 0x45U,
    EDITOR_RAWKEY_DELETE = 0x46U,
    EDITOR_RAWKEY_UP = 0x4cU,
    EDITOR_RAWKEY_DOWN = 0x4dU,
    EDITOR_RAWKEY_RIGHT = 0x4eU,
    EDITOR_RAWKEY_LEFT = 0x4fU,
    EDITOR_RAWKEY_F5 = 0x54U,
    EDITOR_RAWKEY_RELEASE = 0x80U,
    EDITOR_TAB_COLUMNS = 4U
};

static int valid_text(const char *text, size_t length)
{
    size_t index;

    if (text == NULL && length != 0U) {
        return 0;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)text[index];

        if (character != (unsigned char)'\n' &&
            character != (unsigned char)'\t' &&
            (character < 0x20U || character > 0x7eU)) {
            return 0;
        }
    }
    return 1;
}

int miga80_editor_init(struct Miga80EditorDocument *document,
                       char *text, size_t capacity, size_t length,
                       char *clipboard, size_t clipboard_capacity)
{
    if (document == NULL || text == NULL || clipboard == NULL ||
        length > capacity || !valid_text(text, length)) {
        return 0;
    }
    (void)memset(document, 0, sizeof(*document));
    document->text = text;
    document->capacity = capacity;
    document->length = length;
    document->anchor = MIGA80_EDITOR_NO_ANCHOR;
    document->clipboard = clipboard;
    document->clipboard_capacity = clipboard_capacity;
    text[length] = '\0';
    if (clipboard_capacity != 0U) {
        clipboard[0] = '\0';
    }
    return 1;
}

enum Miga80EditorStatus miga80_editor_set_text(
    struct Miga80EditorDocument *document, const char *text, size_t length)
{
    if (document == NULL || document->text == NULL ||
        (text == NULL && length != 0U)) {
        return MIGA80_EDITOR_INVALID_ARGUMENT;
    }
    if (length > document->capacity) {
        return MIGA80_EDITOR_CAPACITY;
    }
    if (!valid_text(text, length)) {
        return MIGA80_EDITOR_INVALID_CHARACTER;
    }
    if (length != 0U) {
        (void)memmove(document->text, text, length);
    }
    document->text[length] = '\0';
    document->length = length;
    document->cursor = 0U;
    document->anchor = MIGA80_EDITOR_NO_ANCHOR;
    document->preferred_column = 0U;
    document->preferred_column_valid = 0;
    document->first_visible_line = 0U;
    document->first_visible_column = 0U;
    document->dirty = 0;
    return MIGA80_EDITOR_OK;
}

enum Miga80EditorStatus miga80_editor_normalize_text(
    char *destination, size_t capacity, const char *source,
    size_t source_length, size_t *destination_length)
{
    size_t read_index, write_index = 0U;

    if (destination == NULL || source == NULL || destination_length == NULL) {
        return MIGA80_EDITOR_INVALID_ARGUMENT;
    }
    for (read_index = 0U; read_index < source_length; ++read_index) {
        unsigned char character = (unsigned char)source[read_index];

        if (character == (unsigned char)'\r') {
            if (read_index + 1U >= source_length ||
                source[read_index + 1U] != '\n') {
                return MIGA80_EDITOR_INVALID_CHARACTER;
            }
            ++read_index;
            character = (unsigned char)'\n';
        } else if (character != (unsigned char)'\n' &&
                   character != (unsigned char)'\t' &&
                   (character < 0x20U || character > 0x7eU)) {
            return MIGA80_EDITOR_INVALID_CHARACTER;
        }
        if (write_index >= capacity) {
            return MIGA80_EDITOR_CAPACITY;
        }
        destination[write_index++] = (char)character;
    }
    destination[write_index] = '\0';
    *destination_length = write_index;
    return MIGA80_EDITOR_OK;
}

int miga80_editor_has_selection(const struct Miga80EditorDocument *document)
{
    return document != NULL && document->anchor != MIGA80_EDITOR_NO_ANCHOR &&
           document->anchor != document->cursor;
}

void miga80_editor_selection(const struct Miga80EditorDocument *document,
                             size_t *start, size_t *end)
{
    size_t selection_start = 0U, selection_end = 0U;

    if (document != NULL && miga80_editor_has_selection(document)) {
        selection_start = document->anchor < document->cursor
                              ? document->anchor : document->cursor;
        selection_end = document->anchor > document->cursor
                            ? document->anchor : document->cursor;
    } else if (document != NULL) {
        selection_start = selection_end = document->cursor;
    }
    if (start != NULL) {
        *start = selection_start;
    }
    if (end != NULL) {
        *end = selection_end;
    }
}

static void erase_range(struct Miga80EditorDocument *document,
                        size_t start, size_t end)
{
    if (start >= end || end > document->length) {
        return;
    }
    (void)memmove(document->text + start, document->text + end,
                  document->length - end + 1U);
    document->length -= end - start;
    document->cursor = start;
    document->anchor = MIGA80_EDITOR_NO_ANCHOR;
    document->preferred_column_valid = 0;
    document->dirty = 1;
}

enum Miga80EditorStatus miga80_editor_insert(
    struct Miga80EditorDocument *document, const char *text, size_t length)
{
    size_t start, end, selected;

    if (document == NULL || document->text == NULL ||
        (text == NULL && length != 0U)) {
        return MIGA80_EDITOR_INVALID_ARGUMENT;
    }
    if (!valid_text(text, length)) {
        return MIGA80_EDITOR_INVALID_CHARACTER;
    }
    miga80_editor_selection(document, &start, &end);
    selected = end - start;
    if (length > document->capacity - (document->length - selected)) {
        return MIGA80_EDITOR_CAPACITY;
    }
    if (selected == 0U && length == 0U) {
        return MIGA80_EDITOR_OK;
    }
    (void)memmove(document->text + start + length, document->text + end,
                  document->length - end + 1U);
    if (length != 0U) {
        (void)memmove(document->text + start, text, length);
    }
    document->length = document->length - selected + length;
    document->cursor = start + length;
    document->anchor = MIGA80_EDITOR_NO_ANCHOR;
    document->preferred_column_valid = 0;
    document->dirty = 1;
    return MIGA80_EDITOR_OK;
}

void miga80_editor_backspace(struct Miga80EditorDocument *document)
{
    size_t start, end;

    if (document == NULL) {
        return;
    }
    miga80_editor_selection(document, &start, &end);
    if (start != end) {
        erase_range(document, start, end);
    } else if (document->cursor != 0U) {
        erase_range(document, document->cursor - 1U, document->cursor);
    }
}

void miga80_editor_delete(struct Miga80EditorDocument *document)
{
    size_t start, end;

    if (document == NULL) {
        return;
    }
    miga80_editor_selection(document, &start, &end);
    if (start != end) {
        erase_range(document, start, end);
    } else if (document->cursor < document->length) {
        erase_range(document, document->cursor, document->cursor + 1U);
    }
}

enum Miga80EditorStatus miga80_editor_copy(
    struct Miga80EditorDocument *document)
{
    size_t start, end, length;

    if (document == NULL || document->clipboard == NULL) {
        return MIGA80_EDITOR_INVALID_ARGUMENT;
    }
    if (!miga80_editor_has_selection(document)) {
        return MIGA80_EDITOR_OK;
    }
    miga80_editor_selection(document, &start, &end);
    length = end - start;
    if (length > document->clipboard_capacity) {
        return MIGA80_EDITOR_CLIPBOARD_CAPACITY;
    }
    (void)memmove(document->clipboard, document->text + start, length);
    document->clipboard[length] = '\0';
    document->clipboard_length = length;
    return MIGA80_EDITOR_OK;
}

enum Miga80EditorStatus miga80_editor_cut(
    struct Miga80EditorDocument *document)
{
    size_t start, end;
    enum Miga80EditorStatus status = miga80_editor_copy(document);

    if (status != MIGA80_EDITOR_OK ||
        !miga80_editor_has_selection(document)) {
        return status;
    }
    miga80_editor_selection(document, &start, &end);
    erase_range(document, start, end);
    return MIGA80_EDITOR_OK;
}

enum Miga80EditorStatus miga80_editor_paste(
    struct Miga80EditorDocument *document)
{
    if (document == NULL || document->clipboard == NULL) {
        return MIGA80_EDITOR_INVALID_ARGUMENT;
    }
    if (document->clipboard_length == 0U) {
        return MIGA80_EDITOR_OK;
    }
    return miga80_editor_insert(document, document->clipboard,
                                document->clipboard_length);
}

static size_t line_start(const struct Miga80EditorDocument *document,
                         size_t offset)
{
    while (offset != 0U && document->text[offset - 1U] != '\n') {
        --offset;
    }
    return offset;
}

static size_t line_end(const struct Miga80EditorDocument *document,
                       size_t offset)
{
    while (offset < document->length && document->text[offset] != '\n') {
        ++offset;
    }
    return offset;
}

static size_t visual_column(const struct Miga80EditorDocument *document,
                            size_t offset)
{
    size_t index = line_start(document, offset), column = 0U;

    while (index < offset) {
        if (document->text[index] == '\t') {
            column += EDITOR_TAB_COLUMNS - (column % EDITOR_TAB_COLUMNS);
        } else {
            ++column;
        }
        ++index;
    }
    return column;
}

static size_t offset_for_column(const struct Miga80EditorDocument *document,
                                size_t start, size_t wanted)
{
    const size_t end = line_end(document, start);
    size_t offset = start, column = 0U;

    while (offset < end) {
        size_t next = column + 1U;

        if (document->text[offset] == '\t') {
            next = column + EDITOR_TAB_COLUMNS -
                   (column % EDITOR_TAB_COLUMNS);
        }
        if (next > wanted) {
            break;
        }
        column = next;
        ++offset;
    }
    return offset;
}

static size_t vertical_move(struct Miga80EditorDocument *document,
                            enum Miga80EditorDirection direction)
{
    const size_t start = line_start(document, document->cursor);
    size_t target_start;

    if (!document->preferred_column_valid) {
        document->preferred_column = visual_column(document, document->cursor);
        document->preferred_column_valid = 1;
    }
    if (direction == MIGA80_EDITOR_UP) {
        if (start == 0U) {
            return document->cursor;
        }
        target_start = line_start(document, start - 1U);
    } else {
        const size_t end = line_end(document, document->cursor);

        if (end == document->length) {
            return document->cursor;
        }
        target_start = end + 1U;
    }
    return offset_for_column(document, target_start,
                             document->preferred_column);
}

void miga80_editor_move(struct Miga80EditorDocument *document,
                        enum Miga80EditorDirection direction, int selecting)
{
    size_t start, end, original, destination;

    if (document == NULL) {
        return;
    }
    original = document->cursor;
    if (!selecting && miga80_editor_has_selection(document) &&
        (direction == MIGA80_EDITOR_LEFT ||
         direction == MIGA80_EDITOR_RIGHT)) {
        miga80_editor_selection(document, &start, &end);
        document->cursor = direction == MIGA80_EDITOR_LEFT ? start : end;
        document->anchor = MIGA80_EDITOR_NO_ANCHOR;
        document->preferred_column_valid = 0;
        return;
    }
    if (selecting && document->anchor == MIGA80_EDITOR_NO_ANCHOR) {
        document->anchor = original;
    } else if (!selecting) {
        document->anchor = MIGA80_EDITOR_NO_ANCHOR;
    }
    destination = original;
    if (direction == MIGA80_EDITOR_LEFT && destination != 0U) {
        --destination;
        document->preferred_column_valid = 0;
    } else if (direction == MIGA80_EDITOR_RIGHT &&
               destination < document->length) {
        ++destination;
        document->preferred_column_valid = 0;
    } else if (direction == MIGA80_EDITOR_UP ||
               direction == MIGA80_EDITOR_DOWN) {
        destination = vertical_move(document, direction);
    }
    document->cursor = destination;
}

void miga80_editor_cursor_position(
    const struct Miga80EditorDocument *document, size_t *line, size_t *column)
{
    size_t offset, line_number = 0U;

    if (document == NULL) {
        if (line != NULL) { *line = 0U; }
        if (column != NULL) { *column = 0U; }
        return;
    }
    for (offset = 0U; offset < document->cursor; ++offset) {
        if (document->text[offset] == '\n') {
            ++line_number;
        }
    }
    if (line != NULL) {
        *line = line_number;
    }
    if (column != NULL) {
        *column = visual_column(document, document->cursor);
    }
}

void miga80_editor_ensure_cursor_visible(
    struct Miga80EditorDocument *document, size_t rows, size_t columns)
{
    size_t line, column;

    if (document == NULL || rows == 0U || columns == 0U) {
        return;
    }
    miga80_editor_cursor_position(document, &line, &column);
    if (line < document->first_visible_line) {
        document->first_visible_line = line;
    } else if (line >= document->first_visible_line + rows) {
        document->first_visible_line = line - rows + 1U;
    }
    if (column < document->first_visible_column) {
        document->first_visible_column = column;
    } else if (column >= document->first_visible_column + columns) {
        document->first_visible_column = column - columns + 1U;
    }
}

static int command_character(const char *translated, size_t length, int value)
{
    if (translated == NULL || length != 1U) {
        return 0;
    }
    return (unsigned char)translated[0] == (unsigned int)value ||
           translated[0] == (char)('a' + value - 1) ||
           translated[0] == (char)('A' + value - 1);
}

enum Miga80EditorCommand miga80_editor_decode_key(
    unsigned int raw_code, unsigned int qualifiers,
    const char *translated, size_t translated_length)
{
    enum Miga80EditorCommand command = MIGA80_EDITOR_COMMAND_NONE;

    if ((raw_code & EDITOR_RAWKEY_RELEASE) != 0U) {
        return MIGA80_EDITOR_COMMAND_NONE;
    }
    if ((qualifiers & MIGA80_EDITOR_KEY_CONTROL) != 0U &&
        (qualifiers & (MIGA80_EDITOR_KEY_ALT | MIGA80_EDITOR_KEY_COMMAND)) == 0U) {
        if (command_character(translated, translated_length, 3)) {
            command = MIGA80_EDITOR_COMMAND_COPY;
        } else if (command_character(translated, translated_length, 24)) {
            command = MIGA80_EDITOR_COMMAND_CUT;
        } else if (command_character(translated, translated_length, 22)) {
            command = MIGA80_EDITOR_COMMAND_PASTE;
        } else if (command_character(translated, translated_length, 15)) {
            command = MIGA80_EDITOR_COMMAND_OPEN;
        } else if (command_character(translated, translated_length, 19)) {
            command = (qualifiers & MIGA80_EDITOR_KEY_SHIFT) != 0U
                          ? MIGA80_EDITOR_COMMAND_SAVE_AS
                          : MIGA80_EDITOR_COMMAND_SAVE;
        } else if (command_character(translated, translated_length, 17)) {
            command = MIGA80_EDITOR_COMMAND_QUIT;
        }
    } else if (raw_code == EDITOR_RAWKEY_F5) {
        command = MIGA80_EDITOR_COMMAND_RUN;
    } else if (raw_code == EDITOR_RAWKEY_ESCAPE) {
        command = MIGA80_EDITOR_COMMAND_ESCAPE;
    } else if (raw_code == EDITOR_RAWKEY_BACKSPACE) {
        command = MIGA80_EDITOR_COMMAND_BACKSPACE;
    } else if (raw_code == EDITOR_RAWKEY_DELETE) {
        command = MIGA80_EDITOR_COMMAND_DELETE;
    } else if (raw_code == EDITOR_RAWKEY_LEFT) {
        command = MIGA80_EDITOR_COMMAND_LEFT;
    } else if (raw_code == EDITOR_RAWKEY_RIGHT) {
        command = MIGA80_EDITOR_COMMAND_RIGHT;
    } else if (raw_code == EDITOR_RAWKEY_UP) {
        command = MIGA80_EDITOR_COMMAND_UP;
    } else if (raw_code == EDITOR_RAWKEY_DOWN) {
        command = MIGA80_EDITOR_COMMAND_DOWN;
    } else if (raw_code == EDITOR_RAWKEY_RETURN ||
               raw_code == EDITOR_RAWKEY_ENTER) {
        command = MIGA80_EDITOR_COMMAND_ENTER;
    } else if (raw_code == EDITOR_RAWKEY_TAB) {
        command = MIGA80_EDITOR_COMMAND_TAB;
    } else if (translated != NULL && translated_length != 0U &&
               valid_text(translated, translated_length)) {
        command = MIGA80_EDITOR_COMMAND_TEXT;
    }
    if ((qualifiers & MIGA80_EDITOR_KEY_REPEAT) != 0U &&
        command != MIGA80_EDITOR_COMMAND_TEXT &&
        command != MIGA80_EDITOR_COMMAND_ENTER &&
        command != MIGA80_EDITOR_COMMAND_TAB &&
        command != MIGA80_EDITOR_COMMAND_BACKSPACE &&
        command != MIGA80_EDITOR_COMMAND_DELETE &&
        command != MIGA80_EDITOR_COMMAND_LEFT &&
        command != MIGA80_EDITOR_COMMAND_RIGHT &&
        command != MIGA80_EDITOR_COMMAND_UP &&
        command != MIGA80_EDITOR_COMMAND_DOWN) {
        return MIGA80_EDITOR_COMMAND_NONE;
    }
    return command;
}
