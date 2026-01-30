/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Element_CPP_
#define Element_CPP_

#include "../utils.hpp"
#include "Element.h"
#include "../thirdpart/tinyxml2/tinyxml2.h"
#include "../thirdpart/json/json.hpp"


namespace fastbotx {

    Element::Element()
            : _enabled(false), _checked(false), _checkable(false), _clickable(false),
              _focusable(false), _scrollable(false), _longClickable(false), _childCount(0),
              _focused(false), _index(0), _password(false), _selected(false), _isEditable(false),
              _cachedScrollType(ScrollType::NONE), _scrollTypeCached(false),
              _cachedHash(0), _hashCached(false) {
        _children.clear();
        this->_bounds = Rect::RectZero;
    }

    /**
     * @brief Remove this element from its parent's children list
     * 
     * Removes this element from the parent's children vector and decrements
     * the parent's child count. This is used for dynamic UI tree manipulation.
     * 
     * Performance optimization:
     * - Uses remove_if algorithm for efficient removal
     * - Locks parent weak_ptr only once and reuses it
     * 
     * @note This method does not delete the element itself, only removes it from the tree
     * @warning Root elements cannot be deleted (they have no parent)
     */
    void Element::deleteElement() {
        auto parentOfElement = this->getParent();
        if (parentOfElement.expired()) {
            BLOGE("%s", "element is a root elements");
            return;
        }
        
        // Performance: Lock parent once and reuse the shared_ptr
        auto parentLocked = parentOfElement.lock();
        
        // Use remove_if algorithm for efficient removal
        auto iter = std::remove_if(parentLocked->_children.begin(),
                                   parentLocked->_children.end(),
                                   [this](const ElementPtr &elem) { 
                                       return elem.get() == this; 
                                   });
        
        // If element was found and removed, update parent's child count
        if (iter != parentLocked->_children.end()) {
            parentLocked->_childCount--;
            parentLocked->_children.erase(iter, parentLocked->_children.end());
        }
        
        // Clear this element's parent reference
        this->_parent.reset();
    }

/// According to given xpath selector, containing text, content, classname, resource id, test if
/// this current element has the same property value as the given xpath selector.
/// \param xpathSelector Describe property values of a xml element should have.
/// \return If this element could be matched to the given xpath selector, return true.
    bool Element::matchXpathSelector(const XpathPtr &xpathSelector) const {
        if (!xpathSelector)
            return false;
        bool match;
        bool isResourceIDEqual = (!xpathSelector->resourceID.empty() &&
                                  this->getResourceID() == xpathSelector->resourceID);
        bool isTextEqual = (!xpathSelector->text.empty() && this->getText() == xpathSelector->text);
        
        // Performance optimization: Early exit for ContentDesc comparison
        // Most elements don't have ContentDesc, so check emptiness first
        bool isContentEqual = false;
        if (!xpathSelector->contentDescription.empty()) {
            const std::string &contentDesc = this->getContentDesc();
            // Performance: Check length first to avoid string comparison if lengths differ
            if (contentDesc.length() == xpathSelector->contentDescription.length()) {
                isContentEqual = (contentDesc == xpathSelector->contentDescription);
            }
        }
        bool isClassNameEqual = (!xpathSelector->clazz.empty() &&
                                 this->getClassname() == xpathSelector->clazz);
        bool isIndexEqual = xpathSelector->index > -1 && this->getIndex() == xpathSelector->index;
        
        // Performance optimization: Only log detailed xpath matching when enabled
#if FASTBOT_LOG_XPATH_MATCH
        BDLOG("begin find xpathSelector :\n "
              "XPathSelector:\n resourceID: %s text: %s contentDescription: %s clazz: %s index: %d \n"
              "UIPageElement:\n resourceID: %s text: %s contentDescription: %s clazz: %s index: %d \n"
              "equality: \n isResourceIDEqual:%d isTextEqual:%d isContentEqual:%d isClassNameEqual:%d isIndexEqual:%d",
              xpathSelector->resourceID.c_str(),
              xpathSelector->text.c_str(),
              xpathSelector->contentDescription.c_str(),
              xpathSelector->clazz.c_str(),
              xpathSelector->index,
              this->getResourceID().c_str(),
              this->getText().c_str(),
              this->getContentDesc().c_str(),
              this->getClassname().c_str(),
              this->getIndex(),
              isResourceIDEqual,
              isTextEqual,
              isContentEqual,
              isClassNameEqual,
              isIndexEqual);
#endif
        if (xpathSelector->operationAND) {
            // Performance optimization: AND mode with early exit
            // Fix bug: previous code had `match = isClassNameEqual` which overwrote the initial true
            match = true;
            // Early exit: if any required field doesn't match, return false immediately
            if (!xpathSelector->clazz.empty() && !isClassNameEqual) {
                match = false;
            } else if (!xpathSelector->contentDescription.empty() && !isContentEqual) {
                match = false;
            } else if (!xpathSelector->text.empty() && !isTextEqual) {
                match = false;
            } else if (!xpathSelector->resourceID.empty() && !isResourceIDEqual) {
                match = false;
            } else if (xpathSelector->index != -1 && !isIndexEqual) {
                match = false;
            }
        } else {
            // Performance optimization: OR mode with early exit
            // Return true as soon as any field matches
            match = isResourceIDEqual || isTextEqual || isContentEqual || isClassNameEqual;
        }
        return match;
    }

