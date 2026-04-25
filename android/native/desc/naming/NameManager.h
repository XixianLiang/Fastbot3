/*
 * APE NameManager-compatible name interning and stable order assignment.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMEMANAGER_H_
#define FASTBOTX_DESC_NAMING_NAMEMANAGER_H_

#include "Name.h"

namespace fastbotx {
namespace naming {

NamePtr cacheName(const NamePtr &name);

} // namespace naming
} // namespace fastbotx

#endif
