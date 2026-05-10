/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @file Element.h
 *
 * Parsed accessibility/UI hierarchy: `Element` trees mirror dumps from XML, compact binary, or JSON;
 * `Xpath` holds coarse selector patterns used by `matchXpathSelector`.
 *
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Element_H_
#define Element_H_

#include "../Base.h"
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <functional>

namespace tinyxml2 {
    class XMLElement;

    class XMLDocument;
}


namespace fastbotx {

    /**
     * Coarse selector for filtering nodes (parsed from an XPath-like string in the ctor).
     * Matching rules live in `Element::matchXpathSelector` (substring vs equality per field; AND/OR).
     */
    class Xpath {
    public:
        Xpath();

        /** Parses `xpathString` into fields stored on this object. */
        explicit Xpath(const std::string &xpathString);

        std::string clazz;

        std::string resourceID;

        std::string text;

        std::string contentDescription;

        /** `-1` means index is ignored during matching. */
        int index;

        /** When true, every non-empty field must match; when false, any single match suffices. */
        bool operationAND;

        /** Original selector text used at construction time. */
        std::string toString() const { return _xpathStr; }

    private:
        std::string _xpathStr;
    };

    typedef std::shared_ptr<Xpath> XpathPtr;


    /**
     * One node in the UI tree: geometry, resource identity, text flags, and ordered children.
     * Built via `createFromXml`, `createFromBinary`, or programmatic `reSet*` / `reAddChild` edits.
     */
    class Element : public Serializable, public std::enable_shared_from_this<Element> {
    public:
        Element();

        bool matchXpathSelector(const XpathPtr &xpathSelector) const;

        /** Removes this node from its parent's child list (does not destroy shared_ptr targets). */
        void deleteElement();

        /**
         * Detaches all children and clears parent links on them.
         * Used when trimming oversized subtrees (for example deep WebView content).
         */
        void clearChildren();


        bool isWebView() const;

        bool isEditText() const;

        const std::vector<std::shared_ptr<Element> > &
        getChildren() const { return this->_children; }

        /** DFS collect: appends every descendant for which `func` returns true (includes nested matches). */
        void recursiveElements(const std::function<bool(std::shared_ptr<Element>)> &func,
                               std::vector<std::shared_ptr<Element>> &result) const;

        /** Applies `doFunc` to each direct child, then recurses depth-first. */
        void recursiveDoElements(const std::function<void(std::shared_ptr<Element>)> &doFunc);

        std::weak_ptr<Element> getParent() const { return this->_parent; }

        const std::string &getClassname() const { return this->_classname; }

        const std::string &getResourceID() const { return this->_resourceID; }

        const std::string &getText() const { return this->_text; }

        const std::string &getContentDesc() const { return this->_contentDesc; }

        const std::string &getPackageName() const { return this->_packageName; }

        RectPtr getBounds() const { return this->_bounds; };

        /** True when the dump carried `vu` / `visible-to-user`. */
        bool hasApeVisibleToUserAttribute() const { return _apeHasVisibleToUserAttr; }

        /** Interpreted visible-to-user flag when the attribute was present. */
        bool getApeVisibleToUser() const { return _apeVisibleToUser; }

        /** Raw `visibility` string when abbreviated `vis` / full `visibility` was supplied. */
        const std::string &getApeVisibilityRaw() const { return _apeVisibilityRaw; }

        /** True when `bounds`/`bnd` existed but was explicitly empty in the source XML. */
        bool hasApeEmptyBoundsAttribute() const { return _apeEmptyBoundsAttr; }

        int getIndex() const { return this->_index; }

        bool getClickable() const { return this->_clickable; }

        bool getLongClickable() const { return this->_longClickable; }

        bool getCheckable() const { return this->_checkable; }

        bool getChecked() const { return this->_checked; }

        bool getFocusable() const { return this->_focusable; }

        bool getScrollable() const { return this->_scrollable; }

        bool getEnable() const { return this->_enabled; }

        bool getFocused() const { return this->_focused; }

        bool getPassword() const { return this->_password; }

        ScrollType getScrollType() const;

        /** Classifies vertical vs horizontal scrolling from widget class names when `_scrollable` is set. */
        ScrollType _computeScrollType() const;

        /** Marks subtree hash and serialized XML stale after mutating fields (see `Preference` / fuzz paths). */
        void invalidateCaches() const { _hashCached = false; _xmlCached = false; }

        void reSetResourceID(const std::string &resourceID) {
            this->_resourceID = resourceID;
            invalidateCaches();
        }

        void reSetContentDesc(const std::string &content) {
            this->_contentDesc = content;
            invalidateCaches();
        }