    /**
     * @brief Recursively select elements that satisfy a predicate function
     * 
     * Traverses the element tree recursively and collects all elements (including
     * descendants) that match the given predicate function. Results are appended
     * to the provided result vector.
     * 
     * Performance optimization:
     * - Early return if function is null
     * - Uses const reference for children to avoid copying
     * - Result vector is passed by reference to avoid copying
     * 
     * @param func Predicate function that returns true for matching elements
     * @param result Output vector to store matching elements (appended to)
     * 
     * @note Time complexity: O(n) where n is the number of elements in the tree
     */
    void Element::recursiveElements(const std::function<bool(ElementPtr)> &func,
                                    std::vector<ElementPtr> &result) const {
        if (func != nullptr) {
            // Performance: Reserve to reduce reallocations when many children match
            result.reserve(result.size() + this->_children.size());
            for (const auto &child: this->_children) {
                // Check if current child matches
                if (func(child)) {
                    result.push_back(child);
                }
                // Recursively check children
                child->recursiveElements(func, result);
            }
        }
    }

    /**
     * @brief Recursively apply a function to all elements in the tree
     * 
     * Traverses the element tree recursively and applies the given function
     * to each element (including this element's children and their descendants).
     * 
     * Performance optimization:
     * - Early return if function is null
     * - Uses const reference for children to avoid copying
     * 
     * @param doFunc Function to apply to each element (takes ElementPtr as parameter)
     * 
     * @note Time complexity: O(n) where n is the number of elements in the tree
     * @note This is used for operations like setting all elements clickable
     */
    void Element::recursiveDoElements(const std::function<void(std::shared_ptr<Element>)> &doFunc) {
        if (doFunc != nullptr) {
            for (const auto &child: this->_children) {
                // Apply function to current child
                doFunc(child);
                // Recursively apply to children
                child->recursiveDoElements(doFunc);
            }
        }
    }

    bool Element::_allClickableFalse = false;

