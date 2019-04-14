/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kerror.h"

/*
 *
 */
struct compile_info {
	/* Used to report compilation issues */
	struct issue_handler issue_handler;
	/* Memory allocator to use for compilation. */
	struct koji_allocator allocator;
	/* Stream containing the source code. */
	struct koji_source *source;
};

kintern enum koji_result
compile(struct compile_info *, struct prototype **proto);
