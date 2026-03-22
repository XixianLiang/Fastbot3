#include "Namelet.h"
#include "Naming.h"

#include <stdexcept>

namespace fastbotx {
namespace naming {

    Namelet::Namelet(Type type, std::string expr_str, NamerPtr namer)
        : type_(type),
          expr_str_(std::move(expr_str)),
          namer_(std::move(namer)) {
        if (!namer_) {
            throw std::invalid_argument("Namelet: namer is null");
        }
    }

    Namelet::Namelet(std::string expr_str, NamerPtr namer)
        : Namelet(Type::REFINE, std::move(expr_str), std::move(namer)) {}

    Namelet::~Namelet() = default;

    bool Namelet::operator<(const Namelet &other) const {
        int c = expr_str_.compare(other.expr_str_);
        if (c != 0) return c < 0;
        if (type_ != other.type_) return static_cast<unsigned char>(type_) < static_cast<unsigned char>(other.type_);
        return compareNamer(*namer_, *other.namer_) < 0;
    }

    bool Namelet::operator==(const Namelet &other) const {
        return type_ == other.type_ && expr_str_ == other.expr_str_ && compareNamer(*namer_, *other.namer_) == 0;
    }

    void Namelet::addChildNaming(const std::shared_ptr<Namelet> &child, const std::shared_ptr<Naming> &childNaming) {
        if (!child || !childNaming) return;
        child_namings_[child] = childNaming;
    }

    std::shared_ptr<Naming> Namelet::getChildNaming(const std::shared_ptr<Namelet> &child) const {
        auto it = child_namings_.find(child);
        if (it == child_namings_.end()) return nullptr;
        return it->second;
    }

    void Namelet::setParent(const std::shared_ptr<Namelet> &parent) {
        if (!parent) {
            throw std::invalid_argument("Namelet::setParent: parent is null");
        }
        if (parent_.lock()) {
            throw std::logic_error("Namelet::setParent: already has parent");
        }
        parent_ = parent;
        depth_ = parent->getDepth() + 1;
    }

} // namespace naming
} // namespace fastbotx
