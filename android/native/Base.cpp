/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su
 */
#ifndef BASE_CPP_
#define BASE_CPP_

#include "Base.h"
#include "utils.hpp"
#include <algorithm>

namespace fastbotx {

    std::string sanitizeUtf8ForJson(std::string s) {
        if (s.empty()) {
            return s;
        }
        std::string out;
        out.reserve(s.size());
        const size_t n = s.size();
        size_t i = 0;
        while (i < n) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            size_t step = 1;
            if ((c & 0x80U) == 0U) {
                step = 1;
            } else if ((c & 0xE0U) == 0xC0U) {
                step = 2;
            } else if ((c & 0xF0U) == 0xE0U) {
                step = 3;
            } else if ((c & 0xF8U) == 0xF0U) {
                step = 4;
            } else {
                ++i;
                continue;
            }
            if (i + step > n) {
                break;
            }
            for (size_t j = 0; j < step; ++j) {
                const unsigned char b = static_cast<unsigned char>(s[i + j]);
                if (j > 0 && (b & 0xC0U) != 0x80U) {
                    step = 0;
                    break;
                }
            }
            if (step == 0) {
                ++i;
                continue;
            }
            out.append(s, i, step);
            i += step;
        }
        return out;
    }

    std::string safe_utf8_substr(const std::string &str, size_t start, size_t len) {
        if (str.empty() || len == 0) {
            return "";
        }

        const size_t n = str.size();
        size_t cpIndex = 0;
        size_t bytePos = 0;

        auto stepUtf8 = [&](size_t pos) -> size_t {
            unsigned char c = static_cast<unsigned char>(str[pos]);
            if ((c & 0x80U) == 0) return 1;       // 0xxxxxxx
            if ((c & 0xE0U) == 0xC0U) return 2;   // 110xxxxx
            if ((c & 0xF0U) == 0xE0U) return 3;   // 1110xxxx
            if ((c & 0xF8U) == 0xF0U) return 4;   // 11110xxx
            return 1; // invalid lead byte, degrade gracefully
        };

        while (bytePos < n && cpIndex < start) {
            size_t step = stepUtf8(bytePos);
            bytePos += std::min(step, n - bytePos);
            ++cpIndex;
        }

        if (bytePos >= n) {
            return "";
        }

        size_t begin = bytePos;
        size_t taken = 0;
        while (bytePos < n && taken < len) {
            size_t step = stepUtf8(bytePos);
            bytePos += std::min(step, n - bytePos);
            ++taken;
        }
        return str.substr(begin, bytePos - begin);
    }

    /// The mapping table for converting string to ActionType
    const std::string actName[ActTypeSize] = {
            "CRASH",
            "FUZZ",
            "START",
            "RESTART",
            "CLEAN_RESTART",
            "NOP",
            "ACTIVATE",
            "BACK",
            "FEED",
            "CLICK",
            "LONG_CLICK",
            "SCROLL_TOP_DOWN",
            "SCROLL_BOTTOM_UP",
            "SCROLL_LEFT_RIGHT",
            "SCROLL_RIGHT_LEFT",
            "SCROLL_BOTTOM_UP_N",
            "SHELL_EVENT",
            "DEEP_LINK",
            "HOVER"
    };

    const std::string ScrollTypeName[] = {
            "all",
            "horizontal",
            "vertical",
            "none",
            "verticaleries"
    };

    /// According to the string of action type, convert it to the corresponding value of ActionType
    /// \param actionTypeString String of action type to be converted
    /// \return The converted value of ActionType
    ActionType stringToActionType(const std::string &actionTypeString) {
        for (int i = 0; i < ActTypeSize; i++) {
            if (actName[i] == actionTypeString)
                return (ActionType) i;
        }
        return ActionType::ActTypeSize;
    }

    ScrollType stringToScrollType(const std::string &str) {
        for (int i = 0; i < ScrollTypeSize; i++) {
            if (ScrollTypeName[i] == str)
                return (ScrollType) i;
        }
        return ScrollType::NONE;
    }

    PriorityNode::PriorityNode()
            : _priority(0) {

    }

    Rect::Rect() {
        this->top = 0;
        this->bottom = 0;
        this->right = 0;
        this->left = 0;
    }

    Rect::Rect(const Rect &rect) {
        this->top = rect.top;
        this->bottom = rect.bottom;
        this->right = rect.right;
        this->left = rect.left;
    }

    Rect::Rect(int left, int top, int right, int bottom) {
        this->top = top;
        this->bottom = bottom;
        this->right = right;
        this->left = left;
    }

    bool Rect::isEmpty() const {
        return this->left >= this->right || this->top >= this->bottom;
    }

    Point Rect::center() const {
        return {(int) ((double) (this->top) + 0.5f * (double) (this->bottom - this->top)),
                (int) ((double) this->left + 0.5f * (double) (this->right - this->left))};
    }

    bool Rect::contains(const Point &point) const {
        return point.x >= this->left && point.x <= this->right
               && point.y >= this->top && point.y <= this->bottom;
    }

    uintptr_t Rect::hash() const {
        return (31U * std::hash<int>{}(top) << 1 ^ std::hash<int>{}(bottom) << 2) ^
               ((std::hash<int>{}(left) << 1 ^ 127U * std::hash<int>{}(right) << 2) >> 1);
    }

    uintptr_t Rect::hash2() const {
        const uintptr_t maskedTop = static_cast<uintptr_t>(static_cast<uint16_t>(top));
        const uintptr_t maskedBottom = static_cast<uintptr_t>(static_cast<uint16_t>(bottom));
        const uintptr_t maskedLeft = static_cast<uintptr_t>(static_cast<uint16_t>(left));
        const uintptr_t maskedRight = static_cast<uintptr_t>(static_cast<uint16_t>(right));
        uintptr_t hashedTop = maskedTop << 48;
        uintptr_t hashedBottom = maskedBottom << 32;
        uintptr_t hashedLeft = maskedLeft << 16;
        uintptr_t hashedRight = maskedRight;
        return hashedTop | hashedBottom | hashedLeft | hashedRight;
    }

    std::string Rect::toString() const {
        std::stringstream strs;
        strs << "[" << this->left << "," << this->top
             << "][" << this->right << "," << this->bottom << "]";
        return strs.str();
    }

    bool Rect::operator==(const Rect &node) const {
        return this->left == node.left &&
               this->top == node.top &&
               this->right == node.right &&
               this->bottom == node.bottom;
    }


    Rect &Rect::operator=(const Rect &node) {
        this->left = node.left;
        this->top = node.top;
        this->right = node.right;
        this->bottom = node.bottom;
        return *this;
    }

    const RectPtr Rect::RectZero = std::make_shared<Rect>();
    std::vector<std::shared_ptr<Rect>> Rect::_rectPool;

    RectPtr Rect::getRect(const RectPtr &rect) {
        if (nullptr == rect || rect->isEmpty()) {
            return RectZero;
        }
        return rect;
    }

    Point::Point() {
        this->x = 0;
        this->y = 0;
    }

    Point::Point(const Point &point) {
        this->x = point.x;
        this->y = point.y;
    }

    Point::Point(int x, int y) {
        this->x = x;
        this->y = y;
    }

    uintptr_t Point::hash() const {
        return (31U * std::hash<int>{}(x) << 1) ^
               ((127U * std::hash<int>{}(y) << 2) >> 1);
    }

    bool Point::operator==(const Point &node) const {
        return this->x == node.x && this->y == node.y;
    }


    Point &Point::operator=(const Point &node) {
        this->x = node.x;
        this->y = node.y;
        return *this;
    }

}
#endif //BASE_CPP_
