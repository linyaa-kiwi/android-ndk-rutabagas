#pragma once

#include "attribs.h"

typedef struct ANativeActivity ANativeActivity;

char * _malloc_ _must_use_result_
ru_activity_get_package_name(ANativeActivity *activity);

char * _malloc_ _must_use_result_
ru_activity_get_string_extra(ANativeActivity *activity, const char *name);
