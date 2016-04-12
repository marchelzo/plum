#ifndef EDITOR_FUNCTIONS_H_INCLUDED
#define EDITOR_FUNCTIONS_H_INCLUDED

#include "value.h"

struct value
builtin_editor_insert(value_vector *args);

struct value
builtin_editor_forward(value_vector *args);

struct value
builtin_editor_backward(value_vector *args);

struct value
builtin_editor_remove(value_vector *args);

struct value
builtin_editor_line(value_vector *args);

struct value
builtin_editor_column(value_vector *args);

struct value
builtin_editor_lines(value_vector *args);

struct value
builtin_editor_grow_horizontally(value_vector *args);

struct value
builtin_editor_grow_vertically(value_vector *args);

struct value
builtin_editor_next_line(value_vector *args);

struct value
builtin_editor_prev_line(value_vector *args);

struct value
builtin_editor_scroll_down(value_vector *args);

struct value
builtin_editor_scroll_up(value_vector *args);

struct value
builtin_editor_move_right(value_vector *args);

struct value
builtin_editor_move_left(value_vector *args);

struct value
builtin_editor_prev_window(value_vector *args);

struct value
builtin_editor_next_window(value_vector *args);

struct value
builtin_editor_map_normal(value_vector *args);

struct value
builtin_editor_map_insert(value_vector *args);

struct value
builtin_editor_normal_mode(value_vector *args);

struct value
builtin_editor_insert_mode(value_vector *args);

struct value
builtin_editor_source(value_vector *args);

struct value
builtin_editor_start_of_line(value_vector *args);

struct value
builtin_editor_end_of_line(value_vector *args);

struct value
builtin_editor_cut_line(value_vector *args);

struct value
builtin_editor_goto_end(value_vector *args);

struct value
builtin_editor_goto_start(value_vector *args);

struct value
builtin_editor_get_char(value_vector *args);

struct value
builtin_editor_get_line(value_vector *args);

struct value
builtin_editor_save_excursion(value_vector *args);

struct value
builtin_editor_point(value_vector *args);

struct value
builtin_editor_log(value_vector *args);

struct value
builtin_editor_undo(value_vector *args);

struct value
builtin_editor_redo(value_vector *args);

struct value
builtin_editor_center_current_line(value_vector *args);

struct value
builtin_editor_next_match(value_vector *args);

struct value
builtin_editor_seek(value_vector *args);

struct value
builtin_editor_spawn(value_vector *args);

struct value
builtin_editor_proc_kill(value_vector *args);

struct value
builtin_editor_proc_close(value_vector *args);

struct value
builtin_editor_proc_write(value_vector *args);

struct value
builtin_editor_proc_write_line(value_vector *args);

struct value
builtin_editor_write_file(value_vector *args);

struct value
builtin_editor_file_name(value_vector *args);

struct value
builtin_editor_show_console(value_vector *args);

struct value
builtin_editor_horizontal_split(value_vector *args);

struct value
builtin_editor_vertical_split(value_vector *args);

struct value
builtin_editor_current_window(value_vector *args);

struct value
builtin_editor_delete_window(value_vector *args);

struct value
builtin_editor_on_message(value_vector *args);

struct value
builtin_editor_send_message(value_vector *args);

#endif
