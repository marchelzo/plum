#include <limits.h>
#include <string.h>

#include "vm.h"
#include "value.h"
#include "buffer.h"

#define ASSERT_ARGC(func, argc) \
        if (args->count != (argc)) { \
                vm_panic(func " expects " #argc " argument(s) but got %zu", args->count); \
        }

struct value
builtin_editor_insert(value_vector *args)
{
        ASSERT_ARGC("buffer::insert()", 1);

        struct value text = args->items[0];

        if (text.type != VALUE_STRING) {
                vm_panic("non-string passed as argument to buffer::insert()");
        }

        buffer_insert_n(text.string, text.bytes);

        return NIL;
}

struct value
builtin_editor_forward(value_vector *args)
{
        ASSERT_ARGC("buffer::forward()", 1);

        struct value distance = args->items[0];
        if (distance.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::forward()");
        }

        if (distance.integer < 0) {
                vm_panic("negative distance passed to buffer::forward()");
        }

        return INTEGER(buffer_forward(distance.integer));
}

struct value
builtin_editor_backward(value_vector *args)
{
        ASSERT_ARGC("buffer::backward()", 1);

        struct value distance = args->items[0];
        if (distance.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::backward()");
        }

        if (distance.integer < 0) {
                vm_panic("negative distance passed to buffer::backward()");
        }

        return INTEGER(buffer_backward(distance.integer));
}

struct value
builtin_editor_remove(value_vector *args)
{
        ASSERT_ARGC("buffer::remove()", 1);

        struct value amount = args->items[0];

        if (amount.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::remove()");
        }

        if (amount.integer < 0) {
                vm_panic("negative amount passed to buffer::remove()");
        }

        return INTEGER(buffer_remove(amount.integer));
}

struct value
builtin_editor_line(value_vector *args)
{
        ASSERT_ARGC("buffer::line()", 0);
        return INTEGER(buffer_line());
}

struct value
builtin_editor_column(value_vector *args)
{
        ASSERT_ARGC("buffer::column()", 0);
        return INTEGER(buffer_column());
}

struct value
builtin_editor_lines(value_vector *args)
{
        ASSERT_ARGC("buffer::lines()", 0);
        return INTEGER(buffer_lines());
}

struct value
builtin_editor_grow_vertically(value_vector *args)
{
        ASSERT_ARGC("buffer::growVertically()", 1);

        struct value amount = args->items[0];

        if (amount.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::growVertically()");
        }

        buffer_grow_y(amount.integer);

        return NIL;
}

struct value
builtin_editor_grow_horizontally(value_vector *args)
{
        ASSERT_ARGC("buffer::growHorizontally()", 1);

        struct value amount = args->items[0];

        if (amount.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::growHorizontally()");
        }

        buffer_grow_x(amount.integer);

        return NIL;
}

struct value
builtin_editor_next_line(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::nextLine() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_next_line(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::nextLine()");
                }

                return INTEGER(buffer_next_line(amount.integer));
        }
}

struct value
builtin_editor_prev_line(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::prevLine() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_prev_line(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::prevLine()");
                }

                return INTEGER(buffer_prev_line(amount.integer));
        }
}

struct value
builtin_editor_scroll_up(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::scrollUp() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_scroll_up(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::scrollUp()");
                }

                return INTEGER(buffer_scroll_up(amount.integer));
        }
}

struct value
builtin_editor_scroll_down(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::scrollDown() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_scroll_down(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::scrollDown()");
                }

                return INTEGER(buffer_scroll_down(amount.integer));
        }
}

struct value
builtin_editor_move_right(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::moveRight() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_right(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::moveRight()");
                }

                return INTEGER(buffer_right(amount.integer));
        }
}

struct value
builtin_editor_move_left(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::moveLeft() expects 0 or 1 arguments but got %zu", args->count);
        }

        if (args->count == 0) {
                return INTEGER(buffer_left(1));
        } else {
                struct value amount = args->items[0];

                if (amount.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::moveLeft()");
                }

                return INTEGER(buffer_left(amount.integer));
        }
}

struct value
builtin_editor_prev_window(value_vector *args)
{
        ASSERT_ARGC("editor::prevWindow()", 0);
        buffer_prev_window();
        return NIL;
}

struct value
builtin_editor_next_window(value_vector *args)
{
        ASSERT_ARGC("editor::nextWindow()", 0);
        buffer_next_window();
        return NIL;
}

struct value
builtin_editor_map_normal(value_vector *args)
{
        ASSERT_ARGC("buffer::mapNormal()", 2);

        struct value chord = args->items[0];
        struct value action = args->items[1];

        if (action.type != VALUE_FUNCTION && action.type != VALUE_BUILTIN_FUNCTION) {
                vm_panic("the second argument to buffer::mapNormal() must be a function");
        }

        if (chord.type != VALUE_ARRAY || chord.array->count == 0) {
                vm_panic("the first argument to buffer::mapNormal() must be a non-empty array of strings");
        }

        for (int i = 0; i < chord.array->count; ++i) {
                if (chord.array->items[i].type != VALUE_STRING) {
                        vm_panic("non-string in the first argument to buffer::mapNormal()");
                }
        }

        buffer_map_normal(chord.array, action);

        return NIL;
}

