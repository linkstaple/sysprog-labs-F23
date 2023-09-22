#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"

void print_numbers(int *arr, int length);
int read_numbers_from_file(char *filename, int *dest);
void write_numbers_to_file(char *filename, int *numbers, int len);
struct array_of_ints get_sorted_numbers(int *numbers, int len);
struct array_of_ints merge_sorted_arrays(struct array_of_ints a, struct array_of_ints b);
struct array_of_ints get_sorted_numbers_from_file(char* filename);


struct array_of_ints {
    int *array;
    int len;
};


const int MAX_INTEGERS_AMOUNT = 40000;

struct my_context {
	char *name;
	int work_time;
	char *filename;
	struct array_of_ints *dest;
	/** ADD HERE YOUR OWN MEMBERS, SUCH AS FILE NAME, WORK TIME, ... */
};

static struct my_context *
my_context_new(const char *name, const char *filename, struct array_of_ints *dest)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	ctx->filename = strdup(filename);
	ctx->work_time = 0;
	ctx->dest = dest;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
	/* IMPLEMENT SORTING OF INDIVIDUAL FILES HERE. */

	// struct coro *this = coro_this();
	struct my_context *ctx = context;
	char *name = ctx->name;
	char *filename = ctx->filename;

	printf("coro name=%s\n", name);

	// coro_yield();

	int *file_numbers = malloc(MAX_INTEGERS_AMOUNT * sizeof(int));
	int amount_of_numbers = read_numbers_from_file(filename, file_numbers);
	struct array_of_ints sorted_data = get_sorted_numbers(file_numbers, amount_of_numbers);
	// print_numbers(sorted_data.array, sorted_data.len);
	// TODO fix destination
	ctx->dest->array = sorted_data.array;
	ctx->dest->len = sorted_data.len;

	my_context_delete(ctx);
	/* This will be returned from coro_status(). */
	return 0;
}

int
main(int argc, char **argv)
{
	int amount_of_files = argc - 1;
	struct array_of_ints *destinations = malloc(amount_of_files * sizeof(struct array_of_ints));
	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();
	/* Start several coroutines. */
	for (int i = 0; i < amount_of_files; ++i) {
		/*
		 * The coroutines can take any 'void *' interpretation of which
		 * depends on what you want. Here as an example I give them
		 * some names.
		 */
		char name[16];
		sprintf(name, "coro_%d", i);
		// char filename
		/*
		 * I have to copy the name. Otherwise all the coroutines would
		 * have the same name when they finally start.
		 */
		coro_new(coroutine_func_f, my_context_new(name, argv[i + 1], destinations + i));
	};
	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("Finished %d\n", coro_status(c));
		printf("switch count - %lld\n", coro_switch_count(c));
		coro_delete(c);
	}
	/* All coroutines have finished. */

	/* IMPLEMENT MERGING OF THE SORTED ARRAYS HERE. */
	int final_length = 0;
	// struct array_of_ints *sorted_files = (struct array_of_ints*) malloc(amount_of_files * sizeof(struct array_of_ints));

    for (int i = 1; i <= amount_of_files; i++) {
        final_length += destinations[i - 1].len;
    }

	printf("amount of files %d\n", amount_of_files);

	// print_numbers(destinations[0].array, destinations[0].len);

    int *all_numbers = (int*) malloc(final_length * sizeof(int));
    for (int i = 0, idx = 0; i < amount_of_files; i++) {
        memcpy(all_numbers + idx, destinations[i].array, destinations[i].len * sizeof(int));
        idx += destinations[i].len;
        free(destinations[i].array);
    }

	// print_numbers(all_numbers, final_length);
    struct array_of_ints result = get_sorted_numbers(all_numbers, final_length);
    write_numbers_to_file("result.txt", result.array, result.len);

	return 0;
}

int read_numbers_from_file(char *filename, int *dest) {
    FILE *file = fopen(filename, "r");

    int length = 0;
    for (int num; fscanf(file, "%d", &num) != EOF; length++) {
        dest[length] = num;
    }

    fclose(file);

    return length;
}

void write_numbers_to_file(char *filename, int *numbers, int len) {
    FILE *file = fopen(filename, "w+");
    for (int i = 0; i < len; i++) {
        fprintf(file, "%d ", numbers[i]);
    }

    fclose(file);
}

void print_numbers(int *numbers, int len) {
    for (int i = 0; i < len; i++) {
        printf("%d ", numbers[i]);
    }

    printf("\n");
}

struct array_of_ints get_sorted_numbers(int *numbers, int len) {
    struct array_of_ints result;

    if (len == 1) {
        int *p = malloc(sizeof(int));
        *p = numbers[0];
        result.array = p;
        result.len = 1;
    } else if (len == 2) {
        int *sorted = (int*) malloc(2 * sizeof(int));
        memcpy(sorted, numbers, 2 * sizeof(int));
        if (sorted[0] > sorted[1]) {
            int temp = sorted[0];
            sorted[0] = sorted[1];
            sorted[1] = temp;            
        }

        result.array = sorted;
        result.len = 2;
    } else {
        int left_len = len / 2;
        int right_len = len - left_len;

        struct array_of_ints left_sorted = get_sorted_numbers(numbers, left_len);
        struct array_of_ints right_sorted = get_sorted_numbers(numbers + left_len, right_len);
        result = merge_sorted_arrays(left_sorted, right_sorted);
        free(left_sorted.array);
        free(right_sorted.array);
    }

	coro_yield();

    return result;

}

struct array_of_ints merge_sorted_arrays(struct array_of_ints a, struct array_of_ints b) {
    int *left = a.array;
    int *right = b.array;

    int idx = 0;
    int *result = (int*) malloc((a.len + b.len) * sizeof(int));

    for (int l_idx = 0, r_idx = 0; l_idx < a.len || r_idx < b.len;) {
        if (l_idx >= a.len) {
            while(r_idx < b.len) {
                result[idx++] = right[r_idx++];
            }
        } else if (r_idx >= b.len) {
            while(l_idx < a.len) {
                result[idx++] = left[l_idx++];
            }
        } else {
            if (left[l_idx] <= right[r_idx]) {
                result[idx++] = left[l_idx++];
            } else {
                while (r_idx < b.len && right[r_idx] < left[l_idx]) {
                    result[idx++] = right[r_idx++];
                }
            }
        }
    }

    struct array_of_ints merged = {result, a.len + b.len};
    return merged;
}

struct array_of_ints get_sorted_numbers_from_file(char* filename) {
    int *numbers = malloc(MAX_INTEGERS_AMOUNT * sizeof(int));
    int len = read_numbers_from_file(filename, numbers);
    struct array_of_ints result =  get_sorted_numbers(numbers, len);
    free(numbers);
    return result;
}
