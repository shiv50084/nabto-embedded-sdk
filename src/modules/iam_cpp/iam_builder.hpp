#pragma once

#include "condition.hpp"
#include "statement.hpp"
#include "effect.hpp"
#include "policy.hpp"

#include <string>
#include <set>
#include <vector>

namespace nabto {
namespace iam {


class StatementBuilder {
 public:
    StatementBuilder(Effect effect) : effect_(effect) {}
    StatementBuilder allow()
    {
        effect_ = Effect::ALLOW;
        return *this;
    }

    StatementBuilder deny()
    {
        effect_ = Effect::DENY;
        return *this;
    }

    StatementBuilder addAction(const std::string& action)
    {
        actions_.insert(action);
        return *this;
    }

    StatementBuilder addCondition(const Condition& condition)
    {
        conditions_.push_back(condition);
        return *this;
    }

    Statement build() const {
        return Statement(effect_, actions_, conditions_);
    }

 private:
    Effect effect_;
    std::set<std::string> actions_;
    std::vector<Condition> conditions_;

};

class PolicyBuilder {
 public:
    PolicyBuilder(const std::string& id) : id_(id) {}

    PolicyBuilder addStatement(const Statement& statement)
    {
        statements_.push_back(statement);
        return *this;
    }

    PolicyBuilder addStatement(const StatementBuilder& statement)
    {
        statements_.push_back(statement.build());
        return *this;
    }

    Policy build() {
        return Policy(id_, statements_);
    }

    std::string getId() const { return id_; }

    std::vector<Statement> getStatements() const { return statements_; }
 private:
    std::string id_;
    std::vector<Statement> statements_;
};


class RoleBuilder {
 public:
    RoleBuilder(const std::string& id) : id_(id) {}

    RoleBuilder addPolicy(const std::string& policyId)
    {
        policies_.insert(policyId);
        return *this;
    }

    std::string getId() const
    {
        return id_;
    }

    std::set<std::string> getPolicies() const
    {
        return policies_;
    }
 private:
    std::string id_;
    std::set<std::string> policies_;
};

class ConditionBuilder {
 public:
    ConditionBuilder(Condition::Operator op, const std::string& key)
        : operator_(op), key_(key)
    {
    }
    ConditionBuilder addValue(const std::string& value)
    {
        values_.push_back(value);
        return *this;
    }

    Condition build()
    {
        return Condition(operator_, key_, values_);
    }

 private:
    Condition::Operator operator_;
    std::string key_;
    std::vector<std::string> values_;
};

} } // namespace