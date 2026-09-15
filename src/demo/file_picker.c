#include "demo/file_picker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include "ui/source_view.h"

static int lower_ascii(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int compare_names(const char *a, const char *b)
{
    while (*a != '\0' && lower_ascii((unsigned char)*a) ==
                              lower_ascii((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return lower_ascii((unsigned char)*a) - lower_ascii((unsigned char)*b);
}

static int compare_entries(const void *a, const void *b)
{
    const struct miga80_file_entry *left = a, *right = b;
    if (left->directory != right->directory) {
        return right->directory - left->directory;
    }
    return compare_names(left->name, right->name);
}

void miga80_file_picker_init(struct miga80_file_picker *picker)
{
    (void)memset(picker, 0, sizeof(*picker));
    picker->selected = picker->click_index = -1;
    (void)strcpy(picker->path, "SYS:");
}

void miga80_file_picker_free(struct miga80_file_picker *picker)
{
    free(picker->entries);
    miga80_file_picker_init(picker);
}

int miga80_file_picker_scan(struct miga80_file_picker *picker, const char *path)
{
    struct FileInfoBlock *info = NULL;
    struct miga80_file_entry *entries = NULL;
    BPTR lock = (BPTR)0;
    int count = 0, capacity = 0, success = 0;
    const char *failure = "CANNOT READ DIRECTORY - REFRESH OR CANCEL";

    picker->click_index = -1;
    if (strlen(path) >= sizeof(picker->path)) {
        failure = "PATH TOO LONG";
        goto done;
    }
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock == (BPTR)0) { goto done; }
    info = AllocDosObject(DOS_FIB, NULL);
    if (info == NULL) { failure = "NOT ENOUGH MEMORY"; goto done; }
    if (!Examine(lock, info) || info->fib_DirEntryType <= 0) { goto done; }
    while (ExNext(lock, info)) {
        const size_t length = strlen((const char *)info->fib_FileName);
        struct miga80_file_entry *entry;
        if (info->fib_DirEntryType == 0 ||
            (info->fib_DirEntryType < 0 && (length < 4U ||
             compare_names((const char *)info->fib_FileName + length - 4U, ".lua") != 0))) {
            continue;
        }
        if (count == capacity) {
            struct miga80_file_entry *grown;
            /* Bound memory on a 2 MiB machine; never show a partial listing. */
            if (capacity == 4096) {
                failure = "TOO MANY ENTRIES (LIMIT 4096)";
                goto done;
            }
            capacity = capacity == 0 ? 16 : capacity * 2;
            grown = realloc(entries, (size_t)capacity * sizeof(*entries));
            if (grown == NULL) { failure = "NOT ENOUGH MEMORY"; goto done; }
            entries = grown;
        }
        entry = &entries[count++];
        (void)snprintf(entry->name, sizeof(entry->name), "%s", info->fib_FileName);
        entry->bytes = info->fib_Size;
        entry->directory = info->fib_DirEntryType > 0;
    }
    if (IoErr() != ERROR_NO_MORE_ENTRIES) { goto done; }
    if (count > 1) { qsort(entries, (size_t)count, sizeof(*entries), compare_entries); }
    /* Commit only a complete scan. The previous directory survives errors. */
    free(picker->entries);
    picker->entries = entries;
    entries = NULL;
    picker->count = count;
    picker->first = 0;
    picker->selected = -1;
    (void)memmove(picker->path, path, strlen(path) + 1U);
    (void)snprintf(picker->status, sizeof(picker->status),
                  "%d ENTRIES - DOUBLE CLICK TO OPEN - ESC CANCEL", count);
    success = 1;
done:
    free(entries);
    if (info != NULL) { FreeDosObject(DOS_FIB, info); }
    if (lock != (BPTR)0) { UnLock(lock); }
    if (!success) {
        (void)snprintf(picker->status, sizeof(picker->status), "%s", failure);
    }
    return success;
}

void miga80_file_picker_render(const struct miga80_file_picker *picker,
                              uint8_t *chunky)
{
    int row;
    char text[65];
    (void)memset(chunky, 0, 256U * 256U);
    miga80_source_view_draw_row(chunky, 256U, 0U,
        "MIGA-80 / OPEN LUA                                   CTRL-Q EXIT", 9U, 2U);
    (void)snprintf(text, sizeof(text), "PATH: %.58s", picker->path);
    miga80_source_view_draw_row(chunky, 256U, 2U, text, 8U, 0U);
    miga80_source_view_draw_row(chunky, 256U, 3U,
        "[ SYS: ][ PARENT ][ REFRESH ]  FOLDERS + *.LUA", 9U, 2U);
    for (row = 0; row < MIGA80_PICKER_PAGE_SIZE; ++row) {
        const int index = picker->first + row;
        const uint8_t background = index == picker->selected ? 14U : 0U;
        if (index >= picker->count) { break; }
        (void)snprintf(text, sizeof(text), "%c %-5s %-44.44s %8ld",
            index == picker->selected ? '>' : ' ',
            picker->entries[index].directory ? "[DIR]" : "LUA",
            picker->entries[index].name, (long)picker->entries[index].bytes);
        miga80_source_view_draw_row(chunky, 256U, (size_t)(5 + row * 2),
                                   text, 8U, background);
        miga80_source_view_draw_row(chunky, 256U, (size_t)(6 + row * 2),
                                   "", 8U, background);
    }
    if (picker->count == 0) {
        miga80_source_view_draw_row(chunky, 256U, 7U,
            "  NO LUA FILES OR FOLDERS HERE", 8U, 0U);
    }
    (void)snprintf(text, sizeof(text), "PAGE %d / %d   -   UP/DOWN SELECT, RETURN OPEN",
        picker->first / MIGA80_PICKER_PAGE_SIZE + 1,
        picker->count == 0 ? 1 : (picker->count - 1) / MIGA80_PICKER_PAGE_SIZE + 1);
    miga80_source_view_draw_row(chunky, 256U, 4U, text, 8U, 0U);
    miga80_source_view_draw_row(chunky, 256U, 29U,
        "[ PREV PAGE ]   [ NEXT PAGE ]   [ OPEN ]        [ CANCEL ]", 9U, 2U);
    miga80_source_view_draw_row(chunky, 256U, 31U, picker->status, 9U, 14U);
}

void miga80_file_picker_render_save_as(
    const struct miga80_file_picker *picker, const char *filename,
    uint8_t *chunky)
{
    int row;
    char text[65];

    (void)memset(chunky, 0, 256U * 256U);
    miga80_source_view_draw_row(chunky, 256U, 0U,
        "MIGA-80 / SAVE AS", 9U, 2U);
    (void)snprintf(text, sizeof(text), "NAME: %.30s_", filename);
    miga80_source_view_draw_row(chunky, 256U, 1U, text, 8U, 14U);
    (void)snprintf(text, sizeof(text), "PATH: %.58s", picker->path);
    miga80_source_view_draw_row(chunky, 256U, 2U, text, 8U, 0U);
    miga80_source_view_draw_row(chunky, 256U, 3U,
        "[ SYS: ][ PARENT ][ REFRESH ]  FOLDERS + *.LUA", 9U, 2U);
    (void)snprintf(text, sizeof(text),
        "PAGE %d / %d   TYPE NAME, RETURN SAVE",
        picker->first / MIGA80_PICKER_PAGE_SIZE + 1,
        picker->count == 0 ? 1 :
            (picker->count - 1) / MIGA80_PICKER_PAGE_SIZE + 1);
    miga80_source_view_draw_row(chunky, 256U, 4U, text, 8U, 0U);
    for (row = 0; row < MIGA80_PICKER_PAGE_SIZE; ++row) {
        const int index = picker->first + row;
        const uint8_t background = index == picker->selected ? 14U : 0U;
        if (index >= picker->count) { break; }
        (void)snprintf(text, sizeof(text), "%c %-5s %-44.44s %8ld",
            index == picker->selected ? '>' : ' ',
            picker->entries[index].directory ? "[DIR]" : "LUA",
            picker->entries[index].name, (long)picker->entries[index].bytes);
        miga80_source_view_draw_row(chunky, 256U, (size_t)(5 + row * 2),
                                    text, 8U, background);
        miga80_source_view_draw_row(chunky, 256U, (size_t)(6 + row * 2),
                                    "", 8U, background);
    }
    miga80_source_view_draw_row(chunky, 256U, 29U,
        "[ PREV PAGE ] [ NEXT PAGE ] [ SAVE ]       [ CANCEL ]", 9U, 2U);
    miga80_source_view_draw_row(chunky, 256U, 31U,
                                picker->status, 9U, 14U);
}

int miga80_file_picker_selected_path(struct miga80_file_picker *picker,
                                   char *path)
{
    if (picker->selected < 0 || picker->selected >= picker->count) { return 0; }
    (void)strcpy(path, picker->path);
    if (!AddPart(path, picker->entries[picker->selected].name,
                 MIGA80_PICKER_PATH_SIZE)) {
        (void)strcpy(picker->status, "PATH TOO LONG");
        return 0;
    }
    return 1;
}

static enum miga80_picker_action open_selected(struct miga80_file_picker *picker)
{
    char path[MIGA80_PICKER_PATH_SIZE];
    picker->click_index = -1;
    if (!miga80_file_picker_selected_path(picker, path)) { return MIGA80_PICKER_REDRAW; }
    if (!picker->entries[picker->selected].directory) { return MIGA80_PICKER_LOAD; }
    (void)miga80_file_picker_scan(picker, path);
    return MIGA80_PICKER_REDRAW;
}

static void parent_directory(struct miga80_file_picker *picker)
{
    char path[MIGA80_PICKER_PATH_SIZE];
    char *slash;
    (void)strcpy(path, picker->path);
    slash = strrchr(path, '/');
    if (slash == NULL) { slash = strchr(path, ':'); if (slash != NULL) { ++slash; } }
    if (slash != NULL) { *slash = '\0'; }
    (void)miga80_file_picker_scan(picker, path);
}

enum miga80_picker_action miga80_file_picker_mouse(
    struct miga80_file_picker *picker, int x, int y, ULONG seconds, ULONG micros)
{
    if (x < 0 || x >= 256 || y < 0 || y >= 256) {
        picker->click_index = -1;
        return MIGA80_PICKER_NONE;
    }
    if (y >= 40 && y < 232) {
        const int index = picker->first + (y - 40) / 16;
        const int double_click = picker->click_index == index &&
            DoubleClick(picker->click_seconds, picker->click_micros, seconds, micros);
        picker->click_index = -1;
        if (index >= picker->count) { return MIGA80_PICKER_NONE; }
        picker->selected = index;
        if (double_click) { return open_selected(picker); }
        picker->click_index = index;
        picker->click_seconds = seconds;
        picker->click_micros = micros;
        return MIGA80_PICKER_REDRAW;
    }
    picker->click_index = -1;
    if (y >= 24 && y < 32) {
        if (x < 32) { (void)miga80_file_picker_scan(picker, "SYS:"); }
        else if (x < 72) { parent_directory(picker); }
        else if (x < 116) { (void)miga80_file_picker_scan(picker, picker->path); }
        return MIGA80_PICKER_REDRAW;
    }
    if (y >= 232 && y < 240) {
        if (x < 60) {
            if (picker->first >= MIGA80_PICKER_PAGE_SIZE) {
                picker->first -= MIGA80_PICKER_PAGE_SIZE;
                picker->selected = -1;
            }
        } else if (x < 124) {
            if (picker->first + MIGA80_PICKER_PAGE_SIZE < picker->count) {
                picker->first += MIGA80_PICKER_PAGE_SIZE;
                picker->selected = -1;
            }
        } else if (x < 180) { return open_selected(picker); }
        else { return MIGA80_PICKER_CANCEL; }
        return MIGA80_PICKER_REDRAW;
    }
    return MIGA80_PICKER_NONE;
}

enum miga80_picker_action miga80_file_picker_key(
    struct miga80_file_picker *picker, UWORD code)
{
    if ((code & 0x80U) != 0U) { return MIGA80_PICKER_NONE; }
    picker->click_index = -1;
    if (code == 0x45U) { return MIGA80_PICKER_CANCEL; }
    if (code == 0x44U || code == 0x43U) { return open_selected(picker); }
    if (code == 0x41U) { parent_directory(picker); return MIGA80_PICKER_REDRAW; }
    if ((code == 0x4cU || code == 0x4dU) && picker->count != 0) {
        if (picker->selected < 0) { picker->selected = picker->first; }
        else if (code == 0x4cU && picker->selected > 0) { --picker->selected; }
        else if (code == 0x4dU && picker->selected + 1 < picker->count) { ++picker->selected; }
        picker->first = picker->selected / MIGA80_PICKER_PAGE_SIZE * MIGA80_PICKER_PAGE_SIZE;
        return MIGA80_PICKER_REDRAW;
    }
    return MIGA80_PICKER_NONE;
}