    /**
     * @brief Create an Element tree from XML string content
     * 
     * Parses XML string content and creates a hierarchical Element tree structure.
     * If no elements are clickable, all elements are made clickable as a fallback.
     * The root element is always set to scrollable.
     * 
     * Performance optimization:
     * - Pre-allocates string vector with estimated capacity
     * - Uses efficient string operations
     * 
     * @param xmlContent The XML content as a string
     * @return Shared pointer to root Element, or nullptr if parsing fails
     */
    ElementPtr Element::createFromXml(const std::string &xmlContent) {
        tinyxml2::XMLDocument doc;
        
        // Raw guitree log for debugging: log XML line by line (same format as logcat)
        // Performance optimization: Only log when FASTBOT_LOG_RAW_GUITREE is enabled
#if FASTBOT_LOG_RAW_GUITREE
        std::vector<std::string> strings;
        strings.reserve(xmlContent.size() / 100 + 1);
        int startIndex = 0, endIndex = 0;
        for (size_t i = 0; i <= xmlContent.size(); i++) {
            if (i >= xmlContent.size() || xmlContent[i] == '\n') {
                endIndex = static_cast<int>(i);
                strings.emplace_back(xmlContent, startIndex, endIndex - startIndex);
                startIndex = endIndex + 1;
            }
        }
        for (const auto &line : strings) {
            BLOG("The content of XML is: %s", line.c_str());
        }
#else
        // Only log size summary when detailed logging is disabled
        BLOG("guitree size=%zu", xmlContent.size());
#endif
        
        // Parse XML content
        tinyxml2::XMLError errXml = doc.Parse(xmlContent.c_str());

        if (errXml != tinyxml2::XML_SUCCESS) {
            BLOGE("parse xml error %d", static_cast<int>(errXml));
            return nullptr;
        }

        ElementPtr elementPtr = std::make_shared<Element>();

        // Track if any element is clickable during parsing. Root has no parent (nullptr).
        _allClickableFalse = true;
        elementPtr->fromXml(doc, nullptr);
        
        // If no elements are clickable, make all clickable as fallback
        // This ensures the UI can still be interacted with
        if (_allClickableFalse) {
            elementPtr->recursiveDoElements([](const ElementPtr &elm) {
                elm->_clickable = true;
            });
        }
        
        // Force set root element scrollable = true
        // Root element should always be scrollable to allow navigation
        elementPtr->_scrollable = true;
        
        doc.Clear();
        return elementPtr;
    }

    ElementPtr Element::createFromXml(const tinyxml2::XMLDocument &doc) {
        ElementPtr elementPtr = std::make_shared<Element>(); // Root element has no parent
        _allClickableFalse = true;
        elementPtr->fromXml(doc, nullptr);
        if (_allClickableFalse) {
            elementPtr->recursiveDoElements([](const ElementPtr &elm) {
                elm->_clickable = true;
            });
        }
        return elementPtr;
    }

    void Element::fromJson(const std::string &/*jsonData*/) {
        //nlohmann::json
    }

    std::string Element::toString() const {
        return this->toJson();
    }


    std::string Element::toJson() const {
        nlohmann::json j;
        RectPtr bounds = this->getBounds();
        j["bounds"] = bounds ? bounds->toString().c_str() : "";
        j["index"] = this->getIndex();
        j["class"] = this->getClassname().c_str();
        j["resource-id"] = this->getResourceID().c_str();
        j["package"] =  this->getPackageName().c_str();
        j["content-desc"] = this->getContentDesc().c_str();
        j["checkable"] = this->getCheckable() ? "true" : "false";
        j["checked"] = this->_checked ? "true" : "false";
        j["clickable"] = this->getClickable() ? "true" : "false";
        j["enabled"] = this->getEnable() ? "true" : "false";
        j["focusable"] = this->_focusable ? "true" : "false";
        j["focused"] = this->_focused ? "true" : "false";
        j["scrollable"] = this->_scrollable ? "true" : "false";
        j["long-clickable"] = this->_longClickable ? "true" : "false";
        j["password"] = this->_password ? "true" : "false";
        return j.dump();
    }