struct value
builtin_editor_map_insert(value_vector *args)
{
        ASSERT_ARGC("buffer::mapInsert()", 2);

        struct value chord = args->items[0];
        struct value action = args->items[1];

        if (action.type != VALUE_FUNCTION && action.type != VALUE_BUILTIN_FUNCTION) {
                vm_panic("the second argument to buffer::mapInsert() must be a function");
        }

        if (chord.type != VALUE_ARRAY || chord.array->count == 0) {
                vm_panic("the first argument to buffer::mapInsert() must be a non-empty array of strings");
        }

        for (int i = 0; i < chord.array->count; ++i) {
                if (chord.array->items[i].type != VALUE_STRING) {
                        vm_panic("non-string in the first argument to buffer::mapInsert()");
                }
        }

        buffer_map_insert(chord.array, action);

        return NIL;
}

struct value
builtin_editor_source(value_vector *args)
{
        ASSERT_ARGC("buffer::source()", 1);

        struct value path = args->items[0];
        if (path.type != VALUE_STRING) {
                vm_panic("non-string passed to buffer::source()");
        }

        buffer_source_file(path.string, path.bytes);

        return NIL;
}

struct value
builtin_editor_insert_mode(value_vector *args)
{
        ASSERT_ARGC("buffer::insertMode()", 0);
        buffer_insert_mode();
        return NIL;
}

struct value
builtin_editor_normal_mode(value_vector *args)
{
        ASSERT_ARGC("buffer::normalMode()", 0);
        buffer_normal_mode();
        return NIL;
}

struct value
builtin_editor_start_of_line(value_vector *args)
{
        ASSERT_ARGC("buffer::startOfLine()", 0);
        buffer_start_of_line();
        return NIL;
}

struct value
builtin_editor_end_of_line(value_vector *args)
{
        ASSERT_ARGC("buffer::endOfLine()", 0);
        buffer_end_of_line();
        return NIL;
}

struct value
builtin_editor_cut_line(value_vector *args)
{
        ASSERT_ARGC("buffer::cutLine()", 0);
        buffer_cut_line();
        return NIL;
}

struct value
builtin_editor_goto_start(value_vector *args)
{
        ASSERT_ARGC("buffer::gotoStart()", 0);
        buffer_start();
        return NIL;
}

struct value
builtin_editor_goto_end(value_vector *args)
{
        ASSERT_ARGC("buffer::gotoStart()", 0);
        buffer_end();
        return NIL;
}

struct value
builtin_editor_get_char(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::getChar() expects 0 or 1 arguments but got %zu", args->count);
        }

        int i;
        if (args->count == 0) {
                i = -1; // bad
        } else {
                struct value v = args->items[0];
                if (v.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::getChar()");
                }
                if (v.integer < 0) {
                        return NIL;
                }
                i = v.integer;
        }

        return buffer_get_char(i);
}

struct value
builtin_editor_get_line(value_vector *args)
{
        if (args->count != 0 && args->count != 1) {
                vm_panic("buffer::getLine() expects 0 or 1 arguments but got %zu", args->count);
        }

        int i;
        if (args->count == 0) {
                i = -1; // bad
        } else {
                struct value v = args->items[0];
                if (v.type != VALUE_INTEGER) {
                        vm_panic("non-integer passed to buffer::getLine()");
                }
                if (v.integer < 0) {
                        return NIL;
                }
                i = v.integer;
        }

        return buffer_get_line(i);
}

struct value
builtin_editor_save_excursion(value_vector *args)
{
        ASSERT_ARGC("buffer::saveExcursion()", 1);
        
        struct value f = args->items[0];

        if (f.type != VALUE_FUNCTION && f.type != VALUE_BUILTIN_FUNCTION) {
                vm_panic("non-function passed to buffer::saveExcursion()");
        }

        return buffer_save_excursion(&f);
}

struct value
builtin_editor_point(value_vector *args)
{
        ASSERT_ARGC("buffer::point()", 0);
        return INTEGER(buffer_point());
}

struct value
builtin_editor_log(value_vector *args)
{
        if (args->count == 0) {
                vm_panic("editor::log() expects at least 1 arugment");
        }

        for (int i = 0; i < args->count; ++i) {
                char *s = value_show(&args->items[i]);
                buffer_log(s);
                free(s);
        }

        return NIL;
}

struct value
builtin_editor_undo(value_vector *args)
{
        ASSERT_ARGC("buffer::undo()", 0);
        return BOOLEAN(buffer_undo());
}

struct value
builtin_editor_redo(value_vector *args)
{
        ASSERT_ARGC("buffer::redo()", 0);
        return BOOLEAN(buffer_redo());
}

struct value
builtin_editor_center_current_line(value_vector *args)
{
        ASSERT_ARGC("buffer::centerCurrentLine()", 0);
        buffer_center_current_line();
        return NIL;
}

