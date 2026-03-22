/*
 * APE Namelet: XPath expression + Namer (Java: naming.Namelet).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMELET_H_
#define FASTBOTX_DESC_NAMING_NAMELET_H_

#include "Namer.h"

#include <map>
#include <memory>
#include <string>

namespace fastbotx {
namespace naming {

    class Naming;

    class Namelet : public std::enable_shared_from_this<Namelet> {
    public:
        enum class Type : unsigned char { BASE = 0, REFINE = 1 };

        Namelet(Type type, std::string expr_str, NamerPtr namer);

        /** REFINE namelet (Java Namelet(String, Namer)). */
        explicit Namelet(std::string expr_str, NamerPtr namer);

        ~Namelet();

        /** Java compareTo: expr, type, then namerComparator. */
        bool operator<(const Namelet &other) const;

        bool operator==(const Namelet &other) const;

        Type getType() const { return type_; }

        const std::string &getExprString() const { return expr_str_; }

        Namer &getNamer() const { return *namer_; }

        NamerPtr getNamerPtr() const { return namer_; }

        int getDepth() const { return depth_; }

        bool isBase() const { return type_ == Type::BASE; }

        bool isRefine() const { return type_ == Type::REFINE; }

        std::shared_ptr<Namelet> getParent() const { return parent_.lock(); }

        const std::map<std::shared_ptr<Namelet>, std::shared_ptr<Naming>, std::owner_less<std::shared_ptr<Namelet>>> &
        getChildNamings() const { return child_namings_; }

        void addChildNaming(const std::shared_ptr<Namelet> &child, const std::shared_ptr<Naming> &childNaming);

        std::shared_ptr<Naming> getChildNaming(const std::shared_ptr<Namelet> &child) const;

        void setParent(const std::shared_ptr<Namelet> &parent);

    private:
        Type type_{Type::REFINE};
        std::string expr_str_;
        NamerPtr namer_{nullptr};

        int depth_{0};
        std::weak_ptr<Namelet> parent_{};

        std::map<std::shared_ptr<Namelet>, std::shared_ptr<Naming>, std::owner_less<std::shared_ptr<Namelet>>>
            child_namings_{};
    };

    using NameletPtr = std::shared_ptr<Namelet>;

} // namespace naming
} // namespace fastbotx

#endif