    void Element::recursiveToXML(tinyxml2::XMLElement *xml, const Element *elm) const {
        BDLOG("add a xml %p %p", xml, elm);
        RectPtr bounds = elm->getBounds();
        xml->SetAttribute("bounds", bounds ? bounds->toString().c_str() : "");
        BDLOG("add a xml 111");
        xml->SetAttribute("index", elm->getIndex());
        xml->SetAttribute("class", elm->getClassname().c_str());
        xml->SetAttribute("resource-id", elm->getResourceID().c_str());
        xml->SetAttribute("package", elm->getPackageName().c_str());
        xml->SetAttribute("content-desc", elm->getContentDesc().c_str());
        xml->SetAttribute("checkable", elm->getCheckable() ? "true" : "false");
        xml->SetAttribute("checked", elm->_checked ? "true" : "false");
        xml->SetAttribute("clickable", elm->getClickable() ? "true" : "false");
        xml->SetAttribute("enabled", elm->getEnable() ? "true" : "false");
        xml->SetAttribute("focusable", elm->_focusable ? "true" : "false");
        xml->SetAttribute("focused", elm->_focused ? "true" : "false");
        xml->SetAttribute("scrollable", elm->_scrollable ? "true" : "false");
        xml->SetAttribute("long-clickable", elm->_longClickable ? "true" : "false");
        xml->SetAttribute("password", elm->_password ? "true" : "false");
        xml->SetAttribute("scroll-type", "none");

        BDLOG("add a xml 1111");
        for (const auto &child: elm->getChildren()) {
            tinyxml2::XMLElement *xmlChild = xml->InsertNewChildElement("node");
            xml->LinkEndChild(xmlChild);
            recursiveToXML(xmlChild, child.get());
        }
    }

    std::string Element::toXML() const {
        tinyxml2::XMLDocument doc;
        tinyxml2::XMLDeclaration *xmlDeclarationNode = doc.NewDeclaration();
        doc.InsertFirstChild(xmlDeclarationNode);

        tinyxml2::XMLElement *root = doc.NewElement("node");
        recursiveToXML(root, this);
        doc.LinkEndChild(root);

        tinyxml2::XMLPrinter printer;
        doc.Print(&printer);
        std::string xmlStr = std::string(printer.CStr());
        return xmlStr;
    }

    void Element::fromXml(const tinyxml2::XMLDocument &nodeOfDoc, const ElementPtr &parentOfNode) {
        const ::tinyxml2::XMLElement *node = nodeOfDoc.RootElement();
        this->fromXMLNode(node, parentOfNode);

        if (0 != nodeOfDoc.ErrorID())
            BLOGE("parse xml error %s", nodeOfDoc.ErrorStr());

    }

