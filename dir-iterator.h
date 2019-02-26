#ifndef DIR_ITERATOR_H
#define DIR_ITERATOR_H

#include "strbuf.h"

/*
 * Iterate over a directory tree.
 *
 * With no flags to modify behaviour, iterate over a directory tree,
 * recursively, including paths of all types and hidden paths. Skip
 * "." and ".." entries and don't follow symlinks except for the
 * original path.
 *
 * Every time dir_iterator_advance() is called, update the members of
 * the dir_iterator structure to reflect the next path in the
 * iteration. The order that paths are iterated over within a
 * directory is undefined, but directory paths are always iterated
 * over before the subdirectory contents.
 *
 * A typical iteration looks like this:
 *
 *     int ok;
 *     struct iterator *iter = dir_iterator_begin(path, 0);
 *
 *     while ((ok = dir_iterator_advance(iter)) == ITER_OK) {
 *             if (want_to_stop_iteration()) {
 *                     ok = dir_iterator_abort(iter);
 *                     break;
 *             }
 *
 *             // Access information about the current path:
 *             if (S_ISDIR(iter->st.st_mode))
 *                     printf("%s is a directory\n", iter->relative_path);
 *     }
 *
 *     if (ok != ITER_DONE)
 *             handle_error();
 *
 * Callers are allowed to modify iter->path while they are working,
 * but they must restore it to its original contents before calling
 * dir_iterator_advance() again.
 */

/*
 * Flags for dir_iterator_begin:
 *
 * - DIR_ITERATOR_PEDANTIC: override dir-iterator's default behavior
 *   in case of an error while trying to fetch the next entry, which is
 *   to emit a warning and keep going. With this flag, resouces are
 *   freed and ITER_ERROR is return immediately.
 *
 * - DIR_ITERATOR_FOLLOW_SYMLINKS: make dir-iterator follow symlinks to
 *   directories, i.e., iterate over linked directories' contents.
 */
#define DIR_ITERATOR_PEDANTIC (1 << 0)
#define DIR_ITERATOR_FOLLOW_SYMLINKS (1 << 1)

struct dir_iterator {
	/* The current path: */
	struct strbuf path;

	/*
	 * The current path relative to the starting path. This part
	 * of the path always uses "/" characters to separate path
	 * components:
	 */
	const char *relative_path;

	/* The current basename: */
	const char *basename;

	/* The result of calling lstat() on path: */
	struct stat st;
};

/*
 * Start a directory iteration over path with the combination of
 * options specified by flags. Return a dir_iterator that holds the
 * internal state of the iteration.
 *
 * The iteration includes all paths under path, not including path
 * itself and not including "." or ".." entries.
 *
 * Parameters are:
 *  - path is the starting directory. An internal copy will be made.
 *  - flags is a combination of the possible flags to initialize a
 *    dir-iterator or 0 for default behaviour.
 */
struct dir_iterator *dir_iterator_begin(const char *path, unsigned flags);

/*
 * Advance the iterator to the first or next item and return ITER_OK.
 * If the iteration is exhausted, free the dir_iterator and any
 * resources associated with it and return ITER_DONE. On error, free
 * dir_iterator and associated resources and return ITER_ERROR. It is
 * a bug to use iterator or call this function again after it has
 * returned ITER_DONE or ITER_ERROR.
 *
 * Note that whether dir-iterator will return ITER_ERROR when failing
 * to fetch the next entry or just emit a warning and try to fetch the
 * next is defined by the 'pedantic' option at dir-iterator's
 * initialization.
 */
int dir_iterator_advance(struct dir_iterator *iterator);

/*
 * End the iteration before it has been exhausted. Free the
 * dir_iterator and any associated resources and return ITER_DONE. On
 * error, free the dir_iterator and return ITER_ERROR.
 */
int dir_iterator_abort(struct dir_iterator *iterator);

#endif
