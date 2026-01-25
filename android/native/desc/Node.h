/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Node_H_
#define Node_H_

#include <string>
#include <ctime>
#include "../Base.h"


/**
 * @brief Macro to declare a static prefix string for class ID generation
 * 
 * This macro creates a static const string member that serves as a prefix
 * for generating unique IDs for instances of class X.
 * 
 * @param X The class name
 */
#define PropertyIDPrefix(X)  \
        const static std::string __##X##Prefix

/**
 * @brief Macro to implement the prefix string for class ID generation
 * 
 * This macro defines the value of the prefix string declared by PropertyIDPrefix.
 * 
 * @param X The class name
 * @param value The prefix string value
 */
#define PropertyIDPrefixImpl(X, value) \
        const std::string X::__##X##Prefix = value

/**
 * @brief Macro to generate getId() method implementation
 * 
 * This macro generates a getId() method that returns a string ID by
 * concatenating the class prefix with the instance ID number.
 * 
 * @param X The class name
 */
#define FuncGetID(X) \
        const std::string getId() const override {return X::__##X##Prefix+std::to_string(_id);}

namespace fastbotx {

    /**
     * @brief Base class for all nodes in the state-action graph
     * 
     * Node is the base class used for statistics tracking. It retains the status
     * and counts of being visited. All graph nodes (State, Action, etc.) inherit
     * from this class to track their visit history.
     * 
     * Features:
     * - Visit counting for statistics
     * - Unique ID assignment
     * - Serializable interface implementation
     */
    class Node : public Serializable {
    public:
        /**
         * @brief Constructor initializes node with zero visit count and zero ID
         */
        Node();

        /**
         * @brief Update the visit count when this node is visited
         * 
         * Increments the visit counter and logs the visit event.
         * 
         * @param timestamp The timestamp when the node was visited (currently unused)
         */
        virtual void visit(time_t timestamp);

        /**
         * @brief Check if this node has been visited at least once
         * 
         * @return true if visit count > 0, false otherwise
         */
        bool isVisited() const { return _visitedCount > 0; }

        /**
         * @brief Get the total number of times this node has been visited
         * 
         * @return The visit count
         */
        int getVisitedCount() const { return this->_visitedCount; }

        /**
         * @brief Convert node to string representation (implements Serializable)
         * 
         * @return String representation of the node (typically the ID)
         */
        std::string toString() const override;

        /**
         * @brief Get the unique string ID of this node
         * 
         * Default implementation returns the numeric ID as a string.
         * Subclasses can override to include a prefix.
         * 
         * @return String ID of this node
         */
        virtual const std::string getId() const { return std::to_string(_id); }

        /**
         * @brief Set the numeric ID of this node
         * 
         * @param id The numeric ID to assign
         */
        void setId(const int &id) { this->_id = id; }

        /**
         * @brief Get the numeric ID of this node
         * 
         * @return The numeric ID
         */
        int getIdi() const { return this->_id; }

    protected:
        /// Counter for tracking how many times this node has been visited
        int _visitedCount;
        
        /// Unique numeric identifier for this node instance
        int _id;
    };
}

#endif  // Node_H_
