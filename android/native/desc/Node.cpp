/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Node_CPP_
#define Node_CPP_

#include "Node.h"
#include "../utils.hpp"

namespace fastbotx {

    /**
     * @brief Constructor initializes node with zero visit count and zero ID
     * 
     * Creates a new node instance with default values. The node will be
     * assigned an ID later when added to the graph.
     */
    Node::Node()
            : _visitedCount(0), _id(0) {}

    /**
     * @brief Update the visit count when this node is visited
     * 
     * Increments the internal visit counter and logs the visit event
     * for debugging and statistics purposes.
     * 
     * @param timestamp The timestamp when visited (parameter reserved for future use)
     * 
     * @note The timestamp parameter is currently unused but kept for
     *       potential future statistics tracking enhancements.
     */
    void Node::visit(time_t /*timestamp*/) {
        _visitedCount++;
        BDLOG("visit id:%s times %d", this->getId().c_str(), this->_visitedCount);
    }

    /**
     * @brief Convert node to string representation
     * 
     * Implements the Serializable interface by returning the node's ID
     * as a string. This is used for logging and serialization.
     * 
     * @return String representation of the node (its ID)
     */
    std::string Node::toString() const {
        return this->getId();
    }

}
#endif //Node_CPP_
