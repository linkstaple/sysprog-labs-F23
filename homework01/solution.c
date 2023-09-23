#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libcoro.h"

void print_numbers(int *arr, int length);
int read_numbers_from_file(char *filename, int *dest);
void write_numbers_to_file(char *filename, int *numbers, int len);
struct array_of_ints merge_sorted_arrays(struct array_of_ints a, struct array_of_ints b);


struct array_of_ints {
    int *array;
    int len;
};


const int MAX_INTEGERS_AMOUNT = 40000;

struct my_context {
	char *name;
	int files_total;
	char **filenames;
	struct array_of_ints *dest;
	struct timespec* prev_timestamp;
	struct timespec* coro_work_time;
	int *next_file_idx;
};

struct array_of_ints get_sorted_numbers(int *numbers, int len, struct my_context *ctx);
void update_coro_work_time(struct my_context *ctx);
void set_coro_timestamp(struct my_context *ctx);

static struct my_context *
my_context_new(const char *name, int files_total, char** filenames, int *next_file_idx, struct array_of_ints *dest)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
	ctx->files_total = files_total;
	ctx->filenames = filenames;
	ctx->next_file_idx = next_file_idx;
	ctx->dest = dest;
	ctx->coro_work_time = (struct timespec*) malloc(sizeof(struct timespec*));
	ctx->coro_work_time->tv_nsec = 0;
	ctx->coro_work_time->tv_sec = 0;

	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx->coro_work_time);
	free(ctx->prev_timestamp);
	free(ctx);
}

static int
coroutine_func_f(void *context)
{
	struct my_context *ctx = context;
	set_coro_timestamp(ctx);
	char *name = ctx->name;

	int file_idx;
	while ((file_idx = *ctx->next_file_idx) < ctx->files_total) {
		char *filename = ctx->filenames[file_idx];
		struct array_of_ints *dest = ctx->dest+file_idx;
		(*ctx->next_file_idx)++;
		printf("coro name=%s file_idx=%d filename=%s\n", name, file_idx, filename);

		int *file_numbers = malloc(MAX_INTEGERS_AMOUNT * sizeof(int));
		int amount_of_numbers = read_numbers_from_file(filename, file_numbers);
		struct array_of_ints sorted_data = get_sorted_numbers(file_numbers, amount_of_numbers, ctx);
		dest->array = sorted_data.array;
		dest->len = sorted_data.len;
	}

	update_coro_work_time(ctx);

	printf("coro %s has been working for %ld secs and %ld nanosecs\n", name, ctx->coro_work_time->tv_sec, ctx->coro_work_time->tv_nsec);
	printf("coro %s has had %lld switches\n", name, coro_switch_count(coro_this()));

	my_context_delete(ctx);

	return 0;
}

int
main(int argc, char **argv)
{
	struct timespec* program_start_timestamp = (struct timespec*) malloc(sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, program_start_timestamp);

	int coros_total = atoi(argv[1]);
	printf("number of coros: %d\n", coros_total);

	int files_total = argc - 2;
	char **filenames = (char**) malloc(files_total * sizeof(char*));
	memcpy(filenames, argv + 2, files_total * sizeof(char*));
	printf("files to process:%d\n", files_total);

	struct array_of_ints *destinations = malloc(files_total * sizeof(struct array_of_ints));

	int *next_file_idx = (int*) malloc(sizeof(int));
	*next_file_idx = 0;

	coro_sched_init();

	for (int i = 0; i < coros_total; ++i) {
		char name[16];
		sprintf(name, "coro_%d", i);
		coro_new(coroutine_func_f, my_context_new(name, files_total, filenames, next_file_idx, destinations));
	};

	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		coro_delete(c);
	}

	free(filenames);

	int final_length = 0;

    for (int i = 1; i <= files_total; i++) {
        final_length += destinations[i - 1].len;
    }

    int *all_numbers = (int*) malloc(final_length * sizeof(int));
    for (int i = 0, idx = 0; i < files_total; i++) {
        memcpy(all_numbers + idx, destinations[i].array, destinations[i].len * sizeof(int));
        idx += destinations[i].len;
        free(destinations[i].array);
    }

	free(destinations);

    struct array_of_ints result = get_sorted_numbers(all_numbers, final_length, NULL);
    write_numbers_to_file("result.txt", result.array, result.len);

	free(all_numbers);

	struct timespec* program_end_timestamp = (struct timespec*) malloc(sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, program_end_timestamp);

	printf("Program has been working for %ld secs and %ld nanosecs\n",
		program_end_timestamp->tv_sec - program_start_timestamp->tv_sec,
		program_end_timestamp->tv_nsec - program_start_timestamp->tv_nsec);

	free(program_start_timestamp);
	free(program_end_timestamp);

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

struct array_of_ints get_sorted_numbers(int *numbers, int len, struct my_context *ctx) {
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

        struct array_of_ints left_sorted = get_sorted_numbers(numbers, left_len, ctx);
        struct array_of_ints right_sorted = get_sorted_numbers(numbers + left_len, right_len, ctx);
        result = merge_sorted_arrays(left_sorted, right_sorted);
        free(left_sorted.array);
        free(right_sorted.array);
    }

	if (ctx != NULL) {
		update_coro_work_time(ctx);
	} 
	coro_yield();
	if (ctx != NULL) {
		set_coro_timestamp(ctx);
	}

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

void update_coro_work_time(struct my_context *ctx) {
	struct timespec *timestamp = (struct timespec*) malloc(sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, timestamp);

	time_t sec_diff = timestamp->tv_sec - ctx->prev_timestamp->tv_sec;
	long nsec_diff = timestamp->tv_nsec - ctx->prev_timestamp->tv_nsec;
	ctx->coro_work_time->tv_sec += sec_diff;
	ctx->coro_work_time->tv_nsec += nsec_diff;

	free(timestamp);
}

void set_coro_timestamp(struct my_context *ctx) {
	struct timespec *timestamp = (struct timespec*) malloc(sizeof(struct timespec));
	clock_gettime(CLOCK_MONOTONIC, timestamp);
	free(ctx->prev_timestamp);
	ctx->prev_timestamp = timestamp;
}
