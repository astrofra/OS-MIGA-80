#ifndef MIGA80_DEMO_FILE_PICKER_H
#define MIGA80_DEMO_FILE_PICKER_H

#include <stdint.h>
#include <exec/types.h>

#define MIGA80_PICKER_PATH_SIZE 512U
#define MIGA80_PICKER_PAGE_SIZE 12

struct miga80_file_entry {
    char name[108];
    LONG bytes;
    int directory;
};

struct miga80_file_picker {
    struct miga80_file_entry *entries;
    int count, selected, first, click_index;
    ULONG click_seconds, click_micros;
    char path[MIGA80_PICKER_PATH_SIZE];
    char status[65];
};

enum miga80_picker_action {
    MIGA80_PICKER_NONE, MIGA80_PICKER_REDRAW,
    MIGA80_PICKER_LOAD, MIGA80_PICKER_CANCEL
};

void miga80_file_picker_init(struct miga80_file_picker *picker);
void miga80_file_picker_free(struct miga80_file_picker *picker);
int miga80_file_picker_scan(struct miga80_file_picker *picker, const char *path);
void miga80_file_picker_render(const struct miga80_file_picker *picker,
                              uint8_t *chunky);
void miga80_file_picker_render_save_as(
    const struct miga80_file_picker *picker, const char *filename,
    uint8_t *chunky);
enum miga80_picker_action miga80_file_picker_mouse(
    struct miga80_file_picker *picker, int x, int y, ULONG seconds, ULONG micros);
enum miga80_picker_action miga80_file_picker_key(
    struct miga80_file_picker *picker, UWORD code);
int miga80_file_picker_selected_path(struct miga80_file_picker *picker,
                                   char *path);

#endif