    void Element::fromXMLNode(const tinyxml2::XMLElement *xmlNode, const ElementPtr &parentOfNode) {
        if (nullptr == xmlNode)
            return;
        if (parentOfNode)
            this->_parent = parentOfNode;
//    BLOG("This Node is %s", std::string(xmlNode->GetText()).c_str());
        int indexOfNode = 0;
        tinyxml2::XMLError err = xmlNode->QueryIntAttribute("index", &indexOfNode);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_index = indexOfNode;
        }
        const char *boundingBoxStr = "get attribute failed";
        err = xmlNode->QueryStringAttribute("bounds", &boundingBoxStr);
        if (err == tinyxml2::XML_SUCCESS) {
            // Performance optimization: Lightweight bounds parser instead of sscanf
            // Format: "[xl,yl][xr,yr]" - e.g., "[100,200][300,400]"
            int xl = 0, yl = 0, xr = 0, yr = 0;
            bool parseSuccess = false;
            
            // Fast parsing: find positions of brackets and commas
            const char *p = boundingBoxStr;
            if (*p == '[') {
                p++; // skip '['
                // Parse xl
                xl = 0;
                bool negative = false;
                if (*p == '-') {
                    negative = true;
                    p++;
                }
                while (*p >= '0' && *p <= '9') {
                    xl = xl * 10 + (*p - '0');
                    p++;
                }
                if (negative) xl = -xl;
                
                if (*p == ',') {
                    p++; // skip ','
                    // Parse yl
                    yl = 0;
                    negative = false;
                    if (*p == '-') {
                        negative = true;
                        p++;
                    }
                    while (*p >= '0' && *p <= '9') {
                        yl = yl * 10 + (*p - '0');
                        p++;
                    }
                    if (negative) yl = -yl;
                    
                    if (*p == ']' && *(p+1) == '[') {
                        p += 2; // skip ']['
                        // Parse xr
                        xr = 0;
                        negative = false;
                        if (*p == '-') {
                            negative = true;
                            p++;
                        }
                        while (*p >= '0' && *p <= '9') {
                            xr = xr * 10 + (*p - '0');
                            p++;
                        }
                        if (negative) xr = -xr;
                        
                        if (*p == ',') {
                            p++; // skip ','
                            // Parse yr
                            yr = 0;
                            negative = false;
                            if (*p == '-') {
                                negative = true;
                                p++;
                            }
                            while (*p >= '0' && *p <= '9') {
                                yr = yr * 10 + (*p - '0');
                                p++;
                            }
                            if (negative) yr = -yr;
                            
                            if (*p == ']') {
                                parseSuccess = true;
                            }
                        }
                    }
                }
            }
            
            if (parseSuccess) {
                this->_bounds = std::make_shared<Rect>(xl, yl, xr, yr);
                if (this->_bounds->isEmpty())
                    this->_bounds = Rect::RectZero;
            }
        }
        // Performance optimization: Use move semantics and avoid unnecessary copies
        // tinyxml2 returns const char* which points to internal buffer, so we need to copy
        // But we can optimize by checking if string is empty before copying
        const char *text = "attribute text get failed";  // need copy
        err = xmlNode->QueryStringAttribute("text", &text);
        if (err == tinyxml2::XML_SUCCESS && text && *text != '\0') {
            this->_text = std::string(text); // copy only if non-empty
        }
        const char *resource_id = "attribute resource_id get failed";  // need copy
        err = xmlNode->QueryStringAttribute("resource-id", &resource_id);
        if (err == tinyxml2::XML_SUCCESS && resource_id && *resource_id != '\0') {
            this->_resourceID = std::string(resource_id); // copy only if non-empty
        }
        const char *tclassname = "attribute class name get failed";  // need copy
        err = xmlNode->QueryStringAttribute("class", &tclassname);
        if (err == tinyxml2::XML_SUCCESS && tclassname && *tclassname != '\0') {
            // Performance: For common class names, we could use string interning
            // For now, just copy (full interning would require a global string pool)
            this->_classname = std::string(tclassname); // copy
        }
        const char *pkgname = "attribute package name get failed";  // need copy
        err = xmlNode->QueryStringAttribute("package", &pkgname);
        if (err == tinyxml2::XML_SUCCESS && pkgname && *pkgname != '\0') {
            this->_packageName = std::string(pkgname); // copy only if non-empty
        }
        const char *content_desc = "attribute content description get failed";  // need copy
        err = xmlNode->QueryStringAttribute("content-desc", &content_desc);
        if (err == tinyxml2::XML_SUCCESS && content_desc && *content_desc != '\0') {
            this->_contentDesc = std::string(content_desc); // copy only if non-empty
        }
        bool checkable = false;
        err = xmlNode->QueryBoolAttribute("checkable", &checkable);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_checkable = checkable;
        }
        bool clickable = false;
        err = xmlNode->QueryBoolAttribute("clickable", &clickable);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_clickable = clickable;
        }
        if (clickable)
            _allClickableFalse = false;
        bool checked = false;
        err = xmlNode->QueryBoolAttribute("checked", &checked);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_checked = checked;
        }
        bool enabled = false;
        err = xmlNode->QueryBoolAttribute("enabled", &enabled);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_enabled = enabled;
        }
        bool focused = false;
        err = xmlNode->QueryBoolAttribute("focused", &focused);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_focused = focused;
        }
        bool focusable = false;
        err = xmlNode->QueryBoolAttribute("focusable", &focusable);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_focusable = focusable;
        }
        bool scrollable = false;
        err = xmlNode->QueryBoolAttribute("scrollable", &scrollable);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_scrollable = scrollable;
        }
        bool longclickable = false;
        err = xmlNode->QueryBoolAttribute("long-clickable", &longclickable);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_longClickable = longclickable;
        }
        bool password = false;
        err = xmlNode->QueryBoolAttribute("password", &password);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_password = password;
        }
        bool selected = false;
        err = xmlNode->QueryBoolAttribute("selected", &selected);
        if (err == tinyxml2::XML_SUCCESS) {
            this->_selected = selected;
        }

        this->_isEditable = "android.widget.EditText" == this->_classname;
        if (FORCE_EDITTEXT_CLICK_TRUE && this->_isEditable) {
            this->_longClickable = this->_clickable = this->_enabled = true;
        }

        if (PARENT_CLICK_CHANGE_CHILDREN && parentOfNode && parentOfNode->_longClickable) {
            this->_longClickable = parentOfNode->_longClickable;
        }
        if (PARENT_CLICK_CHANGE_CHILDREN && parentOfNode && parentOfNode->_clickable) {
            this->_clickable = parentOfNode->_clickable;
        }
        if (this->_clickable || this->_longClickable) {
            this->_enabled = true;
        }

        // Performance optimization: Pre-compute and cache scroll type during parsing
        // This avoids repeated string comparisons in getScrollType()
        this->_cachedScrollType = this->_computeScrollType();
        this->_scrollTypeCached = true;

        // Performance optimization: Pre-count children to avoid multiple vector reallocations
        int childrenCountOfCurrentNode = 0;
        if (!xmlNode->NoChildren()) {
            // First pass: count children to pre-allocate vector capacity
            for (const tinyxml2::XMLElement *childNode = xmlNode->FirstChildElement();
                 nullptr != childNode; childNode = childNode->NextSiblingElement()) {
                childrenCountOfCurrentNode++;
            }
            
            // Pre-allocate vector capacity to avoid reallocations during emplace_back
            if (childrenCountOfCurrentNode > 0) {
                this->_children.reserve(childrenCountOfCurrentNode);
            }
            
            // Second pass: create and populate children
            for (const tinyxml2::XMLElement *childNode = xmlNode->FirstChildElement();
                 nullptr != childNode; childNode = childNode->NextSiblingElement()) {
                const tinyxml2::XMLElement *nextXMLElement = childNode;
                ElementPtr childElement = std::make_shared<Element>();
                this->_children.emplace_back(childElement);
                // Pass shared_from_this() as parent so child's _parent points to this node
                childElement->fromXMLNode(nextXMLElement, shared_from_this());
            }
        }
        this->_childCount = childrenCountOfCurrentNode;
    }

    bool Element::isWebView() const {
        return "android.webkit.WebView" == this->_classname;
    }

    bool Element::isEditText() const {
        return this->_isEditable;
    }

    ScrollType Element::_computeScrollType() const {
        if (!this->_scrollable) {
            return ScrollType::NONE;
        }
        if ("android.widget.ScrollView" == this->_classname
            || "android.widget.ListView" == _classname
            || "android.widget.ExpandableListView" == _classname
            || "android.support.v17.leanback.widget.VerticalGridView" == _classname
            || "android.support.v7.widget.RecyclerView" == _classname
            || "androidx.recyclerview.widget.RecyclerView" == _classname) {
            return ScrollType::Vertical;
        } else if ("android.widget.HorizontalScrollView" == _classname
                   || "android.support.v17.leanback.widget.HorizontalGridView" == _classname
                   || "android.support.v4.view.ViewPager" == _classname) {
            return ScrollType::Horizontal;
        }
        if (this->_classname.find("ScrollView") != std::string::npos) {
            return ScrollType::ALL;
        }

        // for ios
//    return ScrollType::NONE;
        return ScrollType::ALL;
    }

    ScrollType Element::getScrollType() const {
        // Performance optimization: Return cached value if available
        if (this->_scrollTypeCached) {
            return this->_cachedScrollType;
        }
        // Fallback: compute and cache (should not happen in normal flow)
        this->_cachedScrollType = this->_computeScrollType();
        this->_scrollTypeCached = true;
        return this->_cachedScrollType;
    }

    Element::~Element() {
        this->_children.clear();
        this->_parent.reset();
    }

    /**
     * @brief Compute hash code for this element and optionally its children
     * 
     * Generates a hash code based on element properties (resource ID, class name,
     * package name, text, content description, activity, clickable state).
     * If recursive is true, also includes children hashes with order information.
     * 
     * Performance optimization:
     * - Uses bit shifting and XOR for efficient hash combination
     * - Caches individual property hashes before combining
     * - Only processes children if recursive flag is set
     * 
     * @param recursive If true, include children in hash computation
     * @return Hash code as long integer
     * 
     * @note Hash computation includes order information for children when recursive=true
     */
    long Element::hash(bool recursive) {
        // Performance optimization: Return cached hash if available and recursive flag matches
        // Note: We cache only recursive hash (recursive=true) as it's the most expensive
        // Non-recursive hash is cheap and rarely reused, so we don't cache it
        if (recursive && this->_hashCached) {
            return this->_cachedHash;
        }
        
        uintptr_t hashcode = 0x1;
        
        // Performance optimization: Use fast string hash function instead of std::hash
        // This provides better performance for typical UI strings (short to medium length)
        // Compute individual property hashes with different bit shifts for better distribution
        uintptr_t hashcode1 = 127U * fastbotx::fastStringHash(this->_resourceID) << 1;
        uintptr_t hashcode2 = fastbotx::fastStringHash(this->_classname) << 2;
        uintptr_t hashcode3 = fastbotx::fastStringHash(this->_packageName) << 3;
        uintptr_t hashcode4 = 256U * fastbotx::fastStringHash(this->_text) << 4;
        
        // Performance optimization: Only compute ContentDesc hash if not empty
        // Most elements don't have ContentDesc, so this avoids unnecessary hash computation
        // Use fast string hash for better performance
        uintptr_t hashcode5 = 0;
        if (!this->_contentDesc.empty()) {
            hashcode5 = fastbotx::fastStringHash(this->_contentDesc) << 5;
        }
        
        uintptr_t hashcode6 = fastbotx::fastStringHash(this->_activity) << 2;
        uintptr_t hashcode7 = 64U * std::hash<int>{}(static_cast<int>(this->_clickable)) << 6;

        // Combine all property hashes using XOR
        hashcode = hashcode1 ^ hashcode2 ^ hashcode3 ^ hashcode4 ^ hashcode5 ^ hashcode6 ^ hashcode7;
        
        // If recursive, include children hashes with order information
        if (recursive) {
            // Performance: Use size_t for index to match container type
            for (size_t i = 0; i < this->_children.size(); i++) {
                // Get child hash and shift for better distribution
                long childHash = this->_children[i]->hash() << 2;
                hashcode ^= static_cast<uintptr_t>(childHash);
                // Include order information in hash to distinguish different child orders
                hashcode ^= 0x7398c + (std::hash<size_t>{}(i) << 8);
            }
            
            // Cache the computed hash for future use
            this->_cachedHash = static_cast<long>(hashcode);
            this->_hashCached = true;
        }
        
        return static_cast<long>(hashcode);
    }

}

#endif //Element_CPP_
