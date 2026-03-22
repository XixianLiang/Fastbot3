#include "Name.h"

namespace fastbotx {
namespace naming {

    Name::~Name() = default;

    bool Name::operator<(const Name &other) const { return toXPath() < other.toXPath(); }

    bool Name::operator==(const Name &other) const { return toXPath() == other.toXPath(); }

} // namespace naming
} // namespace fastbotx
