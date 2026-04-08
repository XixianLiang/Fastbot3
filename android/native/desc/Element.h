/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
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
     * @brief XPath selector for matching UI elements
     * 
     * Represents an XPath-like selector that can match elements based on
     * class name, resource ID, text, content description, and index.
     * Supports both AND and OR operations for matching.
     */
    class Xpath {
    public:
        /**
         * @brief Default constructor creates empty XPath selector
         */
        Xpath();

        /**
         * @brief Constructor from XPath string
         * 
         * @param xpathString XPath string to parse
         */
        explicit Xpath(const std::string &xpathString);

        /// Class name to match
        std::string clazz;
        
        /// Resource ID to match
        std::string resourceID;
        
        /// Text content to match
        std::string text;
        
        /// Content description to match
        std::string contentDescription;
        
        /// Index to match (-1 means ignore index)
        int index;
        
        /// If true, use AND operation (all non-empty fields must match)
        /// If false, use OR operation (any non-empty field can match)
        bool operationAND;

        /**
         * @brief Get string representation of this XPath
         * 
         * @return Original XPath string
         */
        std::string toString() const { return _xpathStr; }

    private:
        /// Original XPath string
        std::string _xpathStr;
    };

    typedef std::shared_ptr<Xpath> XpathPtr;


    /**
     * @brief Element class representing a UI element in the XML hierarchy
     * 
     * Element represents a single UI element parsed from XML. It maintains
     * a tree structure with parent-child relationships and contains all
     * properties of the UI element (bounds, text, class name, etc.).
     * 
     * Features:
     * - XML parsing and serialization
     * - Tree structure with parent-child relationships
     * - XPath matching
     * - Hash computation for state comparison
     * - JSON/XML conversion
     */
    class Element : public Serializable, public std::enable_shared_from_this<Element> {
    public:
        Element();

        bool matchXpathSelector(const XpathPtr &xpathSelector) const;

        void deleteElement();

        /**
         * Clear all children nodes (detach subtree).
         * Used by APE-style input normalization (e.g. trimming oversized WebView subtrees).
         */
        void clearChildren();


        bool isWebView() const;

        bool isEditText() const;

        const std::vector<std::shared_ptr<Element> > &
        getChildren() const { return this->_children; }

        // recursive get elements depends func
        void recursiveElements(const std::function<bool(std::shared_ptr<Element>)> &func,
                               std::vector<std::shared_ptr<Element>> &result) const;

        void recursiveDoElements(const std::function<void(std::shared_ptr<Element>)> &doFunc);

        std::weak_ptr<Element> getParent() const { return this->_parent; }

        const std::string &getClassname() const { return this->_classname; }

        const std::string &getResourceID() const { return this->_resourceID; }

        const std::string &getText() const { return this->_text; }

        const std::string &getContentDesc() const { return this->_contentDesc; }

        const std::string &getPackageName() const { return this->_packageName; }

        RectPtr getBounds() const { return this->_bounds; };

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
        
        // Internal method to compute scroll type (used for caching)
        ScrollType _computeScrollType() const;

        // reset properties, in Preference
        // Performance: Clear hash cache when properties change
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

        const std::string &toXMLCached() const;

        void fromJson(const std::string &jsonData);

        std::string toString() const override;

        static std::shared_ptr<Element> createFromXml(const std::string &xmlContent);

        static std::shared_ptr<Element> createFromXml(const tinyxml2::XMLDocument &doc);

        /** Create tree from compact binary (SECURITY_AND_OPTIMIZATION §7 opt1). Magic "FB\\0\\1" then nodes. */
        static std::shared_ptr<Element> createFromBinary(const char *buf, size_t len);

        /** Parse one node from binary buffer; used by createFromBinary. */
        static std::shared_ptr<Element> parseBinaryNode(const char *buf, size_t len, size_t *offset,
                                                        const std::shared_ptr<Element> &parent);

        /** Instance helper: fill this node from binary buffer; used by parseBinaryNode. */
        bool parseBinaryNodeSelf(const char *buf, size_t len, size_t *offset,
                                 const std::shared_ptr<Element> &parent);

        long hash(bool recursive = true);

        std::string validText;

        /** DFS-assigned id for LLMDroid payload element references (see ReuseState::rebuildElementIdMaps). */
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

        // Performance optimization: Cache scroll type to avoid repeated string comparisons
        mutable ScrollType _cachedScrollType;
        mutable bool _scrollTypeCached;
        
        // Performance optimization: Cache hash to avoid repeated computation
        mutable long _cachedHash;
        mutable bool _hashCached;
        
        mutable std::string _cachedXml;
        mutable bool _xmlCached = false;

        // a construct helper
        static bool _allClickableFalse;

        int _stableElementId{-1};
    };

    typedef std::shared_ptr<Element> ElementPtr;


}

#endif //Element_H_
