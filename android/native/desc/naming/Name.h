/*
 * APE-compatible abstract widget name (Java: com.android.commands.monkey.ape.naming.Name).
 * Concrete implementations (TypeNamer, XPath composition, etc.) are added incrementally.
 */
#ifndef FASTBOTX_DESC_NAMING_NAME_H_
#define FASTBOTX_DESC_NAMING_NAME_H_

#include <memory>
#include <string>

namespace fastbotx {
namespace naming {

    class Namer;

    class Name {
    public:
        virtual ~Name();

        virtual std::shared_ptr<Namer> getNamer() const = 0;

        /** XPath fragment for this name (APE: toXPath). */
        virtual std::string toXPath() const = 0;

        /** Local predicate fragment after `// *` (any node) for AbstractLocalName-style names. */
        virtual void appendXPathLocalProperties(std::string &sb) const { (void)sb; }

        /** Lexicographic order on XPath string (APE: Comparable<Name>). */
        bool operator<(const Name &other) const;
        bool operator==(const Name &other) const;
    };

    using NamePtr = std::shared_ptr<Name>;

} // namespace naming
} // namespace fastbotx

#endif
