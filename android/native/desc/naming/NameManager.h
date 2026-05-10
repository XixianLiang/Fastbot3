/*
 * Name interning and stable `order_` assignment for `Name` objects (process-global cache keyed by namer).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMEMANAGER_H_
#define FASTBOTX_DESC_NAMING_NAMEMANAGER_H_

#include "Name.h"

namespace fastbotx {
namespace naming {

/** Interns `name` or returns an equal existing instance; see `NameManager.cpp`. */
NamePtr cacheName(const NamePtr &name);

} // namespace naming
} // namespace fastbotx

#endif
