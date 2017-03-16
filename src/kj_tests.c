
#include "koji.h"
#include "kj_vm.h"
#include "kj_table.h"
#include <assert.h>

struct koji_state {
	struct koji_allocator allocator;
	struct vm vm;
};

void test_table(koji_state_t* state)
{
   struct table t;

   table_init(&t, &state->vm, TABLE_DEFAULT_CAPACITY);
   table_set(&t, &state->vm, value_number(10), value_number(118));
   table_set(&t, &state->vm, value_number(11), value_number(119));

   value_t value = table_get(&t, value_number(10));
   assert(value_is_number(value) && value.number == 118.0);

	for (int i = 0; i < 100; ++i)
		table_set(&t, &state->vm, value_number(i), value_number(i * 1000));

	for (int i = 0; i < 100; ++i)
		assert(table_get(&t, value_number(i)).number == i * 1000);

	table_deinit(&t, &state->vm);
}

int main()
{
   koji_state_t* state = koji_open(NULL);

   test_table(state);

   koji_close(state);
}