        void reSetText(const std::string &text) {
            this->_text = text;
            invalidateCaches();
        }

        void reSetIndex(const int &index) {
            this->_index = index;
            invalidateCaches();
        }

        void reSetClassname(const std::string &className) {
            this->_classname = className;
            invalidateCaches();
        }

        void reSetClickable(bool clickable) {
            this->_clickable = clickable;
            invalidateCaches();
        }

        void reSetLongClickable(bool longClickable) {
            this->_longClickable = longClickable;
            invalidateCaches();
        }

        void reSetCheckable(bool checkable) {
            this->_checkable = checkable;
            invalidateCaches();
        }

        void reSetScrollable(bool scrollable) {
            this->_scrollable = scrollable;
            invalidateCaches();
        }

        void reSetEnabled(bool enable) {
            this->_enabled = enable;
            invalidateCaches();
        }

        void reSetBounds(RectPtr rect) {
            this->_bounds = std::move(rect);
            invalidateCaches();
        }

        void reSetParent(const std::shared_ptr<Element> &parent) {
            this->_parent = parent;
            invalidateCaches();
        }

        void reAddChild(const std::shared_ptr<Element> &child) {
            this->_children.emplace_back(child);
            invalidateCaches();
        }

        std::string toJson() const;

        std::string toXML() const;

        /** Memoized `toXML()` until `invalidateCaches` / structural edits clear `_xmlCached`. */
        const std::string &toXMLCached() const;

        void fromJson(const std::string &jsonData);

        std::string toString() const override;

        static std::shared_ptr<Element> createFromXml(const std::string &xmlContent);

        static std::shared_ptr<Element> createFromXml(const tinyxml2::XMLDocument &doc);

        /**
         * Compact binary tree (SECURITY_AND_OPTIMIZATION option 1, section 7).
         * Magic bytes `FB\\0\\1` followed by depth-first node records.
         */
        static std::shared_ptr<Element> createFromBinary(const char *buf, size_t len);

        /** Reads one node header + payload at `*offset`; used by `createFromBinary`. */
        static std::shared_ptr<Element> parseBinaryNode(const char *buf, size_t len, size_t *offset,
                                                        const std::shared_ptr<Element> &parent);

        /** Fills the current instance from the binary stream at `*offset`. */
        bool parseBinaryNodeSelf(const char *buf, size_t len, size_t *offset,
                                 const std::shared_ptr<Element> &parent);

        /**
         * Structural fingerprint for state naming; when `recursive`, folds in ordered child hashes.
         * Recursive results are cached on `this` until `invalidateCaches`.
         */
        long hash(bool recursive = true);

        /** Normalized label text consumed by reuse/naming layers (parallel to widget display text). */
        std::string validText;

        /** Stable preorder id assigned during `ReuseState::rebuildElementIdMaps` for cross-layer action lookup. */
        int getStableElementId() const { return _stableElementId; }

        void setStableElementId(int id) { _stableElementId = id; }

        virtual ~Element();

    protected:
        void fromXMLNode(const tinyxml2::XMLElement *xmlNode,
                         const std::shared_ptr<Element> &parentOfNode);

        void fromXml(const tinyxml2::XMLDocument &nodeOfDoc,
                     const std::shared_ptr<Element> &parentOfNode);

        void recursiveToXML(tinyxml2::XMLElement *xml, const Element *elm) const;

        std::string _resourceID;
        std::string _classname;
        std::string _packageName;
        std::string _text;
        std::string _contentDesc;
        std::string _inputText;
        std::string _activity;

        bool _enabled;
        bool _checked;
        bool _checkable;
        bool _clickable;
        bool _focusable;
        bool _scrollable;
        bool _longClickable;
        int _childCount;
        bool _focused;
        int _index;
        bool _password;
        bool _selected;
        bool _isEditable;

        RectPtr _bounds;
        std::vector<std::shared_ptr<Element> > _children;
        std::weak_ptr<Element> _parent;

        mutable ScrollType _cachedScrollType;
        mutable bool _scrollTypeCached;

        mutable long _cachedHash;
        mutable bool _hashCached;

        mutable std::string _cachedXml;
        mutable bool _xmlCached = false;

        int _stableElementId{-1};

        /** Parsed visibility hints from XML (`vu`/`visible-to-user`, `vis`/`visibility`); names retain legacy prefix. */
        bool _apeHasVisibleToUserAttr = false;
        bool _apeVisibleToUser = true;
        std::string _apeVisibilityRaw;
        bool _apeEmptyBoundsAttr = false;
    };

    typedef std::shared_ptr<Element> ElementPtr;


}

#endif //Element_H_
