#include <stdio.h>
#include <string.h>

#include "ui/editor.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "editor check failed at line %d: %s\n", \
                __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static int set_document(struct Miga80EditorDocument *document,
                        const char *text)
{
    return miga80_editor_set_text(document, text, strlen(text)) ==
           MIGA80_EDITOR_OK;
}

int main(void)
{
    char text[65] = "abc\ndef";
    char clipboard[65];
    char normalized[65];
    struct Miga80EditorDocument document;
    size_t length, start, end, line, column;
    const char ctrl_c = 3, ctrl_o = 15, ctrl_q = 17, ctrl_s = 19;

    CHECK(miga80_editor_init(&document, text, 64U, strlen(text),
                             clipboard, 64U));
    CHECK(!document.dirty && document.length == 7U &&
          document.anchor == MIGA80_EDITOR_NO_ANCHOR);

    CHECK(miga80_editor_normalize_text(normalized, 64U, "a\r\nb\n\t", 6U,
                                       &length) == MIGA80_EDITOR_OK);
    CHECK(length == 5U && memcmp(normalized, "a\nb\n\t", 6U) == 0);
    CHECK(miga80_editor_normalize_text(normalized, 64U, "a\rb", 3U,
                                       &length) ==
          MIGA80_EDITOR_INVALID_CHARACTER);
    CHECK(miga80_editor_normalize_text(normalized, 2U, "abc", 3U,
                                       &length) == MIGA80_EDITOR_CAPACITY);

    CHECK(set_document(&document, "abc\ndef"));
    CHECK(miga80_editor_insert(&document, "X", 1U) == MIGA80_EDITOR_OK);
    CHECK(strcmp(document.text, "Xabc\ndef") == 0 && document.cursor == 1U &&
          document.dirty);
    miga80_editor_backspace(&document);
    CHECK(strcmp(document.text, "abc\ndef") == 0 && document.cursor == 0U);
    miga80_editor_delete(&document);
    CHECK(strcmp(document.text, "bc\ndef") == 0);
    CHECK(set_document(&document, "a\nb"));
    document.cursor = 2U;
    miga80_editor_backspace(&document);
    CHECK(strcmp(document.text, "ab") == 0 && document.cursor == 1U);
    CHECK(set_document(&document, "a\nb"));
    document.cursor = 1U;
    miga80_editor_delete(&document);
    CHECK(strcmp(document.text, "ab") == 0 && document.cursor == 1U);

    CHECK(set_document(&document, "one\ntwo\nthree"));
    miga80_editor_move(&document, MIGA80_EDITOR_RIGHT, 1);
    miga80_editor_move(&document, MIGA80_EDITOR_RIGHT, 1);
    miga80_editor_move(&document, MIGA80_EDITOR_RIGHT, 1);
    CHECK(miga80_editor_has_selection(&document));
    miga80_editor_selection(&document, &start, &end);
    CHECK(start == 0U && end == 3U);
    CHECK(miga80_editor_copy(&document) == MIGA80_EDITOR_OK &&
          strcmp(document.clipboard, "one") == 0);
    CHECK(miga80_editor_cut(&document) == MIGA80_EDITOR_OK &&
          strcmp(document.text, "\ntwo\nthree") == 0 &&
          strcmp(document.clipboard, "one") == 0);
    CHECK(miga80_editor_paste(&document) == MIGA80_EDITOR_OK &&
          strcmp(document.text, "one\ntwo\nthree") == 0);

    document.cursor = 5U;
    document.anchor = MIGA80_EDITOR_NO_ANCHOR;
    miga80_editor_move(&document, MIGA80_EDITOR_LEFT, 1);
    miga80_editor_move(&document, MIGA80_EDITOR_LEFT, 1);
    miga80_editor_move(&document, MIGA80_EDITOR_RIGHT, 1);
    miga80_editor_selection(&document, &start, &end);
    CHECK(document.anchor == 5U && document.cursor == 4U &&
          start == 4U && end == 5U);
    miga80_editor_move(&document, MIGA80_EDITOR_RIGHT, 0);
    CHECK(document.cursor == 5U && !miga80_editor_has_selection(&document));

    CHECK(set_document(&document, "12345\nx\n12345"));
    document.cursor = 5U;
    miga80_editor_move(&document, MIGA80_EDITOR_DOWN, 0);
    CHECK(document.cursor == 7U);
    miga80_editor_move(&document, MIGA80_EDITOR_DOWN, 0);
    CHECK(document.cursor == document.length);
    miga80_editor_move(&document, MIGA80_EDITOR_UP, 0);
    CHECK(document.cursor == 7U);

    CHECK(set_document(&document, "\tX\n\t0123456789\nlast"));
    document.cursor = 1U;
    miga80_editor_cursor_position(&document, &line, &column);
    CHECK(line == 0U && column == 4U);
    miga80_editor_move(&document, MIGA80_EDITOR_DOWN, 0);
    miga80_editor_cursor_position(&document, &line, &column);
    CHECK(line == 1U && column == 4U);
    document.cursor = document.length;
    miga80_editor_ensure_cursor_visible(&document, 2U, 4U);
    CHECK(document.first_visible_line == 1U &&
          document.first_visible_column == 1U);

    CHECK(set_document(&document, "0123456789012345678901234567890123456789012345678901234567890123"));
    document.cursor = document.length;
    document.anchor = 0U;
    (void)strcpy(document.clipboard, "zz");
    document.clipboard_length = 2U;
    CHECK(miga80_editor_paste(&document) == MIGA80_EDITOR_OK &&
          strcmp(document.text, "zz") == 0);
    CHECK(set_document(&document, "0123456789012345678901234567890123456789012345678901234567890123"));
    document.cursor = document.length;
    CHECK(miga80_editor_insert(&document, "x", 1U) ==
          MIGA80_EDITOR_CAPACITY);
    CHECK(document.length == 64U && document.cursor == 64U &&
          !document.dirty);
    document.anchor = 63U;
    (void)strcpy(document.clipboard, "zz");
    document.clipboard_length = 2U;
    CHECK(miga80_editor_paste(&document) == MIGA80_EDITOR_CAPACITY);
    CHECK(document.length == 64U && document.cursor == 64U &&
          document.anchor == 63U && !document.dirty &&
          strcmp(document.clipboard, "zz") == 0);

    CHECK(miga80_editor_decode_key(0U, MIGA80_EDITOR_KEY_CONTROL,
                                   &ctrl_c, 1U) ==
          MIGA80_EDITOR_COMMAND_COPY);
    CHECK(miga80_editor_decode_key(0U, MIGA80_EDITOR_KEY_CONTROL,
                                   &ctrl_q, 1U) ==
          MIGA80_EDITOR_COMMAND_QUIT);
    CHECK(miga80_editor_decode_key(0U, MIGA80_EDITOR_KEY_CONTROL,
                                   &ctrl_o, 1U) ==
          MIGA80_EDITOR_COMMAND_OPEN);
    CHECK(miga80_editor_decode_key(0U,
          MIGA80_EDITOR_KEY_CONTROL | MIGA80_EDITOR_KEY_SHIFT,
          &ctrl_s, 1U) == MIGA80_EDITOR_COMMAND_SAVE_AS);
    CHECK(miga80_editor_decode_key(0x4fU, MIGA80_EDITOR_KEY_REPEAT,
                                   NULL, 0U) ==
          MIGA80_EDITOR_COMMAND_LEFT);
    CHECK(miga80_editor_decode_key(0x44U, MIGA80_EDITOR_KEY_REPEAT,
                                   NULL, 0U) ==
          MIGA80_EDITOR_COMMAND_ENTER);
    CHECK(miga80_editor_decode_key(0x51U, 0U,
                                   NULL, 0U) ==
          MIGA80_EDITOR_COMMAND_NONE);
    CHECK(miga80_editor_decode_key(0U,
                                   MIGA80_EDITOR_KEY_CONTROL |
                                       MIGA80_EDITOR_KEY_REPEAT,
                                   &ctrl_o, 1U) ==
          MIGA80_EDITOR_COMMAND_NONE);
    CHECK(miga80_editor_decode_key(0x80U, 0U, "x", 1U) ==
          MIGA80_EDITOR_COMMAND_NONE);
    CHECK(miga80_editor_decode_key(0U, 0U, "!=", 2U) ==
          MIGA80_EDITOR_COMMAND_TEXT);

    puts("document_edits=pass");
    puts("selection_clipboard=pass");
    puts("cursor_scrolling=pass");
    puts("normalization_capacity=pass");
    puts("keyboard_dispatch=pass");
    puts("result=pass");
    return 0;
}
