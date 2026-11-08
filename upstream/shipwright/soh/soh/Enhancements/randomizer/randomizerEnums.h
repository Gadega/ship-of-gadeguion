#pragma once
// Default expansion: real enums

#if !defined(RANDO_ENUM_BEGIN) && !defined(RANDO_ENUM_ITEM) && !defined(RANDO_ENUM_END)
// clang-format off
#define RANDO_ENUM_BEGIN(EnumName) typedef enum EnumName {
// GCC 7 supports variadic macros, but not __VA_OPT__. Preserve explicit values.
#define RANDO_ENUM__ITEM_ONE(name) name,
#define RANDO_ENUM__ITEM_TWO(name, value) name = value,
#define RANDO_ENUM__SELECT(_1, _2, selected, ...) selected
#define RANDO_ENUM_ITEM(...) \
    RANDO_ENUM__SELECT(__VA_ARGS__, RANDO_ENUM__ITEM_TWO, RANDO_ENUM__ITEM_ONE)(__VA_ARGS__)
#define RANDO_ENUM_END(EnumName) } EnumName;
#define RANDO_ENUM__CLEANUP
// clang-format on
#endif

#include "randomizerEnumList.h"

// Clean up only the defaults we defined.
#ifdef RANDO_ENUM__CLEANUP
#undef RANDO_ENUM_BEGIN
#undef RANDO_ENUM_ITEM
#undef RANDO_ENUM__ITEM_ONE
#undef RANDO_ENUM__ITEM_TWO
#undef RANDO_ENUM__SELECT
#undef RANDO_ENUM_END
#undef RANDO_ENUM__CLEANUP
#endif