struct value
builtin_editor_next_match(value_vector *args)
{
        ASSERT_ARGC("buffer::findNext()", 1);

        struct value pattern = args->items[0];

        if (pattern.type != VALUE_REGEX) {
                vm_panic("non-regex passed to buffer::findNext()");
        }

        return BOOLEAN(buffer_next_match(pattern.regex, pattern.extra));
}

struct value
builtin_editor_seek(value_vector *args)
{
        ASSERT_ARGC("buffer::seek()", 1);

        struct value position = args->items[0];

        if (position.type != VALUE_INTEGER) {
                vm_panic("non-integer passed to buffer::seek()");
        }

        if (position.integer < 0) {
                vm_panic("negative integer passed to buffer::seek()");
        }

        return INTEGER(buffer_seek(position.integer));
}

struct value
builtin_editor_spawn(value_vector *args)
{
        ASSERT_ARGC("buffer::spawn()", 4);

        struct value path = args->items[0];
        struct value argv = args->items[1];
        struct value on_out = args->items[2];
        struct value on_exit = args->items[3];

        if (path.type != VALUE_STRING)
                vm_panic("non-string passed as the first argument to buffer::spawn()");

        if (argv.type != VALUE_ARRAY)
                vm_panic("non-array passed as the second argument to buffer::spawn()");

        for (int i = 0; i < argv.array->count; ++i)
                if (argv.array->items[i].type != VALUE_STRING)
                        vm_panic("args array passed to buffer::spawn() contains a non-string at index %d", i);

        if (on_out.type != VALUE_FUNCTION && on_out.type != VALUE_BUILTIN_FUNCTION && on_out.type != VALUE_NIL)
                vm_panic("the output handler (3rd argument) to buffer::spawn() must be either nil or a function");

        if (on_exit.type != VALUE_FUNCTION && on_exit.type != VALUE_BUILTIN_FUNCTION && on_exit.type != VALUE_NIL)
                vm_panic("the exit handler (4th argument) to buffer::spawn() must be either nil or a function");

        /*
         * This is freed in buffer_spawn if the spawn fails, or by sp_on_exit
         * when the process exits if the spawn succeeds.
         */
        char *pathstr = alloc(path.bytes + 1);
        memcpy(pathstr, path.string, path.bytes);
        pathstr[path.bytes] = '\0';

        int fd = buffer_spawn(pathstr, argv.array, on_out, on_exit);
        if (fd == -1)
                return NIL;
        else
                return INTEGER(fd);
}

struct value
builtin_editor_proc_kill(value_vector *args)
{
        ASSERT_ARGC("proc::kill()", 1);
        
        struct value proc = args->items[0];
        
        if (proc.type != VALUE_INTEGER)
                vm_panic("non-integer passed to proc::kill()");

        return BOOLEAN(buffer_kill_subprocess(proc.integer));
}

struct value
builtin_editor_proc_close(value_vector *args)
{
        ASSERT_ARGC("proc::close()", 1);
        
        struct value proc = args->items[0];
        
        if (proc.type != VALUE_INTEGER)
                vm_panic("non-integer passed to proc::close()");

        return BOOLEAN(buffer_close_subprocess(proc.integer));
}

struct value
builtin_editor_proc_write(value_vector *args)
{
        ASSERT_ARGC("proc::write()", 2);

        struct value proc = args->items[0];
        struct value data = args->items[1];

        if (proc.type != VALUE_INTEGER)
                vm_panic("non-integer passed as first argument to proc::write()");

        if (data.type != VALUE_STRING)
                vm_panic("non-string passed as second argument to proc::write()");

        if (!buffer_write_to_subprocess(proc.integer, data.string, data.bytes))
                vm_panic("attempt to write to non-existent subprocess");

        return NIL;
}

struct value
builtin_editor_proc_write_line(value_vector *args)
{
        /* maybe there should be a function specifically for doing this */
        builtin_editor_proc_write(args);
        buffer_write_to_subprocess(args->items[0].integer, "\n", 1);
        return NIL;
}

struct value
builtin_editor_write_file(value_vector *args)
{
        if (args->count == 0) {
                if (!buffer_save_file())
                        blog("There is no file associated with this buffer");
                return NIL;
        }

        if (args->count != 1)
                vm_panic("buffer::writeFile() expects 0 or 1 argument but got %zu", args->count);

        struct value filename = args->items[0];

        if (filename.type != VALUE_STRING)
                vm_panic("non-string passed to buffer::writeFile()");

        buffer_write_file(filename.string, filename.bytes);

        return NIL;
}

struct value
builtin_editor_file_name(value_vector *args)
{
        ASSERT_ARGC("buffer::fileName()", 0);

        char const *filename = buffer_file_name();
        if (filename == NULL)
                return NIL;
        else
                return STRING_CLONE(filename, strlen(filename));
}

struct value
builtin_editor_show_console(value_vector *args)
{
        ASSERT_ARGC("editor::showConsole()", 0);
        buffer_show_console();
        return NIL;
}